/* PC tests of src/porpoise_ns2gc.cpp: the Switch 2's GameCube controller over
 * a simulated console's ugen node. They test the module's logic (what it
 * sends, how it reads what comes back, plugging in and out), not a PS5 and
 * not the real controller: its replies here are made up in its format.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "porpoise_ns2gc.hpp"
#include "porpoise_ugen.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace n = porpoise::ns2gc;
namespace u = porpoise::ugen;

namespace
{
int g_failures = 0;
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                   \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

std::uint64_t now_ms()
{
    return std::uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count());
}

void pack12(std::uint8_t *p, int x, int y)
{
    p[0] = std::uint8_t(x);
    p[1] = std::uint8_t(((x >> 8) & 0x0f) | ((y & 0x0f) << 4));
    p[2] = std::uint8_t(y >> 4);
}

/* ---- the simulated console and controller ------------------------------------------------ */
struct Sim
{
    std::mutex lock;
    bool plugged = true;
    bool other_busy = false; /* ugen1.2 (another device) can't be opened */
    int opens_of_other = 0;
    int fd_open = -1;
    u::FsEndpoint *eps = nullptr;
    int max = 0;
    std::uint8_t ep_no[8] = {};
    bool pending[8] = {};
    std::deque<int> done;
    std::deque<std::vector<std::uint8_t>> replies; /* bulk IN */
    std::vector<std::vector<std::uint8_t>> commands;
    std::vector<std::vector<std::uint8_t>> rumble;
    bool streaming = false;
    std::uint64_t next_report = 0;
    /* the controller's state */
    std::uint8_t buttons[3] = {};
    int sticks[4] = {2048, 2048, 2048, 2048};
    int triggers[2] = {30, 32};
    unsigned reports = 0;
} g;

std::vector<std::uint8_t> reply_for(const std::uint8_t *c, std::uint32_t len)
{
    std::vector<std::uint8_t> r(16, 0);
    r[0] = c[0];
    r[1] = 0x01;
    r[3] = c[3];
    if (c[0] == 0x02 && c[3] == 0x01 && len >= 16) /* flash read */
    {
        const std::uint32_t a = c[12] | (c[13] << 8) | (c[14] << 16) | (std::uint32_t(c[15]) << 24);
        r.assign(0x50, 0);
        r[0] = 0x02;
        std::uint8_t *d = r.data() + 0x10;
        if (a >= 0x1fc000)
            std::memset(d, 0xff, 0x40); /* no user calibration */
        else if (a == 0x13000)
            std::memcpy(d + 2, "HEJ70012345678", 14);
        else if (a == 0x13080 || a == 0x130c0)
        {
            pack12(d + 0x28, 2048, 2048);
            pack12(d + 0x2b, 1400, 1400); /* above */
            pack12(d + 0x2e, 1300, 1300); /* below */
        }
        else if (a == 0x13140)
        {
            d[0] = 30;
            d[1] = 32;
        }
    }
    if (c[0] == 0x03 && c[3] == 0x0d)
        g.streaming = true;
    return r;
}

void make_report(std::uint8_t *r)
{
    std::memset(r, 0, 64);
    r[0] = 0x09;
    r[1] = std::uint8_t(g.reports++);
    std::memcpy(r + 5, g.buttons, 3);
    pack12(r + 11, g.sticks[0], g.sticks[1]);
    pack12(r + 14, g.sticks[2], g.sticks[3]);
    r[61] = std::uint8_t(g.triggers[0]);
    r[62] = std::uint8_t(g.triggers[1]);
}

void finish(int t, int status, std::uint32_t len)
{
    g.pending[t] = false;
    g.eps[t].status = status;
    g.eps[t].aFrames = status == 0 ? 1 : 0;
    *g.eps[t].pLength = len;
    g.done.push_back(t);
}

/* What is due: reports every 8 ms while streaming, replies waiting. */
void advance()
{
    const std::uint64_t t = now_ms();
    for (int i = 1; i < g.max; ++i)
    {
        if (!g.pending[i])
            continue;
        if (g.ep_no[i] == 0x81 && g.streaming && t >= g.next_report)
        {
            make_report(static_cast<std::uint8_t *>(*g.eps[i].ppBuffer));
            finish(i, 0, 64);
            g.next_report = t + 8;
        }
        else if (g.ep_no[i] == 0x82 && !g.replies.empty())
        {
            const auto rep = g.replies.front();
            g.replies.pop_front();
            const std::uint32_t n = std::min<std::uint32_t>(std::uint32_t(rep.size()), *g.eps[i].pLength);
            std::memcpy(*g.eps[i].ppBuffer, rep.data(), n);
            finish(i, 0, n);
        }
    }
}

int sim_open(const char *path, bool)
{
    std::lock_guard<std::mutex> l(g.lock);
    const std::string p = path;
    if (p == "/dev/ugen1.3")
    {
        if (!g.plugged)
            return errno = ENOENT, -1;
        if (g.fd_open >= 0)
            return errno = EBUSY, -1;
        g.fd_open = 10;
        return 10;
    }
    if (p == "/dev/ugen1.2")
    {
        ++g.opens_of_other;
        if (g.other_busy)
            return errno = EBUSY, -1;
        return 11;
    }
    if (p == "/dev/ugen0.2" || p == "/dev/ugen0.1")
    {
        std::printf("FAIL: %s was opened\n", path);
        ++g_failures;
    }
    return errno = ENOENT, -1;
}

int sim_close(int fd)
{
    std::lock_guard<std::mutex> l(g.lock);
    if (fd == 10)
    {
        g.fd_open = -1;
        g.eps = nullptr;
        g.streaming = false;
        g.done.clear();
        g.replies.clear();
        std::memset(g.pending, 0, sizeof g.pending);
    }
    return 0;
}

const std::uint8_t kConfig[] = {
    9, 2, 55, 0, 2, 1, 0, 0x80, 250,
    9, 4, 0, 0, 2, 3, 0, 0, 0,     /* interface 0: HID */
    9, 0x21, 0x11, 1, 0, 1, 0x22, 61, 0,
    7, 5, 0x81, 3, 64, 0, 4,       /* interrupt IN */
    7, 5, 0x01, 3, 64, 0, 4,       /* interrupt OUT */
    9, 4, 1, 0, 2, 0xff, 0, 0, 0,  /* interface 1: vendor */
    7, 5, 0x02, 2, 64, 0, 0,       /* bulk OUT */
};
const std::uint8_t kConfigEnd[] = {7, 5, 0x82, 2, 64, 0, 0}; /* bulk IN */

int sim_ioctl(int fd, unsigned long req, void *arg)
{
    std::lock_guard<std::mutex> l(g.lock);
    if (fd == 11)
    {
        if (req == u::kDeviceInfoPs5)
        {
            auto *i = static_cast<u::DeviceInfo *>(arg);
            i->vendorNo = 0x054c;
            i->productNo = 0x0ce6;
            return 0;
        }
        return errno = ENOTTY, -1;
    }
    if (fd != 10)
        return errno = EBADF, -1;
    if (!g.plugged && req != u::kFsUninit)
        return errno = ENXIO, -1;
    if (req == u::kDeviceInfoPs5)
    {
        auto *i = static_cast<u::DeviceInfo *>(arg);
        i->vendorNo = 0x057e;
        i->productNo = 0x2073;
        std::strcpy(i->product, "Nintendo GameCube Controller");
        return 0;
    }
    if (req == u::kGetFullDesc)
    {
        auto *d = static_cast<u::GenDescriptor *>(arg);
        std::uint8_t all[sizeof kConfig + sizeof kConfigEnd];
        std::memcpy(all, kConfig, sizeof kConfig);
        std::memcpy(all + sizeof kConfig, kConfigEnd, sizeof kConfigEnd);
        all[2] = sizeof all;
        std::memcpy(d->ugd_data, all, sizeof all);
        d->ugd_actlen = sizeof all;
        return 0;
    }
    if (req == u::kIfaceDriverActive)
        return errno = ENOENT, -1;
    if (req == u::kFsInit)
    {
        auto *i = static_cast<u::FsInit *>(arg);
        g.eps = i->pEndpoints;
        g.max = i->ep_index_max;
        return 0;
    }
    if (req == u::kFsOpen)
    {
        auto *o = static_cast<u::FsOpen *>(arg);
        if (o->ep_index >= g.max)
            return errno = EINVAL, -1;
        g.ep_no[o->ep_index] = o->ep_no;
        return 0;
    }
    if (req == u::kFsStart)
    {
        const int t = static_cast<u::FsIndex *>(arg)->ep_index;
        if (g.pending[t])
            return errno = EBUSY, -1;
        g.pending[t] = true;
        const std::uint8_t *b = static_cast<const std::uint8_t *>(*g.eps[t].ppBuffer);
        const std::uint32_t len = *g.eps[t].pLength;
        if (g.ep_no[t] == 0x02)
        {
            g.commands.emplace_back(b, b + len);
            g.replies.push_back(reply_for(b, len));
            finish(t, 0, len);
        }
        else if (g.ep_no[t] == 0x01)
        {
            g.rumble.emplace_back(b, b + len);
            finish(t, 0, len);
        }
        return 0;
    }
    if (req == u::kFsStop)
    {
        const int t = static_cast<u::FsIndex *>(arg)->ep_index;
        if (g.pending[t])
            finish(t, u::kErrCancelled, 0);
        return 0;
    }
    if (req == u::kFsComplete)
    {
        advance();
        if (g.done.empty())
            return errno = EBUSY, -1;
        static_cast<u::FsIndex *>(arg)->ep_index = std::uint8_t(g.done.front());
        g.done.pop_front();
        return 0;
    }
    if (req == u::kFsUninit)
        return 0;
    return errno = ENOTTY, -1;
}

bool sim_list(std::vector<std::string> &names)
{
    std::lock_guard<std::mutex> l(g.lock);
    names = {"ugen0.1", "ugen0.2", "ugen1.1", "ugen1.2"};
    if (g.plugged)
        names.push_back("ugen1.3");
    return true;
}

void sim_sleep(unsigned us) { usleep(us); }

std::mutex g_log_lock;
std::vector<std::string> g_lines;
void log_line(const char *line)
{
    std::lock_guard<std::mutex> l(g_log_lock);
    g_lines.emplace_back(line);
    if (std::getenv("NS2GC_LOG"))
        std::printf("  log: %s\n", line);
}

template <typename F> bool wait_until(F f, unsigned ms)
{
    const std::uint64_t until = now_ms() + ms;
    while (now_ms() < until)
    {
        if (f())
            return true;
        usleep(2000);
    }
    return f();
}

/* ---- the protocol, alone ------------------------------------------------------------------ */
void test_protocol()
{
    n::Calibration cal;
    std::uint8_t c[9];
    pack12(c, 2000, 2100);
    pack12(c + 3, 1500, 1400);
    pack12(c + 6, 1300, 1200);
    CHECK(n::parse_stick_calibration(c, cal, 0));
    CHECK(cal.centre[0] == 2000 && cal.centre[1] == 2100 && cal.above[0] == 1500 && cal.below[1] == 1200);
    std::uint8_t erased[9];
    std::memset(erased, 0xff, sizeof erased);
    n::Calibration untouched;
    CHECK(!n::parse_stick_calibration(erased, untouched, 2));
    CHECK(untouched.centre[2] == 2048);

    std::uint8_t r[64] = {};
    r[5] = 0x08 | 0x80; /* A, Z */
    r[6] = 0x02 | 0x10; /* START, HOME */
    r[7] = 0x02 | 0x40; /* up, L */
    pack12(r + 11, 2000 + 1500, 2100 - 1200); /* the control stick right and down, all the way */
    pack12(r + 14, 2048, 2048);
    r[61] = 131; /* half way from 30 to 232 */
    r[62] = 232;
    cal.trigger_zero[0] = 30;
    cal.trigger_zero[1] = 30;
    n::Status s;
    CHECK(n::decode_report(r, 64, cal, s));
    CHECK(s.buttons == (n::GcA | n::GcZ | n::GcStart | n::GcHome | n::GcUp | n::GcL));
    CHECK(std::fabs(s.lx - 1.0f) < 1e-4f && std::fabs(s.ly + 1.0f) < 1e-4f);
    CHECK(std::fabs(s.rx) < 0.01f && std::fabs(s.ry) < 0.01f); /* uncalibrated: /2048 */
    CHECK(std::fabs(s.lt - 0.5f) < 0.01f && s.rt == 1.0f);
    CHECK(!n::decode_report(r, 20, cal, s));

    /* its Bluetooth report 0x0A: A, Z, Home, Up; the control stick left all the way */
    std::uint8_t b[63] = {};
    b[2] = 0x02 | 0x10;
    b[3] = 0x08;
    b[4] = 0x01;
    pack12(b + 5, 2000 - 1300, 2100);
    b[12] = 30;
    b[13] = 232;
    n::Status sb;
    CHECK(n::decode_report_0a(b, 63, cal, sb));
    CHECK(sb.buttons == (n::GcA | n::GcZ | n::GcHome | n::GcUp));
    CHECK(std::fabs(sb.lx + 1.0f) < 1e-4f && std::fabs(sb.ly) < 1e-4f);
    CHECK(sb.lt == 0.0f && sb.rt == 1.0f);

    std::uint8_t cmd[32];
    const std::uint8_t data[4] = {0x05, 0, 0, 0};
    CHECK(n::make_command(cmd, 0x03, 0x0a, data, 4) == 12);
    const std::uint8_t want[12] = {0x03, 0x91, 0x00, 0x0a, 0x00, 0x04, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00};
    CHECK(std::memcmp(cmd, want, 12) == 0);
    std::uint8_t rum[64];
    n::make_rumble(rum, 0x13, true);
    CHECK(rum[0] == 0x03 && rum[1] == 0x53 && rum[2] == 1);
    n::make_rumble(rum, 4, false);
    CHECK(rum[1] == 0x54 && rum[2] == 2);
}

/* ---- the controller through the simulated node ------------------------------------------- */
void test_device()
{
    n::SysOps ops{sim_open, sim_close, sim_ioctl, sim_list, sim_sleep, now_ms};
    n::set_sys_ops(ops);
    n::set_log(log_line);
    n::start();
    CHECK(wait_until([] { return n::connected(); }, 3000));
    n::Status s = n::status();
    CHECK(s.node == "ugen1.3");
    CHECK(s.serial == "HEJ70012345678");
    CHECK(s.stick_calibration && s.trigger_calibration);
    CHECK(s.commands_sent == 11 && s.commands_ok == 11);
    CHECK(s.rumble_out);
    {
        std::lock_guard<std::mutex> l(g.lock);
        /* 6 flash reads, then the set-up; the last starts the input */
        CHECK(g.commands.size() == 17);
        CHECK(g.commands.back()[0] == 0x03 && g.commands.back()[3] == 0x0d);
        CHECK(g.commands[6][0] == 0x07); /* the set-up's first */
        for (const auto &c : g.commands)
            CHECK(c.size() >= 8 && c[1] == 0x91 && c.size() == std::size_t(8 + c[5]));
        CHECK(g.opens_of_other == 1); /* looked at once, then remembered */
    }

    /* buttons and sticks, as the game gets them */
    {
        std::lock_guard<std::mutex> l(g.lock);
        g.buttons[0] = 0x08; /* A */
        g.sticks[0] = 2048 + 1400;
        g.sticks[1] = 2048 + 1400; /* up */
        g.triggers[1] = 232;
    }
    CHECK(wait_until([] { return n::pad().buttons == n::GcA; }, 500));
    n::Pad p = n::pad();
    CHECK(p.connected && p.lx == 32767 && p.ly == -32767 && p.rt == 32767 && p.lt == 0);

    /* HOME: once per press */
    {
        std::lock_guard<std::mutex> l(g.lock);
        g.buttons[1] = 0x10;
    }
    CHECK(wait_until([] { return (n::pad().buttons & n::GcHome) != 0; }, 500));
    CHECK(n::take_home_press());
    CHECK(!n::take_home_press());
    {
        std::lock_guard<std::mutex> l(g.lock);
        g.buttons[1] = 0;
    }

    /* rumble on: sent and sent again; off: a stop */
    n::set_rumble(true);
    CHECK(wait_until(
        [] {
            std::lock_guard<std::mutex> l(g.lock);
            return g.rumble.size() >= 3;
        },
        500));
    n::set_rumble(false);
    CHECK(wait_until(
        [] {
            std::lock_guard<std::mutex> l(g.lock);
            return !g.rumble.empty() && g.rumble.back()[2] == 2;
        },
        500));
    {
        std::lock_guard<std::mutex> l(g.lock);
        CHECK(g.rumble.front()[0] == 0x03 && g.rumble.front()[2] == 1 && g.rumble.front().size() == 64);
    }
    CHECK(wait_until([] { return n::status().report_rate > 50; }, 1500));

    /* unplugged, then plugged in again */
    const std::uint32_t gen = n::status().generation;
    {
        std::lock_guard<std::mutex> l(g.lock);
        g.plugged = false;
    }
    CHECK(wait_until([] { return n::status().link == n::Link::NotConnected; }, 2000));
    CHECK(!n::pad().connected);
    CHECK(n::status().generation != gen);
    {
        std::lock_guard<std::mutex> l(g.lock);
        g.commands.clear();
        g.plugged = true;
        g.buttons[0] = 0;
    }
    CHECK(wait_until([] { return n::connected(); }, 3000));

    CHECK(n::close_for_exit(1000));
    {
        std::lock_guard<std::mutex> l(g.lock);
        CHECK(g.fd_open < 0);
    }
}
} // namespace

int main()
{
    test_protocol();
    test_device();
    if (g_failures)
    {
        std::lock_guard<std::mutex> l(g_log_lock);
        for (const auto &line : g_lines)
            std::printf("  log: %s\n", line.c_str());
    }
    std::printf("%s (%d failed)\n", g_failures ? "FAILED" : "all passed", g_failures);
    return g_failures ? 1 : 0;
}
