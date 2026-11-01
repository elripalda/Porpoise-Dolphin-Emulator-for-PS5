/* Porpoise - the Switch 2's GameCube controller over USB (porpoise_ns2gc.hpp
 * says what and why).
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The node's transfer table, opened once while the controller is there:
 *
 *   transfer 1, 2  two reads armed on interface 0's interrupt IN (the input)
 *   transfer 3     interface 0's interrupt OUT (rumble), when it has one
 *   transfer 4     interface 1's bulk OUT (commands)
 *   transfer 5     interface 1's bulk IN (their replies)
 *
 * The thread is the only one that touches the node: it sends a command and
 * waits for its reply, collects finished reads (USB_FS_COMPLETE, which never
 * waits) and arms them again, about every 2 ms. What it reads is published
 * under g_lock for status() and pad(). */
#include "porpoise_ns2gc.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>

#ifndef PORPOISE_DESKTOP
#include <fcntl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#if defined(PORPOISE_NS2GC_HOST_TEST)
#include <dirent.h>
#else
#include <ps5platform/libc.h> /* ps5_opendir: a title's own opendir is refused */
#include "porpoise_dolphinbar.hpp"
#include "porpoise_usbhid.hpp"
#include "title_threads.hpp"
#endif
#include "porpoise_ugen.hpp"
#endif

namespace porpoise::ns2gc
{
/* ---- the protocol ------------------------------------------------------------------------ */

const char *button_name(int bit)
{
    static const char *const kNames[kButtons] = {"A",    "B",       "X", "Y",  "Z",    "ZL",   "L",    "R",
                                                 "START", "HOME", "Capture", "C", "Up", "Down", "Left", "Right"};
    return bit >= 0 && bit < kButtons ? kNames[bit] : "";
}

namespace
{
/* Where each Button is in the report's bytes 5..7 (the format SDL's driver
 * sets up, report format 5; byte 5 as the Switch Pro Controller has it: Y X B
 * A, then R and ZR, which on this controller is Z). */
struct Bit
{
    int byte;
    std::uint8_t mask;
    std::uint32_t button;
};
constexpr Bit kBits[] = {
    {5, 0x08, GcA},     {5, 0x04, GcB},       {5, 0x02, GcX},    {5, 0x01, GcY},     {5, 0x80, GcZ},
    {5, 0x40, GcR},     {6, 0x02, GcStart},   {6, 0x10, GcHome}, {6, 0x20, GcCapture}, {6, 0x40, GcC},
    {7, 0x01, GcDown},  {7, 0x02, GcUp},      {7, 0x04, GcRight}, {7, 0x08, GcLeft}, {7, 0x40, GcL},
    {7, 0x80, GcZL},
};

int twelve_low(const std::uint8_t *p) { return p[0] | ((p[1] & 0x0f) << 8); }
int twelve_high(const std::uint8_t *p) { return (p[1] >> 4) | (p[2] << 4); }

float axis(int raw, const Calibration &cal, int a)
{
    float v = float(raw - cal.centre[a]);
    if (cal.below[a] > 0 && cal.above[a] > 0)
        v /= float(v < 0 ? cal.below[a] : cal.above[a]);
    else
        v /= 2048.0f;
    return std::clamp(v, -1.0f, 1.0f);
}

float trigger(int raw, int zero)
{
    /* SDL's: from its rest (calibrated) to 232, the end of its travel */
    const float top = 232.0f - float(zero);
    if (top <= 0)
        return 0;
    return std::clamp((float(raw) - float(zero)) / top, 0.0f, 1.0f);
}
} // namespace

bool decode_report(const std::uint8_t *r, int length, const Calibration &cal, Status &s)
{
    if (length < 63)
        return false;
    s.report_id = r[0];
    s.report_length = length;
    std::uint32_t b = 0;
    for (const Bit &k : kBits)
        if (r[k.byte] & k.mask)
            b |= k.button;
    s.buttons = b;
    std::memcpy(s.raw_buttons, r + 5, 3);
    s.raw_stick[0] = twelve_low(r + 11);
    s.raw_stick[1] = twelve_high(r + 11);
    s.raw_stick[2] = twelve_low(r + 14);
    s.raw_stick[3] = twelve_high(r + 14);
    s.raw_trigger[0] = r[61];
    s.raw_trigger[1] = r[62];
    s.lx = axis(s.raw_stick[0], cal, 0);
    s.ly = axis(s.raw_stick[1], cal, 1);
    s.rx = axis(s.raw_stick[2], cal, 2);
    s.ry = axis(s.raw_stick[3], cal, 3);
    s.lt = trigger(s.raw_trigger[0], cal.trigger_zero[0]);
    s.rt = trigger(s.raw_trigger[1], cal.trigger_zero[1]);
    return true;
}

bool decode_report_0a(const std::uint8_t *r, int length, const Calibration &cal, Status &s)
{
    if (length < 14)
        return false;
    /* ndeadly/switch2_controller_research, hid_reports.md, Input Report 0x0A */
    static const Bit k0a[] = {
        {2, 0x02, GcA},     {2, 0x01, GcB},       {2, 0x08, GcX},     {2, 0x04, GcY},       {2, 0x10, GcZ},
        {2, 0x20, GcR},     {2, 0x40, GcStart},   {4, 0x01, GcHome},  {4, 0x02, GcCapture}, {4, 0x10, GcC},
        {3, 0x01, GcDown},  {3, 0x08, GcUp},      {3, 0x02, GcRight}, {3, 0x04, GcLeft},    {3, 0x20, GcL},
        {3, 0x10, GcZL},
    };
    s.report_id = 0x0a;
    s.report_length = length;
    std::uint32_t b = 0;
    for (const Bit &k : k0a)
        if (r[k.byte] & k.mask)
            b |= k.button;
    s.buttons = b;
    std::memcpy(s.raw_buttons, r + 2, 3);
    s.raw_stick[0] = twelve_low(r + 5);
    s.raw_stick[1] = twelve_high(r + 5);
    s.raw_stick[2] = twelve_low(r + 8);
    s.raw_stick[3] = twelve_high(r + 8);
    s.raw_trigger[0] = r[12];
    s.raw_trigger[1] = r[13];
    s.lx = axis(s.raw_stick[0], cal, 0);
    s.ly = axis(s.raw_stick[1], cal, 1);
    s.rx = axis(s.raw_stick[2], cal, 2);
    s.ry = axis(s.raw_stick[3], cal, 3);
    s.lt = trigger(s.raw_trigger[0], cal.trigger_zero[0]);
    s.rt = trigger(s.raw_trigger[1], cal.trigger_zero[1]);
    return true;
}

bool parse_stick_calibration(const std::uint8_t *d, Calibration &cal, int x)
{
    const int cx = twelve_low(d), cy = twelve_high(d), ax = twelve_low(d + 3), ay = twelve_high(d + 3),
              bx = twelve_low(d + 6), by = twelve_high(d + 6);
    /* an erased flash (0xfff) or zeros: not a calibration */
    auto sane = [](int centre, int above, int below) {
        return centre > 1024 && centre < 3072 && above > 256 && above < 2048 && below > 256 && below < 2048;
    };
    if (!sane(cx, ax, bx) || !sane(cy, ay, by))
        return false;
    cal.centre[x] = cx;
    cal.centre[x + 1] = cy;
    cal.above[x] = ax;
    cal.above[x + 1] = ay;
    cal.below[x] = bx;
    cal.below[x + 1] = by;
    return true;
}

int make_command(std::uint8_t *out, std::uint8_t command, std::uint8_t sub, const std::uint8_t *data, int length)
{
    out[0] = command;
    out[1] = 0x91; /* over USB */
    out[2] = 0;
    out[3] = sub;
    out[4] = 0;
    out[5] = std::uint8_t(length);
    out[6] = 0;
    out[7] = 0;
    if (length > 0)
        std::memcpy(out + 8, data, std::size_t(length));
    return 8 + length;
}

void make_rumble(std::uint8_t out[64], unsigned sequence, bool on)
{
    std::memset(out, 0, 64);
    out[0] = 0x03;
    out[1] = std::uint8_t(0x50 | (sequence & 0x0f));
    out[2] = on ? 1 : 2;
}
} // namespace porpoise::ns2gc

#ifdef PORPOISE_DESKTOP
/* A desktop has SDL, which drives the controller itself. */
namespace porpoise::ns2gc
{
namespace
{
#ifdef PORPOISE_UI_PREVIEW
const Status *g_preview = nullptr;
#endif
} // namespace
#ifdef PORPOISE_UI_PREVIEW
void preview_status(const Status *s) { g_preview = s; }
Status status() { return g_preview ? *g_preview : Status{}; }
bool connected() { return g_preview && g_preview->link == Link::Ready; }
#else
Status status() { return {}; }
bool connected() { return false; }
#endif
void start() {}
bool close_for_exit(unsigned) { return true; }
Pad pad() { return {}; }
bool take_home_press() { return false; }
void set_rumble(bool) {}
void test_rumble(unsigned) {}
void set_log(void (*)(const char *)) {}
void bt_link(bool, const std::string &, const Calibration *) {}
void bt_input(const std::uint8_t *, int, bool) {}
bool rumble_wanted() { return false; }
} // namespace porpoise::ns2gc
#else

namespace porpoise::ns2gc
{
namespace
{
using namespace porpoise::ugen;

/* ---- the system calls -------------------------------------------------------------------- */
#ifdef PORPOISE_NS2GC_HOST_TEST
SysOps g_sys{};
int sys_open(const char *path, bool nb) { return g_sys.open(path, nb); }
int sys_close(int fd) { return g_sys.close(fd); }
int sys_ioctl(int fd, unsigned long req, void *arg) { return g_sys.ioctl(fd, req, arg); }
void sys_sleep_us(unsigned us) { g_sys.sleep_us(us); }
std::uint64_t now_ms() { return g_sys.now_ms(); }
bool list_dev(std::vector<std::string> &names) { return g_sys.list_dev(names); }
#else
int sys_open(const char *path, bool nb) { return ::open(path, O_RDWR | (nb ? O_NONBLOCK : 0)); }
int sys_close(int fd) { return ::close(fd); }
int sys_ioctl(int fd, unsigned long req, void *arg) { return ::ioctl(fd, req, arg); }
void sys_sleep_us(unsigned us) { ::usleep(us); }
std::uint64_t now_ms()
{
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return std::uint64_t(t.tv_sec) * 1000u + std::uint64_t(t.tv_nsec) / 1000000u;
}
bool list_dev(std::vector<std::string> &names)
{
    DIR *d = ps5_opendir("/dev");
    if (!d)
        return false;
    while (const dirent *e = ps5_readdir(d))
        if (std::strncmp(e->d_name, "ugen", 4) == 0)
            names.emplace_back(e->d_name);
    ps5_closedir(d);
    return true;
}
#endif

void (*g_log)(const char *) = nullptr;

void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void logf(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (g_log)
        g_log(line);
#ifndef PORPOISE_NS2GC_HOST_TEST
    else
        porpoise::dolphinbar::log("%s", line);
#endif
}

std::string hex(const std::uint8_t *p, int n)
{
    std::string s;
    char b[4];
    for (int i = 0; i < n; ++i)
    {
        std::snprintf(b, sizeof b, i ? " %02x" : "%02x", p[i]);
        s += b;
    }
    return s;
}

/* The console's own Bluetooth chip (BlueV, section 12): never opened. */
constexpr const char *kConsoleBluetooth = "ugen0.2";

constexpr int kTransfers = 6;
constexpr int kReadA = 1, kReadB = 2, kRumbleOut = 3, kBulkOut = 4, kBulkIn = 5;
constexpr std::uint32_t kBuf = 128;
constexpr std::uint32_t kReport = 64;

/* ---- what the thread knows and publishes ------------------------------------------------- */
std::mutex g_lock;
Status g_status;
std::atomic<bool> g_started{false}, g_run{false}, g_finished{true};
std::atomic<bool> g_rumble_want{false};
std::atomic<std::uint64_t> g_test_rumble_until{0};
/* HOME: when it was last pressed, and whether that press was taken */
std::atomic<std::uint64_t> g_home_at{0};
std::atomic<bool> g_home_taken{true};
/* the Bluetooth link (bt_link): up, its calibration, its address */
std::atomic<bool> g_bt_up{false}, g_usb_open{false};
Calibration g_bt_cal;
std::string g_bt_address;
std::uint32_t g_bt_prev_buttons = 0, g_bt_count = 0;
std::uint64_t g_bt_rate_since = 0;

void publish(const std::function<void(Status &)> &change)
{
    std::lock_guard<std::mutex> g(g_lock);
    change(g_status);
}

/* ---- the device ------------------------------------------------------------------------------ */
struct Device
{
    std::string node;
    int fd = -1;
    bool dead = false;
    std::uint8_t ep_in = 0, ep_out = 0, bulk_out = 0, bulk_in = 0;
    std::uint16_t in_size = kReport, out_size = 0, bulk_out_size = 64, bulk_in_size = 64;
    int if_hid = 0, if_cmd = 1;
    /* the transfer table, which the kernel reads and writes: a Device never moves */
    FsEndpoint eps[kTransfers];
    void *buf_ptr[kTransfers];
    std::uint32_t buf_len[kTransfers];
    std::uint8_t buf[kTransfers][kBuf];
    bool opened[kTransfers];
    bool pending[kTransfers];
    unsigned start_errors = 0, complete_errors = 0;
    Calibration cal;
    /* input */
    std::uint64_t started_at = 0, last_in = 0, last_restart = 0, rate_since = 0;
    std::uint32_t rate_count = 0;
    bool first_logged = false;
    std::uint32_t prev_buttons = 0;
    bool clear_stall = false;
    /* rumble */
    unsigned rumble_seq = 0;
    bool rumble_on = false;
    std::uint64_t rumble_next = 0;
    unsigned rumble_errors = 0, rumble_failed = 0, rumble_switches = 0;
    bool rumble_by_control = false, rumble_confirmed = false;
};

/* Nodes looked at, and what they are (a node name comes back as another
 * device only after a replug, when it's gone from /dev in between). */
struct Seen
{
    std::string node;
    std::uint16_t vid = 0, pid = 0;
    std::string product;
};
std::vector<Seen> g_seen;

void mark_dead(Device &d, const char *what, int err)
{
    if (!d.dead)
        logf("ns2gc: %s: %s failed (errno %d): the controller is gone", d.node.c_str(), what, err);
    d.dead = true;
}

void handle_report(Device &d, const std::uint8_t *r, std::uint32_t len);
void rumble_done(Device &d);

/* Collect every finished transfer; reads are handed on. */
void reap(Device &d)
{
    for (;;)
    {
        FsIndex c{};
        if (sys_ioctl(d.fd, kFsComplete, &c) != 0)
        {
            if (errno == ENXIO || errno == ENODEV)
                mark_dead(d, "USB_FS_COMPLETE", errno);
            else if (errno != EBUSY && d.complete_errors++ < 5)
                logf("ns2gc: %s: USB_FS_COMPLETE errno %d", d.node.c_str(), errno);
            return;
        }
        const int t = c.ep_index;
        if (t <= 0 || t >= kTransfers)
            continue;
        d.pending[t] = false;
        if (t == kRumbleOut)
        {
            rumble_done(d);
            continue;
        }
        if (t != kReadA && t != kReadB)
            continue; /* their callers read eps[t] themselves */
        const int status = d.eps[t].status;
        if (status == 0 && d.eps[t].aFrames > 0 && d.buf_len[t] > 0)
            handle_report(d, d.buf[t], d.buf_len[t]);
        else if (status != 0 && status != kErrCancelled)
        {
            d.clear_stall = true;
            std::uint32_t errors = 0;
            publish([&](Status &s) {
                errors = ++s.read_errors;
                s.last_read_status = status;
            });
            if (errors <= 5)
                logf("ns2gc: %s: read status %d", d.node.c_str(), status);
        }
    }
}

bool start_transfer(Device &d, int t, std::uint32_t len, std::uint16_t flags, std::uint16_t timeout)
{
    d.buf_len[t] = len;
    d.eps[t].nFrames = 1;
    d.eps[t].aFrames = 0;
    d.eps[t].status = 0;
    d.eps[t].flags = flags;
    d.eps[t].timeout = timeout;
    FsIndex s{std::uint8_t(t)};
    if (sys_ioctl(d.fd, kFsStart, &s) != 0)
    {
        if (errno == ENXIO || errno == ENODEV)
            mark_dead(d, "USB_FS_START", errno);
        else if (d.start_errors++ < 5)
            logf("ns2gc: %s: USB_FS_START %d errno %d", d.node.c_str(), t, errno);
        return false;
    }
    d.pending[t] = true;
    return true;
}

void stop(Device &d, int t)
{
    if (!d.opened[t] || !d.pending[t])
        return;
    FsIndex s{std::uint8_t(t)};
    sys_ioctl(d.fd, kFsStop, &s);
}

/* Until transfer t has finished (true) or ms have gone by: then it's stopped
 * (false). Reads that finish meanwhile are handled. */
bool wait_for(Device &d, int t, unsigned ms)
{
    const std::uint64_t until = now_ms() + ms;
    while (d.pending[t] && !d.dead)
    {
        reap(d);
        if (!d.pending[t])
            break;
        if (now_ms() >= until)
        {
            stop(d, t);
            const std::uint64_t drain = now_ms() + 200;
            while (d.pending[t] && !d.dead && now_ms() < drain)
            {
                reap(d);
                if (d.pending[t])
                    sys_sleep_us(1000);
            }
            return false;
        }
        sys_sleep_us(1000);
    }
    return !d.dead;
}

void arm_reads(Device &d)
{
    for (int t : {kReadA, kReadB})
    {
        if (!d.opened[t] || d.pending[t] || d.dead)
            continue;
        std::uint16_t flags = kFlagSingleShortOk;
        if (d.clear_stall)
        {
            flags |= kFlagClearStall;
            d.clear_stall = false;
        }
        start_transfer(d, t, std::min<std::uint32_t>(kReport, d.in_size ? d.in_size : kReport), flags, 0);
    }
}

/* A command on interface 1 and its reply (into reply, up to reply_max; got:
 * its length). False when it couldn't be sent or nothing came back. */
bool command(Device &d, const std::uint8_t *cmd, int len, std::uint8_t *reply, int reply_max, int &got,
             const char *what)
{
    got = 0;
    if (d.pending[kBulkOut] || d.pending[kBulkIn])
        return false;
    std::memcpy(d.buf[kBulkOut], cmd, std::size_t(len));
    if (!start_transfer(d, kBulkOut, std::uint32_t(len), 0, 1000) || !wait_for(d, kBulkOut, 1200))
    {
        logf("ns2gc: %s: %s (%02x %02x): not sent", d.node.c_str(), what, cmd[0], cmd[3]);
        return false;
    }
    if (d.eps[kBulkOut].status != 0)
    {
        logf("ns2gc: %s: %s (%02x %02x): sending failed, status %d", d.node.c_str(), what, cmd[0], cmd[3],
             d.eps[kBulkOut].status);
        return false;
    }
    if (!start_transfer(d, kBulkIn, std::uint32_t(std::min(reply_max, int(kBuf))), kFlagSingleShortOk, 200) ||
        !wait_for(d, kBulkIn, 400))
    {
        logf("ns2gc: %s: %s (%02x %02x): no reply", d.node.c_str(), what, cmd[0], cmd[3]);
        return false;
    }
    if (d.eps[kBulkIn].status != 0 || d.eps[kBulkIn].aFrames == 0)
    {
        logf("ns2gc: %s: %s (%02x %02x): no reply (status %d)", d.node.c_str(), what, cmd[0], cmd[3],
             d.eps[kBulkIn].status);
        return false;
    }
    got = int(d.buf_len[kBulkIn]);
    std::memcpy(reply, d.buf[kBulkIn], std::size_t(got));
    return true;
}

/* 0x40 bytes of its flash at address (SDL's ReadFlashBlock). */
bool read_flash(Device &d, std::uint32_t address, std::uint8_t out[0x40])
{
    /* as SDL's: bytes 8..11 zero, the address in 12..15 */
    const std::uint8_t data[8] = {0, 0, 0, 0, std::uint8_t(address), std::uint8_t(address >> 8),
                                  std::uint8_t(address >> 16), std::uint8_t(address >> 24)};
    std::uint8_t cmd[16], reply[kBuf];
    const int n = make_command(cmd, 0x02, 0x01, data, 8);
    int got = 0;
    char what[32];
    std::snprintf(what, sizeof what, "flash %#x", unsigned(address));
    if (!command(d, cmd, n, reply, 0x50, got, what))
        return false;
    if (got < 0x50)
    {
        logf("ns2gc: %s: flash %#x: reply of %d bytes (%s)", d.node.c_str(), unsigned(address), got,
             hex(reply, std::min(got, 16)).c_str());
        return false;
    }
    std::memcpy(out, reply + 0x10, 0x40);
    return true;
}

void read_calibration(Device &d)
{
    std::uint8_t b[0x40];
    bool sticks = false, triggers = false;
    std::string serial;
    if (read_flash(d, 0x13000, b))
    {
        char s[17] = {};
        std::memcpy(s, b + 2, 16);
        for (char &c : s)
            if (c && (c < 0x20 || c > 0x7e))
                c = '?';
        serial = s;
    }
    if (read_flash(d, 0x13080, b))
        sticks = parse_stick_calibration(b + 0x28, d.cal, 0);
    if (read_flash(d, 0x130c0, b))
        sticks = parse_stick_calibration(b + 0x28, d.cal, 2) && sticks;
    if (read_flash(d, 0x13140, b) && b[0] < 0x80 && b[1] < 0x80)
    {
        d.cal.trigger_zero[0] = b[0];
        d.cal.trigger_zero[1] = b[1];
        triggers = true;
    }
    /* the player's own, from the Switch's calibration, when there is one */
    if (read_flash(d, 0x1fc040, b) && b[0] == 0xb2 && b[1] == 0xa1)
        parse_stick_calibration(b + 2, d.cal, 0);
    if (read_flash(d, 0x1fc080, b) && b[0] == 0xb2 && b[1] == 0xa1)
        parse_stick_calibration(b + 2, d.cal, 2);
    logf("ns2gc: %s: serial %s; sticks %s: centre %d %d / %d %d, reach %d..%d %d..%d / %d..%d %d..%d; triggers "
         "%s: rest %d %d",
         d.node.c_str(), serial.empty() ? "(unread)" : serial.c_str(), sticks ? "calibrated" : "NOT calibrated",
         d.cal.centre[0], d.cal.centre[1], d.cal.centre[2], d.cal.centre[3], d.cal.below[0], d.cal.above[0],
         d.cal.below[1], d.cal.above[1], d.cal.below[2], d.cal.above[2], d.cal.below[3], d.cal.above[3],
         triggers ? "calibrated" : "NOT calibrated", d.cal.trigger_zero[0], d.cal.trigger_zero[1]);
    publish([&](Status &s) {
        s.serial = serial;
        s.stick_calibration = sticks;
        s.trigger_calibration = triggers;
    });
}

/* The set-up SDL's driver sends (HIDAPI_DriverSwitch2_InitUSB), then player
 * 1's light. The last command starts the input reports. */
bool send_setup(Device &d)
{
    struct Cmd
    {
        std::uint8_t command, sub;
        std::uint8_t data[20];
        int length;
        const char *what;
    };
    static const Cmd kSetup[] = {
        {0x07, 0x01, {}, 0, "unknown 07 01"},
        {0x0c, 0x02, {0x27, 0, 0, 0}, 4, "feature mask"},
        {0x11, 0x01, {}, 0, "unknown 11 01"},
        {0x0a, 0x08, {0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x35, 0x00, 0x46, 0, 0, 0, 0, 0, 0, 0, 0},
         20, "vibration set-up"},
        {0x0c, 0x04, {0x27, 0, 0, 0}, 4, "features on"},
        {0x01, 0x0c, {}, 0, "unknown 01 0c"},
        {0x01, 0x01, {}, 0, "vibration on"},
        {0x08, 0x02, {0x01, 0, 0, 0}, 4, "grip buttons"},
        {0x03, 0x0a, {0x05, 0, 0, 0}, 4, "report format 5"},
        {0x09, 0x07, {0, 0, 0, 0, 0, 0, 0, 0}, 8, "player light"},
        {0x03, 0x0d, {0x01, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, 8, "start input"},
    };
    int ok = 0, sent = 0;
    for (const Cmd &c : kSetup)
    {
        std::uint8_t data[20];
        std::memcpy(data, c.data, sizeof data);
        if (c.command == 0x09)
            data[0] = 0x01; /* player 1 */
        std::uint8_t cmd[32], reply[kBuf];
        const int n = make_command(cmd, c.command, c.sub, data, c.length);
        int got = 0;
        ++sent;
        if (command(d, cmd, n, reply, 64, got, c.what))
        {
            ++ok;
            logf("ns2gc: %s: %s: reply %s", d.node.c_str(), c.what, hex(reply, std::min(got, 16)).c_str());
        }
        if (d.dead)
            break;
    }
    publish([&](Status &s) {
        s.commands_ok = ok;
        s.commands_sent = sent;
    });
    return !d.dead && ok > 0;
}

void handle_report(Device &d, const std::uint8_t *r, std::uint32_t len)
{
    const std::uint64_t t = now_ms();
    if (!d.first_logged)
    {
        d.first_logged = true;
        logf("ns2gc: %s: first input report, %u bytes, %llu ms after the set-up: %s", d.node.c_str(), unsigned(len),
             (unsigned long long)(t - d.started_at), hex(r, std::min<int>(int(len), 20)).c_str());
    }
    d.last_in = t;
    ++d.rate_count;
    Status got;
    {
        std::lock_guard<std::mutex> g(g_lock);
        got = g_status;
    }
    if (!decode_report(r, int(len), d.cal, got))
        return;
    if ((got.buttons & GcHome) && !(d.prev_buttons & GcHome))
    {
        g_home_at = t;
        g_home_taken = false;
    }
    if (got.buttons != d.prev_buttons)
    {
        /* Only while the buttons' meaning is being confirmed on a console. */
        static unsigned logged = 0;
        if (logged < 60)
        {
            ++logged;
            logf("ns2gc: buttons %02x %02x %02x", r[5], r[6], r[7]);
        }
    }
    d.prev_buttons = got.buttons;
    publish([&](Status &s) {
        s.buttons = got.buttons;
        s.lx = got.lx;
        s.ly = got.ly;
        s.rx = got.rx;
        s.ry = got.ry;
        s.lt = got.lt;
        s.rt = got.rt;
        std::memcpy(s.raw_stick, got.raw_stick, sizeof s.raw_stick);
        std::memcpy(s.raw_trigger, got.raw_trigger, sizeof s.raw_trigger);
        std::memcpy(s.raw_buttons, got.raw_buttons, sizeof s.raw_buttons);
        s.report_id = got.report_id;
        s.report_length = got.report_length;
        ++s.reports;
        s.last_report_age = 0;
        if (s.link == Link::Starting)
            s.link = Link::Ready;
    });
}

/* ---- descriptors ------------------------------------------------------------------------------ */
bool read_layout(Device &d)
{
    std::uint8_t cfg[512];
    GenDescriptor g{};
    g.ugd_data = cfg;
    g.ugd_maxlen = sizeof cfg;
    g.ugd_config_index = 0xff; /* the current configuration */
    if (sys_ioctl(d.fd, kGetFullDesc, &g) != 0 || g.ugd_actlen < 9)
    {
        logf("ns2gc: %s: no configuration descriptor (errno %d)", d.node.c_str(), errno);
        return false;
    }
    const std::size_t len = g.ugd_actlen;
    int iface = -1;
    for (std::size_t off = 0; off + 2 <= len;)
    {
        const std::uint8_t blen = cfg[off], type = cfg[off + 1];
        if (blen < 2 || off + blen > len)
            break;
        if (type == 4 && blen >= 9) /* interface */
            iface = cfg[off + 3] == 0 ? cfg[off + 2] : -1;
        else if (type == 5 && blen >= 7 && iface >= 0) /* endpoint */
        {
            const std::uint8_t addr = cfg[off + 2], attr = cfg[off + 3] & 3;
            const std::uint16_t size = std::uint16_t(cfg[off + 4] | (cfg[off + 5] << 8)) & 0x7ff;
            const bool in = (addr & 0x80) != 0;
            if (iface == d.if_hid && attr == 3)
            {
                if (in && !d.ep_in)
                    d.ep_in = addr, d.in_size = size;
                else if (!in && !d.ep_out)
                    d.ep_out = addr, d.out_size = size;
            }
            else if (iface == d.if_cmd && attr == 2)
            {
                if (in && !d.bulk_in)
                    d.bulk_in = addr, d.bulk_in_size = size;
                else if (!in && !d.bulk_out)
                    d.bulk_out = addr, d.bulk_out_size = size;
            }
        }
        off += blen;
    }
    logf("ns2gc: %s: interface %d interrupt IN %#x (%u) OUT %#x (%u), interface %d bulk OUT %#x (%u) IN %#x (%u)",
         d.node.c_str(), d.if_hid, d.ep_in, d.in_size, d.ep_out, d.out_size, d.if_cmd, d.bulk_out, d.bulk_out_size,
         d.bulk_in, d.bulk_in_size);
    if (!d.ep_in || !d.bulk_in || !d.bulk_out)
    {
        logf("ns2gc: %s: the descriptor: %s", d.node.c_str(), hex(cfg, int(std::min<std::size_t>(len, 96))).c_str());
        return false;
    }
    return true;
}

bool open_slot(Device &d, int t, std::uint8_t ep, std::uint32_t size)
{
    FsOpen o{};
    o.max_bufsize = std::min<std::uint32_t>(std::max<std::uint32_t>(size, 64), kBuf);
    o.max_frames = 1;
    o.ep_index = std::uint8_t(t);
    o.ep_no = ep;
    if (sys_ioctl(d.fd, kFsOpen, &o) != 0)
    {
        logf("ns2gc: %s: USB_FS_OPEN endpoint %#x (transfer %d) errno %d", d.node.c_str(), ep, t, errno);
        return false;
    }
    d.opened[t] = true;
    return true;
}

void close_device(Device &d)
{
    if (d.fd < 0)
        return;
    if (!d.dead)
    {
        /* the motor off first, if it ran */
        if (d.rumble_on && d.opened[kRumbleOut] && !d.pending[kRumbleOut])
        {
            make_rumble(d.buf[kRumbleOut], d.rumble_seq++, false);
            if (start_transfer(d, kRumbleOut, kReport, 0, 200))
                wait_for(d, kRumbleOut, 250);
        }
        for (int t = 1; t < kTransfers; ++t)
            stop(d, t);
        const std::uint64_t until = now_ms() + 500;
        auto running = [&] {
            for (int t = 1; t < kTransfers; ++t)
                if (d.pending[t])
                    return true;
            return false;
        };
        while (running() && !d.dead && now_ms() < until)
        {
            reap(d);
            if (running())
                sys_sleep_us(1000);
        }
        if (running() && !d.dead)
        {
            logf("ns2gc: %s: transfers still running after being stopped: the node is left open", d.node.c_str());
            return;
        }
        FsIndex u{0};
        sys_ioctl(d.fd, kFsUninit, &u);
    }
    sys_close(d.fd);
    d.fd = -1;
}

/* Open the node, its transfers, and start the controller. Null when that
 * failed (said in the status and the log). */
Device *open_device(const std::string &node, const std::string &product)
{
    publish([&](Status &s) {
        const std::uint32_t gen = s.generation + 1;
        s = Status{};
        s.link = Link::Opening;
        s.node = node;
        s.product = product;
        s.generation = gen;
    });
    auto fail = [&](Device *d, const char *step, int err) -> Device * {
        logf("ns2gc: %s: %s failed (errno %d)", node.c_str(), step, err);
        publish([&](Status &s) {
            s.link = Link::Failed;
            s.failed_step = step;
            s.failed_errno = err;
        });
        if (d)
        {
            close_device(*d);
            if (d->fd < 0)
                delete d; /* else the kernel may still write into it: left */
        }
        return nullptr;
    };
    Device *d = new Device;
    d->node = node;
    d->fd = sys_open(("/dev/" + node).c_str(), false);
    if (d->fd < 0)
    {
        const int e = errno;
        delete d;
        return fail(nullptr, "open", e);
    }
    std::memset(d->eps, 0, sizeof d->eps);
    for (int t = 0; t < kTransfers; ++t)
    {
        d->buf_ptr[t] = d->buf[t];
        d->buf_len[t] = kBuf;
        d->eps[t].ppBuffer = &d->buf_ptr[t];
        d->eps[t].pLength = &d->buf_len[t];
        d->eps[t].nFrames = 1;
        d->opened[t] = d->pending[t] = false;
    }
    if (!read_layout(*d))
        return fail(d, "reading its descriptors", errno);
    int drivers[2] = {-1, -1};
    for (int i = 0; i < 2; ++i)
    {
        int n = i;
        drivers[i] = sys_ioctl(d->fd, kIfaceDriverActive, &n) == 0 ? 1 : 0;
    }
    logf("ns2gc: %s: the console's driver: interface 0 %s, interface 1 %s", node.c_str(),
         drivers[0] ? "attached" : "none", drivers[1] ? "attached" : "none");
    publish([&](Status &s) {
        s.kernel_driver[0] = drivers[0];
        s.kernel_driver[1] = drivers[1];
        s.rumble_out = d->ep_out != 0;
    });
    FsInit init{};
    init.pEndpoints = d->eps;
    init.ep_index_max = kTransfers;
    if (sys_ioctl(d->fd, kFsInit, &init) != 0)
        return fail(d, "USB_FS_INIT", errno);
    if (!open_slot(*d, kBulkOut, d->bulk_out, d->bulk_out_size) ||
        !open_slot(*d, kBulkIn, d->bulk_in, kBuf))
    {
        /* the console's driver holds interface 1: let go of it, once */
        int n = d->if_cmd;
        if (drivers[1] == 1 && sys_ioctl(d->fd, kIfaceDriverDetach, &n) == 0)
            logf("ns2gc: %s: the console's driver detached from interface 1", node.c_str());
        if ((!d->opened[kBulkOut] && !open_slot(*d, kBulkOut, d->bulk_out, d->bulk_out_size)) ||
            (!d->opened[kBulkIn] && !open_slot(*d, kBulkIn, d->bulk_in, kBuf)))
            return fail(d, "opening its command endpoints", errno);
    }
    if (!open_slot(*d, kReadA, d->ep_in, d->in_size) || !open_slot(*d, kReadB, d->ep_in, d->in_size))
        return fail(d, "opening its input endpoint", errno);
    if (d->ep_out)
        open_slot(*d, kRumbleOut, d->ep_out, d->out_size);
    publish([](Status &s) { s.link = Link::Starting; });
    read_calibration(*d);
    if (d->dead)
        return fail(d, "reading its calibration", ENXIO);
    d->started_at = now_ms();
    if (!send_setup(*d))
        return fail(d, "its set-up (no command was answered)", d->dead ? ENXIO : 0);
    d->started_at = now_ms();
    d->rate_since = d->started_at;
    arm_reads(*d);
    return d;
}

/* ---- what is plugged in ------------------------------------------------------------------------ */

bool read_info(int fd, DeviceInfo &info)
{
    return sys_ioctl(fd, kDeviceInfoPs5, &info) == 0 || sys_ioctl(fd, kDeviceInfo, &info) == 0;
}

/* A node that is the controller, or "". Opens each node not seen before,
 * briefly; a node already open elsewhere (the DolphinBar's) is asked again
 * next time. */
std::string find_controller(std::string &product)
{
    std::vector<std::string> names;
    if (!list_dev(names))
        return {};
    g_seen.erase(std::remove_if(g_seen.begin(), g_seen.end(),
                                [&](const Seen &s) {
                                    return std::find(names.begin(), names.end(), s.node) == names.end();
                                }),
                 g_seen.end());
    std::sort(names.begin(), names.end());
    for (const std::string &n : names)
    {
        /* "ugenB.A": not the console's Bluetooth, not a root hub (address 1) */
        const std::size_t dot = n.find('.');
        if (n == kConsoleBluetooth || dot == std::string::npos || n.compare(dot, std::string::npos, ".1") == 0)
            continue;
        auto it = std::find_if(g_seen.begin(), g_seen.end(), [&](const Seen &s) { return s.node == n; });
        if (it == g_seen.end())
        {
            const int fd = sys_open(("/dev/" + n).c_str(), true);
            if (fd < 0)
                continue; /* busy or refused: next time */
            DeviceInfo info{};
            Seen s;
            s.node = n;
            if (read_info(fd, info))
            {
                s.vid = info.vendorNo;
                s.pid = info.productNo;
                info.product[sizeof info.product - 1] = 0;
                s.product = info.product;
            }
            sys_close(fd);
            g_seen.push_back(s);
            it = g_seen.end() - 1;
            if (s.vid == kVendor && s.pid == kProduct)
                logf("ns2gc: found on /dev/%s: %04x:%04x \"%s\"", n.c_str(), s.vid, s.pid, s.product.c_str());
        }
        if (it->vid == kVendor && it->pid == kProduct)
        {
            product = it->product;
            return n;
        }
    }
    return {};
}

void forget(const std::string &node)
{
    g_seen.erase(std::remove_if(g_seen.begin(), g_seen.end(), [&](const Seen &s) { return s.node == node; }),
                 g_seen.end());
}

#ifndef PORPOISE_NS2GC_HOST_TEST
void mark_in_use(const std::string &node, bool in_use) { porpoise::usbhid::set_node_in_use(node, in_use); }
#else
void mark_in_use(const std::string &, bool) {}
#endif

/* ---- the thread -------------------------------------------------------------------------------- */

/* Rumble: the game's, or the test's; sent again every 12 ms while it's on,
 * as SDL's driver does, a stop once when it ends. Over the interrupt OUT;
 * when that keeps failing (or there is none), as a SET_REPORT. */
void service_rumble(Device &d)
{
    const std::uint64_t t = now_ms();
    const bool want = g_rumble_want.load() || t < g_test_rumble_until.load();
    if (want == d.rumble_on && (!want || t < d.rumble_next))
        return;
    if (d.opened[kRumbleOut] && !d.rumble_by_control && d.pending[kRumbleOut])
        return; /* the last one is still going */
    std::uint8_t r[kReport];
    make_rumble(r, d.rumble_seq++, want);
    bool ok = false;
    int err = 0;
    const char *how = "interrupt OUT";
    if (d.opened[kRumbleOut] && !d.rumble_by_control)
    {
        std::memcpy(d.buf[kRumbleOut], r, kReport);
        ok = start_transfer(d, kRumbleOut, kReport, 0, 100);
        err = errno;
    }
    else
    {
        CtlRequest q{};
        q.ucr_data = r;
        q.ucr_flags = 0x0001;
        q.ucr_request.bmRequestType = 0x21;
        q.ucr_request.bRequest = 0x09; /* SET_REPORT, output report 3 */
        q.ucr_request.wValue[0] = 0x03;
        q.ucr_request.wValue[1] = 0x02;
        q.ucr_request.wIndex[0] = std::uint8_t(d.if_hid);
        q.ucr_request.wLength[0] = kReport;
        ok = sys_ioctl(d.fd, kDoRequest, &q) == 0;
        err = errno;
        how = "SET_REPORT";
    }
    if (!ok && d.rumble_errors++ < 5)
        logf("ns2gc: %s: rumble not sent (%s, errno %d)", d.node.c_str(), how, err);
    if (want != d.rumble_on)
    {
        if (d.rumble_switches++ < 40)
            logf("ns2gc: %s: rumble %s (%s%s)", d.node.c_str(), want ? "on" : "off", how, ok ? "" : ", failed");
        publish([&](Status &s) { s.rumbling = want; });
    }
    d.rumble_on = want;
    d.rumble_next = t + 12;
}

/* A rumble report over the interrupt OUT finished: one that the controller
 * refused is said (the first few), and after three, SET_REPORT is used. */
void rumble_done(Device &d)
{
    const int status = d.eps[kRumbleOut].status;
    if (status == 0)
    {
        if (!d.rumble_confirmed)
        {
            d.rumble_confirmed = true;
            logf("ns2gc: %s: first rumble report taken (%u bytes)", d.node.c_str(), unsigned(d.buf_len[kRumbleOut]));
        }
        return;
    }
    if (status == kErrCancelled)
        return;
    if (d.rumble_failed++ < 5)
        logf("ns2gc: %s: rumble report refused, status %d", d.node.c_str(), status);
    if (d.rumble_failed >= 3 && !d.rumble_by_control)
    {
        d.rumble_by_control = true;
        logf("ns2gc: %s: rumble as SET_REPORT from now on", d.node.c_str());
        publish([](Status &s) { s.rumble_out = false; });
    }
}

/* Input stopped: the reads are stopped and armed again (a stall cleared),
 * and if the console's driver has interface 0 it's taken off, once. */
void watch(Device &d)
{
    const std::uint64_t t = now_ms();
    if (t - d.rate_since >= 1000)
    {
        const float rate = float(d.rate_count) * 1000.0f / float(t - d.rate_since);
        const double age = d.last_in ? double(t - d.last_in) / 1000.0 : -1.0;
        publish([&](Status &s) {
            s.report_rate = rate;
            s.last_report_age = age;
        });
        d.rate_count = 0;
        d.rate_since = t;
    }
    const std::uint64_t quiet_since = d.last_in ? d.last_in : d.started_at;
    if (t - quiet_since < 1500 || t - d.last_restart < 3000)
        return;
    d.last_restart = t;
    bool detached = false;
    Status now = status();
    if (now.kernel_driver[0] == 1 && !now.driver_detached)
    {
        int n = d.if_hid;
        detached = sys_ioctl(d.fd, kIfaceDriverDetach, &n) == 0;
        logf("ns2gc: %s: no input for %llu ms with the console's driver on interface 0: detached %s (errno %d)",
             d.node.c_str(), (unsigned long long)(t - quiet_since), detached ? "ok" : "failed", detached ? 0 : errno);
        publish([&](Status &s) { s.driver_detached = true; });
    }
    else
        logf("ns2gc: %s: no input for %llu ms: reads armed again", d.node.c_str(),
             (unsigned long long)(t - quiet_since));
    stop(d, kReadA);
    stop(d, kReadB);
    wait_for(d, kReadA, 100);
    wait_for(d, kReadB, 100);
    d.clear_stall = true;
    /* the input may need the start again after a stall */
    if (!d.last_in)
    {
        std::uint8_t cmd[16], reply[kBuf];
        const std::uint8_t data[8] = {0x01, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        int got = 0;
        command(d, cmd, make_command(cmd, 0x03, 0x0d, data, 8), reply, 64, got, "start input again");
    }
}

void run()
{
    Device *d = nullptr;
    std::uint64_t next_scan = 0;
    unsigned failures = 0;
    std::string failed_node;
    while (g_run)
    {
        const std::uint64_t t = now_ms();
        if (!d)
        {
            if (t >= next_scan)
            {
                std::string product;
                const std::string node = find_controller(product);
                next_scan = t + 1000;
                if (node.empty())
                {
                    failures = 0;
                    if (status().link != Link::NotConnected && !status().bluetooth)
                        publish([](Status &s) {
                            const std::uint32_t gen = s.generation + 1;
                            s = Status{};
                            s.generation = gen;
                        });
                }
                else if (node != failed_node || failures < 3)
                {
                    mark_in_use(node, true);
                    g_usb_open = true;
                    d = open_device(node, product);
                    if (!d)
                    {
                        g_usb_open = false;
                        mark_in_use(node, false);
                        failures = node == failed_node ? failures + 1 : 1;
                        failed_node = node;
                        next_scan = t + 5000;
                        if (failures >= 3)
                            logf("ns2gc: %s: given up until it's plugged in again", node.c_str());
                    }
                    else
                        logf("ns2gc: %s: set up, waiting for input", node.c_str());
                }
            }
            for (int i = 0; i < 25 && g_run; ++i)
                sys_sleep_us(2000);
            continue;
        }
        reap(*d);
        arm_reads(*d);
        service_rumble(*d);
        watch(*d);
        if (d->dead)
        {
            const std::string node = d->node;
            close_device(*d);
            if (d->fd < 0)
                delete d;
            d = nullptr;
            mark_in_use(node, false);
            g_usb_open = false;
            forget(node);
            failures = 0;
            failed_node.clear();
            next_scan = now_ms() + 500;
            publish([](Status &s) {
                const std::uint32_t gen = s.generation + 1;
                s = Status{};
                s.generation = gen;
            });
            logf("ns2gc: %s: unplugged", node.c_str());
            continue;
        }
        sys_sleep_us(2000);
    }
    if (d)
    {
        const std::string node = d->node;
        close_device(*d);
        if (d->fd < 0)
            delete d;
        mark_in_use(node, false);
        logf("ns2gc: %s: closed", node.c_str());
    }
}

void *thread_main(void *)
{
    run();
    g_finished = true;
    return nullptr;
}
} // namespace

void set_log(void (*fn)(const char *line)) { g_log = fn; }
#ifdef PORPOISE_NS2GC_HOST_TEST
void set_sys_ops(const SysOps &ops) { g_sys = ops; }
#endif

Status status()
{
    std::lock_guard<std::mutex> g(g_lock);
    return g_status;
}

bool connected()
{
    std::lock_guard<std::mutex> g(g_lock);
    return g_status.link == Link::Ready;
}

void start()
{
    if (g_started.exchange(true))
        return;
    g_run = true;
    g_finished = false;
    pthread_t t{};
#ifdef PORPOISE_NS2GC_HOST_TEST
    const int r = pthread_create(&t, nullptr, &thread_main, nullptr);
#else
    const int r = create_title_thread(&t, &thread_main, nullptr);
#endif
    if (r != 0)
    {
        g_finished = true;
        logf("ns2gc: no thread (%d)", r);
        return;
    }
    pthread_detach(t);
}

bool close_for_exit(unsigned max_ms)
{
    if (!g_started)
        return true;
    g_run = false;
    const std::uint64_t until = now_ms() + max_ms;
    while (!g_finished && now_ms() < until)
        sys_sleep_us(2000);
    return g_finished;
}

Pad pad()
{
    Pad p;
    std::lock_guard<std::mutex> g(g_lock);
    const Status &s = g_status;
    if (s.link != Link::Ready)
        return p;
    auto stick = [](float v) { return std::int16_t(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f)); };
    p.connected = true;
    p.buttons = s.buttons;
    p.lx = stick(s.lx);
    p.ly = stick(-s.ly);
    p.rx = stick(s.rx);
    p.ry = stick(-s.ry);
    p.lt = std::int16_t(std::lround(s.lt * 32767.0f));
    p.rt = std::int16_t(std::lround(s.rt * 32767.0f));
    return p;
}

bool take_home_press()
{
    /* a press that waited longer was meant for something else (a menu) */
    if (g_home_taken.load() || now_ms() - g_home_at.load() > 500)
        return false;
    return !g_home_taken.exchange(true);
}

void set_rumble(bool on) { g_rumble_want = on; }
void test_rumble(unsigned ms) { g_test_rumble_until = now_ms() + ms; }
bool rumble_wanted() { return g_rumble_want.load() || now_ms() < g_test_rumble_until.load(); }

void bt_link(bool up, const std::string &address, const Calibration *cal)
{
    std::lock_guard<std::mutex> g(g_lock);
    g_bt_up = up;
    if (up)
    {
        g_bt_cal = cal ? *cal : Calibration{};
        g_bt_address = address;
        g_bt_prev_buttons = 0;
        g_bt_count = 0;
        g_bt_rate_since = now_ms();
        logf("ns2gc: Bluetooth %s: connected%s", address.c_str(), cal ? "" : " (no calibration)");
    }
    else
        logf("ns2gc: Bluetooth %s: gone", g_bt_address.c_str());
    if (g_usb_open)
        return; /* its cable has it: the status is the USB one */
    const std::uint32_t gen = g_status.generation + 1;
    g_status = Status{};
    g_status.generation = gen;
    if (up)
    {
        g_status.link = Link::Starting;
        g_status.bluetooth = true;
        g_status.node = address;
        g_status.product = "Nintendo GameCube Controller";
        g_status.stick_calibration = cal != nullptr;
        g_status.trigger_calibration = cal != nullptr;
    }
}

void bt_input(const std::uint8_t *r, int length, bool common)
{
    const std::uint64_t t = now_ms();
    std::lock_guard<std::mutex> g(g_lock);
    if (!g_bt_up || g_usb_open)
    {
        static unsigned ignored = 0;
        if (g_usb_open && ignored++ == 0)
            logf("ns2gc: Bluetooth input while its cable is plugged in: the cable's is used");
        return;
    }
    Status &s = g_status;
    if (!s.bluetooth)
    {
        /* its cable was pulled out: back to Bluetooth */
        const std::uint32_t gen = s.generation + 1;
        s = Status{};
        s.generation = gen;
        s.bluetooth = true;
        s.node = g_bt_address;
        s.product = "Nintendo GameCube Controller";
    }
    if (common)
    {
        /* as by cable, with the report id in front */
        std::uint8_t full[64] = {0x05};
        std::memcpy(full + 1, r, std::size_t(std::min(length, 63)));
        if (!decode_report(full, std::min(length + 1, 64), g_bt_cal, s))
            return;
    }
    else if (!decode_report_0a(r, length, g_bt_cal, s))
        return;
    if (s.link != Link::Ready)
    {
        s.link = Link::Ready;
        logf("ns2gc: Bluetooth: first input report (%s), %d bytes: %s", common ? "0x05" : "0x0a", length,
             hex(r, std::min(length, 16)).c_str());
    }
    if ((s.buttons & GcHome) && !(g_bt_prev_buttons & GcHome))
    {
        g_home_at = t;
        g_home_taken = false;
    }
    if (s.buttons != g_bt_prev_buttons)
    {
        static unsigned logged = 0;
        if (logged < 60)
        {
            ++logged;
            logf("ns2gc: Bluetooth buttons %02x %02x %02x", s.raw_buttons[0], s.raw_buttons[1], s.raw_buttons[2]);
        }
    }
    g_bt_prev_buttons = s.buttons;
    ++s.reports;
    ++g_bt_count;
    s.last_report_age = 0;
    s.rumbling = rumble_wanted();
    if (t - g_bt_rate_since >= 1000)
    {
        s.report_rate = float(g_bt_count) * 1000.0f / float(t - g_bt_rate_since);
        g_bt_count = 0;
        g_bt_rate_since = t;
    }
}
} // namespace porpoise::ns2gc
#endif
