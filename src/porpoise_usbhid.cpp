/* Porpoise - hidapi over the console's ugen USB nodes, for real Wii Remotes
 * through a Mayflash DolphinBar (porpoise_usbhid.hpp says why and how).
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * One ugen node per USB device, opened once however many of its interfaces
 * are in use (FreeBSD lets a ugen node be opened once at a time), with the
 * USB_FS_* transfer table of that open:
 *
 *   transfer 3n+1, 3n+2  two reads armed on interface n's interrupt IN
 *   transfer 3n+3        interface n's interrupt OUT, when it has one
 *
 * Output reports go to the interrupt OUT endpoint, or as a HID SET_REPORT
 * (USB_DO_REQUEST) when the interface has none, the way hidapi's libusb
 * backend does. There is no thread of our own: whichever call is waiting
 * collects the finished transfers (USB_FS_COMPLETE) under the device's lock
 * and hands each report to its interface's queue, so Dolphin's read thread,
 * write thread and scanner can share a node. */
#include "porpoise_usbhid.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <memory>
#include <mutex>

#ifndef PORPOISE_DESKTOP
#include <fcntl.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#if defined(PORPOISE_USBHID_PAYLOAD) || defined(PORPOISE_USBHID_HOST_TEST)
#include <dirent.h>
#else
#include <ps5platform/libc.h> /* ps5_opendir: a title's own opendir is refused */
#endif
#endif

namespace porpoise::usbhid
{
namespace
{
LogFn g_log = nullptr;
bool g_detach = false;
std::atomic<bool> g_speaker_on{true};

void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void logf(const char *fmt, ...)
{
    if (!g_log)
        return;
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    g_log(line);
}
} // namespace

void set_log(LogFn fn) { g_log = fn; }
void set_detach_kernel_driver(bool on) { g_detach = on; }
void set_speaker_enabled(bool on)
{
    if (g_speaker_on.exchange(on) != on)
        logf("usbhid: real Wii Remotes' speakers %s", on ? "on" : "off (no speaker data sent)");
}
bool speaker_enabled() { return g_speaker_on.load(); }
} // namespace porpoise::usbhid

#ifdef PORPOISE_DESKTOP
/* A desktop's Dolphin core has its own hidapi and Bluetooth. */
namespace porpoise::usbhid
{
std::vector<DeviceStatus> status() { return {}; }
std::vector<std::pair<std::string, int>> unreadable() { return {}; }
bool interface_stats(const void *, InterfaceStatus &) { return false; }
std::vector<std::string> stuck_calls(unsigned) { return {}; }
void shutdown() {}
void set_home_menu(bool) {}
bool take_home_hold() { return false; }
bool take_attachment_event(int &, bool &) { return false; }
int latest_report(int, std::uint8_t *, int, unsigned &) { return 0; }
std::uint16_t remote_buttons() { return 0; }
std::string describe() { return "DolphinBar: the desktop core finds Wii Remotes itself\n"; }
void set_node_in_use(const std::string &, bool) {}
} // namespace porpoise::usbhid
#else

/* ---- hidapi's interface (hidapi 0.15, Externals/hidapi in Dolphin's tree) ------------- */
#if defined(PORPOISE_USBHID_HOST_TEST) && __has_include(<hidapi.h>)
#include <hidapi.h> /* the PC tests check these against the real header */
#else
#include "porpoise_hidapi.h"
#endif

namespace porpoise::usbhid
{
namespace
{
/* ---- FreeBSD's USB ioctls (sys/dev/usb/usb_ioctl.h), spelt out ----------------------------
 * so the PC tests can build them; on the console they're checked against the
 * SDK's header below. */
constexpr unsigned long kIocOut = 0x40000000UL, kIocIn = 0x80000000UL, kIocInOut = kIocIn | kIocOut;
constexpr unsigned long ioc(unsigned long inout, unsigned long num, unsigned long len)
{
    return inout | ((len & 0x1fffUL) << 16) | (0x55UL /* 'U' */ << 8) | num;
}

struct DeviceRequest /* usb_device_request */
{
    std::uint8_t bmRequestType, bRequest, wValue[2], wIndex[2], wLength[2];
};
struct CtlRequest /* usb_ctl_request */
{
    void *ucr_data;
    std::uint16_t ucr_flags, ucr_actlen;
    std::uint8_t ucr_addr;
    DeviceRequest ucr_request;
} __attribute__((aligned(8)));
struct FsEndpoint /* usb_fs_endpoint */
{
    void **ppBuffer;
    std::uint32_t *pLength;
    std::uint32_t nFrames, aFrames;
    std::uint16_t flags, timeout, isoc_time_complete;
    int status;
} __attribute__((aligned(8)));
struct FsInit /* usb_fs_init */
{
    FsEndpoint *pEndpoints;
    std::uint8_t ep_index_max;
} __attribute__((aligned(8)));
struct FsOpen /* usb_fs_open */
{
    std::uint32_t max_bufsize, max_frames;
    std::uint16_t max_packet_length;
    std::uint8_t dev_index, ep_index, ep_no;
} __attribute__((aligned(4)));
struct FsIndex /* usb_fs_start, _stop, _complete, _close, _uninit */
{
    std::uint8_t ep_index;
};
struct GenDescriptor /* usb_gen_descriptor */
{
    void *ugd_data;
    std::uint16_t ugd_lang_id, ugd_maxlen, ugd_actlen, ugd_offset;
    std::uint8_t ugd_config_index, ugd_string_index, ugd_iface_index, ugd_altif_index, ugd_endpt_index,
        ugd_report_type;
    std::uint8_t reserved[8];
} __attribute__((aligned(8)));
/* usb_device_info: the fields we read. The console's is 8 bytes longer than
 * FreeBSD's 0x170, and BlueV (platform/ps5/ps5_usb_directory.cpp) reads it on
 * a ugen node with 0xc1785504 (USB_DEVICEINFO's number at that size); that is
 * tried first, then FreeBSD's USB_GET_DEVICEINFO. */
struct DeviceInfo
{
    std::uint16_t productNo, vendorNo, releaseNo, power;
    std::uint8_t bus, addr, index, cls, subclass, protocol, config_no, config_index, speed, mode, nports,
        hubaddr, hubindex, hubport, power_mode, suspended;
    std::uint8_t reserved[16];
    char product[128], vendor[128], serial[64], release[8];
    std::uint8_t ps5_more[8];
};
static_assert(sizeof(DeviceRequest) == 8 && sizeof(CtlRequest) == 24 && sizeof(FsEndpoint) == 40 &&
              sizeof(FsInit) == 16 && sizeof(FsOpen) == 16 && sizeof(GenDescriptor) == 32 &&
              sizeof(DeviceInfo) == 0x178);

constexpr unsigned long kFsStart = ioc(kIocIn, 192, 1), kFsStop = ioc(kIocIn, 193, 1),
                        kFsComplete = ioc(kIocOut, 194, 1), kFsInit = ioc(kIocIn, 195, sizeof(FsInit)),
                        kFsUninit = ioc(kIocIn, 196, 1), kFsOpen = ioc(kIocInOut, 197, sizeof(FsOpen)),
                        kFsClose = ioc(kIocIn, 198, 1), kDoRequest = ioc(kIocInOut, 111, sizeof(CtlRequest)),
                        kGetFullDesc = ioc(kIocInOut, 109, sizeof(GenDescriptor)),
                        kDeviceInfoPs5 = ioc(kIocInOut, 4, 0x178), kDeviceInfo = ioc(kIocOut, 112, 0x170),
                        kIfaceDriverActive = ioc(kIocIn, 124, sizeof(int)),
                        kIfaceDriverDetach = ioc(kIocIn, 125, sizeof(int));
static_assert(kDeviceInfoPs5 == 0xc1785504UL, "BlueV's number");
} // namespace
} // namespace porpoise::usbhid

#if !defined(PORPOISE_USBHID_HOST_TEST) && __has_include(<dev/usb/usb_ioctl.h>)
#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>
static_assert(porpoise::usbhid::kFsStart == USB_FS_START && porpoise::usbhid::kFsStop == USB_FS_STOP &&
              porpoise::usbhid::kFsComplete == USB_FS_COMPLETE && porpoise::usbhid::kFsInit == USB_FS_INIT &&
              porpoise::usbhid::kFsUninit == USB_FS_UNINIT && porpoise::usbhid::kFsOpen == USB_FS_OPEN &&
              porpoise::usbhid::kFsClose == USB_FS_CLOSE && porpoise::usbhid::kDoRequest == USB_DO_REQUEST &&
              porpoise::usbhid::kGetFullDesc == USB_GET_FULL_DESC &&
              porpoise::usbhid::kDeviceInfo == USB_GET_DEVICEINFO &&
              porpoise::usbhid::kIfaceDriverActive == USB_IFACE_DRIVER_ACTIVE &&
              porpoise::usbhid::kIfaceDriverDetach == USB_IFACE_DRIVER_DETACH,
              "FreeBSD's USB ioctls as spelt out here");
static_assert(sizeof(struct usb_fs_endpoint) == sizeof(porpoise::usbhid::FsEndpoint) &&
              sizeof(struct usb_ctl_request) == sizeof(porpoise::usbhid::CtlRequest) &&
              sizeof(struct usb_fs_open) == sizeof(porpoise::usbhid::FsOpen) &&
              sizeof(struct usb_gen_descriptor) == sizeof(porpoise::usbhid::GenDescriptor) &&
              sizeof(struct usb_device_info) + 8 == sizeof(porpoise::usbhid::DeviceInfo));
#endif

namespace porpoise::usbhid
{
namespace
{
/* ---- the system calls ------------------------------------------------------------------ */
#ifdef PORPOISE_USBHID_HOST_TEST
SysOps g_sys{};
#define SYS_OPEN(path, nb) g_sys.open(path, nb)
#define SYS_CLOSE(fd) g_sys.close(fd)
#define SYS_IOCTL(fd, req, arg) g_sys.ioctl(fd, req, arg)
#define SYS_SLEEP_US(us) g_sys.sleep_us(us)
std::uint64_t now_ms() { return g_sys.now_ms(); }
bool list_dev(std::vector<std::string> &names) { return g_sys.list_dev(names); }
#else
#define SYS_OPEN(path, nb) ::open(path, O_RDWR | ((nb) ? O_NONBLOCK : 0))
#define SYS_CLOSE(fd) ::close(fd)
#define SYS_IOCTL(fd, req, arg) ::ioctl(fd, req, arg)
#define SYS_SLEEP_US(us) ::usleep(us)
std::uint64_t now_ms()
{
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return std::uint64_t(t.tv_sec) * 1000u + std::uint64_t(t.tv_nsec) / 1000000u;
}
bool list_dev(std::vector<std::string> &names)
{
#if defined(PORPOISE_USBHID_PAYLOAD)
    DIR *d = opendir("/dev");
#else
    DIR *d = ps5_opendir("/dev");
#endif
    if (!d)
        return false;
#if defined(PORPOISE_USBHID_PAYLOAD)
    while (const dirent *e = readdir(d))
#else
    while (const dirent *e = ps5_readdir(d))
#endif
        if (std::strncmp(e->d_name, "ugen", 4) == 0)
            names.emplace_back(e->d_name);
#if defined(PORPOISE_USBHID_PAYLOAD)
    closedir(d);
#else
    ps5_closedir(d);
#endif
    return true;
}
#endif

/* The calls into the console's USB stack that can wait (a control request, a
 * transfer table's teardown, a node's open and close), while they run: a
 * call that doesn't come back can't log itself, so the DolphinBar log's
 * writer (porpoise_dolphinbar.cpp) asks stuck_calls() every second. A call
 * that came back late says so itself. */
struct InFlight
{
    std::atomic<std::uint64_t> since{0};
    const char *what = nullptr;
    char where[32] = {};
};
InFlight g_flight[16];

class Track
{
public:
    Track(const char *what, const std::string &where)
    {
        for (int k = 0; k < 16; ++k)
        {
            std::uint64_t free = 0;
            if (g_flight[k].since.compare_exchange_strong(free, ~std::uint64_t(0)))
            {
                slot_ = k;
                g_flight[k].what = what;
                std::snprintf(g_flight[k].where, sizeof g_flight[k].where, "%s", where.c_str());
                g_flight[k].since = now_ms();
                break;
            }
        }
        start_ = now_ms();
    }
    ~Track()
    {
        const std::uint64_t took = now_ms() - start_;
        if (slot_ >= 0)
        {
            if (took >= 200)
                logf("usbhid: %s on %s came back after %llu ms", g_flight[slot_].what, g_flight[slot_].where,
                     (unsigned long long)took);
            g_flight[slot_].since = 0;
        }
    }

private:
    int slot_ = -1;
    std::uint64_t start_ = 0;
};

/* The console's own Bluetooth chip, which its gamepads use (BlueV, section 12):
 * never opened. */
constexpr const char *kConsoleBluetooth = "ugen0.2";
constexpr std::uint16_t kNintendo = 0x057e;
bool wanted(std::uint16_t vid, std::uint16_t pid) { return vid == kNintendo && (pid == 0x0306 || pid == 0x0330); }

constexpr int kMaxInterfaces = 4;
constexpr int kTransfers = 1 + 3 * kMaxInterfaces;
constexpr int kReadsPerInterface = 2;
constexpr std::uint32_t kBuf = 64; /* a Wii Remote's reports are at most 22 bytes */
constexpr int kQueue = 64;         /* reports kept per interface; the oldest go first */
constexpr std::uint16_t kWriteTimeoutMs = 1000;

int read_slot(int iface, int k) { return 1 + 3 * iface + k; }
int write_slot(int iface) { return 3 + 3 * iface; }

struct Interface
{
    InterfaceStatus st;
    std::uint16_t in_size = kBuf, out_size = 0;
    bool slots_open = false; /* its transfers were opened with the device (kept until it's gone) */
    bool clear_stall = false; /* a read stalled: clear it with the next one */
    /* when things last happened (now_ms), for the diagnosis and the watchdog */
    std::uint64_t last_in = 0, last_in_before = 0, last_out_ok = 0, last_restart = 0, last_summary = 0;
    bool stall_logged = false;
    std::uint32_t summary_in = 0, summary_out = 0;
    /* who takes the reports and whether they still change: tells a Remote that
     * stopped, a transport that hangs and a reader (Dolphin) that stopped apart */
    std::uint64_t last_taken = 0, last_change = 0, last_probe_refused = 0;
    std::uint64_t waiting_since = 0; /* the oldest report not yet taken came then */
    std::uint8_t prev_report[kBuf] = {};
    std::uint32_t prev_len = 0;
    bool reader_logged = false, frozen_logged = false;
    std::uint64_t write_ms_total = 0;
    /* HOME held in a game (home_filter), and the Remote's buttons as the
     * reports last had them, whoever reads them */
    bool home_down = false, home_fired = false;
    std::uint64_t home_since = 0;
    std::uint16_t buttons = 0;
    std::uint64_t buttons_at = 0;
    /* the latest report as it came, whoever reads it (latest_report) */
    std::uint8_t latest[kBuf] = {};
    std::uint32_t latest_len = 0;
    std::uint64_t latest_at = 0;
    /* the attachment flag of the status reports in a game: as last told (-1:
     * not yet), as last seen and since when (take_attachment_event) */
    int ext_told = -1, ext_seen = -1;
    std::uint64_t ext_since = 0;
    /* reports read and not yet taken */
    std::uint8_t queue[kQueue][kBuf];
    std::uint32_t queue_len[kQueue];
    int head = 0, count = 0;
};

struct Device
{
    std::string node;
    int fd = -1;
    int refs = 0;   /* open interfaces */
    bool dead = false;
    unsigned complete_errors = 0, start_errors = 0;
    std::uint16_t vid = 0, pid = 0;
    std::string product;
    bool descriptors_read = false;
    int iface_count = 0;
    Interface iface[kMaxInterfaces];
    /* The transfer table the kernel reads and writes: it must not move while
     * the node is open, so a Device is never copied. */
    FsEndpoint eps[kTransfers];
    void *buf_ptr[kTransfers];
    std::uint32_t buf_len[kTransfers];
    std::uint8_t buf[kTransfers][kBuf];
    bool pending[kTransfers];
    bool opened[kTransfers];
    std::mutex lock;
};

/* What a ugen node is, remembered from the last enumeration. */
struct Known
{
    std::string node;
    std::uint16_t vid = 0, pid = 0;
    std::string product;
    bool layout_read = false;
    std::vector<std::uint8_t> config;
    int iface_count = 0;
    InterfaceStatus iface[kMaxInterfaces];
    std::uint16_t in_size[kMaxInterfaces] = {}, out_size[kMaxInterfaces] = {};
};

std::mutex g_devices_lock; /* g_devices, g_known and g_others */
std::vector<std::unique_ptr<Device>> g_devices;
/* Devices whose transfers wouldn't stop: left open, and in memory (retire). */
std::vector<std::unique_ptr<Device>> g_stuck_devices;
std::vector<Known> g_known;
/* Nodes that aren't Nintendo's and when they were looked at: Dolphin
 * enumerates twice a second, and another device is opened again only after
 * kRecheckMs (a node name can come back as another device after a replug). */
struct Other
{
    std::string node;
    std::uint64_t checked;
};
std::vector<Other> g_others;
std::vector<std::pair<std::string, int>> g_unreadable, g_unreadable_now;
std::mutex g_in_use_lock; /* g_in_use: its own, so the module that set it never waits for a USB call here */
std::vector<std::string> g_in_use;
bool in_use(const std::string &node)
{
    std::lock_guard<std::mutex> g(g_in_use_lock);
    return std::find(g_in_use.begin(), g_in_use.end(), node) != g_in_use.end();
}
constexpr std::uint64_t kRecheckMs = 10000;

struct Handle
{
    Device *dev;
    int iface;
    bool nonblocking = false;
};

/* ---- descriptors --------------------------------------------------------------------- */

/* The HID interfaces of a configuration descriptor and their interrupt
 * endpoints. */
int parse_config(const std::uint8_t *d, std::size_t len, Known &k)
{
    int n = 0;
    int current = -1; /* index in k.iface of the interface being read, -1: not one of ours */
    for (std::size_t off = 0; off + 2 <= len;)
    {
        const std::uint8_t blen = d[off], type = d[off + 1];
        if (blen < 2 || off + blen > len)
            break;
        if (type == 4 && blen >= 9) /* interface */
        {
            current = -1;
            const std::uint8_t number = d[off + 2], alt = d[off + 3], cls = d[off + 5];
            if (alt == 0 && cls == 3 /* HID */ && n < kMaxInterfaces)
            {
                current = n++;
                k.iface[current] = InterfaceStatus{};
                k.iface[current].number = number;
                k.in_size[current] = 0;
                k.out_size[current] = 0;
            }
        }
        else if (type == 5 && blen >= 7 && current >= 0) /* endpoint */
        {
            const std::uint8_t addr = d[off + 2], attr = d[off + 3];
            const std::uint16_t size = std::uint16_t(d[off + 4] | (d[off + 5] << 8)) & 0x7ff;
            if ((attr & 3) == 3) /* interrupt */
            {
                if ((addr & 0x80) && !k.iface[current].ep_in)
                {
                    k.iface[current].ep_in = addr;
                    k.in_size[current] = size;
                }
                else if (!(addr & 0x80) && !k.iface[current].ep_out)
                {
                    k.iface[current].ep_out = addr;
                    k.out_size[current] = size;
                }
            }
        }
        off += blen;
    }
    /* An interface without an interrupt IN is no use. */
    int kept = 0;
    for (int i = 0; i < n; ++i)
        if (k.iface[i].ep_in)
        {
            k.iface[kept] = k.iface[i];
            k.in_size[kept] = k.in_size[i];
            k.out_size[kept] = k.out_size[i];
            ++kept;
        }
    return kept;
}

/* The DolphinBar W010's own layout in its Wii Remote mode, when the
 * descriptors can't be read: four HID interfaces, IN 0x81..0x84. */
void assume_dolphinbar(Known &k)
{
    k.iface_count = kMaxInterfaces;
    for (int i = 0; i < kMaxInterfaces; ++i)
    {
        k.iface[i] = InterfaceStatus{};
        k.iface[i].number = i;
        k.iface[i].ep_in = std::uint8_t(0x81 + i);
        k.in_size[i] = kBuf;
        k.out_size[i] = 0;
    }
}

bool do_request(int fd, std::uint8_t type, std::uint8_t request, std::uint16_t value, std::uint16_t index,
                void *data, std::uint16_t length, std::uint16_t *actual = nullptr)
{
    CtlRequest r{};
    r.ucr_data = data;
    r.ucr_flags = 0x0001; /* USB_SHORT_XFER_OK */
    r.ucr_request.bmRequestType = type;
    r.ucr_request.bRequest = request;
    r.ucr_request.wValue[0] = std::uint8_t(value);
    r.ucr_request.wValue[1] = std::uint8_t(value >> 8);
    r.ucr_request.wIndex[0] = std::uint8_t(index);
    r.ucr_request.wIndex[1] = std::uint8_t(index >> 8);
    r.ucr_request.wLength[0] = std::uint8_t(length);
    r.ucr_request.wLength[1] = std::uint8_t(length >> 8);
    char where[24];
    std::snprintf(where, sizeof where, "fd %d iface %u", fd, unsigned(index));
    Track track(request == 0x09 ? "SET_REPORT" : "a control request", where);
    if (SYS_IOCTL(fd, kDoRequest, &r) != 0)
        return false;
    if (actual)
        *actual = r.ucr_actlen;
    return true;
}

/* The configuration in use: from the kernel's copy (no bus traffic), else a
 * GET_DESCRIPTOR on the bus, else the DolphinBar's layout. */
void read_layout(int fd, Known &k)
{
    std::uint8_t cfg[512];
    GenDescriptor g{};
    g.ugd_data = cfg;
    g.ugd_maxlen = sizeof cfg;
    g.ugd_config_index = 0xff; /* USB_UNCONFIG_INDEX: the current configuration */
    std::size_t len = 0;
    if (SYS_IOCTL(fd, kGetFullDesc, &g) == 0 && g.ugd_actlen >= 9)
        len = g.ugd_actlen;
    else
    {
        const int e1 = errno;
        std::uint16_t got = 0;
        if (do_request(fd, 0x80, 6 /* GET_DESCRIPTOR */, 0x0200, 0, cfg, 9, &got) && got >= 9)
        {
            const std::uint16_t total = std::min<std::uint16_t>(std::uint16_t(cfg[2] | (cfg[3] << 8)), sizeof cfg);
            if (do_request(fd, 0x80, 6, 0x0200, 0, cfg, total, &got) && got >= 9)
                len = got;
        }
        if (!len)
            logf("usbhid: %s: no configuration descriptor (full desc errno %d, request errno %d)", k.node.c_str(),
                 e1, errno);
    }
    k.layout_read = false;
    k.config.assign(cfg, cfg + len);
    if (len)
    {
        k.iface_count = parse_config(cfg, len, k);
        k.layout_read = k.iface_count > 0;
    }
    if (!k.layout_read)
        assume_dolphinbar(k);
    for (int i = 0; i < k.iface_count; ++i)
    {
        int n = k.iface[i].number;
        k.iface[i].kernel_driver = SYS_IOCTL(fd, kIfaceDriverActive, &n) == 0 ? 1 : 0;
    }
}

bool read_info(int fd, Known &k)
{
    DeviceInfo info{};
    if (SYS_IOCTL(fd, kDeviceInfoPs5, &info) != 0 && SYS_IOCTL(fd, kDeviceInfo, &info) != 0)
        return false;
    k.vid = info.vendorNo;
    k.pid = info.productNo;
    info.product[sizeof info.product - 1] = 0;
    k.product = info.product;
    return true;
}

Device *find_device(const std::string &node)
{
    for (auto &d : g_devices)
        if (d->node == node && !d->dead)
            return d.get();
    return nullptr;
}

Known *find_known(const std::string &node)
{
    for (auto &k : g_known)
        if (k.node == node)
            return &k;
    return nullptr;
}

/* ---- transfers (the device's lock held) ------------------------------------------------ */

void mark_dead(Device &d, const char *what, int err)
{
    if (!d.dead)
        logf("usbhid: %s: %s failed (errno %d): the device is gone", d.node.c_str(), what, err);
    d.dead = true;
}

void push_report(Interface &i, const std::uint8_t *data, std::uint32_t len)
{
    if (i.count == kQueue)
    {
        /* nobody took the oldest: the reader is behind or has stopped */
        i.head = (i.head + 1) % kQueue;
        --i.count;
        ++i.st.queue_drops;
    }
    if (i.count == 0)
        i.waiting_since = now_ms();
    const int slot = (i.head + i.count) % kQueue;
    std::memcpy(i.queue[slot], data, std::min(len, kBuf));
    i.queue_len[slot] = std::min(len, kBuf);
    ++i.count;
}

/* HOME held for a second in a game opens Porpoise's in-game menu
 * (set_home_menu): while HOME is down the game doesn't see it, and a short
 * press reaches the game as it's let go, as a report with HOME before the
 * one without it, so the Wii's own HOME menu still opens. Returns true when
 * that press report is due. */
std::atomic<bool> g_home_menu{false};
std::atomic<int> g_home_holds{0};
constexpr std::uint64_t kHomeHoldMs = 1000;

bool home_filter(Interface &i, std::uint8_t *b, std::uint32_t len)
{
    std::memcpy(i.latest, b, std::min(len, kBuf));
    i.latest_len = std::min(len, kBuf);
    i.latest_at = now_ms();
    const std::uint8_t id = b[0];
    if (len < 3 || !((id >= 0x20 && id <= 0x22) || (id >= 0x30 && id <= 0x37)))
        return false;
    const std::uint64_t t = now_ms();
    i.buttons = std::uint16_t(((b[1] << 8) | b[2]) & 0x1f9f);
    i.buttons_at = t;
    if (!g_home_menu.load(std::memory_order_relaxed))
    {
        i.home_down = false;
        i.ext_told = i.ext_seen = -1;
        return false;
    }
    if (id == 0x20 && len >= 4)
    {
        const int ext = (b[3] & 0x02) ? 1 : 0;
        if (ext != i.ext_seen)
        {
            i.ext_seen = ext;
            i.ext_since = t;
        }
    }
    if (b[2] & 0x80)
    {
        if (!i.home_down)
        {
            i.home_down = true;
            i.home_fired = false;
            i.home_since = t;
        }
        if (!i.home_fired && t - i.home_since >= kHomeHoldMs)
        {
            i.home_fired = true;
            g_home_holds.fetch_add(1);
            logf("usbhid: interface %d: HOME held: Porpoise's in-game menu", i.st.number);
        }
        b[2] &= std::uint8_t(~0x80);
        return false;
    }
    if (!i.home_down)
        return false;
    i.home_down = false;
    return !i.home_fired;
}

/* Collect every finished transfer. */
void reap(Device &d)
{
    for (;;)
    {
        FsIndex c{};
        if (SYS_IOCTL(d.fd, kFsComplete, &c) != 0)
        {
            /* EBUSY: nothing finished. Only a device that's gone ends it all;
             * anything else is counted and tried again with the next call. */
            if (errno == ENXIO || errno == ENODEV)
                mark_dead(d, "USB_FS_COMPLETE", errno);
            else if (errno != EBUSY && d.complete_errors++ < 5)
                logf("usbhid: %s: USB_FS_COMPLETE errno %d", d.node.c_str(), errno);
            return;
        }
        const int t = c.ep_index;
        if (t <= 0 || t >= kTransfers)
            continue;
        d.pending[t] = false;
        const int n = (t - 1) / 3;
        if (n >= d.iface_count)
            continue;
        Interface &i = d.iface[n];
        if (t == write_slot(n))
            continue; /* hid_write reads eps[t].status itself */
        const int status = d.eps[t].status;
        if (status == 0 && d.eps[t].aFrames > 0 && d.buf_len[t] > 0)
        {
            ++i.st.reports_in;
            i.last_in_before = i.last_in;
            i.last_in = now_ms();
            i.stall_logged = false;
            if (home_filter(i, d.buf[t], d.buf_len[t]) && i.st.open)
            {
                std::uint8_t press[kBuf];
                std::memcpy(press, d.buf[t], d.buf_len[t]);
                press[2] |= 0x80;
                push_report(i, press, d.buf_len[t]);
            }
            if (i.st.open)
            {
                push_report(i, d.buf[t], d.buf_len[t]);
                if (d.buf_len[t] != i.prev_len || std::memcmp(d.buf[t], i.prev_report, d.buf_len[t]) != 0)
                {
                    i.last_change = now_ms();
                    i.frozen_logged = false;
                    std::memcpy(i.prev_report, d.buf[t], d.buf_len[t]);
                    i.prev_len = d.buf_len[t];
                }
            }
        }
        else if (status != 0 && status != 5 /* USB_ERR_CANCELLED */)
        {
            i.st.last_read_status = status;
            if (i.st.read_errors++ < 5)
                logf("usbhid: %s:i%d read status %d", d.node.c_str(), i.st.number, status);
            i.clear_stall = true;
        }
    }
}

bool start(Device &d, int t, std::uint32_t len, std::uint16_t flags, std::uint16_t timeout)
{
    d.buf_len[t] = len;
    d.eps[t].nFrames = 1;
    d.eps[t].aFrames = 0;
    d.eps[t].status = 0;
    d.eps[t].flags = flags;
    d.eps[t].timeout = timeout;
    FsIndex s{std::uint8_t(t)};
    if (SYS_IOCTL(d.fd, kFsStart, &s) != 0)
    {
        if (errno == ENXIO || errno == ENODEV)
            mark_dead(d, "USB_FS_START", errno);
        else if (d.start_errors++ < 5)
            logf("usbhid: %s: USB_FS_START %d errno %d", d.node.c_str(), t, errno);
        return false;
    }
    d.pending[t] = true;
    return true;
}

void arm_reads(Device &d, int n)
{
    Interface &i = d.iface[n];
    for (int k = 0; k < kReadsPerInterface && !d.dead; ++k)
    {
        const int t = read_slot(n, k);
        if (!d.opened[t] || d.pending[t])
            continue;
        std::uint16_t flags = 0x0001; /* USB_FS_FLAG_SINGLE_SHORT_OK */
        if (i.clear_stall)
        {
            flags |= 0x0008; /* USB_FS_FLAG_CLEAR_STALL */
            i.clear_stall = false;
        }
        start(d, t, std::min<std::uint32_t>(kBuf, i.in_size ? i.in_size : kBuf), flags, 0);
    }
}

bool open_slot(Device &d, int t, std::uint8_t ep, std::uint32_t size)
{
    FsOpen o{};
    o.max_bufsize = std::min<std::uint32_t>(size ? size : kBuf, kBuf);
    o.max_frames = 1;
    o.ep_index = std::uint8_t(t);
    o.ep_no = ep;
    if (SYS_IOCTL(d.fd, kFsOpen, &o) != 0)
    {
        logf("usbhid: %s: USB_FS_OPEN endpoint %#x (transfer %d) errno %d", d.node.c_str(), ep, t, errno);
        return false;
    }
    d.opened[t] = true;
    return true;
}

/* The device closed: every transfer stopped first and waited for (a stop
 * never waits in the kernel; the transfer table's teardown and the node's
 * close drain what is still running, and a drain that doesn't end can't be
 * interrupted). If something is still running after 500 ms, the node is
 * left open and that's logged: better a node left open than a process that
 * can't end. */
void close_device(Device &d)
{
    if (d.fd < 0)
        return;
    if (!d.dead)
    {
        for (int t = 0; t < kTransfers; ++t)
            if (d.opened[t] && d.pending[t])
            {
                FsIndex s{std::uint8_t(t)};
                SYS_IOCTL(d.fd, kFsStop, &s);
            }
        const std::uint64_t until = now_ms() + 500;
        auto running = [&] {
            for (int t = 0; t < kTransfers; ++t)
                if (d.pending[t])
                    return true;
            return false;
        };
        while (running() && !d.dead && now_ms() < until)
        {
            reap(d);
            if (running())
                SYS_SLEEP_US(1000);
        }
        if (running() && !d.dead)
        {
            logf("usbhid: %s: transfers still running after being stopped: the node is left open", d.node.c_str());
            return;
        }
        if (!d.dead)
        {
            Track track("USB_FS_UNINIT", d.node);
            FsIndex u{0};
            SYS_IOCTL(d.fd, kFsUninit, &u);
        }
    }
    Track track("close", d.node);
    SYS_CLOSE(d.fd);
    d.fd = -1;
}

/* Input stopped although the Remote still takes reports (the console's own
 * driver can hold an endpoint of interface 0, a transfer can be left
 * hanging): the interface's reads are stopped and started again, once for
 * each stop, and it's said in the log. And every 30 s, a line of what the
 * interface did, for the log of a game (not when nothing happened). */
void diagnose(Device &d, int n);

void restart_reads(Device &d, int n)
{
    for (int k = 0; k < kReadsPerInterface; ++k)
    {
        const int t = read_slot(n, k);
        if (d.opened[t] && d.pending[t])
        {
            FsIndex s{std::uint8_t(t)};
            SYS_IOCTL(d.fd, kFsStop, &s);
        }
    }
    reap(d);
    d.iface[n].clear_stall = true;
    arm_reads(d, n);
}

void watch(Device &d, int n)
{
    Interface &i = d.iface[n];
    const std::uint64_t t = now_ms();
    /* A stream that stopped (reports came at 20 a second or more right before):
     * not a Remote that reports only changes and has none. Once a stop. */
    const bool streaming = i.last_in_before && i.last_in - i.last_in_before <= 50;
    if (streaming && !i.stall_logged && t - i.last_in > 1500 && i.last_out_ok && t - i.last_out_ok < 1500 &&
        t - i.last_restart > 2000)
    {
        int pending = 0;
        for (int k = 0; k < kReadsPerInterface; ++k)
            pending += d.pending[read_slot(n, k)];
        if (!i.stall_logged)
            logf("usbhid: %s:i%d no input for %llu ms while the Remote takes reports: reads restarted (%d were "
                 "pending, last read status %d)",
                 d.node.c_str(), i.st.number, (unsigned long long)(t - i.last_in), pending, i.st.last_read_status);
        i.stall_logged = true;
        i.last_restart = t;
        ++i.st.read_restarts;
        restart_reads(d, n);
    }
    diagnose(d, n);
}

/* What tells the three failures apart, said once each time one starts:
 *  - reports arrive, nobody takes them: the reader (Dolphin's read thread)
 *    has stopped; the transport and the Remote are fine;
 *  - reports arrive and are taken, but haven't changed for 10 s: what the
 *    Remote (or the DolphinBar) sends is frozen;
 *  - nothing arrives while the Remote takes reports: the transport (watch).
 * Called by readers and writers both (a stopped reader calls nothing). And
 * every 30 s, a summary line. */
void diagnose(Device &d, int n)
{
    Interface &i = d.iface[n];
    const std::uint64_t t = now_ms();
    if (!i.st.open)
        return;
    /* A report that has waited a second, not one that has just come after a
     * quiet while (a Remote reporting only on a change). */
    if (i.count > 0 && t - i.waiting_since > 1000 && i.last_taken && t - i.last_taken > 2000 && !i.reader_logged)
    {
        i.reader_logged = true;
        logf("usbhid: %s:i%d reports arrive but nobody has taken one for %llu ms (%d waiting, %u dropped): the "
             "reader has stopped, not the Remote",
             d.node.c_str(), i.st.number, (unsigned long long)(t - i.last_taken), i.count, i.st.queue_drops);
    }
    if (i.last_change && i.last_in && t - i.last_in < 1000 && t - i.last_change > 10000 && !i.frozen_logged)
    {
        i.frozen_logged = true;
        logf("usbhid: %s:i%d reports arrive but haven't changed for %llu ms (last %02x %02x %02x %02x %02x %02x): "
             "the Remote's data is frozen",
             d.node.c_str(), i.st.number, (unsigned long long)(t - i.last_change), i.prev_report[0],
             i.prev_report[1], i.prev_report[2], i.prev_report[3], i.prev_report[4], i.prev_report[5]);
    }
    if (!i.last_summary)
        i.last_summary = t;
    else if (t - i.last_summary >= 30000)
    {
        const double secs = double(t - i.last_summary) / 1000.0;
        if (i.st.reports_in != i.summary_in || i.st.writes_ok + i.st.writes_failed != i.summary_out)
            logf("usbhid: %s:i%d %.0f s: %.0f reports/s in (%u in all, %u taken, %u dropped unread, last change "
                 "%.1f s ago), writes %u ok %u failed %u retried, speaker %u (%u dropped, %u held back), write avg "
                 "%.1f ms max %u ms (%u slow), read errors %u (last status %d), reads restarted %u",
                 d.node.c_str(), i.st.number, secs, double(i.st.reports_in - i.summary_in) / secs, i.st.reports_in,
                 i.st.reports_taken, i.st.queue_drops,
                 i.last_change ? double(t - i.last_change) / 1000.0 : -1.0, i.st.writes_ok, i.st.writes_failed,
                 i.st.write_retries, i.st.speaker_reports, i.st.speaker_dropped, i.st.speaker_held_back,
                 double(i.st.write_avg_us) / 1000.0, i.st.write_max_ms, i.st.slow_writes, i.st.read_errors,
                 i.st.last_read_status, i.st.read_restarts);
        i.last_summary = t;
        i.summary_in = i.st.reports_in;
        i.summary_out = i.st.writes_ok + i.st.writes_failed;
    }
}
/* Empty slots that refused a status request, and when (kept across the
 * device's opens and closes, as Dolphin's scanner opens a slot to ask). */
std::mutex g_probe_lock;
std::vector<std::pair<std::string, std::uint64_t>> g_probe_refused;

bool probe_refused_recently(const std::string &key)
{
    std::lock_guard<std::mutex> g(g_probe_lock);
    for (const auto &p : g_probe_refused)
        if (p.first == key)
            return now_ms() - p.second < 2000;
    return false;
}

void note_probe_refused(const std::string &key)
{
    std::lock_guard<std::mutex> g(g_probe_lock);
    for (auto &p : g_probe_refused)
        if (p.first == key)
        {
            p.second = now_ms();
            return;
        }
    g_probe_refused.emplace_back(key, now_ms());
}

/* ---- the device table (g_devices_lock held) ----------------------------------------------- */

Device *open_device(const Known &k)
{
    auto d = std::make_unique<Device>();
    d->node = k.node;
    {
        Track track("open", k.node);
        d->fd = SYS_OPEN(("/dev/" + k.node).c_str(), false);
    }
    if (d->fd < 0)
    {
        logf("usbhid: open /dev/%s errno %d", k.node.c_str(), errno);
        return nullptr;
    }
    std::memset(d->eps, 0, sizeof d->eps);
    for (int t = 0; t < kTransfers; ++t)
    {
        d->buf_ptr[t] = d->buf[t];
        d->buf_len[t] = kBuf;
        d->eps[t].ppBuffer = &d->buf_ptr[t];
        d->eps[t].pLength = &d->buf_len[t];
        d->eps[t].nFrames = 1;
        d->pending[t] = false;
        d->opened[t] = false;
    }
    FsInit init{};
    init.pEndpoints = d->eps;
    init.ep_index_max = kTransfers;
    if (SYS_IOCTL(d->fd, kFsInit, &init) != 0)
    {
        logf("usbhid: %s: USB_FS_INIT errno %d", k.node.c_str(), errno);
        SYS_CLOSE(d->fd);
        return nullptr;
    }
    d->vid = k.vid;
    d->pid = k.pid;
    d->product = k.product;
    d->descriptors_read = k.layout_read;
    d->iface_count = k.iface_count;
    for (int n = 0; n < k.iface_count; ++n)
    {
        d->iface[n].st = k.iface[n];
        d->iface[n].st.open = false;
        d->iface[n].in_size = k.in_size[n];
        d->iface[n].out_size = k.out_size[n];
    }
    /* Every interface's transfers, once, for as long as the device is there:
     * opening and closing them for each hid_open_path / hid_close (Dolphin's
     * scanner asks the empty slots twice a second) made the console stop
     * and drain transfers all the time, and a drain that never ended held
     * the device's lock: input stopped reaching Dolphin, and the game, then
     * Porpoise, could no longer be closed (the first long PS5 sessions). */
    for (int n = 0; n < k.iface_count; ++n)
    {
        Interface &i = d->iface[n];
        bool ok = true;
        for (int r = 0; r < kReadsPerInterface && ok; ++r)
            ok = open_slot(*d, read_slot(n, r), i.st.ep_in, i.in_size);
        if (ok && i.st.ep_out)
            ok = open_slot(*d, write_slot(n), i.st.ep_out, i.out_size);
        i.slots_open = ok;
    }
    g_devices.push_back(std::move(d));
    return g_devices.back().get();
}

/* A handle is closed. The device and its transfers stay (see open_device),
 * unless it's gone: then it's closed for good. */
/* A device closed and taken off the list. One whose transfers wouldn't stop
 * is left in memory as well as open: the kernel may still write into its
 * buffers. */
void retire(std::unique_ptr<Device> &p)
{
    {
        std::lock_guard<std::mutex> dl(p->lock);
        close_device(*p);
    }
    if (p->fd >= 0)
        g_stuck_devices.push_back(std::move(p));
    p.reset();
}

void release(Device *dev)
{
    if (--dev->refs > 0 || !dev->dead)
        return;
    for (auto &p : g_devices)
        if (p.get() == dev)
        {
            retire(p);
            break;
        }
    g_devices.erase(std::remove(g_devices.begin(), g_devices.end(), nullptr), g_devices.end());
}

/* What a node is, opening it briefly if it isn't open already; false when it
 * isn't one of Nintendo's. */
bool identify(const std::string &node, Known &k)
{
    k = Known{};
    k.node = node;
    if (Device *d = find_device(node))
    {
        k.vid = d->vid;
        k.pid = d->pid;
        k.product = d->product;
        k.layout_read = d->descriptors_read;
        k.iface_count = d->iface_count;
        for (int n = 0; n < d->iface_count; ++n)
        {
            k.iface[n] = d->iface[n].st;
            k.in_size[n] = d->iface[n].in_size;
            k.out_size[n] = d->iface[n].out_size;
        }
        return wanted(k.vid, k.pid);
    }
    if (in_use(node))
        return false;
    const int fd = SYS_OPEN(("/dev/" + node).c_str(), true);
    if (fd < 0)
    {
        if (errno != ENOENT)
            g_unreadable_now.emplace_back(node, errno);
        return false;
    }
    bool ours = read_info(fd, k) && wanted(k.vid, k.pid);
    if (ours)
        read_layout(fd, k);
    SYS_CLOSE(fd);
    return ours;
}

bool parse_path(const char *path, std::string &node, int &number)
{
    const char *colon = path ? std::strstr(path, ":i") : nullptr;
    if (!colon || std::strncmp(path, "ugen", 4) != 0)
        return false;
    node.assign(path, std::size_t(colon - path));
    char *end = nullptr;
    const long n = std::strtol(colon + 2, &end, 10);
    if (end == colon + 2 || *end || n < 0 || n > 255)
        return false;
    number = int(n);
    return true;
}

wchar_t *wide(const std::string &s)
{
    wchar_t *w = static_cast<wchar_t *>(std::calloc(s.size() + 1, sizeof(wchar_t)));
    if (w)
        for (std::size_t i = 0; i < s.size(); ++i)
            w[i] = wchar_t(static_cast<unsigned char>(s[i]));
    return w;
}
} // namespace

/* ---- status ---------------------------------------------------------------------------- */

std::vector<DeviceStatus> status()
{
    std::vector<DeviceStatus> out;
    std::lock_guard<std::mutex> g(g_devices_lock);
    for (const Known &k : g_known)
    {
        DeviceStatus s;
        s.node = k.node;
        s.vid = k.vid;
        s.pid = k.pid;
        s.product = k.product;
        s.descriptors_read = k.layout_read;
        s.config = k.config;
        if (Device *d = find_device(k.node))
        {
            std::lock_guard<std::mutex> dl(d->lock);
            const std::uint64_t t = now_ms();
            for (int n = 0; n < d->iface_count; ++n)
            {
                const Interface &i = d->iface[n];
                InterfaceStatus st = i.st;
                st.last_input_age = i.last_in ? double(t - i.last_in) / 1000.0 : -1;
                st.last_output_age = i.last_out_ok ? double(t - i.last_out_ok) / 1000.0 : -1;
                st.reads_pending = 0;
                for (int k = 0; k < kReadsPerInterface; ++k)
                    st.reads_pending += d->pending[read_slot(n, k)] ? 1 : 0;
                s.interfaces.push_back(st);
            }
        }
        else
            for (int n = 0; n < k.iface_count; ++n)
                s.interfaces.push_back(k.iface[n]);
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<std::string> stuck_calls(unsigned min_ms)
{
    std::vector<std::string> out;
    const std::uint64_t t = now_ms();
    for (InFlight &f : g_flight)
    {
        const std::uint64_t since = f.since.load();
        if (since && since != ~std::uint64_t(0) && t - since >= min_ms)
        {
            char line[128];
            std::snprintf(line, sizeof line, "%s on %s for %llu ms", f.what ? f.what : "?", f.where,
                          (unsigned long long)(t - since));
            out.emplace_back(line);
        }
    }
    return out;
}

void set_node_in_use(const std::string &node, bool busy)
{
    std::lock_guard<std::mutex> g(g_in_use_lock);
    g_in_use.erase(std::remove(g_in_use.begin(), g_in_use.end(), node), g_in_use.end());
    if (busy)
        g_in_use.push_back(node);
}

void set_home_menu(bool on) { g_home_menu = on; }

bool take_home_hold()
{
    /* A Remote that reports only on a change sends nothing more while HOME
     * is just held: the second is also counted here. */
    if (g_home_menu.load(std::memory_order_relaxed))
    {
        std::lock_guard<std::mutex> g(g_devices_lock);
        const std::uint64_t t = now_ms();
        for (auto &d : g_devices)
        {
            std::lock_guard<std::mutex> dl(d->lock);
            for (int k = 0; k < d->iface_count; ++k)
            {
                Interface &i = d->iface[k];
                if (i.home_down && !i.home_fired && t - i.home_since >= kHomeHoldMs)
                {
                    i.home_fired = true;
                    g_home_holds.fetch_add(1);
                    logf("usbhid: interface %d: HOME held: Porpoise's in-game menu", i.st.number);
                }
            }
        }
    }
    int n = g_home_holds.load();
    while (n > 0 && !g_home_holds.compare_exchange_weak(n, 0))
        ;
    return n > 0;
}

bool take_attachment_event(int &interface_number, bool &plugged)
{
    /* Settled for 1.5 s: a game turning a MotionPlus on or off makes the
     * Remote report its attachment gone and back within a moment. */
    std::lock_guard<std::mutex> g(g_devices_lock);
    const std::uint64_t t = now_ms();
    for (auto &d : g_devices)
    {
        std::lock_guard<std::mutex> dl(d->lock);
        for (int k = 0; k < d->iface_count; ++k)
        {
            Interface &i = d->iface[k];
            if (i.ext_seen < 0 || i.ext_seen == i.ext_told || t - i.ext_since < 1500)
                continue;
            const bool first = i.ext_told < 0;
            i.ext_told = i.ext_seen;
            if (first)
                continue; /* how the game found it */
            interface_number = i.st.number;
            plugged = i.ext_told == 1;
            logf("usbhid: interface %d: attachment %s in the game", interface_number, plugged ? "plugged in" : "pulled out");
            return true;
        }
    }
    return false;
}

int latest_report(int interface_number, std::uint8_t *out, int max, unsigned &age_ms)
{
    std::lock_guard<std::mutex> g(g_devices_lock);
    const std::uint64_t t = now_ms();
    for (auto &d : g_devices)
    {
        std::lock_guard<std::mutex> dl(d->lock);
        if (d->dead)
            continue;
        reap(*d);
        for (int k = 0; k < d->iface_count; ++k)
        {
            const Interface &i = d->iface[k];
            if (i.st.number != interface_number || !i.latest_at)
                continue;
            const int n = int(std::min<std::uint32_t>(i.latest_len, std::uint32_t(max)));
            std::memcpy(out, i.latest, std::size_t(n));
            age_ms = unsigned(std::min<std::uint64_t>(t - i.latest_at, 0xffffffffu));
            return n;
        }
    }
    return 0;
}

std::uint16_t remote_buttons()
{
    std::lock_guard<std::mutex> g(g_devices_lock);
    const std::uint64_t t = now_ms();
    for (auto &d : g_devices)
    {
        std::lock_guard<std::mutex> dl(d->lock);
        if (!d->dead)
            reap(*d);
        for (int n = 0; n < d->iface_count; ++n)
        {
            const Interface &i = d->iface[n];
            if (i.st.remote && i.buttons_at && t - i.buttons_at < 5000)
                return i.buttons;
        }
    }
    return 0;
}

void shutdown()
{
    std::lock_guard<std::mutex> g(g_devices_lock);
    for (auto &d : g_devices)
        retire(d);
    g_devices.clear();
    logf("usbhid: closed for Porpoise's exit");
}

std::vector<std::pair<std::string, int>> unreadable()
{
    std::lock_guard<std::mutex> g(g_devices_lock);
    return g_unreadable;
}

bool interface_stats(const void *hid_device, InterfaceStatus &out)
{
    const Handle *h = static_cast<const Handle *>(hid_device);
    if (!h)
        return false;
    std::lock_guard<std::mutex> dl(h->dev->lock);
    out = h->dev->iface[h->iface].st;
    return true;
}

std::string describe()
{
    std::string text;
    char line[256];
    const auto devices = status();
    if (devices.empty())
        return "DolphinBar: none found (057e:0306 / 057e:0330)\n";
    for (const DeviceStatus &d : devices)
    {
        std::snprintf(line, sizeof line, "%s %04x:%04x \"%s\"%s\n", d.node.c_str(), d.vid, d.pid, d.product.c_str(),
                      d.descriptors_read ? "" : " (layout assumed)");
        text += line;
        for (const InterfaceStatus &i : d.interfaces)
        {
            std::snprintf(line, sizeof line,
                          "  i%d in %#04x out %#04x driver %s%s%s, reports %u, writes %u ok %u failed\n", i.number,
                          i.ep_in, i.ep_out,
                          i.kernel_driver < 0 ? "?" : i.kernel_driver ? "console's" : "none",
                          i.open ? ", open" : "", i.remote ? ", Wii Remote" : "", i.reports_in, i.writes_ok,
                          i.writes_failed);
            text += line;
        }
    }
    return text;
}

#ifdef PORPOISE_USBHID_HOST_TEST
void set_sys_ops(const SysOps &ops) { g_sys = ops; }
namespace abi
{
unsigned long fs_init() { return kFsInit; }
unsigned long fs_uninit() { return kFsUninit; }
unsigned long fs_open() { return kFsOpen; }
unsigned long fs_close() { return kFsClose; }
unsigned long fs_start() { return kFsStart; }
unsigned long fs_stop() { return kFsStop; }
unsigned long fs_complete() { return kFsComplete; }
unsigned long do_request() { return kDoRequest; }
unsigned long get_full_desc() { return kGetFullDesc; }
unsigned long device_info_ps5() { return kDeviceInfoPs5; }
unsigned long device_info() { return kDeviceInfo; }
unsigned long iface_driver_active() { return kIfaceDriverActive; }
unsigned long iface_driver_detach() { return kIfaceDriverDetach; }
} // namespace abi
#endif
} // namespace porpoise::usbhid

/* ---- hidapi ------------------------------------------------------------------------------- */
using namespace porpoise::usbhid;

extern "C" int hid_init(void) { return 0; }

extern "C" int hid_exit(void) { return 0; }

extern "C" struct hid_device_info *hid_enumerate(unsigned short vendor_id, unsigned short product_id)
{
    std::vector<std::string> names;
    if (!list_dev(names))
    {
        /* No listing: the usual node names (bus 0..7, address 2..15). */
        for (int b = 0; b < 8; ++b)
            for (int a = 2; a < 16; ++a)
                names.push_back("ugen" + std::to_string(b) + "." + std::to_string(a));
    }
    std::sort(names.begin(), names.end());

    hid_device_info *first = nullptr, **tail = &first;
    std::lock_guard<std::mutex> g(g_devices_lock);
    std::vector<Known> seen;
    const std::uint64_t now = now_ms();
    g_others.erase(std::remove_if(g_others.begin(), g_others.end(),
                                  [&](const Other &o) {
                                      return now - o.checked >= kRecheckMs ||
                                             std::find(names.begin(), names.end(), o.node) == names.end();
                                  }),
                   g_others.end());
    /* Unreadable nodes stay so while they're only being skipped. */
    g_unreadable_now.clear();
    for (const auto &u : g_unreadable)
        if (std::any_of(g_others.begin(), g_others.end(), [&](const Other &o) { return o.node == u.first; }))
            g_unreadable_now.push_back(u);
    for (const std::string &node : names)
    {
        if (node == kConsoleBluetooth || node.size() < 3 || node.compare(node.size() - 2, 2, ".1") == 0)
            continue; /* the console's Bluetooth chip, root hubs */
        if (std::any_of(g_others.begin(), g_others.end(), [&](const Other &o) { return o.node == node; }))
            continue;
        Known k;
        if (const Known *before = find_known(node); before && find_device(node))
            k = *before; /* open: nothing to ask the device */
        if (k.node.empty() && !identify(node, k))
        {
            g_others.push_back({node, now});
            continue;
        }
        if (!find_known(node))
            logf("usbhid: found %s %04x:%04x \"%s\", %d interface(s)%s", node.c_str(), k.vid, k.pid,
                 k.product.c_str(), k.iface_count, k.layout_read ? "" : " (DolphinBar layout assumed)");
        seen.push_back(k);
        if ((vendor_id && vendor_id != k.vid) || (product_id && product_id != k.pid))
            continue;
        for (int n = 0; n < k.iface_count; ++n)
        {
            hid_device_info *info = static_cast<hid_device_info *>(std::calloc(1, sizeof(hid_device_info)));
            if (!info)
                break;
            const std::string path = node + ":i" + std::to_string(k.iface[n].number);
            info->path = strdup(path.c_str());
            info->vendor_id = k.vid;
            info->product_id = k.pid;
            info->serial_number = wide("");
            info->manufacturer_string = wide("Nintendo");
            info->product_string = wide(k.product.empty() ? "RVL-CNT-01" : k.product);
            info->usage_page = 1;
            info->usage = 5;
            info->interface_number = k.iface[n].number;
            info->bus_type = HID_API_BUS_USB;
            *tail = info;
            tail = &info->next;
        }
    }
    for (const Known &old : g_known)
        if (std::none_of(seen.begin(), seen.end(), [&](const Known &k) { return k.node == old.node; }))
            logf("usbhid: %s is gone", old.node.c_str());
    g_known = std::move(seen);
    g_unreadable = g_unreadable_now;
    /* A device that's gone (unplugged, or its node no longer listed) and no
     * longer in use is closed now. */
    for (auto it = g_devices.begin(); it != g_devices.end();)
    {
        Device &d = **it;
        const bool listed = std::find(names.begin(), names.end(), d.node) != names.end();
        if (d.refs == 0 && (d.dead || !listed))
        {
            {
                std::lock_guard<std::mutex> dl(d.lock);
                d.dead = d.dead || !listed;
            }
            retire(*it);
            it = g_devices.erase(it);
        }
        else
            ++it;
    }
    return first;
}

extern "C" void hid_free_enumeration(struct hid_device_info *devs)
{
    while (devs)
    {
        hid_device_info *next = devs->next;
        std::free(devs->path);
        std::free(devs->serial_number);
        std::free(devs->manufacturer_string);
        std::free(devs->product_string);
        std::free(devs);
        devs = next;
    }
}

extern "C" hid_device *hid_open_path(const char *path)
{
    std::string node;
    int number = -1;
    if (!parse_path(path, node, number))
    {
        errno = ENOENT;
        return nullptr;
    }
    std::lock_guard<std::mutex> g(g_devices_lock);
    Device *dev = find_device(node);
    if (!dev)
    {
        Known k;
        if (const Known *before = find_known(node))
            k = *before;
        else if (!identify(node, k))
        {
            errno = ENOENT;
            return nullptr;
        }
        dev = open_device(k);
        if (!dev)
            return nullptr;
    }
    Handle *handle = nullptr;
    int error = EIO;
    {
        std::lock_guard<std::mutex> dl(dev->lock);
        int n = 0;
        while (n < dev->iface_count && dev->iface[n].st.number != number)
            ++n;
        if (n == dev->iface_count || dev->iface[n].st.open || dev->dead)
        {
            logf("usbhid: %s: %s", path,
                 dev->dead ? "the device is gone" : n == dev->iface_count ? "no such interface" : "already open");
            error = n == dev->iface_count || dev->dead ? ENOENT : EBUSY;
        }
        else
        {
            Interface &i = dev->iface[n];
            if (i.st.kernel_driver == 1 && g_detach)
            {
                int num = i.st.number;
                const bool detached = SYS_IOCTL(dev->fd, kIfaceDriverDetach, &num) == 0;
                logf("usbhid: %s: the console's driver %s (errno %d)", path,
                     detached ? "detached" : "could not be detached", detached ? 0 : errno);
                if (detached)
                    i.st.kernel_driver = 0;
            }
            if (i.slots_open)
            {
                i.st.open = true;
                i.head = i.count = 0;
                ++dev->refs;
                arm_reads(*dev, n);
                handle = new Handle{dev, n};
            }
            else
                logf("usbhid: %s: its transfers could not be opened", path);
        }
    }
    if (!handle && dev->refs == 0 && dev->dead)
    {
        dev->refs = 1; /* gone: closed for good */
        release(dev);
    }
    if (!handle)
        errno = error;
    return reinterpret_cast<hid_device *>(handle);
}

extern "C" hid_device *hid_open(unsigned short vendor_id, unsigned short product_id, const wchar_t *)
{
    hid_device_info *list = hid_enumerate(vendor_id, product_id);
    hid_device *h = list ? hid_open_path(list->path) : nullptr;
    hid_free_enumeration(list);
    return h;
}

extern "C" void hid_close(hid_device *device)
{
    Handle *h = reinterpret_cast<Handle *>(device);
    if (!h)
        return;
    std::lock_guard<std::mutex> g(g_devices_lock);
    {
        std::lock_guard<std::mutex> dl(h->dev->lock);
        /* Only no longer delivered: the transfers stay as they are (open_device). */
        const int n = h->iface;
        h->dev->iface[n].st.open = false;
        h->dev->iface[n].count = 0;
    }
    release(h->dev);
    delete h;
}

extern "C" int hid_read_timeout(hid_device *device, unsigned char *data, size_t length, int milliseconds)
{
    Handle *h = reinterpret_cast<Handle *>(device);
    if (!h || !data)
    {
        errno = EINVAL;
        return -1;
    }
    Device &d = *h->dev;
    const std::uint64_t deadline = milliseconds < 0 ? ~std::uint64_t(0) : now_ms() + std::uint64_t(milliseconds);
    for (;;)
    {
        {
            std::lock_guard<std::mutex> dl(d.lock);
            Interface &i = d.iface[h->iface];
            if (!d.dead)
            {
                reap(d);
                arm_reads(d, h->iface);
                watch(d, h->iface);
            }
            if (i.count)
            {
                const std::uint32_t len = std::min<std::uint32_t>(i.queue_len[i.head], std::uint32_t(length));
                std::memcpy(data, i.queue[i.head], len);
                i.head = (i.head + 1) % kQueue;
                --i.count;
                ++i.st.reports_taken;
                i.last_taken = now_ms();
                i.waiting_since = i.last_taken; /* the next one's age, near enough */
                i.reader_logged = false;
                return int(len);
            }
            if (d.dead)
            {
                errno = ENXIO;
                return -1;
            }
        }
        if (milliseconds == 0 || now_ms() >= deadline)
            return 0;
        SYS_SLEEP_US(1000);
    }
}

extern "C" int hid_read(hid_device *device, unsigned char *data, size_t length)
{
    Handle *h = reinterpret_cast<Handle *>(device);
    return hid_read_timeout(device, data, length, h && h->nonblocking ? 0 : -1);
}

extern "C" int hid_set_nonblocking(hid_device *device, int nonblock)
{
    Handle *h = reinterpret_cast<Handle *>(device);
    if (!h)
        return -1;
    h->nonblocking = nonblock != 0;
    return 0;
}

extern "C" int hid_write(hid_device *device, const unsigned char *data, size_t length)
{
    Handle *h = reinterpret_cast<Handle *>(device);
    if (!h || !data || !length || length > kBuf)
    {
        errno = EINVAL;
        return -1;
    }
    Device &d = *h->dev;
    const int n = h->iface;
    Interface &i = d.iface[n];
    /* Settings > Controllers & Accessories > Wii Remote Speaker off: no
     * speaker data goes out at all (no transfer), and the speaker is kept
     * switched off and muted whatever the game asks. The TV's sound and the
     * DualSense's speaker are another path and stay as they are. */
    std::uint8_t changed[kBuf];
    if (!g_speaker_on.load(std::memory_order_relaxed))
    {
        if (data[0] == 0x18)
        {
            std::lock_guard<std::mutex> dl(d.lock);
            ++i.st.speaker_held_back;
            return int(length);
        }
        if ((data[0] == 0x14 || data[0] == 0x19) && length >= 2)
        {
            std::memcpy(changed, data, length);
            changed[1] = data[0] == 0x14 ? std::uint8_t(data[1] & 0x01) /* off, the rumble bit kept */
                                         : std::uint8_t(data[1] | 0x04); /* muted */
            data = changed;
        }
    }
    /* A slot without a Remote that refused a status request less than 2 s
     * ago is not asked again yet: Dolphin's scanner asks every empty slot twice
     * a second, and on a DolphinBar without an interrupt OUT endpoint each
     * question is a control transfer on the same pipe as the speaker data. */
    const std::string probe_key = d.node + ":" + std::to_string(i.st.number);
    if (data[0] == 0x15 && !i.st.remote && probe_refused_recently(probe_key))
    {
        errno = EPIPE;
        return -1;
    }
    auto attempt = [&]() {
        bool ok = false;
        if (i.st.ep_out)
        {
            /* The interrupt OUT endpoint: the report as it is, its id first. */
            const int t = write_slot(n);
            const std::uint64_t until = now_ms() + kWriteTimeoutMs + 500;
            bool started = false;
            for (;;)
            {
                {
                    std::lock_guard<std::mutex> dl(d.lock);
                    if (d.dead)
                        break;
                    reap(d);
                    if (!started && !d.pending[t])
                    {
                        std::memcpy(d.buf[t], data, length);
                        if (!start(d, t, std::uint32_t(length), 0x0004 /* USB_FS_FLAG_FORCE_SHORT */, kWriteTimeoutMs))
                            break;
                        started = true;
                    }
                    else if (started && !d.pending[t])
                    {
                        ok = d.eps[t].status == 0;
                        if (!ok && (i.st.writes_failed < 5 || i.st.remote))
                            logf("usbhid: %s:i%d write status %d", d.node.c_str(), i.st.number, d.eps[t].status);
                        break;
                    }
                }
                if (now_ms() >= until)
                    break;
                SYS_SLEEP_US(250);
            }
        }
        else
        {
            /* SET_REPORT (Output, the report's id) to the interface: the
             * DolphinBar stalls it when no Remote is in that slot. Not under
             * the device's lock: reads go on meanwhile. */
            std::uint8_t copy[kBuf];
            std::memcpy(copy, data, length);
            const int fd = d.dead ? -1 : d.fd;
            ok = fd >= 0 && do_request(fd, 0x21, 0x09, std::uint16_t(0x0200 | data[0]),
                                       std::uint16_t(i.st.number), copy, std::uint16_t(length));
        }
        return ok;
    };

    /* What Dolphin does with a failed write (WiimoteReal.cpp, the write
     * thread): the Remote is taken as gone and disconnected from the game at
     * once. So:
     *  - the Remote's speaker data (0x18) is sound, sent 75 times a second; a
     *    packet the DolphinBar refuses is a moment of lost sound and is
     *    dropped and counted, never reported as a disconnect;
     *  - any other report refused once is sent a second time, so one
     *    hiccup doesn't end the game's connection; a Remote that's really
     *    gone refuses both, and Dolphin's once-a-second write still finds it. */
    const bool speaker = data[0] == 0x18;
    const std::uint64_t t0 = now_ms();
    bool ok = attempt();
    bool retried = false;
    if (!ok && !d.dead && !speaker)
    {
        SYS_SLEEP_US(2000);
        ok = attempt();
        retried = true;
    }
    const std::uint64_t took = now_ms() - t0;

    std::lock_guard<std::mutex> dl(d.lock);
    if (!d.dead)
    {
        /* A reader that has stopped re-arms nothing: the writer keeps the
         * reads going, so the reports are still counted and its stop is seen. */
        reap(d);
        arm_reads(d, n);
    }
    diagnose(d, n);
    i.write_ms_total += took;
    i.st.write_max_ms = std::max<std::uint32_t>(i.st.write_max_ms, std::uint32_t(took));
    if (took >= 50)
        ++i.st.slow_writes;
    if (speaker)
        ++i.st.speaker_reports;
    if (ok)
    {
        if (!i.st.remote)
            logf("usbhid: %s:i%d a Wii Remote answers", d.node.c_str(), i.st.number);
        if (retried && i.st.write_retries++ < 5)
            logf("usbhid: %s:i%d report %02x refused once, taken the second time", d.node.c_str(), i.st.number,
                 data[0]);
        i.st.remote = true;
        i.last_out_ok = now_ms();
        ++i.st.writes_ok;
        i.st.write_avg_us = std::uint32_t(i.write_ms_total * 1000 / std::max<std::uint32_t>(1, i.st.writes_ok));
        return int(length);
    }
    if (speaker && !d.dead)
    {
        if (i.st.speaker_dropped++ % 200 == 0)
            logf("usbhid: %s:i%d speaker data refused (%u dropped so far); the Remote stays connected", d.node.c_str(),
                 i.st.number, i.st.speaker_dropped);
        return int(length);
    }
    if (data[0] == 0x15 && !i.st.remote)
        note_probe_refused(probe_key);
    if (i.st.remote)
        logf("usbhid: %s:i%d the Wii Remote is gone (report %02x refused twice)", d.node.c_str(), i.st.number,
             data[0]);
    i.st.remote = false;
    ++i.st.writes_failed;
    errno = d.dead ? ENXIO : EPIPE;
    return -1;
}
/* Feature reports: no Wii Remote has any. The core's Steam Deck input
 * (InputCommon/ControllerInterface/SteamDeck) imports this, but it never gets
 * a device: only Nintendo's are listed. */
extern "C" int hid_send_feature_report(hid_device *, const unsigned char *, size_t)
{
    errno = ENOSYS;
    return -1;
}

extern "C" const wchar_t *hid_error(hid_device *) { return L"porpoise usbhid: see the DolphinBar log"; }

#endif /* !PORPOISE_DESKTOP */
