/* Porpoise - DolphinBar test payloads for the PS5.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Two payloads for the jailbreak's ELF loader (port 9021), built by
 * tools/dolphinbar-probe/build.sh from this file and src/porpoise_usbhid.cpp,
 * the same USB code Porpoise uses for real Wii Remotes:
 *
 *   dolphinbar-probe-1.elf  read only. Lists the console's USB devices
 *                           (their ugen nodes' device information, no bus
 *                           traffic), and for a DolphinBar its configuration
 *                           descriptor and whether the console's own driver
 *                           has its interfaces. Opens no transfer, sends
 *                           nothing to a Wii Remote.
 *   dolphinbar-probe-2.elf  talks to the Remotes, as Dolphin will: a status
 *                           request to each of the four slots, then for each
 *                           Remote found its LEDs, a short rumble, its
 *                           attachment (Nunchuk, Classic Controller,
 *                           MotionPlus), 20 s of buttons and motion, 10 s of
 *                           the IR camera, and Remotes switched off and on
 *                           during a last 30 s.
 *
 * Each writes /data/dolphinbar-probe/probe-<n>.log and shows a notification
 * when it starts and ends. Neither touches the console's Bluetooth chip
 * (ugen0.2) or any device but Nintendo's 057e:0306 / 057e:0330. */
#include "porpoise_hidapi.h"
#include "porpoise_usbhid.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

extern "C" long sceKernelSendNotificationRequest(int type, void *buf, long size, int unk);

#ifndef PROBE_STAGE
#define PROBE_STAGE 1
#endif

namespace
{
std::FILE *g_log = nullptr;

std::uint64_t now_ms()
{
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return std::uint64_t(t.tv_sec) * 1000u + std::uint64_t(t.tv_nsec) / 1000000u;
}
std::uint64_t g_start = 0;

void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    const std::uint64_t t = now_ms() - g_start;
    if (g_log)
    {
        std::fprintf(g_log, "%3llu.%03llu %s\n", (unsigned long long)(t / 1000), (unsigned long long)(t % 1000), line);
        std::fflush(g_log);
    }
    std::printf("%s\n", line);
}

void notify(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void notify(const char *fmt, ...)
{
    struct
    {
        char unused[45];
        char message[3075];
    } request;
    std::memset(&request, 0, sizeof request);
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(request.message, sizeof request.message, fmt, ap);
    va_end(ap);
    say("[notification] %s", request.message);
    sceKernelSendNotificationRequest(0, &request, sizeof request, 0);
}

void sleep_ms(unsigned ms) { usleep(ms * 1000u); }

std::string hex(const std::uint8_t *d, std::size_t n)
{
    std::string s;
    char b[4];
    for (std::size_t i = 0; i < n; ++i)
    {
        std::snprintf(b, sizeof b, "%02x", d[i]);
        if (i)
            s += ' ';
        s += b;
    }
    return s;
}

/* ---- stage 1: what is there ---------------------------------------------------------------- */

/* Every ugen node and its device information (the same read-only ioctl
 * BlueV lists adapters with), the console's Bluetooth chip excepted. */
void list_usb()
{
    DIR *d = opendir("/dev");
    if (!d)
    {
        say("opendir /dev failed: errno %d", errno);
        return;
    }
    std::vector<std::string> nodes;
    while (const dirent *e = readdir(d))
        if (std::strncmp(e->d_name, "ugen", 4) == 0)
            nodes.emplace_back(e->d_name);
    closedir(d);
    say("%zu ugen node(s) in /dev", nodes.size());
    for (const std::string &n : nodes)
    {
        if (n == "ugen0.2")
        {
            say("  %s: the console's Bluetooth chip, not opened", n.c_str());
            continue;
        }
        const int fd = open(("/dev/" + n).c_str(), O_RDWR | O_NONBLOCK);
        if (fd < 0)
        {
            say("  %s: open errno %d", n.c_str(), errno);
            continue;
        }
        std::uint8_t info[0x180];
        std::memset(info, 0, sizeof info);
        const int r = ioctl(fd, 0xc1785504UL, info);
        const int err = errno;
        close(fd);
        if (r != 0)
        {
            say("  %s: device information errno %d", n.c_str(), err);
            continue;
        }
        std::uint16_t pid, vid;
        std::memcpy(&pid, info, 2);
        std::memcpy(&vid, info + 2, 2);
        char product[129] = {}, vendor[129] = {};
        std::memcpy(product, info + 40, 128);
        std::memcpy(vendor, info + 168, 128);
        say("  %s: %04x:%04x class %02x/%02x/%02x bus %u addr %u \"%s\" \"%s\"", n.c_str(), vid, pid, info[11],
            info[12], info[13], info[8], info[9], vendor, product);
    }
}

void describe_devices()
{
    for (const auto &d : porpoise::usbhid::status())
    {
        say("DolphinBar candidate %s %04x:%04x \"%s\", descriptors %s", d.node.c_str(), d.vid, d.pid,
            d.product.c_str(), d.descriptors_read ? "read" : "NOT read (layout assumed)");
        for (std::size_t off = 0; off < d.config.size(); off += 32)
            say("  config %03zx: %s", off, hex(d.config.data() + off, std::min<std::size_t>(32, d.config.size() - off)).c_str());
        for (const auto &i : d.interfaces)
        {
            char out[24];
            std::snprintf(out, sizeof out, "%#04x", i.ep_out);
            say("  interface %d: IN %#04x, OUT %s, console's driver: %s", i.number, i.ep_in,
                i.ep_out ? out : "none (SET_REPORT)",
                i.kernel_driver < 0 ? "unknown" : i.kernel_driver ? "ATTACHED" : "none");
        }
    }
}

int stage1()
{
    notify("DolphinBar probe 1 (read only) running");
    say("uid %d euid %d gid %d", int(getuid()), int(geteuid()), int(getgid()));
    list_usb();
    hid_init();
    hid_device_info *list = hid_enumerate(0, 0);
    int n = 0;
    for (hid_device_info *i = list; i; i = i->next)
        ++n;
    hid_free_enumeration(list);
    say("hid_enumerate: %d Wii Remote interface(s)", n);
    describe_devices();
    hid_exit();
    if (n == 0)
        notify("DolphinBar probe 1: NO DolphinBar found. Log: /data/dolphinbar-probe/probe-1.log");
    else
        notify("DolphinBar probe 1: DolphinBar found, %d interfaces. Log: /data/dolphinbar-probe/probe-1.log", n);
    return n ? 0 : 1;
}

/* ---- stage 2: the Remotes ---------------------------------------------------------------- */

struct Remote
{
    std::string path;
    int slot = 0;
    hid_device *h = nullptr;
    bool present = false;
    std::uint16_t buttons = 0;
    std::uint64_t last_accel_log = 0;
    unsigned reports = 0;
};

const char *button_names(std::uint16_t b, char *out, std::size_t n)
{
    static const struct
    {
        std::uint16_t bit;
        const char *name;
    } k[] = {{0x0001, "Left"}, {0x0002, "Right"}, {0x0004, "Down"}, {0x0008, "Up"}, {0x0010, "+"},
             {0x0100, "2"},    {0x0200, "1"},     {0x0400, "B"},    {0x0800, "A"},  {0x1000, "-"},
             {0x8000, "Home"}};
    out[0] = 0;
    for (const auto &e : k)
        if (b & e.bit)
        {
            std::strncat(out, e.name, n - std::strlen(out) - 1);
            std::strncat(out, " ", n - std::strlen(out) - 1);
        }
    if (!out[0])
        std::snprintf(out, n, "(none)");
    return out;
}

bool write(Remote &r, std::initializer_list<std::uint8_t> bytes)
{
    std::uint8_t b[23] = {};
    std::size_t n = 0;
    for (std::uint8_t x : bytes)
        b[n++] = x;
    const int w = hid_write(r.h, b, n);
    if (w < 0)
        say("  remote %d: write %02x failed, errno %d", r.slot + 1, b[0], errno);
    return w >= 0;
}

/* Reads until a report with this id comes (or the time is up). */
int wait_report(Remote &r, std::uint8_t id, std::uint8_t *buf, int ms)
{
    const std::uint64_t until = now_ms() + std::uint64_t(ms);
    while (now_ms() < until)
    {
        const int n = hid_read_timeout(r.h, buf, 64, 50);
        if (n < 0)
            return -1;
        if (n > 0 && buf[0] == id)
            return n;
    }
    return 0;
}

bool write_register(Remote &r, std::uint32_t addr, std::initializer_list<std::uint8_t> data)
{
    std::uint8_t b[22] = {0x16, 0x04, std::uint8_t(addr >> 16), std::uint8_t(addr >> 8), std::uint8_t(addr),
                          std::uint8_t(data.size())};
    std::size_t i = 6;
    for (std::uint8_t x : data)
        b[i++] = x;
    if (hid_write(r.h, b, sizeof b) < 0)
        return false;
    std::uint8_t rep[64];
    return wait_report(r, 0x22, rep, 300) > 0; /* the acknowledgement */
}

bool read_register(Remote &r, std::uint32_t addr, std::uint8_t size, std::uint8_t *out)
{
    const std::uint8_t b[7] = {0x17, 0x04, std::uint8_t(addr >> 16), std::uint8_t(addr >> 8), std::uint8_t(addr), 0,
                               size};
    if (hid_write(r.h, b, sizeof b) < 0)
        return false;
    std::uint8_t rep[64];
    if (wait_report(r, 0x21, rep, 400) < 22 || (rep[3] & 0x0f) != 0)
        return false;
    std::memcpy(out, rep + 6, size);
    return true;
}

void identify_attachment(Remote &r)
{
    std::uint8_t rep[64];
    write(r, {0x15, 0x00});
    const int n = wait_report(r, 0x20, rep, 500);
    if (n < 7)
    {
        say("  remote %d: no status report (%d)", r.slot + 1, n);
        return;
    }
    say("  remote %d: status flags %02x, battery %02x, attachment %s", r.slot + 1, rep[3], rep[6],
        rep[3] & 0x02 ? "plugged in" : "none");
    std::uint8_t id[6];
    if (read_register(r, 0xa600fa, 6, id))
        say("  remote %d: MotionPlus id %s%s", r.slot + 1, hex(id, 6).c_str(),
            id[2] == 0xa6 && id[3] == 0x20 ? " (a Wii MotionPlus, inactive)" : "");
    else
        say("  remote %d: no MotionPlus answers at 0xa600fa", r.slot + 1);
    if (rep[3] & 0x02)
    {
        const bool init = write_register(r, 0xa400f0, {0x55}) && write_register(r, 0xa400fb, {0x00});
        if (init && read_register(r, 0xa400fa, 6, id))
        {
            const char *what = "unknown";
            if (id[2] == 0xa4 && id[3] == 0x20 && id[4] == 0x00 && id[5] == 0x00)
                what = "Nunchuk";
            else if (id[2] == 0xa4 && id[3] == 0x20 && id[4] == 0x01 && id[5] == 0x01)
                what = "Classic Controller (or Pro)";
            else if (id[4] == 0x04 && id[5] == 0x05)
                what = "MotionPlus (active)";
            else if (id[4] == 0x05 && id[5] == 0x05)
                what = "MotionPlus with Nunchuk passthrough";
            say("  remote %d: attachment id %s: %s", r.slot + 1, hex(id, 6).c_str(), what);
        }
        else
            say("  remote %d: the attachment didn't answer", r.slot + 1);
    }
}

void handle_report(Remote &r, const std::uint8_t *b, int n)
{
    ++r.reports;
    if (n >= 3 && b[0] >= 0x30 && b[0] <= 0x3f)
    {
        const std::uint16_t buttons = std::uint16_t((b[1] << 8) | b[2]) & 0x9f1f;
        if (buttons != r.buttons)
        {
            char names[96];
            say("  remote %d: buttons %s", r.slot + 1, button_names(buttons, names, sizeof names));
            r.buttons = buttons;
        }
        if ((b[0] == 0x31 || b[0] == 0x33) && n >= 6 && now_ms() - r.last_accel_log >= 1000)
        {
            r.last_accel_log = now_ms();
            say("  remote %d: accel %3u %3u %3u", r.slot + 1, b[3], b[4], b[5]);
        }
        if (b[0] == 0x33 && n >= 18)
        {
            int dots = 0;
            for (int i = 0; i < 4; ++i)
                dots += !(b[6 + 3 * i] == 0xff && b[7 + 3 * i] == 0xff);
            static std::uint64_t last_ir[4];
            if (now_ms() - last_ir[r.slot] >= 1000)
            {
                last_ir[r.slot] = now_ms();
                say("  remote %d: IR dots visible %d, first at x=%d y=%d", r.slot + 1, dots,
                    b[6] | ((b[8] >> 4) & 3) << 8, b[7] | ((b[8] >> 6) & 3) << 8);
            }
        }
    }
    else if (n >= 7 && b[0] == 0x20)
        say("  remote %d: status report, flags %02x (attachment %s)", r.slot + 1, b[3],
            b[3] & 0x02 ? "plugged in" : "none");
}

/* Reads everything for ms, from every Remote that's there. */
void pump(std::vector<Remote> &remotes, int ms, bool rescan)
{
    const std::uint64_t until = now_ms() + std::uint64_t(ms);
    std::uint64_t next_scan = now_ms() + 1000;
    while (now_ms() < until)
    {
        for (Remote &r : remotes)
        {
            if (!r.present)
                continue;
            std::uint8_t b[64];
            for (int k = 0; k < 8; ++k)
            {
                const int n = hid_read_timeout(r.h, b, sizeof b, 0);
                if (n <= 0)
                    break;
                handle_report(r, b, n);
            }
        }
        if (rescan && now_ms() >= next_scan)
        {
            /* Dolphin's once-a-second write: a rumble-off report. A slot
             * that takes it has a Remote, one that refuses it hasn't. */
            next_scan = now_ms() + 1000;
            for (Remote &r : remotes)
            {
                const std::uint8_t off[2] = {0x10, 0x00};
                const bool ok = hid_write(r.h, off, 2) >= 0;
                if (ok && !r.present)
                {
                    say("remote %d: CONNECTED", r.slot + 1);
                    notify("Wii Remote %d connected", r.slot + 1);
                    r.present = true;
                    write(r, {0x11, std::uint8_t(0x10 << r.slot)});
                    write(r, {0x12, 0x04, 0x31});
                }
                else if (!ok && r.present)
                {
                    say("remote %d: DISCONNECTED", r.slot + 1);
                    notify("Wii Remote %d disconnected", r.slot + 1);
                    r.present = false;
                }
            }
        }
        sleep_ms(2);
    }
}

int stage2()
{
    notify("DolphinBar probe 2: turn your Wii Remotes on (press 1+2 or sync) now");
    hid_init();
    sleep_ms(5000);
    hid_device_info *list = hid_enumerate(0, 0);
    std::vector<Remote> remotes;
    int slot = 0;
    for (hid_device_info *i = list; i; i = i->next)
    {
        Remote r;
        r.path = i->path;
        r.slot = slot++;
        remotes.push_back(r);
    }
    hid_free_enumeration(list);
    describe_devices();
    if (remotes.empty())
    {
        notify("DolphinBar probe 2: no DolphinBar found. Log: /data/dolphinbar-probe/probe-2.log");
        return 1;
    }
    int present = 0;
    for (Remote &r : remotes)
    {
        r.h = hid_open_path(r.path.c_str());
        if (!r.h)
        {
            say("%s: hid_open_path failed, errno %d", r.path.c_str(), errno);
            continue;
        }
        const std::uint8_t status[2] = {0x15, 0x00};
        const int w = hid_write(r.h, status, 2);
        r.present = w >= 0;
        say("%s (slot %d): status request %s (errno %d)", r.path.c_str(), r.slot + 1,
            r.present ? "TAKEN: a Wii Remote is there" : "refused: no Wii Remote", r.present ? 0 : errno);
        present += r.present;
    }
    notify("DolphinBar probe 2: %d Wii Remote(s) found. LEDs and a short rumble now", present);
    for (Remote &r : remotes)
        if (r.present)
        {
            write(r, {0x11, std::uint8_t(0x10 << r.slot)}); /* player r.slot+1's LED */
            write(r, {0x10, 0x01});
            sleep_ms(250);
            write(r, {0x10, 0x00});
            identify_attachment(r);
            write(r, {0x12, 0x04, 0x31}); /* continuous: buttons and motion */
        }
    notify("Press buttons and move the Wii Remotes (20 s)");
    pump(remotes, 20000, false);
    for (Remote &r : remotes)
        say("remote %d: %u reports in the 20 s", r.slot + 1, r.reports);

    notify("Point the Wii Remotes at the DolphinBar (IR camera, 10 s)");
    for (Remote &r : remotes)
        if (r.present)
        {
            /* The camera on, as games do (wiibrew: "IR Camera", sensitivity level 3). */
            write(r, {0x13, 0x04});
            write(r, {0x1a, 0x04});
            const bool ok = write_register(r, 0xb00030, {0x08}) &&
                            write_register(r, 0xb00000, {0x02, 0x00, 0x00, 0x71, 0x01, 0x00, 0xaa, 0x00, 0x64}) &&
                            write_register(r, 0xb0001a, {0x63, 0x03}) && write_register(r, 0xb00033, {0x03}) &&
                            write_register(r, 0xb00030, {0x08});
            say("remote %d: IR camera set up %s", r.slot + 1, ok ? "ok" : "FAILED");
            write(r, {0x12, 0x04, 0x33}); /* buttons, motion and IR */
        }
    pump(remotes, 10000, false);
    for (Remote &r : remotes)
        if (r.present)
        {
            write(r, {0x13, 0x00});
            write(r, {0x1a, 0x00});
            write(r, {0x12, 0x04, 0x31});
        }

    notify("Switch a Wii Remote off and on again, or connect another (30 s)");
    pump(remotes, 30000, true);

    for (Remote &r : remotes)
        if (r.h)
        {
            if (r.present)
                write(r, {0x10, 0x00});
            hid_close(r.h);
        }
    say("%s", porpoise::usbhid::describe().c_str());
    hid_exit();
    notify("DolphinBar probe 2 done. Log: /data/dolphinbar-probe/probe-2.log");
    return 0;
}
} // namespace

int main()
{
    g_start = now_ms();
    mkdir("/data/dolphinbar-probe", 0777);
    char path[64];
    std::snprintf(path, sizeof path, "/data/dolphinbar-probe/probe-%d.log", PROBE_STAGE);
    g_log = std::fopen(path, "w");
    porpoise::usbhid::set_log([](const char *line) { say("[usbhid] %s", line); });
    porpoise::usbhid::set_detach_kernel_driver(access("/data/dolphinbar-probe/detach", F_OK) == 0);
    say("DolphinBar probe %d (Porpoise), built " __DATE__ " " __TIME__, PROBE_STAGE);
    const int result = PROBE_STAGE == 1 ? stage1() : stage2();
    say("done (%d)", result);
    if (g_log)
        std::fclose(g_log);
    return result;
}
