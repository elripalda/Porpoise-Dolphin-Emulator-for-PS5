/* Porpoise - PC tests of src/porpoise_usbhid.cpp against a simulated console.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * These run on a PC (tests/usbhid/run.sh). They check the module's logic
 * against a model of FreeBSD's ugen interface (USB_FS_*, USB_DO_REQUEST) with
 * a simulated DolphinBar behind it; they say nothing about the console's real
 * kernel or a real DolphinBar, which only tools/dolphinbar-probe on a PS5 can.
 *
 * The model: /dev has a root hub (ugen0.1), the console's Bluetooth chip
 * (ugen0.2, which must never be opened), a DualSense (ugen0.3) and a
 * DolphinBar (ugen1.2, 057e:0306) with four HID interfaces, IN 0x81..0x84
 * and, in one variant, OUT 0x01..0x04. A slot with a Remote answers a status
 * request (0x15) with a status report (0x20); a slot without one stalls the
 * output report, as the DolphinBar does. */
#include "porpoise_dolphinbar.hpp"
#include "porpoise_usbhid.hpp"

#include <hidapi.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace usbhid = porpoise::usbhid;

/* ---- the structures as the kernel sees them (FreeBSD amd64) ------------------------------ */
struct FsEndpoint
{
    void **ppBuffer;
    uint32_t *pLength;
    uint32_t nFrames, aFrames;
    uint16_t flags, timeout, isoc_time_complete;
    int status;
} __attribute__((aligned(8)));
struct FsInit
{
    FsEndpoint *pEndpoints;
    uint8_t ep_index_max;
} __attribute__((aligned(8)));
struct FsOpen
{
    uint32_t max_bufsize, max_frames;
    uint16_t max_packet_length;
    uint8_t dev_index, ep_index, ep_no;
} __attribute__((aligned(4)));
struct CtlRequest
{
    void *data;
    uint16_t flags, actlen;
    uint8_t addr;
    uint8_t bmRequestType, bRequest, wValue[2], wIndex[2], wLength[2];
} __attribute__((aligned(8)));
struct GenDescriptor
{
    void *data;
    uint16_t lang, maxlen, actlen, offset;
    uint8_t config_index, string_index, iface_index, altif_index, endpt_index, report_type, reserved[8];
} __attribute__((aligned(8)));

/* ---- the simulated console ------------------------------------------------------------------ */
struct SimDevice
{
    std::string node;
    uint16_t vid, pid;
    std::string product;
    std::vector<uint8_t> config;
    bool out_endpoints = false;
    bool remote[4] = {false, false, false, false};
    int kernel_driver[4] = {0, 0, 0, 0};
    bool present = true;
};

struct Transfer
{
    bool opened = false;
    uint8_t ep = 0;
    bool pending = false;
    uint64_t started = 0;
};

struct Fd
{
    SimDevice *dev;
    FsEndpoint *eps = nullptr;
    int max = 0;
    Transfer t[32];
    std::deque<uint8_t> done;
};

std::recursive_mutex g_sim;
std::vector<SimDevice *> g_sim_devices;
std::map<int, Fd> g_fds;
int g_next_fd = 10;
std::map<std::string, int> g_opens;         /* opens per node */
std::map<std::string, int> g_max_concurrent; /* the most fds open at once per node */
std::deque<std::vector<uint8_t>> g_input[4];  /* reports waiting at the DolphinBar's slots */
std::vector<std::vector<uint8_t>> g_sent[4]; /* output reports the Remotes took */
uint64_t g_seq = 0;
std::vector<std::string> g_log;

std::vector<uint8_t> dolphinbar_config(bool out)
{
    std::vector<uint8_t> c = {9, 2, 0, 0, 4, 1, 0, 0x80, 50};
    for (int i = 0; i < 4; ++i)
    {
        const uint8_t eps = out ? 2 : 1;
        const uint8_t iface[] = {9, 4, uint8_t(i), 0, eps, 3, 0, 0, 0};
        const uint8_t hid[] = {9, 0x21, 0x11, 0x01, 0, 1, 0x22, 0xd1, 0};
        const uint8_t in[] = {7, 5, uint8_t(0x81 + i), 3, 0x40, 0, 1};
        const uint8_t outep[] = {7, 5, uint8_t(0x01 + i), 3, 0x40, 0, 1};
        c.insert(c.end(), iface, iface + 9);
        c.insert(c.end(), hid, hid + 9);
        c.insert(c.end(), in, in + 7);
        if (out)
            c.insert(c.end(), outep, outep + 7);
    }
    c[2] = uint8_t(c.size());
    c[3] = uint8_t(c.size() >> 8);
    return c;
}

/* Complete the oldest pending read on slot n's IN endpoint with its next report. */
/* Slot n's IN transfers left hanging (as a transfer the console's driver keeps can). */
bool g_hold_in[4] = {};
uint64_t sim_now();

void deliver(int n)
{
    if (g_hold_in[n])
        return;
    for (auto &[fd, f] : g_fds)
    {
        if (!f.dev->present || f.dev->vid != 0x057e)
            continue;
        while (!g_input[n].empty())
        {
            int best = -1;
            for (int t = 0; t < f.max; ++t)
                if (f.t[t].opened && f.t[t].pending && f.t[t].ep == uint8_t(0x81 + n) &&
                    (best < 0 || f.t[t].started < f.t[best].started))
                    best = t;
            if (best < 0)
                return;
            auto &rep = g_input[n].front();
            FsEndpoint &e = f.eps[best];
            std::memcpy(e.ppBuffer[0], rep.data(), rep.size());
            e.pLength[0] = uint32_t(rep.size());
            e.aFrames = 1;
            e.status = 0;
            f.t[best].pending = false;
            f.done.push_back(uint8_t(best));
            g_input[n].pop_front();
        }
    }
}

/* What each simulated Remote has: an attachment (0 none, 1 Nunchuk, 2
 * Classic Controller), a Wii MotionPlus, its MotionPlus mode (0 off, 4, 5 or
 * 7 once turned on), its reporting mode, its LEDs, rumble. Registers as
 * WiiBrew documents them. */
struct SimRemote
{
    int ext = 0;
    bool motion_plus = false;
    int mp_mode = 0;
    int mode = 0x30;
    bool continuous = false;
    int leds = 0;
    bool rumble = false;
    bool ir = false;
    /* As a real one: an attachment's id reads as FF until it was initialised
     * (0x55 to 0xa400f0, 0x00 to 0xa400fb), and only after ext_slow failed
     * reads (a freshly plugged attachment). Counted: MotionPlus switches. */
    bool ext_initialised = false;
    int ext_slow = 0;
    int mp_activations = 0, mp_deactivations = 0;
    /* the speaker: enabled (0x14), muted (0x19), its registers (0xa2xxxx), the data it got */
    bool speaker_enabled = false, speaker_muted = false;
    uint8_t speaker_regs[16] = {};
    std::vector<std::vector<uint8_t>> speaker_data;
    std::vector<uint64_t> speaker_times;
};
SimRemote g_wm[4];

void push_status(int n, int ext_flag = -1)
{
    const SimRemote &w = g_wm[n];
    const bool ext = ext_flag >= 0 ? ext_flag != 0 : (w.ext || w.mp_mode);
    const uint8_t flags = uint8_t((0x10 << n) | (ext ? 0x02 : 0x00));
    g_input[n].push_back({0x20, 0x00, 0x00, flags, 0, 0, 0xc0});
}

/* Every k-th register answer (0x21, 0x22) of slot n taken by the console's own driver (0: none). */
int g_drop_replies[4] = {};
int g_reply_count[4] = {};
bool dropped(int n)
{
    return g_drop_replies[n] && ++g_reply_count[n] % g_drop_replies[n] == 0;
}

void read_reply(int n, uint32_t addr, int size, const std::vector<uint8_t> &data, int error)
{
    if (dropped(n))
        return;
    std::vector<uint8_t> r(22, 0);
    r[0] = 0x21;
    r[3] = uint8_t(((size - 1) << 4) | error);
    r[4] = uint8_t(addr >> 8);
    r[5] = uint8_t(addr);
    for (size_t i = 0; i < data.size() && i < 16; ++i)
        r[6 + i] = data[i];
    g_input[n].push_back(r);
}

/* A Remote in slot n takes an output report. */
bool remote_takes(SimDevice *d, int n, const uint8_t *data, size_t len)
{
    if (!d->remote[n])
        return false;
    g_sent[n].emplace_back(data, data + len);
    SimRemote &w = g_wm[n];
    if (len >= 2 && data[0] >= 0x10)
        w.rumble = data[1] & 1;
    if (data[0] == 0x15) /* status request: a status report */
        push_status(n);
    else if (data[0] == 0x11 && len >= 2)
        w.leds = data[1] >> 4;
    else if (data[0] == 0x12 && len >= 3)
    {
        w.continuous = data[1] & 0x04;
        w.mode = data[2];
    }
    else if (data[0] == 0x14 && len >= 2)
        w.speaker_enabled = data[1] & 0x04;
    else if (data[0] == 0x19 && len >= 2)
        w.speaker_muted = data[1] & 0x04;
    else if (data[0] == 0x18 && len >= 22)
    {
        if (w.speaker_enabled && !w.speaker_muted)
        {
            w.speaker_data.emplace_back(data + 2, data + 2 + (data[1] >> 3));
            w.speaker_times.push_back(sim_now());
        }
    }
    else if (data[0] == 0x13 && len >= 2)
        w.ir = data[1] & 0x04;
    else if (data[0] == 0x16 && len >= 7) /* write */
    {
        const uint32_t addr = uint32_t(data[2] << 16 | data[3] << 8 | data[4]);
        const uint8_t value = data[6];
        if (!dropped(n))
            g_input[n].push_back({0x22, 0, 0, 0x16, 0x00});
        if ((addr & 0xffff00) == 0xa20000)
            for (int k = 0; k < data[5] && (addr & 0xff) + k < 16; ++k)
                w.speaker_regs[(addr & 0xff) + k] = data[6 + k];
        if (addr == 0xa600fe && w.motion_plus)
        {
            /* Turned on, the MotionPlus drops the attachment and comes back as
             * the attachment itself: two status reports. */
            ++w.mp_activations;
            push_status(n, 0);
            w.mp_mode = value;
            push_status(n, 1);
        }
        else if (addr == 0xa400f0 && value == 0x55 && w.mp_mode)
        {
            /* the MotionPlus off again: it goes, then what's plugged into it comes back */
            ++w.mp_deactivations;
            w.mp_mode = 0;
            w.ext_initialised = true;
            push_status(n, 0);
            if (w.ext)
                push_status(n, 1);
        }
        else if (addr == 0xa400f0 && value == 0x55)
            w.ext_initialised = false;
        else if (addr == 0xa400fb && value == 0x00)
            w.ext_initialised = true;
    }
    else if (data[0] == 0x17 && len >= 7) /* read */
    {
        const int space = data[1] & 0xfe;
        const uint32_t addr = uint32_t(data[2] << 16 | data[3] << 8 | data[4]);
        const int size = data[6];
        if (space == 0x00 && addr == 0x16)
            read_reply(n, addr, size, {0x80, 0x80, 0x80, 0x00, 0x9a, 0x9a, 0x9a, 0x00, 0x40, 0x00}, 0);
        else if (addr == 0xa400fa && w.mp_mode)
            read_reply(n, addr, size, {0, 0, 0xa4, 0x20, uint8_t(w.mp_mode), 0x05}, 0);
        else if (addr == 0xa400fa && w.ext && !w.ext_initialised)
            read_reply(n, addr, size, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, 0);
        else if (addr == 0xa400fa && w.ext && w.ext_slow > 0)
        {
            --w.ext_slow;
            read_reply(n, addr, size, {}, 7);
        }
        else if (addr == 0xa400fa && w.ext == 1)
            read_reply(n, addr, size, {0, 0, 0xa4, 0x20, 0x00, 0x00}, 0);
        else if (addr == 0xa400fa && w.ext == 2)
            read_reply(n, addr, size, {1, 0, 0xa4, 0x20, 0x01, 0x01}, 0);
        else if (addr == 0xa40020 && w.ext == 1)
            read_reply(n, addr, size, {0x80, 0x80, 0x80, 0x00, 0xb3, 0xb3, 0xb3, 0x00}, 0);
        else if (addr == 0xa600fa && w.motion_plus && !w.mp_mode)
            read_reply(n, addr, size, {0, 0, 0xa6, 0x20, 0x00, 0x05}, 0);
        else
            read_reply(n, addr, size, {}, 7); /* nothing there */
    }
    deliver(n);
    return true;
}

void inject(int n, std::vector<uint8_t> report)
{
    std::lock_guard<std::recursive_mutex> g(g_sim);
    g_input[n].push_back(std::move(report));
    deliver(n);
}

SimDevice *sim_find(const std::string &node)
{
    for (SimDevice *d : g_sim_devices)
        if (d->node == node && d->present)
            return d;
    return nullptr;
}

std::string g_refuse; /* a node whose open is refused (EACCES) */

int sim_open(const char *path, bool)
{
    std::lock_guard<std::recursive_mutex> g(g_sim);
    const std::string node = std::string(path).substr(5);
    if (node == g_refuse)
    {
        errno = EACCES;
        return -1;
    }
    if (node == "ugen0.2")
    {
        std::fprintf(stderr, "FAIL: the console's Bluetooth chip was opened\n");
        std::exit(1);
    }
    SimDevice *d = sim_find(node);
    if (!d)
    {
        errno = ENOENT;
        return -1;
    }
    for (auto &[fd, f] : g_fds)
        if (f.dev == d)
        {
            errno = EBUSY; /* a ugen node is opened once at a time */
            return -1;
        }
    const int fd = g_next_fd++;
    g_fds[fd] = Fd{};
    g_fds[fd].dev = d;
    ++g_opens[node];
    int now = 0;
    for (auto &[k, f] : g_fds)
        now += f.dev == d;
    g_max_concurrent[node] = std::max(g_max_concurrent[node], now);
    return fd;
}

int sim_close(int fd)
{
    std::lock_guard<std::recursive_mutex> g(g_sim);
    g_fds.erase(fd);
    return 0;
}

/* SET_REPORT as a slow, busy DolphinBar: each takes this long (outside the
 * simulation's lock, as a real one blocks only its caller), every k-th
 * speaker report of a slot is refused, and the next n other reports. */
std::atomic<int> g_set_report_ms{0};
int g_refuse_speaker_every[4] = {};
int g_refuse_other_next[4] = {};
int g_speaker_count[4] = {};
int g_set_reports[4] = {}; /* SET_REPORTs that reached the DolphinBar, per slot */
int g_fs_closes = 0, g_fs_uninits = 0; /* transfer teardowns the console was asked for */
bool g_stop_ignored = false;           /* USB_FS_STOP doesn't end a running read */

int sim_ioctl(int fd, unsigned long req, void *arg)
{
    if (req == usbhid::abi::do_request() && g_set_report_ms > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(g_set_report_ms));
    std::lock_guard<std::recursive_mutex> g(g_sim);
    auto it = g_fds.find(fd);
    if (it == g_fds.end())
    {
        errno = EBADF;
        return -1;
    }
    Fd &f = it->second;
    SimDevice *d = f.dev;
    if (!d->present)
    {
        errno = ENXIO;
        return -1;
    }
    if (req == usbhid::abi::device_info_ps5())
    {
        auto *b = static_cast<uint8_t *>(arg);
        std::memset(b, 0, 0x178);
        std::memcpy(b + 0, &d->pid, 2);
        std::memcpy(b + 2, &d->vid, 2);
        std::snprintf(reinterpret_cast<char *>(b + 40), 128, "%s", d->product.c_str());
        return 0;
    }
    if (req == usbhid::abi::get_full_desc())
    {
        auto *g2 = static_cast<GenDescriptor *>(arg);
        if (g2->config_index != 0xff || d->config.empty())
        {
            errno = EINVAL;
            return -1;
        }
        const size_t n = std::min<size_t>(g2->maxlen, d->config.size());
        std::memcpy(g2->data, d->config.data(), n);
        g2->actlen = uint16_t(n);
        return 0;
    }
    if (req == usbhid::abi::iface_driver_active())
    {
        const int n = *static_cast<int *>(arg);
        if (n >= 0 && n < 4 && d->kernel_driver[n])
            return 0;
        errno = ENXIO;
        return -1;
    }
    if (req == usbhid::abi::iface_driver_detach())
    {
        const int n = *static_cast<int *>(arg);
        if (n >= 0 && n < 4)
            d->kernel_driver[n] = 0;
        return 0;
    }
    if (req == usbhid::abi::do_request())
    {
        auto *r = static_cast<CtlRequest *>(arg);
        const uint16_t len = uint16_t(r->wLength[0] | (r->wLength[1] << 8));
        if (r->bmRequestType == 0x21 && r->bRequest == 0x09) /* SET_REPORT */
        {
            const int n = r->wIndex[0];
            const auto *data = static_cast<const uint8_t *>(r->data);
            bool refused = false;
            if (n <= 3)
                ++g_set_reports[n];
            if (n <= 3 && data[0] == 0x18 && g_refuse_speaker_every[n] && ++g_speaker_count[n] % g_refuse_speaker_every[n] == 0)
                refused = true;
            else if (n <= 3 && data[0] != 0x18 && g_refuse_other_next[n] > 0)
            {
                --g_refuse_other_next[n];
                refused = true;
            }
            if (refused || d->out_endpoints || n > 3 || (r->wValue[1] != 2) || r->wValue[0] != data[0] ||
                !remote_takes(d, n, data, len))
            {
                errno = EIO; /* stalled */
                return -1;
            }
            r->actlen = len;
            return 0;
        }
        errno = EINVAL;
        return -1;
    }
    if (req == usbhid::abi::fs_init())
    {
        auto *in = static_cast<FsInit *>(arg);
        f.eps = in->pEndpoints;
        f.max = in->ep_index_max;
        return 0;
    }
    if (req == usbhid::abi::fs_uninit())
    {
        ++g_fs_uninits;
        f.eps = nullptr;
        f.max = 0;
        return 0;
    }
    if (req == usbhid::abi::fs_open())
    {
        auto *o = static_cast<FsOpen *>(arg);
        const bool in = o->ep_no & 0x80;
        const int n = (o->ep_no & 0x0f) - 1;
        if (o->ep_index >= f.max || f.t[o->ep_index].opened || n < 0 || n > 3 || (!in && !d->out_endpoints))
        {
            errno = EINVAL;
            return -1;
        }
        f.t[o->ep_index] = Transfer{true, o->ep_no, false, 0};
        o->max_packet_length = 64;
        return 0;
    }
    if (req == usbhid::abi::fs_close())
    {
        const uint8_t t = *static_cast<uint8_t *>(arg);
        ++g_fs_closes;
        f.t[t] = Transfer{};
        return 0;
    }
    if (req == usbhid::abi::fs_start())
    {
        const uint8_t t = *static_cast<uint8_t *>(arg);
        if (t >= f.max || !f.t[t].opened || f.t[t].pending)
        {
            errno = EINVAL;
            return -1;
        }
        FsEndpoint &e = f.eps[t];
        const int n = (f.t[t].ep & 0x0f) - 1;
        if (f.t[t].ep & 0x80)
        {
            f.t[t].pending = true;
            f.t[t].started = ++g_seq;
            deliver(n);
        }
        else
        {
            const uint8_t *data = static_cast<const uint8_t *>(e.ppBuffer[0]);
            e.status = remote_takes(d, n, data, e.pLength[0]) ? 0 : 22 /* USB_ERR_STALLED */;
            e.aFrames = e.status ? 0 : 1;
            f.done.push_back(t);
        }
        return 0;
    }
    if (req == usbhid::abi::fs_stop())
    {
        const uint8_t t = *static_cast<uint8_t *>(arg);
        if (f.t[t].pending && !g_stop_ignored)
        {
            f.t[t].pending = false;
            f.eps[t].status = 5; /* USB_ERR_CANCELLED */
            f.eps[t].aFrames = 0;
            f.done.push_back(t);
        }
        return 0;
    }
    if (req == usbhid::abi::fs_complete())
    {
        if (f.done.empty())
        {
            errno = EBUSY;
            return -1;
        }
        *static_cast<uint8_t *>(arg) = f.done.front();
        f.done.pop_front();
        return 0;
    }
    errno = ENOTTY;
    return -1;
}

bool sim_list(std::vector<std::string> &names)
{
    std::lock_guard<std::recursive_mutex> g(g_sim);
    for (SimDevice *d : g_sim_devices)
        if (d->present)
            names.push_back(d->node);
    return true;
}

void sim_sleep(unsigned us) { std::this_thread::sleep_for(std::chrono::microseconds(us)); }
uint64_t sim_now()
{
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count());
}

/* ---- the tests -------------------------------------------------------------------------------- */
int g_failures = 0;
#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                      \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

SimDevice g_hub{"ugen0.1", 0x1d6b, 0x0002, "hub", {}};
SimDevice g_bt{"ugen0.2", 0x0e8d, 0x3603, "bt", {}};
SimDevice g_pad{"ugen0.3", 0x054c, 0x0ce6, "DualSense Wireless Controller", {}};
SimDevice g_bar{"ugen1.2", 0x057e, 0x0306, "Nintendo RVL-CNT-01", {}};

void reset(bool out_endpoints, bool remotes0, bool remotes1, bool remotes2, bool remotes3)
{
    usbhid::shutdown(); /* devices stay open between handles: each test starts with none */
    std::lock_guard<std::recursive_mutex> g(g_sim);
    g_fs_closes = g_fs_uninits = 0;
    g_stop_ignored = false;
    g_bar.present = true;
    g_bar.node = "ugen1.2";
    g_bar.out_endpoints = out_endpoints;
    g_bar.config = dolphinbar_config(out_endpoints);
    const bool r[4] = {remotes0, remotes1, remotes2, remotes3};
    for (int i = 0; i < 4; ++i)
    {
        g_bar.remote[i] = r[i];
        g_bar.kernel_driver[i] = 0;
        g_input[i].clear();
        g_sent[i].clear();
    }
    g_opens.clear();
    g_max_concurrent.clear();
}

bool no_interface_open()
{
    for (const auto &d : usbhid::status())
        for (const auto &i : d.interfaces)
            if (i.open)
                return false;
    return true;
}

/* Dolphin's IOhidapi.cpp IsDeviceUsable, as it is. */
bool is_device_usable(const char *path)
{
    hid_device *h = hid_open_path(path);
    if (!h)
        return false;
    static const unsigned char report[] = {0x15, 0};
    const int result = hid_write(h, report, sizeof report);
    const bool epipe = result == -1 && errno == EPIPE;
    hid_close(h);
    if (result == -1)
        CHECK(epipe); /* the DolphinBar's "no Remote here" */
    return result != -1;
}

std::vector<std::string> enumerate_paths()
{
    std::vector<std::string> paths;
    hid_device_info *list = hid_enumerate(0, 0);
    for (hid_device_info *i = list; i; i = i->next)
    {
        CHECK(i->vendor_id == 0x057e && i->product_id == 0x0306);
        CHECK(i->product_string != nullptr);
        paths.push_back(i->path);
    }
    hid_free_enumeration(list);
    return paths;
}

void test_enumeration(bool out)
{
    reset(out, true, false, true, false);
    const auto paths = enumerate_paths();
    CHECK(paths.size() == 4);
    CHECK(paths.size() == 4 && paths[0] == "ugen1.2:i0" && paths[3] == "ugen1.2:i3");
    CHECK(g_opens["ugen0.3"] <= 1); /* the DualSense: read once, then remembered */
    enumerate_paths();
    enumerate_paths();
    CHECK(g_opens["ugen0.3"] <= 1);
    CHECK(g_opens["ugen0.1"] == 0);
}

void test_usable(bool out)
{
    reset(out, true, false, true, false);
    enumerate_paths();
    CHECK(is_device_usable("ugen1.2:i0"));
    CHECK(!is_device_usable("ugen1.2:i1"));
    CHECK(is_device_usable("ugen1.2:i2"));
    CHECK(!is_device_usable("ugen1.2:i3"));
    CHECK(!is_device_usable("ugen1.2:i7"));
    CHECK(!is_device_usable("ugen9.9:i0"));
}

void test_read_write(bool out)
{
    reset(out, true, true, false, false);
    enumerate_paths();
    hid_device *a = hid_open_path("ugen1.2:i0");
    hid_device *b = hid_open_path("ugen1.2:i1");
    CHECK(a && b);
    CHECK(hid_open_path("ugen1.2:i0") == nullptr); /* already open */
    CHECK(g_max_concurrent["ugen1.2"] == 1);        /* one node open for both */
    unsigned char buf[64];
    CHECK(hid_read_timeout(a, buf, sizeof buf, 20) == 0); /* nothing yet */
    const unsigned char status_req[] = {0x15, 0x00};
    CHECK(hid_write(a, status_req, 2) == 2);
    int n = hid_read_timeout(a, buf, sizeof buf, 200);
    CHECK(n == 7 && buf[0] == 0x20 && buf[3] == 0x10);
    CHECK(hid_read_timeout(b, buf, sizeof buf, 10) == 0); /* not b's */
    const unsigned char leds[] = {0x11, 0x20};
    CHECK(hid_write(b, leds, 2) == 2);
    CHECK(g_sent[1].size() == 1 && g_sent[1][0] == std::vector<uint8_t>({0x11, 0x20}));
    /* reports keep their order and their slot */
    for (int i = 0; i < 40; ++i)
    {
        inject(0, {0x30, uint8_t(i), 0});
        inject(1, {0x30, uint8_t(100 + i), 0});
    }
    for (int i = 0; i < 40; ++i)
    {
        n = hid_read_timeout(a, buf, sizeof buf, 200);
        CHECK(n == 3 && buf[1] == i);
        n = hid_read_timeout(b, buf, sizeof buf, 200);
        CHECK(n == 3 && buf[1] == 100 + i);
    }
    /* the Remote in slot 0 goes away and comes back */
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.remote[0] = false;
    }
    errno = 0;
    CHECK(hid_write(a, leds, 2) == -1 && errno == EPIPE);
    bool remote0 = true;
    for (const auto &d : usbhid::status())
        for (const auto &i : d.interfaces)
            if (i.number == 0)
                remote0 = i.remote;
    CHECK(!remote0);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.remote[0] = true;
    }
    CHECK(hid_write(a, leds, 2) == 2);
    hid_close(a);
    hid_close(b);
    {
        /* closing a handle tears nothing down: the node and its transfers stay
         * (a teardown can wait in the console's kernel for good) */
        std::lock_guard<std::recursive_mutex> g(g_sim);
        CHECK(g_fds.size() == 1 && g_fs_closes == 0 && g_fs_uninits == 0);
    }
    usbhid::shutdown();
    std::lock_guard<std::recursive_mutex> g(g_sim);
    CHECK(g_fds.empty() && g_fs_uninits == 1); /* closed for Porpoise's exit */
}

void test_unplug()
{
    reset(false, true, false, false, false);
    enumerate_paths();
    hid_device *a = hid_open_path("ugen1.2:i0");
    CHECK(a != nullptr);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.present = false;
    }
    unsigned char buf[64];
    CHECK(hid_read_timeout(a, buf, sizeof buf, 50) == -1);
    const unsigned char rumble_off[] = {0x10, 0x00};
    CHECK(hid_write(a, rumble_off, 2) == -1);
    hid_close(a);
    CHECK(enumerate_paths().empty());
    /* plugged in again, under another name */
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.present = true;
        g_bar.node = "ugen1.3";
        g_fds.clear();
    }
    const auto paths = enumerate_paths();
    CHECK(paths.size() == 4 && paths[0] == "ugen1.3:i0");
    CHECK(is_device_usable("ugen1.3:i0"));
}

void test_layout_assumed()
{
    reset(false, true, false, false, false);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.node = "ugen1.4";
        g_bar.config.clear(); /* neither the kernel's copy nor GET_DESCRIPTOR */
    }
    const auto paths = enumerate_paths();
    CHECK(paths.size() == 4);
    CHECK(is_device_usable("ugen1.4:i0"));
    bool assumed = false;
    for (const auto &d : usbhid::status())
        assumed |= d.node == "ugen1.4" && !d.descriptors_read;
    CHECK(assumed);
}

void test_kernel_driver()
{
    reset(false, true, false, false, false);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.node = "ugen1.5";
        g_bar.kernel_driver[0] = 1;
    }
    enumerate_paths();
    usbhid::set_detach_kernel_driver(false);
    hid_device *a = hid_open_path("ugen1.5:i0");
    CHECK(a != nullptr);
    hid_close(a);
    CHECK(g_bar.kernel_driver[0] == 1); /* left alone by default */
    usbhid::set_detach_kernel_driver(true);
    a = hid_open_path("ugen1.5:i0");
    hid_close(a);
    CHECK(g_bar.kernel_driver[0] == 0);
    usbhid::set_detach_kernel_driver(false);
}

/* Four Remotes, each with Dolphin's read thread and write thread (and its
 * once-a-second write test), while the scanner enumerates: nothing is lost,
 * mixed up or deadlocked. */
void test_four_players(bool out)
{
    reset(out, true, true, true, true);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.node = out ? "ugen2.2" : "ugen2.3";
    }
    const auto paths = enumerate_paths();
    CHECK(paths.size() == 4);
    std::atomic<bool> stop{false};
    std::atomic<int> got[4] = {0, 0, 0, 0}, wrong{0}, write_fail{0};
    hid_device *h[4];
    for (int i = 0; i < 4; ++i)
        h[i] = hid_open_path(paths[size_t(i)].c_str());
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i)
    {
        threads.emplace_back([&, i] {
            unsigned char buf[64];
            int expect = 0;
            while (!stop)
            {
                const int n = hid_read_timeout(h[i], buf, sizeof buf, 200);
                if (n == 4 && buf[0] == 0x31)
                {
                    if (buf[1] != i || buf[2] != uint8_t(expect))
                        ++wrong;
                    expect = buf[2] + 1;
                    ++got[i];
                }
            }
        });
        threads.emplace_back([&, i] {
            const unsigned char rumble_off[] = {0x10, 0x00};
            while (!stop)
            {
                if (hid_write(h[i], rumble_off, 2) != 2)
                    ++write_fail;
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
        });
    }
    threads.emplace_back([&] {
        while (!stop)
        {
            enumerate_paths();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });
    /* 200 reports a second per Remote, for a second */
    for (int k = 0; k < 200; ++k)
    {
        for (int i = 0; i < 4; ++i)
            inject(i, {0x31, uint8_t(i), uint8_t(k), 0});
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    stop = true;
    for (auto &t : threads)
        t.join();
    for (int i = 0; i < 4; ++i)
    {
        CHECK(got[i] == 200);
        hid_close(h[i]);
    }
    CHECK(wrong == 0);
    CHECK(write_fail == 0);
}

/* ---- the protocol's decoding, from WiiBrew's byte layouts ------------------------------------ */
namespace db = porpoise::dolphinbar;

void test_decode()
{
    db::Remote r;
    /* 0x37: A and Up held, accel 0x200/0x200/0x29a (10 bits), two IR dots, Nunchuk ext */
    r.ext = db::Extension::Nunchuk;
    uint8_t rep[22] = {0x37, 0x08, 0x08, 0x80, 0x80, 0xa6};
    /* accel lows: x bits in byte1 6-5 (0), y bit in byte2 5, z bit in byte2 6 */
    rep[2] |= 0x40; /* z low bit 1 -> z = 0xa6 << 2 | 2 = 666 */
    /* IR basic: dot1 (100, 200), dot2 (900, 700); second pair empty */
    const int x1 = 100, y1 = 200, x2 = 900, y2 = 700;
    rep[6] = uint8_t(x1);
    rep[7] = uint8_t(y1);
    rep[8] = uint8_t(((y1 >> 8) << 6) | ((x1 >> 8) << 4) | ((y2 >> 8) << 2) | (x2 >> 8));
    rep[9] = uint8_t(x2);
    rep[10] = uint8_t(y2);
    std::memset(rep + 11, 0xff, 5);
    /* Nunchuk: stick (30, 220), accel x 0x80<<2|1, C pressed (bit1 = 0), Z released (bit0 = 1) */
    const uint8_t nc[6] = {30, 220, 0x80, 0x90, 0xa0, uint8_t(0x01 | (1 << 2))};
    std::memcpy(rep + 16, nc, 6);
    db::decode_report(r, rep, 22);
    CHECK(r.buttons == (db::RbA | db::RbUp));
    CHECK(r.accel_raw[0] == 512 && r.accel_raw[1] == 512 && r.accel_raw[2] == 666);
    CHECK(std::fabs(r.accel_g[2] - 154.0f / 104.0f) < 0.01f); /* (666 - 512) / (616 - 512) */
    CHECK(r.ir[0].visible && r.ir[0].x == 100 && r.ir[0].y == 200);
    CHECK(r.ir[1].visible && r.ir[1].x == 900 && r.ir[1].y == 700);
    CHECK(!r.ir[2].visible && !r.ir[3].visible);
    CHECK(r.nc_stick[0] == 30 && r.nc_stick[1] == 220 && r.nc_c && !r.nc_z);
    CHECK(r.nc_accel_raw[0] == (0x80 << 2 | 1) && r.nc_accel_raw[1] == 0x90 << 2 && r.nc_accel_raw[2] == 0xa0 << 2);

    /* Classic Controller: every button released but B, ZL and Home; sticks and triggers */
    db::Remote c;
    c.ext = db::Extension::Classic;
    const int lx = 10, ly = 50, rx = 25, ry = 7, lt = 19, rt = 31;
    uint8_t cc[6];
    cc[0] = uint8_t(((rx >> 3) & 3) << 6 | lx);
    cc[1] = uint8_t(((rx >> 1) & 3) << 6 | ly);
    cc[2] = uint8_t((rx & 1) << 7 | ((lt >> 3) & 3) << 5 | ry);
    cc[3] = uint8_t((lt & 7) << 5 | rt);
    cc[4] = uint8_t(~0x08 & 0xff); /* Home */
    cc[5] = uint8_t(~(0x40 | 0x80) & 0xff); /* B, ZL */
    db::decode_extension(c, cc);
    CHECK(c.cc_left[0] == lx && c.cc_left[1] == ly && c.cc_right[0] == rx && c.cc_right[1] == ry);
    CHECK(c.cc_lt == lt && c.cc_rt == rt);
    CHECK(c.cc_buttons == (db::CbHome | db::CbB | db::CbZL));

    /* MotionPlus: yaw 8192 + 400 (slow), roll 8192, pitch 8192 - 1000 (fast) */
    db::Remote m;
    m.ext = db::Extension::MotionPlusNunchuk;
    const int yaw = 8592, roll = 8192, pitch = 7192;
    const uint8_t mp[6] = {uint8_t(yaw), uint8_t(roll), uint8_t(pitch), uint8_t((yaw >> 8) << 2 | 0x02 /* yaw slow */),
                           uint8_t((roll >> 8) << 2 | 0x02), uint8_t((pitch >> 8) << 2 | 0x02 /* M+ data */)};
    db::decode_extension(m, mp);
    CHECK(m.mp_raw[0] == yaw && m.mp_raw[1] == roll && m.mp_raw[2] == pitch);
    CHECK(m.mp_slow[0] && !m.mp_slow[2]);
    CHECK(std::fabs(m.mp_dps[0] - 20.0f) < 0.01f);
    /* the same Remote's Nunchuk, passed through (byte 5 bit 1 clear): C held */
    const uint8_t pt[6] = {128, 128, 0x80, 0x80, uint8_t(0x80 | 1), uint8_t(0x04)};
    db::decode_extension(m, pt);
    CHECK(m.nc_c && !m.nc_z && m.ext_reports == 1 && m.mp_reports == 1);

    /* status: battery and LEDs */
    db::Remote s;
    const uint8_t st[7] = {0x20, 0x00, 0x00, 0x23, 0, 0, 0x64};
    db::decode_report(s, st, 7);
    CHECK(s.have_status && s.battery_percent == 50 && s.battery_low && s.leds == 2);

    /* ids and calibration */
    const uint8_t nunchuk[6] = {0, 0, 0xa4, 0x20, 0, 0}, classic[6] = {1, 0, 0xa4, 0x20, 1, 1},
                  mpn[6] = {0, 0, 0xa4, 0x20, 5, 5}, empty[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
                  board[6] = {0, 0, 0xa4, 0x20, 4, 2};
    CHECK(db::identify(nunchuk) == db::Extension::Nunchuk && db::identify(classic) == db::Extension::Classic);
    CHECK(db::identify(mpn) == db::Extension::MotionPlusNunchuk && db::identify(empty) == db::Extension::Unknown);
    CHECK(db::identify(board) == db::Extension::Other);
    int zero[3], one[3];
    const uint8_t cal[8] = {0x80, 0x81, 0x82, 0x1b /* x 01, y 10, z 11 */, 0x9a, 0x9a, 0x9a, 0x00};
    CHECK(db::parse_calibration(cal, zero, one) && zero[0] == 513 && zero[1] == 518 && zero[2] == 523 && one[0] == 616);
    const uint8_t blank[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(!db::parse_calibration(blank, zero, one));
}

/* ---- the monitor against the simulated DolphinBar ---------------------------------------------- */
template <typename F> bool wait_for(F cond, int ms = 6000)
{
    for (int i = 0; i < ms / 20; ++i)
    {
        if (cond())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return cond();
}

std::vector<uint8_t> report37(uint16_t buttons, const uint8_t ext[6])
{
    std::vector<uint8_t> r(22, 0xff);
    r[0] = 0x37;
    r[1] = uint8_t(buttons >> 8);
    r[2] = uint8_t(buttons);
    r[3] = 0x80;
    r[4] = 0x80;
    r[5] = 0x9a; /* about 1 g on z */
    r[6] = 50;   /* one IR dot at (50, 60) */
    r[7] = 60;
    r[8] = 0x00 | 0x0f; /* dot 2's high bits all ones: not visible */
    for (int i = 0; i < 6; ++i)
        r[16 + i] = ext[i];
    return r;
}

void test_monitor()
{
    reset(false, true, true, false, false);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.node = "ugen3.2";
        g_wm[0] = SimRemote{};
        g_wm[0].ext = 1; /* a Nunchuk */
        g_wm[1] = SimRemote{};
        g_wm[1].ext = 2; /* a Classic Controller through a MotionPlus */
        g_wm[1].motion_plus = true;
    }
    db::monitor(true);
    CHECK(wait_for([] { return db::status().bar == db::BarState::Ready; }));
    CHECK(wait_for([] {
        const auto s = db::status();
        return s.remote[0].state == db::SlotState::Connected && s.remote[0].ext == db::Extension::Nunchuk &&
               s.remote[1].state == db::SlotState::Connected && s.remote[1].ext == db::Extension::MotionPlusClassic;
    }));
    auto s = db::status();
    CHECK(s.node == "ugen3.2" && s.vid == 0x057e && s.pid == 0x0306 && s.interfaces.size() == 4);
    CHECK(s.remote[2].state == db::SlotState::NoRemote && s.remote[3].state == db::SlotState::NoRemote);
    CHECK(s.remote[0].calibrated && s.remote[0].nc_calibrated && s.remote[0].battery_percent == 96);
    CHECK(s.remote[0].ir_on && s.remote[1].motion_plus_seen);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        CHECK(g_wm[0].continuous && g_wm[0].mode == 0x37 && g_wm[0].leds == 1 && g_wm[1].leds == 2);
        CHECK(g_wm[1].mp_mode == 7);
    }
    /* live input: A and the Nunchuk's stick on Remote 1 */
    const uint8_t nc[6] = {200, 20, 0x80, 0x80, 0x80, 0x01 /* C held, Z released */};
    inject(0, report37(db::RbA, nc));
    CHECK(wait_for([] {
        const auto st = db::status();
        return st.remote[0].buttons == db::RbA && st.remote[0].nc_stick[0] == 200 && st.remote[0].nc_c &&
               st.remote[0].ir[0].visible && st.remote[0].ir[0].x == 50 && st.remote[0].reporting;
    }, 2000));
    /* rumble test reaches the Remote, and ends */
    db::rumble(0, 200);
    CHECK(wait_for([] { std::lock_guard<std::recursive_mutex> g(g_sim); return g_wm[0].rumble; }, 2000));
    CHECK(wait_for([] { std::lock_guard<std::recursive_mutex> g(g_sim); return !g_wm[0].rumble; }, 2000));
    /* Remote 2 switched off: lost; on again: connected again */
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.remote[1] = false;
    }
    CHECK(wait_for([] { return db::status().remote[1].state == db::SlotState::Lost; }, 4000));
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_wm[1] = SimRemote{};
        g_bar.remote[1] = true;
    }
    CHECK(wait_for([] {
        const auto st = db::status();
        return st.remote[1].state == db::SlotState::Connected && st.remote[1].ext == db::Extension::None;
    }, 5000));
    /* a Remote in slot 3 joins */
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_wm[2] = SimRemote{};
        g_bar.remote[2] = true;
    }
    CHECK(wait_for([] { return db::status().remote[2].state == db::SlotState::Connected; }, 5000));
    /* the DolphinBar unplugged, then plugged in again elsewhere */
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.present = false;
    }
    CHECK(wait_for([] { return db::status().bar == db::BarState::NotConnected; }, 5000));
    CHECK(db::status().remote[0].state == db::SlotState::Closed);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_fds.clear();
        g_bar.node = "ugen3.3";
        g_bar.present = true;
    }
    CHECK(wait_for([] { return db::status().bar == db::BarState::Ready && db::status().node == "ugen3.3"; }, 5000));
    CHECK(wait_for([] { return db::status().remote[0].state == db::SlotState::Connected; }, 5000));
    /* a game takes over: every interface closed (the node and its transfers
     * stay, for Dolphin's own hidapi calls), and the monitor stays off */
    db::hold_for_game();
    CHECK(no_interface_open());
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        CHECK(!g_wm[0].continuous && !g_wm[0].ir && !g_wm[0].rumble); /* left as Dolphin wants it */
    }
    CHECK(db::status().bar == db::BarState::HeldByGame);
    db::monitor(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(no_interface_open());
    /* Dolphin's own scanner can now open every interface */
    hid_free_enumeration(hid_enumerate(0, 0));
    CHECK(is_device_usable("ugen3.3:i0"));
    db::release_after_game();
    db::monitor(true);
    CHECK(wait_for([] { return db::status().bar == db::BarState::Ready; }));
    db::monitor(false);
    CHECK(no_interface_open());
}

/* The PS5's first hardware test (2026-10-30): an attachment wasn't recognised
 * and the screen kept changing. The cause was the controller test itself: a
 * MotionPlus it turned on drops its attachment and comes back (two status
 * reports), which the monitor took for a plug and identified again; that
 * identification's 0x55 to 0xa400f0 turned the MotionPlus off, which changed
 * the status again, and so on. The simulated Remotes now do what real ones
 * do; the attachments must be found once and stay found. */
void plug(int n, int ext)
{
    std::lock_guard<std::recursive_mutex> g(g_sim);
    g_wm[n].ext = ext;
    g_wm[n].ext_initialised = false;
    if (!g_wm[n].mp_mode)
        push_status(n);
    deliver(n);
}

void test_attachments_stable()
{
    reset(false, true, true, true, true);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.node = "ugen5.2";
        for (int i = 0; i < 4; ++i)
            g_wm[i] = SimRemote{};
        g_wm[0].motion_plus = true; /* a Wii Remote Plus with a Nunchuk */
        g_wm[0].ext = 1;
        g_wm[1].motion_plus = true; /* a Wii Remote Plus alone */
        g_wm[2].ext = 1;            /* a Wii Remote with a Nunchuk that answers late */
        g_wm[2].ext_slow = 2;
        g_wm[3].ext = 2;            /* a Wii Remote with a Classic Controller */
    }
    db::monitor(true);
    auto exts = [] {
        const auto s = db::status();
        return std::vector<db::Extension>{s.remote[0].ext, s.remote[1].ext, s.remote[2].ext, s.remote[3].ext};
    };
    const std::vector<db::Extension> want = {db::Extension::MotionPlusNunchuk, db::Extension::MotionPlus,
                                             db::Extension::Nunchuk, db::Extension::Classic};
    CHECK(wait_for([&] { return exts() == want; }, 8000));
    if (exts() != want)
        for (int i = 0; i < 4; ++i)
            std::fprintf(stderr, "  remote %d: %s\n", i + 1, db::extension_name(exts()[std::size_t(i)]));
    /* and it stays so: no MotionPlus switched again, nothing identified again */
    int on[2], off[2];
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        for (int i = 0; i < 2; ++i)
        {
            on[i] = g_wm[i].mp_activations;
            off[i] = g_wm[i].mp_deactivations;
        }
    }
    CHECK(on[0] == 1 && on[1] == 1 && off[0] == 0 && off[1] == 0);
    bool steady = true;
    for (int k = 0; k < 60; ++k)
    {
        steady = steady && exts() == want;
        for (int i = 0; i < 4; ++i)
            steady = steady && db::status().remote[i].state == db::SlotState::Connected;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    CHECK(steady);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        CHECK(g_wm[0].mp_activations == on[0] && g_wm[1].mp_activations == on[1]);
        CHECK(g_wm[0].mp_deactivations == 0 && g_wm[1].mp_deactivations == 0);
        CHECK(g_wm[0].mp_mode == 5 && g_wm[1].mp_mode == 4);
    }
    /* Triangle on a Remote whose MotionPlus is on: looked at again, not reset */
    db::refresh(0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    CHECK(exts() == want);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        CHECK(g_wm[0].mp_deactivations == 0 && g_wm[0].mp_mode == 5);
    }
    /* Remote 3: the Nunchuk pulled out, a Classic Controller plugged in */
    plug(2, 0);
    CHECK(wait_for([] { return db::status().remote[2].ext == db::Extension::None; }, 3000));
    plug(2, 2);
    CHECK(wait_for([] { return db::status().remote[2].ext == db::Extension::Classic; }, 3000));
    /* the Remotes keep reporting through all of it */
    const uint8_t cc[6] = {32, 32, 0x10, 0x00, 0xff, 0xef /* a */};
    inject(2, report37(db::RbB, cc));
    CHECK(wait_for([] {
        const auto st = db::status();
        return st.remote[2].buttons == db::RbB && (st.remote[2].cc_buttons & db::CbA);
    }, 2000));
    db::monitor(false);
}

/* The PS5 log of that test (2026-10-30, ugen1.2 "Mayflash Wiimote PC
 * Adapter"): a Wii Remote Plus with a Nunchuk whose MotionPlus was already on
 * (id 01 00 a4 20 05 05), and the console's own driver attached to interface
 * 0, so some of the Remote's answers go to it and not to us. */
void test_real_log_case()
{
    reset(false, true, false, false, false);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.node = "ugen1.2";
        g_bar.kernel_driver[0] = 1;
        g_wm[0] = SimRemote{};
        g_wm[0].motion_plus = true;
        g_wm[0].ext = 1;
        g_wm[0].mp_mode = 5; /* left on */
        g_drop_replies[0] = 3; /* every third register answer goes to the console's driver */
    }
    db::monitor(true);
    CHECK(wait_for([] { return db::status().remote[0].ext == db::Extension::MotionPlusNunchuk; }, 8000));
    bool steady = true;
    for (int k = 0; k < 60; ++k)
    {
        const auto st = db::status();
        steady = steady && st.remote[0].state == db::SlotState::Connected &&
                 st.remote[0].ext == db::Extension::MotionPlusNunchuk && !st.remote[0].ext_identifying;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    CHECK(steady);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        CHECK(g_wm[0].mp_deactivations == 0 && g_wm[0].mp_activations == 0 && g_wm[0].mp_mode == 5);
        g_drop_replies[0] = 0;
    }
    db::monitor(false);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        CHECK(g_wm[0].mp_mode == 0); /* left off for Dolphin and the game */
    }
}

/* ---- the speaker, and input while the speaker is fed (the Super Mario Galaxy test) ------------ */

/* Zero crossings a second, halved: the tone's frequency. */
double frequency(const std::vector<int16_t> &pcm)
{
    int crossings = 0;
    for (size_t i = 1; i < pcm.size(); ++i)
        crossings += (pcm[i - 1] < 0) != (pcm[i] < 0);
    return crossings / 2.0 / (double(pcm.size()) / db::kSpeakerRate);
}

std::vector<int16_t> decode_stream(const std::vector<std::vector<uint8_t>> &packets)
{
    db::Adpcm st;
    std::vector<int16_t> out;
    for (const auto &p : packets)
        for (uint8_t b : p)
        {
            out.push_back(db::adpcm_decode_nibble(st, b >> 4));
            out.push_back(db::adpcm_decode_nibble(st, b & 15));
        }
    return out;
}

void test_adpcm()
{
    for (int tone = 0; tone < 3; ++tone)
    {
        std::vector<int16_t> pcm(2400);
        CHECK(db::speaker_tone_samples(tone, 0, pcm.data(), int(pcm.size())));
        CHECK(!db::speaker_tone_samples(tone, 2400, pcm.data(), 1)); /* 0.8 s, then it ends */
        db::Adpcm enc;
        std::vector<std::vector<uint8_t>> packets(1);
        for (size_t i = 0; i < pcm.size(); i += 2)
            packets[0].push_back(db::adpcm_encode_pair(enc, pcm[i], pcm[i + 1]));
        const auto back = decode_stream(packets);
        double sig = 0, err = 0;
        int peak = 0;
        for (size_t i = 0; i < pcm.size(); ++i)
        {
            sig += double(pcm[i]) * pcm[i];
            err += double(pcm[i] - back[i]) * (pcm[i] - back[i]);
            peak = std::max(peak, std::abs(int(pcm[i])));
        }
        const double snr = 10 * std::log10(sig / std::max(1.0, err));
        const double want = tone == 0 ? 440 : tone == 1 ? 660 : 880;
        CHECK(snr > 15);
        CHECK(std::fabs(frequency(back) - want) < want * 0.03);
        CHECK(peak < 32767 / 2); /* not loud: a third of full scale */
        if (snr <= 15)
            std::fprintf(stderr, "  tone %d: SNR %.1f dB\n", tone, snr);
    }
}

void test_speaker_monitor()
{
    reset(false, true, false, false, false);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.node = "ugen6.2";
        g_wm[0] = SimRemote{};
    }
    db::monitor(true);
    CHECK(wait_for([] { return db::status().remote[0].state == db::SlotState::Connected; }, 5000));
    db::speaker_volume(0, 2);
    db::speaker_play(0, 1);
    CHECK(wait_for([] { return db::status().remote[0].speaker_playing; }, 2000));
    /* input keeps coming while the speaker is fed */
    for (int k = 0; k < 20; ++k)
    {
        const uint8_t none[6] = {0};
        inject(0, report37(k % 2 ? db::RbA : db::RbB, none));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(wait_for([] { return db::status().remote[0].buttons == db::RbA; }, 1000));
    CHECK(wait_for([] { return !db::status().remote[0].speaker_playing; }, 3000));
    const db::Remote r = db::status().remote[0];
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        const SimRemote &w = g_wm[0];
        CHECK(w.speaker_regs[9] == 0x01 && w.speaker_regs[8] == 0x01);
        CHECK(w.speaker_regs[1] == 0x00 && w.speaker_regs[2] == 0x00 && w.speaker_regs[3] == 0xd0 &&
              w.speaker_regs[4] == 0x07 && w.speaker_regs[5] == db::kSpeakerVolume[2]);
        CHECK(w.speaker_enabled && w.speaker_muted); /* still on, silent after the tone */
        CHECK(w.speaker_data.size() == 60);          /* 0.8 s at 75 reports a second */
        bool sizes = true;
        for (const auto &p : w.speaker_data)
            sizes = sizes && p.size() == 20;
        CHECK(sizes);
        const double f = frequency(decode_stream(w.speaker_data));
        CHECK(std::fabs(f - 660) < 25);
        if (w.speaker_times.size() > 2)
        {
            const double secs = double(w.speaker_times.back() - w.speaker_times.front()) / 1000.0;
            const double rate = double(w.speaker_times.size() - 1) / secs;
            CHECK(rate > 65 && rate < 85);
        }
    }
    CHECK(r.spk_sent == 60 && r.spk_refused == 0);
    CHECK(r.spk_rate > 60 && r.spk_rate < 90);
    CHECK(r.spk_interval_avg > 10 && r.spk_interval_avg < 17);
    db::speaker_power(0, false);
    CHECK(wait_for([] { std::lock_guard<std::recursive_mutex> g(g_sim); return !g_wm[0].speaker_enabled; }, 2000));
    /* on again, and leaving the screen switches it off */
    db::speaker_power(0, true);
    CHECK(wait_for([] { std::lock_guard<std::recursive_mutex> g(g_sim); return g_wm[0].speaker_enabled; }, 2000));
    db::monitor(false);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        CHECK(!g_wm[0].speaker_enabled && g_wm[0].speaker_muted);
    }
}

/* Dolphin's two threads for one Remote in a game, with or without the
 * speaker fed at 75 reports a second, over a slow DolphinBar that refuses
 * now and then: the input must keep coming, a refused speaker packet must not
 * end the connection, one refused other report must be retried, and a Remote
 * that's really gone must still be reported as gone. */
void game_load(bool speaker)
{
    reset(false, true, false, false, false);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.node = speaker ? "ugen7.2" : "ugen7.3";
        g_wm[0] = SimRemote{};
        g_wm[0].speaker_enabled = true;
        g_refuse_speaker_every[0] = 7;
        g_refuse_other_next[0] = 1; /* the first rumble report: refused once */
        g_speaker_count[0] = 0;
    }
    g_set_report_ms = 3;
    hid_free_enumeration(hid_enumerate(0, 0));
    hid_device *h = hid_open_path(speaker ? "ugen7.2:i0" : "ugen7.3:i0");
    CHECK(h != nullptr);
    if (!h)
        return;
    std::atomic<bool> stop{false};
    std::atomic<int> got{0}, wrong{0}, write_fail{0};
    std::atomic<long long> max_gap_ms{0};
    std::thread reader([&] {
        unsigned char buf[64];
        int expect = 0;
        auto last = std::chrono::steady_clock::now();
        while (!stop)
        {
            const int n = hid_read_timeout(h, buf, sizeof buf, 200); /* as WiimoteHidapi::IORead */
            if (n == 4 && buf[0] == 0x31)
            {
                const auto t = std::chrono::steady_clock::now();
                if (got > 0)
                    max_gap_ms = std::max<long long>(
                        max_gap_ms, std::chrono::duration_cast<std::chrono::milliseconds>(t - last).count());
                last = t;
                if (buf[2] != uint8_t(expect))
                    ++wrong;
                expect = buf[2] + 1;
                ++got;
            }
            else if (n < 0)
                ++write_fail; /* a read error would end Dolphin's connection too */
        }
    });
    std::thread writer([&] {
        const unsigned char rumble_off[] = {0x10, 0x00};
        unsigned char spk[22] = {0x18, 20 << 3};
        auto next_rumble = std::chrono::steady_clock::now();
        while (!stop)
        {
            if (speaker && hid_write(h, spk, sizeof spk) < 0)
                ++write_fail;
            if (std::chrono::steady_clock::now() >= next_rumble)
            {
                if (hid_write(h, rumble_off, 2) < 0)
                    ++write_fail;
                next_rumble += std::chrono::milliseconds(200);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(13333));
        }
    });
    /* 200 reports a second for two seconds */
    for (int k = 0; k < 400; ++k)
    {
        inject(0, {0x31, 0, uint8_t(k), 0});
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    stop = true;
    reader.join();
    writer.join();
    CHECK(got == 400);
    CHECK(wrong == 0);
    CHECK(write_fail == 0);
    CHECK(max_gap_ms < 100);
    usbhid::InterfaceStatus st;
    CHECK(usbhid::interface_stats(h, st));
    CHECK(st.write_retries == 1);
    if (speaker)
        CHECK(st.speaker_reports > 100 && st.speaker_dropped > 10 && st.speaker_dropped == st.speaker_reports / 7);
    else
        CHECK(st.speaker_reports == 0);
    /* the Remote switched off: the next write is refused twice, as gone */
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.remote[0] = false;
    }
    const unsigned char rumble_off[] = {0x10, 0x00};
    errno = 0;
    CHECK(hid_write(h, rumble_off, 2) == -1 && errno == EPIPE);
    hid_close(h);
    g_set_report_ms = 0;
    std::lock_guard<std::recursive_mutex> g(g_sim);
    g_refuse_speaker_every[0] = 0;
    g_refuse_other_next[0] = 0;
}

void test_game_load()
{
    game_load(false);
    game_load(true);
}

/* IN transfers left hanging while the Remote still takes reports: restarted,
 * and the input comes again. */
void test_input_stall()
{
    reset(false, true, false, false, false);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.node = "ugen8.2";
        g_wm[0] = SimRemote{};
    }
    hid_free_enumeration(hid_enumerate(0, 0));
    hid_device *h = hid_open_path("ugen8.2:i0");
    CHECK(h != nullptr);
    if (!h)
        return;
    unsigned char buf[64];
    /* a stream (continuous reporting, as games set it) ... */
    for (int k = 0; k < 10; ++k)
    {
        inject(0, {0x31, 0, 1, 0});
        CHECK(hid_read_timeout(h, buf, sizeof buf, 200) == 4);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    /* ... that stops */
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_hold_in[0] = true;
    }
    const unsigned char rumble_off[] = {0x10, 0x00};
    for (int k = 0; k < 25; ++k) /* 2.5 s: the Remote takes reports, nothing comes in */
    {
        CHECK(hid_write(h, rumble_off, 2) == 2);
        hid_read_timeout(h, buf, sizeof buf, 100);
    }
    usbhid::InterfaceStatus st;
    CHECK(usbhid::interface_stats(h, st) && st.read_restarts >= 1);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_hold_in[0] = false;
        g_input[0].clear();
    }
    inject(0, {0x31, 0, 2, 0});
    int n = 0;
    for (int k = 0; k < 10 && n != 4; ++k)
        n = hid_read_timeout(h, buf, sizeof buf, 100);
    CHECK(n == 4 && buf[2] == 2);
    hid_close(h);
}

/* ---- the menus: the menu Remote, light set-up, notices ------------------------------------ */
std::vector<uint8_t> report30(uint16_t buttons) { return {0x30, uint8_t(buttons >> 8), uint8_t(buttons)}; }

void test_menu_remote_and_notices()
{
    reset(false, false, true, false, false); /* Remote 2 is on from the start */
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.node = "ugen9.2";
        for (int i = 0; i < 4; ++i)
            g_wm[i] = SimRemote{};
        g_wm[1].ext = 1;
    }
    db::clear_notices(); /* nothing left from earlier tests */
    db::set_full_detail(false);
    db::monitor(true);
    CHECK(wait_for([] { return db::status().remote[1].state == db::SlotState::Connected; }, 5000));
    CHECK(wait_for([] { return db::menu_remote() == 1; }, 2000));
    /* the menus' light set-up: LED, continuous reports with the Nunchuk's
     * bytes once it's identified, no camera, no MotionPlus */
    CHECK(wait_for([] {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        return g_wm[1].leds == 2 && g_wm[1].mode == 0x32 && g_wm[1].continuous && !g_wm[1].ir &&
               g_wm[1].ext_initialised && g_wm[1].mp_mode == 0;
    }, 3000));
    /* the Nunchuk works the menus: the stick up, C held */
    inject(1, {0x32, 0, 0, 128, 230, 0x80, 0x80, 0x80, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    CHECK(wait_for([] {
        const db::MenuNunchuk nc = db::menu_nunchuk();
        return nc.present && nc.c && !nc.z && nc.y > 0.55f && std::abs(nc.x) < 0.1f;
    }, 1000));
    /* the Nunchuk pulled out and plugged in again: a notice each, with its name */
    CHECK(db::notices().empty());
    plug(1, 0);
    CHECK(wait_for([] { return db::status().remote[1].ext == db::Extension::None; }, 3000));
    plug(1, 1);
    CHECK(wait_for([] { return db::status().remote[1].ext == db::Extension::Nunchuk; }, 3000));
    {
        const auto n = db::notices();
        CHECK(n.size() == 2);
        if (n.size() == 2)
            CHECK(n[0].attachment && !n[0].connected && n[0].what == "Nunchuk" && n[0].port == 2 &&
                  n[1].attachment && n[1].connected && n[1].what == "Nunchuk");
    }
    db::clear_notices();
    CHECK(db::notices().empty()); /* on from the start: no notice */
    /* Remote 1 comes later: a notice; Remote 2 stays the menu Remote (it was first) */
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.remote[0] = true;
    }
    CHECK(wait_for([] { return db::status().remote[0].state == db::SlotState::Connected; }, 5000));
    CHECK(db::menu_remote() == 1);
    /* only the menu Remote's buttons work the menus */
    inject(0, report30(db::RbA));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(db::menu_buttons() == 0);
    inject(1, report30(db::RbDown | db::RbA));
    CHECK(wait_for([] { return db::menu_buttons() == (db::RbDown | db::RbA); }, 1000));
    inject(1, report30(0));
    CHECK(wait_for([] { return db::menu_buttons() == 0; }, 1000));
    /* Remote 2 switched off: a notice, and Remote 1 takes the menus over */
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.remote[1] = false;
    }
    CHECK(wait_for([] { return db::status().remote[1].state == db::SlotState::Lost; }, 4000));
    CHECK(db::menu_remote() == 0);
    {
        const auto n = db::notices();
        CHECK(n.size() == 2);
        if (n.size() == 2)
            CHECK(n[0].port == 1 && n[0].connected && n[1].port == 2 && !n[1].connected);
    }
    /* the controller test: full set-up (Remote 1 is a Wii Remote Plus with a
     * Nunchuk: its MotionPlus on); back to the menus: camera and MotionPlus
     * off, the Nunchuk read directly, continuous reports (a PS5 test: Z did
     * nothing once the test had been open) */
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_wm[0].motion_plus = true;
    }
    plug(0, 1);
    db::set_full_detail(true);
    CHECK(wait_for([] {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        return g_wm[0].ir && g_wm[0].continuous && g_wm[0].mode == 0x37 && g_wm[0].mp_mode == 5;
    }, 6000));
    db::set_full_detail(false);
    CHECK(wait_for([] {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        return !g_wm[0].ir && g_wm[0].continuous && g_wm[0].mode == 0x32 && g_wm[0].mp_mode == 0;
    }, 6000));
    inject(0, {0x32, 0, 0, 128, 128, 0x80, 0x80, 0x80, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    CHECK(wait_for([] { return db::menu_nunchuk().z; }, 1000));
    /* a button whose report is old counts as let go */
    inject(0, report30(db::RbUp));
    CHECK(wait_for([] { return db::menu_buttons() == db::RbUp; }, 1000));
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    CHECK(db::menu_buttons() == 0);
    db::clear_notices(); /* the Nunchuk plugged in above */
    /* no notice repeats from the status requests or the mode changes */
    std::this_thread::sleep_for(std::chrono::milliseconds(3500));
    CHECK(db::notices().empty());
    /* a game takes the DolphinBar: no menu Remote */
    db::hold_for_game();
    CHECK(db::menu_remote() == -1 && db::menu_buttons() == 0);
    CHECK(db::notices().empty()); /* handing over is not a disconnect */
    db::release_after_game();
    db::monitor(false);
    db::set_full_detail(true);
}

void test_notice_queue()
{
    db::clear_notices();
    for (int p = 1; p <= 4; ++p)
        db::push_notice(p, true);
    db::push_notice(1, false);
    db::push_notice(1, false); /* the same, waiting already: once */
    auto n = db::notices();
    CHECK(n.size() == 4 && n[0].port == 1 && n[3].port == 4);
    std::this_thread::sleep_for(std::chrono::milliseconds(int(db::kNoticeSeconds * 1000) + 200));
    n = db::notices();
    CHECK(n.size() == 1 && n[0].port == 1 && !n[0].connected);
    std::this_thread::sleep_for(std::chrono::milliseconds(int(db::kNoticeSeconds * 1000) + 200));
    CHECK(db::notices().empty());
}

/* Settings > Wii Remote Speaker off: nothing of the speaker goes out. */
void test_speaker_switch()
{
    reset(false, true, false, false, false);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.node = "ugen9.3";
        g_wm[0] = SimRemote{};
    }
    hid_free_enumeration(hid_enumerate(0, 0));
    hid_device *h = hid_open_path("ugen9.3:i0");
    CHECK(h != nullptr);
    if (!h)
        return;
    usbhid::set_speaker_enabled(false);
    const unsigned char on[] = {0x14, 0x04}, unmute[] = {0x19, 0x00};
    unsigned char spk[22] = {0x18, 20 << 3};
    const int before = g_set_reports[0];
    CHECK(hid_write(h, on, 2) == 2 && hid_write(h, unmute, 2) == 2);
    for (int k = 0; k < 50; ++k)
        CHECK(hid_write(h, spk, sizeof spk) == int(sizeof spk));
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        CHECK(!g_wm[0].speaker_enabled && g_wm[0].speaker_muted); /* off and muted, whatever the game asked */
        CHECK(g_wm[0].speaker_data.empty());
        CHECK(g_set_reports[0] - before == 2); /* the two commands only: no speaker data transfers */
    }
    usbhid::InterfaceStatus st;
    CHECK(usbhid::interface_stats(h, st) && st.speaker_held_back == 50);
    usbhid::set_speaker_enabled(true);
    CHECK(hid_write(h, on, 2) == 2 && hid_write(h, unmute, 2) == 2 && hid_write(h, spk, sizeof spk) == int(sizeof spk));
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        CHECK(g_wm[0].speaker_enabled && g_wm[0].speaker_data.size() == 1);
    }
    hid_close(h);
}

/* An empty slot is asked at most every 2 s, however often the scanner asks. */
void test_probe_throttle()
{
    reset(false, false, false, false, false);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.node = "ugen9.4";
    }
    hid_free_enumeration(hid_enumerate(0, 0));
    const int before = g_set_reports[2];
    for (int k = 0; k < 10; ++k)
        CHECK(!is_device_usable("ugen9.4:i2"));
    CHECK(g_set_reports[2] - before == 2); /* the first question, sent twice; then none for 2 s */
    std::this_thread::sleep_for(std::chrono::milliseconds(2100));
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.remote[2] = true;
    }
    CHECK(is_device_usable("ugen9.4:i2")); /* a Remote that came meanwhile is found */
}

/* The log tells a stopped reader and frozen data from a hanging transport. */
void test_reader_and_frozen()
{
    std::vector<std::string> lines;
    std::mutex m;
    static std::vector<std::string> *out = nullptr;
    static std::mutex *om = nullptr;
    out = &lines;
    om = &m;
    usbhid::set_log([](const char *line) {
        std::lock_guard<std::mutex> g(*om);
        out->emplace_back(line);
    });
    reset(false, true, false, false, false);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.node = "ugen9.5";
        g_wm[0] = SimRemote{};
    }
    hid_free_enumeration(hid_enumerate(0, 0));
    hid_device *h = hid_open_path("ugen9.5:i0");
    CHECK(h != nullptr);
    if (!h)
        return;
    unsigned char buf[64];
    const unsigned char rumble_off[] = {0x10, 0x00};
    /* reports keep coming, the reader stops after the first: the writer's calls see it */
    inject(0, {0x31, 0, 1, 0});
    CHECK(hid_read_timeout(h, buf, sizeof buf, 200) == 4);
    for (int k = 0; k < 30; ++k)
    {
        for (int j = 0; j < 3; ++j) /* 90 in all: more than the queue holds */
        {
            inject(0, {0x31, 0, uint8_t(k), uint8_t(j)});
            hid_write(h, rumble_off, 2);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    auto has = [&](const char *what) {
        std::lock_guard<std::mutex> g(m);
        for (const auto &l : lines)
            if (l.find(what) != std::string::npos)
                return true;
        return false;
    };
    CHECK(has("the reader has stopped"));
    /* the reader again; the same report for 11 s: frozen */
    for (int k = 0; k < 115; ++k)
    {
        inject(0, {0x31, 0, 7, 7});
        while (hid_read_timeout(h, buf, sizeof buf, 0) > 0)
        {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    CHECK(has("haven't changed"));
    usbhid::InterfaceStatus st;
    CHECK(usbhid::interface_stats(h, st) && st.queue_drops > 0 && st.reports_taken > 0);
    hid_close(h);
    usbhid::set_log(nullptr);
}

/* Access refused: the node is there but can't be opened. */
void test_access_refused()
{
    reset(false, false, false, false, false);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.node = "ugen4.2";
        g_refuse = "ugen4.2";
    }
    db::monitor(true);
    CHECK(wait_for([] {
        const auto s = db::status();
        return s.bar == db::BarState::NotConnected && !s.unreadable.empty() && s.unreadable[0].first == "ugen4.2" &&
               s.unreadable[0].second == EACCES;
    }, 3000));
    db::monitor(false);
    std::lock_guard<std::recursive_mutex> g(g_sim);
    g_refuse.clear();
}

/* Leaving a game and Porpoise with a Wii Remote on (the PS5 hang): Dolphin's
 * scanner opening and closing the empty slots tears nothing down, a read that
 * doesn't stop keeps its node open rather than holding up the exit, a call
 * that doesn't come back is reported, and the exit waits at most its time. */
/* HOME held for a second in a game: taken from the game's input and counted
 * once; a short press reaches the game as it's let go. */
void test_home_hold()
{
    reset(false, true, false, false, false);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.node = "ugen6.2";
    }
    enumerate_paths();
    hid_device *a = hid_open_path("ugen6.2:i0");
    CHECK(a != nullptr);
    const unsigned char status_req[] = {0x15, 0x00};
    CHECK(hid_write(a, status_req, 2) == 2); /* a Remote answers */
    unsigned char buf[64];
    while (hid_read_timeout(a, buf, sizeof buf, 50) > 0)
        ;
    usbhid::set_home_menu(true);
    CHECK(!usbhid::take_home_hold());
    /* a short press: nothing while held, then HOME and its release */
    inject(0, report30(db::RbHome));
    CHECK(hid_read_timeout(a, buf, sizeof buf, 200) == 3 && !(buf[2] & 0x80));
    inject(0, report30(0));
    CHECK(hid_read_timeout(a, buf, sizeof buf, 200) == 3 && (buf[2] & 0x80));
    CHECK(hid_read_timeout(a, buf, sizeof buf, 200) == 3 && !(buf[2] & 0x80));
    CHECK(!usbhid::take_home_hold());
    /* held for a second: counted once, and the game never sees it */
    inject(0, report30(db::RbHome));
    CHECK(hid_read_timeout(a, buf, sizeof buf, 200) == 3 && !(buf[2] & 0x80));
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    inject(0, report30(db::RbHome | db::RbA));
    int seen_home = 0, n = 0;
    while ((n = hid_read_timeout(a, buf, sizeof buf, 100)) > 0)
        seen_home += (buf[2] & 0x80) != 0;
    CHECK(usbhid::take_home_hold());
    CHECK(!usbhid::take_home_hold());
    CHECK(usbhid::remote_buttons() == (db::RbHome | db::RbA)); /* the in-game menu sees what's held */
    inject(0, report30(0));
    while ((n = hid_read_timeout(a, buf, sizeof buf, 100)) > 0)
        seen_home += (buf[2] & 0x80) != 0;
    CHECK(seen_home == 0);
    CHECK(usbhid::remote_buttons() == 0);
    /* an attachment in a game: told once it has settled, not for a moment's
     * gone-and-back (a MotionPlus switched), not for how the game found it */
    inject(0, {0x20, 0, 0, 0x02, 0, 0, 0x80});
    CHECK(hid_read_timeout(a, buf, sizeof buf, 200) == 7);
    std::this_thread::sleep_for(std::chrono::milliseconds(1600));
    int iface = -1;
    bool plugged = false;
    CHECK(!usbhid::take_attachment_event(iface, plugged)); /* the first state: no event */
    inject(0, {0x20, 0, 0, 0x00, 0, 0, 0x80});
    inject(0, {0x20, 0, 0, 0x02, 0, 0, 0x80});
    while (hid_read_timeout(a, buf, sizeof buf, 100) > 0)
        ;
    std::this_thread::sleep_for(std::chrono::milliseconds(1600));
    CHECK(!usbhid::take_attachment_event(iface, plugged));
    inject(0, {0x20, 0, 0, 0x00, 0, 0, 0x80});
    while (hid_read_timeout(a, buf, sizeof buf, 100) > 0)
        ;
    std::this_thread::sleep_for(std::chrono::milliseconds(1600));
    CHECK(usbhid::take_attachment_event(iface, plugged) && iface == 0 && !plugged);
    CHECK(!usbhid::take_attachment_event(iface, plugged));
    /* outside a game, HOME goes through as it is */
    usbhid::set_home_menu(false);
    inject(0, report30(db::RbHome));
    CHECK(hid_read_timeout(a, buf, sizeof buf, 200) == 3 && (buf[2] & 0x80));
    hid_close(a);
}

std::mutex g_exit_log_lock;
std::vector<std::string> g_exit_log;

void test_exit()
{
    usbhid::set_log([](const char *line) {
        std::lock_guard<std::mutex> g(g_exit_log_lock);
        g_exit_log.emplace_back(line);
    });
    auto logged = [](const char *what) {
        std::lock_guard<std::mutex> g(g_exit_log_lock);
        return std::any_of(g_exit_log.begin(), g_exit_log.end(),
                           [&](const std::string &l) { return l.find(what) != std::string::npos; });
    };
    reset(false, true, false, false, false);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.node = "ugen5.2";
    }
    enumerate_paths();
    hid_device *a = hid_open_path("ugen5.2:i0");
    CHECK(a != nullptr);
    const int opens = g_opens["ugen5.2"];
    for (int k = 0; k < 20; ++k) /* the scanner, ten seconds of it */
    {
        enumerate_paths();
        hid_device *h = hid_open_path("ugen5.2:i1");
        CHECK(h != nullptr);
        hid_close(h);
    }
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        CHECK(g_opens["ugen5.2"] == opens && g_fs_closes == 0 && g_fs_uninits == 0);
    }

    /* a SET_REPORT that takes long: seen while it runs, and said when it's back */
    g_set_report_ms = 700;
    std::thread slow([&] {
        const unsigned char leds[] = {0x11, 0x10};
        hid_write(a, leds, 2);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    const auto stuck = usbhid::stuck_calls(200);
    CHECK(stuck.size() == 1 && stuck[0].find("SET_REPORT") != std::string::npos);
    slow.join();
    g_set_report_ms = 0;
    CHECK(usbhid::stuck_calls(0).empty());
    CHECK(logged("came back after"));

    /* reads that don't stop: the node is left open, the exit isn't held up */
    hid_close(a);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_stop_ignored = true;
    }
    const auto t0 = std::chrono::steady_clock::now();
    usbhid::shutdown();
    const auto took = std::chrono::steady_clock::now() - t0;
    CHECK(took < std::chrono::milliseconds(1500));
    CHECK(logged("left open"));
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        CHECK(g_fds.size() == 1 && g_fs_uninits == 0);
        g_stop_ignored = false;
        g_fds.clear();
    }

    /* Porpoise closes while the monitor's Remote is in a SET_REPORT that
     * doesn't come back for 3 s: it closes after 500 ms anyway */
    reset(false, true, false, false, false);
    {
        std::lock_guard<std::recursive_mutex> g(g_sim);
        g_bar.node = "ugen5.3";
    }
    db::monitor(true);
    CHECK(wait_for([] { return db::status().remote[0].state == db::SlotState::Connected; }, 3000));
    g_set_report_ms = 3000;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const auto t1 = std::chrono::steady_clock::now();
    const bool closed = db::close_for_exit(500);
    const auto waited = std::chrono::steady_clock::now() - t1;
    g_set_report_ms = 0;
    CHECK(!closed);
    CHECK(waited < std::chrono::milliseconds(1000));
    CHECK(wait_for([] { return db::close_for_exit(0); }, 8000)); /* and finishes once the call is back */
    std::lock_guard<std::recursive_mutex> g(g_sim);
    CHECK(g_fds.empty());
}

int main()
{
    usbhid::set_log([](const char *line) {
        g_log.emplace_back(line);
        if (std::getenv("USBHID_TEST_LOG"))
            std::fprintf(stderr, "  log: %s\n", line);
    });
    g_sim_devices = {&g_hub, &g_bt, &g_pad, &g_bar};
    usbhid::set_sys_ops({sim_open, sim_close, sim_ioctl, sim_list, sim_sleep, sim_now});
    CHECK(hid_init() == 0);

    for (bool out : {false, true})
    {
        std::printf("-- DolphinBar with%s interrupt OUT endpoints\n", out ? "" : "out");
        test_enumeration(out);
        test_usable(out);
        test_read_write(out);
        test_four_players(out);
    }
    std::printf("-- unplug and replug\n");
    test_unplug();
    std::printf("-- descriptors unreadable\n");
    test_layout_assumed();
    std::printf("-- the console's own driver on an interface\n");
    test_kernel_driver();
    std::printf("-- Wii Remote reports decoded\n");
    test_decode();
    std::printf("-- the controller test's monitor: Remotes, attachments, hotplug, handing over to a game\n");
    db::set_log_path(std::getenv("DOLPHINBAR_TEST_LOG") ? std::getenv("DOLPHINBAR_TEST_LOG") : "");
    db::set_full_detail(true); /* the controller test's screen is open */
    test_monitor();
    std::printf("-- attachments found once and kept (the first PS5 test's bug)\n");
    test_attachments_stable();
    std::printf("-- the PS5 log's case: a Wii Remote Plus with its MotionPlus on, answers lost to the console's driver\n");
    test_real_log_case();
    std::printf("-- the speaker's ADPCM\n");
    test_adpcm();
    std::printf("-- the speaker test\n");
    test_speaker_monitor();
    std::printf("-- a game's load: input with and without the speaker, a slow DolphinBar refusing now and then\n");
    test_game_load();
    std::printf("-- input stopped with the Remote still there\n");
    test_input_stall();
    std::printf("-- the menus' Wii Remote and the connection notices\n");
    test_menu_remote_and_notices();
    test_notice_queue();
    std::printf("-- Settings: the real Wii Remotes' speaker off\n");
    test_speaker_switch();
    std::printf("-- empty slots asked at most every 2 s\n");
    test_probe_throttle();
    std::printf("-- the log: a stopped reader, frozen data\n");
    test_reader_and_frozen();
    std::printf("-- access refused\n");
    test_access_refused();
    std::printf("-- HOME held in a game: the in-game menu\n");
    test_home_hold();
    std::printf("-- leaving a game and Porpoise with a Wii Remote on\n");
    test_exit();
    std::printf("%s", usbhid::describe().c_str());
    CHECK(hid_exit() == 0);
    if (g_failures)
    {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all usbhid PC tests passed\n");
    return 0;
}
