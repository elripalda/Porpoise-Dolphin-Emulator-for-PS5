/* Porpoise - a USB Bluetooth adapter and the Switch 2's GameCube controller
 * through it (porpoise_bt.hpp says what).
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The node's transfer table:
 *
 *   transfer 1  the HCI interface's interrupt IN (events), one packet at a
 *               time, put back together into events by their header (as
 *               Linux's btusb does)
 *   transfer 2  its bulk IN (ACL data from the controller)
 *   transfer 3  its bulk OUT (ACL data to it)
 *
 * Commands go as class requests on the control endpoint (USB_DO_REQUEST).
 * On the LE link: L2CAP put back together, ATT (the controller's GATT
 * server; we serve nothing) and the signalling channel's parameter update.
 * One thread does all of it; whatever waits (a command, an ATT response)
 * keeps handling everything else that comes in meanwhile. */
#include "porpoise_bt.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
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
#include <ps5platform/libc.h>
#include "porpoise_dolphinbar.hpp"
#include "porpoise_ns2gc.hpp"
#include "porpoise_paths.hpp"
#include "porpoise_ugen.hpp"
#include "porpoise_usbhid.hpp"
#include "title_threads.hpp"
#endif

namespace porpoise::bt
{
/* ---- pure ------------------------------------------------------------------------------------- */
namespace
{
std::uint16_t le16(const std::uint8_t *p) { return std::uint16_t(p[0] | (p[1] << 8)); }
std::uint32_t le32(const std::uint8_t *p)
{
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) |
           (std::uint32_t(p[3]) << 24);
}
} // namespace

std::vector<std::uint8_t> realtek_patch(const std::vector<std::uint8_t> &fw, const std::vector<std::uint8_t> &config,
                                        int rom_version, std::string &why)
{
    static const std::uint8_t kExtension[4] = {0x51, 0x04, 0xfd, 0x77};
    const std::size_t header = 14; /* signature, fw_version, num_patches */
    if (fw.size() < header + 4 + 3 || std::memcmp(fw.data(), "Realtech", 8) != 0)
    {
        why = "not a Realtek patch file (\"Realtech\")";
        return {};
    }
    if (std::memcmp(fw.data() + fw.size() - 4, kExtension, 4) != 0)
    {
        why = "its extension section is missing";
        return {};
    }
    /* the project id, from the end */
    int project = -1;
    for (std::size_t p = fw.size() - 4; p >= header + 3;)
    {
        const std::uint8_t opcode = fw[--p], length = fw[--p], data = fw[--p];
        if (opcode == 0xff)
            break;
        if (length == 0)
        {
            why = "an instruction of length 0";
            return {};
        }
        if (opcode == 0 && length == 1)
        {
            project = data;
            break;
        }
        if (p < length + header + 3)
            break;
        p -= length;
    }
    if (project != 3 && project != 14 && project != 51) /* 8761A, 8761B, 8761C */
    {
        char b[64];
        std::snprintf(b, sizeof b, "it's for project %d, not an RTL8761", project);
        why = b;
        return {};
    }
    const std::uint32_t version = le32(fw.data() + 8);
    const int n = le16(fw.data() + 12);
    if (fw.size() < header + std::size_t(n) * 8 + 4)
    {
        why = "too short for its patches";
        return {};
    }
    const std::uint8_t *ids = fw.data() + header, *lengths = ids + 2 * n, *offsets = lengths + 2 * n;
    for (int i = 0; i < n; ++i)
    {
        if (le16(ids + 2 * i) != rom_version + 1)
            continue;
        const std::uint32_t len = le16(lengths + 2 * i), off = le32(offsets + 4 * i);
        if (len < 4 || off > fw.size() || len > fw.size() - off)
        {
            why = "its patch lies outside the file";
            return {};
        }
        std::vector<std::uint8_t> out(fw.begin() + off, fw.begin() + off + len);
        std::memcpy(out.data() + len - 4, &version, 4); /* little-endian, as the file has it */
        out.insert(out.end(), config.begin(), config.end());
        why.clear();
        return out;
    }
    char b[64];
    std::snprintf(b, sizeof b, "no patch for ROM version %d (%d in the file)", rom_version, n);
    why = b;
    return {};
}

std::string chip_name(int manufacturer, int lmp_subver, int hci_rev)
{
    if (manufacturer == 93) /* Realtek */
    {
        if (lmp_subver == 0x8761)
            return hci_rev == 0x000b ? "Realtek RTL8761BU" : hci_rev == 0x000e ? "Realtek RTL8761CU"
                   : hci_rev == 0x000a ? "Realtek RTL8761AU"
                                       : "Realtek RTL8761 (unknown revision)";
        return "Realtek (firmware running)";
    }
    char b[64];
    std::snprintf(b, sizeof b, "manufacturer %d", manufacturer);
    return b;
}

void parse_advertising(const std::uint8_t *d, int length, Found &f)
{
    for (int p = 0; p + 1 < length;)
    {
        const int len = d[p];
        if (len == 0 || p + 1 + len > length)
            break;
        const std::uint8_t type = d[p + 1];
        const std::uint8_t *v = d + p + 2;
        const int vl = len - 1;
        if ((type == 0x08 || type == 0x09) && vl > 0)
        {
            std::string name(reinterpret_cast<const char *>(v), std::size_t(vl));
            for (char &c : name)
                if (static_cast<unsigned char>(c) < 0x20)
                    c = '?';
            if (type == 0x09 || f.name.empty())
                f.name = name;
        }
        else if (type == 0xff && vl >= 2)
        {
            f.company = le16(v);
            f.nintendo = f.company == 0x0553;
            f.maker.assign(v + 2, v + std::min(vl, 26));
        }
        p += 1 + len;
    }
}

std::string address_text(const std::uint8_t a[6])
{
    char b[24];
    std::snprintf(b, sizeof b, "%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
    return b;
}
bool is_gamecube_controller(const Found &f)
{
    /* after the company id: 01 00 03, then the vendor and product id */
    return f.nintendo && f.maker.size() >= 7 && f.maker[3] == 0x7e && f.maker[4] == 0x05 && f.maker[5] == 0x73 &&
           f.maker[6] == 0x20;
}
} // namespace porpoise::bt

#ifdef PORPOISE_DESKTOP
namespace porpoise::bt
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
#else
Status status() { return {}; }
#endif
void start() {}
void clear_found() {}
bool close_for_exit(unsigned) { return true; }
} // namespace porpoise::bt
#else

namespace porpoise::bt
{
namespace
{
using namespace porpoise::ugen;

std::uint64_t now_ms()
{
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return std::uint64_t(t.tv_sec) * 1000u + std::uint64_t(t.tv_nsec) / 1000000u;
}

void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void logf(const char *fmt, ...)
{
    char line[600];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    porpoise::dolphinbar::log("%s", line);
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

std::mutex g_lock;
Status g_status;
std::atomic<bool> g_started{false}, g_run{false}, g_finished{true}, g_clear{false};

void publish(const std::function<void(Status &)> &f)
{
    std::lock_guard<std::mutex> g(g_lock);
    f(g_status);
}

constexpr const char *kConsoleBluetooth = "ugen0.2";
constexpr int kTransfers = 4, kEvents = 1, kAclIn = 2, kAclOut = 3;
constexpr std::uint32_t kBuf = 1024;

/* ---- the adapter ----------------------------------------------------------------------------- */
struct Adapter
{
    std::string node;
    int fd = -1;
    bool dead = false;
    int iface = 0;
    std::uint8_t ep_events = 0, ep_in = 0, ep_out = 0;
    std::uint16_t event_size = 16, out_size = 64;
    FsEndpoint eps[kTransfers];
    void *buf_ptr[kTransfers];
    std::uint32_t buf_len[kTransfers];
    std::uint8_t buf[kTransfers][kBuf];
    bool opened[kTransfers] = {}, pending[kTransfers] = {};
    bool stopping = false;
    std::vector<std::uint8_t> ev_partial, acl_partial;
    /* Command Complete / Command Status not yet taken (command()) */
    std::vector<std::vector<std::uint8_t>> done;
    /* LE ACL: the controller's buffers and how many of ours it holds */
    int acl_len = 27, acl_num = 1, acl_out = 0;
    std::uint8_t bd[6] = {}; /* its address, as shown */
};

/* ---- the controller's link -------------------------------------------------------------------- */
struct Peer
{
    bool connecting = false, connected = false;
    std::uint16_t handle = 0;
    std::uint8_t addr[6] = {}; /* as shown */
    bool random = false;
    int status = 0;
    int mtu = 23;
    /* an L2CAP frame being put together */
    std::vector<std::uint8_t> l2cap;
    std::size_t l2cap_want = 0;
    /* an ATT response awaited (att_request): its opcode, then what came */
    std::uint8_t att_wait = 0;
    bool att_got = false, att_error = false;
    std::vector<std::uint8_t> att_reply;
    /* a command response awaited on 0x001A */
    std::uint8_t cmd_wait = 0, sub_wait = 0;
    bool cmd_got = false;
    std::vector<std::uint8_t> cmd_reply;
    bool ready = false;
    std::uint32_t notifications = 0, on_0e = 0, on_0a = 0;
    int disconnect_reason = -1;
    unsigned rumble_seq = 0;
    bool rumble_on = false;
    std::uint64_t rumble_next = 0;
};

void handle_event(Adapter &a, Peer &p, const std::vector<std::uint8_t> &e);
void handle_acl(Adapter &a, Peer &p, const std::uint8_t *d, std::size_t n);

bool start_transfer(Adapter &a, int t, std::uint32_t len, std::uint16_t flags, std::uint16_t timeout)
{
    a.buf_len[t] = len;
    a.eps[t].nFrames = 1;
    a.eps[t].aFrames = 0;
    a.eps[t].status = 0;
    a.eps[t].flags = flags;
    a.eps[t].timeout = timeout;
    FsIndex s{std::uint8_t(t)};
    if (::ioctl(a.fd, kFsStart, &s) != 0)
    {
        if (errno == ENXIO || errno == ENODEV)
            a.dead = true;
        logf("bt: %s: USB_FS_START %d errno %d", a.node.c_str(), t, errno);
        return false;
    }
    a.pending[t] = true;
    return true;
}

void arm(Adapter &a)
{
    if (a.stopping || a.dead)
        return;
    if (a.opened[kEvents] && !a.pending[kEvents])
        start_transfer(a, kEvents, a.event_size, kFlagSingleShortOk, 0);
    if (a.opened[kAclIn] && !a.pending[kAclIn])
        start_transfer(a, kAclIn, kBuf, kFlagSingleShortOk, 0);
}

/* Everything that came in, handed on; the reads armed again. */
void pump(Adapter &a, Peer &p)
{
    for (;;)
    {
        FsIndex c{};
        if (::ioctl(a.fd, kFsComplete, &c) != 0)
        {
            if (errno == ENXIO || errno == ENODEV)
            {
                if (!a.dead)
                    logf("bt: %s: the adapter is gone", a.node.c_str());
                a.dead = true;
            }
            break;
        }
        const int t = c.ep_index;
        if (t <= 0 || t >= kTransfers)
            continue;
        a.pending[t] = false;
        const int status = a.eps[t].status;
        if (t == kAclOut)
            continue; /* send_acl looks at it */
        if (status != 0)
        {
            static unsigned logged = 0;
            if (status != kErrCancelled && logged++ < 10)
                logf("bt: %s: read %d status %d", a.node.c_str(), t, status);
            continue;
        }
        if (a.eps[t].aFrames == 0)
            continue;
        std::vector<std::uint8_t> &part = t == kEvents ? a.ev_partial : a.acl_partial;
        part.insert(part.end(), a.buf[t], a.buf[t] + a.buf_len[t]);
        if (t == kEvents)
            while (part.size() >= 2 && part.size() >= std::size_t(2 + part[1]))
            {
                const std::size_t n = 2 + part[1];
                std::vector<std::uint8_t> e(part.begin(), part.begin() + long(n));
                part.erase(part.begin(), part.begin() + long(n));
                handle_event(a, p, e);
            }
        else
            while (part.size() >= 4 && part.size() >= std::size_t(4 + le16(&part[2])))
            {
                const std::size_t n = 4 + le16(&part[2]);
                std::vector<std::uint8_t> d(part.begin(), part.begin() + long(n));
                part.erase(part.begin(), part.begin() + long(n));
                handle_acl(a, p, d.data(), d.size());
            }
    }
    arm(a);
}

/* An HCI command; its Command Complete's parameters (the status first) into
 * ret, or the Command Status's status. False: refused or no answer. */
bool command(Adapter &a, Peer &p, std::uint16_t opcode, const std::uint8_t *params, int plen,
             std::vector<std::uint8_t> &ret, unsigned timeout_ms = 2000)
{
    ret.clear();
    std::uint8_t pkt[3 + 255];
    pkt[0] = std::uint8_t(opcode);
    pkt[1] = std::uint8_t(opcode >> 8);
    pkt[2] = std::uint8_t(plen);
    if (plen)
        std::memcpy(pkt + 3, params, std::size_t(plen));
    CtlRequest q{};
    q.ucr_data = pkt;
    q.ucr_request.bmRequestType = 0x20; /* class, interface */
    q.ucr_request.wIndex[0] = std::uint8_t(a.iface);
    q.ucr_request.wLength[0] = std::uint8_t(3 + plen);
    q.ucr_request.wLength[1] = std::uint8_t((3 + plen) >> 8); /* a firmware part is 256 */
    if (::ioctl(a.fd, kDoRequest, &q) != 0)
    {
        if (errno == ENXIO || errno == ENODEV)
            a.dead = true;
        logf("bt: %s: command %04x not sent (errno %d)", a.node.c_str(), opcode, errno);
        return false;
    }
    const std::uint64_t until = now_ms() + timeout_ms;
    while (!a.dead && g_run)
    {
        pump(a, p);
        for (std::size_t i = 0; i < a.done.size(); ++i)
        {
            const auto &e = a.done[i];
            const bool complete = e[0] == 0x0e && e.size() >= 6 && le16(&e[3]) == opcode;
            const bool status = e[0] == 0x0f && e.size() >= 6 && le16(&e[4]) == opcode;
            if (!complete && !status)
                continue;
            if (complete)
                ret.assign(e.begin() + 5, e.end());
            else
                ret.assign(1, e[2]);
            a.done.erase(a.done.begin() + long(i));
            if (!ret.empty() && ret[0] != 0)
                logf("bt: %s: command %04x: status %#x", a.node.c_str(), opcode, ret[0]);
            return !ret.empty() && ret[0] == 0;
        }
        if (now_ms() >= until)
            break;
        ::usleep(1000);
    }
    logf("bt: %s: command %04x: no answer", a.node.c_str(), opcode);
    return false;
}

/* One L2CAP frame to the controller on cid, cut to the adapter's ACL size. */
bool send_l2cap(Adapter &a, Peer &p, std::uint16_t cid, const std::uint8_t *data, std::size_t len)
{
    if (!p.connected || !a.opened[kAclOut])
        return false;
    std::vector<std::uint8_t> frame(4 + len);
    frame[0] = std::uint8_t(len);
    frame[1] = std::uint8_t(len >> 8);
    frame[2] = std::uint8_t(cid);
    frame[3] = std::uint8_t(cid >> 8);
    std::memcpy(frame.data() + 4, data, len);
    for (std::size_t off = 0; off < frame.size();)
    {
        const std::size_t n = std::min<std::size_t>(std::size_t(a.acl_len), frame.size() - off);
        /* wait for a buffer of the controller's, and our transfer */
        const std::uint64_t until = now_ms() + 1000;
        while ((a.acl_out >= a.acl_num || a.pending[kAclOut]) && !a.dead && p.connected && now_ms() < until)
        {
            pump(a, p);
            ::usleep(500);
        }
        if (a.acl_out >= a.acl_num || a.pending[kAclOut] || a.dead || !p.connected)
        {
            logf("bt: %s: no buffer to send in (%d of %d held)", a.node.c_str(), a.acl_out, a.acl_num);
            return false;
        }
        std::uint8_t *b = a.buf[kAclOut];
        const std::uint16_t flags = off == 0 ? 0x0000 : 0x1000; /* first (not flushable), continuing */
        const std::uint16_t h = std::uint16_t(p.handle | flags);
        b[0] = std::uint8_t(h);
        b[1] = std::uint8_t(h >> 8);
        b[2] = std::uint8_t(n);
        b[3] = std::uint8_t(n >> 8);
        std::memcpy(b + 4, frame.data() + off, n);
        if (!start_transfer(a, kAclOut, std::uint32_t(4 + n), 0, 1000))
            return false;
        ++a.acl_out;
        off += n;
    }
    return true;
}

bool att_send(Adapter &a, Peer &p, const std::vector<std::uint8_t> &pdu)
{
    return send_l2cap(a, p, 0x0004, pdu.data(), pdu.size());
}

/* An ATT request and its response (op_reply), or an Error Response. */
bool att_request(Adapter &a, Peer &p, const std::vector<std::uint8_t> &pdu, std::uint8_t op_reply,
                 std::vector<std::uint8_t> &reply, unsigned timeout_ms = 2000)
{
    p.att_wait = op_reply;
    p.att_got = p.att_error = false;
    if (!att_send(a, p, pdu))
    {
        p.att_wait = 0;
        return false;
    }
    const std::uint64_t until = now_ms() + timeout_ms;
    while (!p.att_got && p.connected && !a.dead && g_run && now_ms() < until)
    {
        pump(a, p);
        if (!p.att_got)
            ::usleep(500);
    }
    p.att_wait = 0;
    reply = p.att_reply;
    return p.att_got && !p.att_error;
}

bool write_request(Adapter &a, Peer &p, std::uint16_t handle, const std::vector<std::uint8_t> &value)
{
    std::vector<std::uint8_t> pdu = {0x12, std::uint8_t(handle), std::uint8_t(handle >> 8)};
    pdu.insert(pdu.end(), value.begin(), value.end());
    std::vector<std::uint8_t> r;
    const bool ok = att_request(a, p, pdu, 0x13, r);
    if (!ok)
        logf("bt: write to %#06x %s%s", handle, p.att_error ? "refused: " : "not answered",
             p.att_error ? hex(r.data(), int(r.size())).c_str() : "");
    return ok;
}

bool write_command(Adapter &a, Peer &p, std::uint16_t handle, const std::uint8_t *v, std::size_t n)
{
    std::vector<std::uint8_t> pdu = {0x52, std::uint8_t(handle), std::uint8_t(handle >> 8)};
    pdu.insert(pdu.end(), v, v + n);
    return att_send(a, p, pdu);
}

/* A command on the controller's command channel (0x0014) and its response
 * (notified on 0x001A): command, 0x91, 0x01 (Bluetooth), sub-command, 0,
 * length, 0, 0, data. */
bool gc_command(Adapter &a, Peer &p, std::uint8_t cmd, std::uint8_t sub, const std::vector<std::uint8_t> &data,
                std::vector<std::uint8_t> &reply)
{
    std::vector<std::uint8_t> c = {cmd, 0x91, 0x01, sub, 0x00, std::uint8_t(data.size()), 0x00, 0x00};
    c.insert(c.end(), data.begin(), data.end());
    p.cmd_wait = cmd;
    p.sub_wait = sub;
    p.cmd_got = false;
    if (!write_command(a, p, 0x0014, c.data(), c.size()))
        return false;
    const std::uint64_t until = now_ms() + 1000;
    while (!p.cmd_got && p.connected && !a.dead && g_run && now_ms() < until)
    {
        pump(a, p);
        if (!p.cmd_got)
            ::usleep(500);
    }
    p.cmd_wait = 0;
    reply = p.cmd_reply;
    if (!p.cmd_got)
        logf("bt: command %02x %02x: no response", cmd, sub);
    return p.cmd_got;
}

/* ---- what comes in ---------------------------------------------------------------------------- */
void add_found(const std::uint8_t *r, int len, Found *out)
{
    /* one report: event type, address type, address, data length, data, RSSI */
    if (len < 10)
        return;
    const int dl = r[8];
    if (9 + dl + 1 > len)
        return;
    Found f;
    for (int i = 0; i < 6; ++i)
        f.addr[i] = r[7 - i]; /* sent least significant byte first */
    f.random = r[1] & 1;
    f.rssi = std::int8_t(r[9 + dl]);
    parse_advertising(r + 9, dl, f);
    if (out)
        *out = f;
    publish([&](Status &s) {
        for (Found &o : s.found)
            if (std::memcmp(o.addr, f.addr, 6) == 0)
            {
                ++o.seen;
                o.rssi = f.rssi;
                if (!f.name.empty())
                    o.name = f.name;
                if (f.company >= 0)
                {
                    o.company = f.company;
                    o.nintendo = f.nintendo;
                    o.maker = f.maker;
                }
                return;
            }
        f.seen = 1;
        if (s.found.size() < 32)
            s.found.push_back(f);
        if (f.nintendo)
            logf("bt: a Nintendo device: %s%s %d dBm, data %s", address_text(f.addr).c_str(),
                 f.random ? " (random)" : "", f.rssi, hex(f.maker.data(), int(f.maker.size())).c_str());
    });
}

/* The controller to connect to, when one advertised (set by handle_event). */
Found g_candidate;
bool g_have_candidate = false;

void handle_event(Adapter &a, Peer &p, const std::vector<std::uint8_t> &e)
{
    const std::uint8_t code = e[0];
    if (code == 0x0e || code == 0x0f)
    {
        a.done.push_back(e);
        if (a.done.size() > 16)
            a.done.erase(a.done.begin());
        return;
    }
    if (code == 0x13 && e.size() >= 3) /* Number Of Completed Packets */
    {
        const int n = e[2];
        for (int i = 0; i < n && 3 + 4 * std::size_t(i) + 4 <= e.size(); ++i)
            a.acl_out = std::max(0, a.acl_out - le16(&e[3 + 4 * i + 2]));
        return;
    }
    if (code == 0x05 && e.size() >= 6) /* Disconnection Complete */
    {
        if (p.connected && le16(&e[3]) == p.handle)
        {
            p.connected = false;
            p.disconnect_reason = e[5];
            a.acl_out = 0;
            logf("bt: %s: disconnected, reason %#x", address_text(p.addr).c_str(), e[5]);
        }
        return;
    }
    if (code != 0x3e || e.size() < 3)
        return;
    const std::uint8_t sub = e[2];
    if (sub == 0x02 && e.size() >= 4) /* LE Advertising Report */
    {
        const int count = e[3];
        std::size_t q = 4;
        for (int k = 0; k < count && q + 9 <= e.size(); ++k)
        {
            const std::size_t one = 9 + std::size_t(e[q + 8]) + 1;
            if (q + one > e.size())
                break;
            Found f;
            add_found(&e[q], int(one), &f);
            /* a controller looking for a host (pair button), or for us (paired, a button pressed); not one
             * that wakes another host, a Switch 2 */
            std::uint8_t host[6] = {};
            if (f.maker.size() >= 16)
                for (int i = 0; i < 6; ++i)
                    host[i] = f.maker[15 - i];
            const bool any = std::all_of(host, host + 6, [](std::uint8_t v) { return v == 0; });
            if (!p.connected && !p.connecting && !g_have_candidate && is_gamecube_controller(f) &&
                (any || std::memcmp(host, a.bd, 6) == 0))
            {
                g_candidate = f;
                g_have_candidate = true;
            }
            q += one;
        }
    }
    else if (sub == 0x01 && e.size() >= 21) /* LE Connection Complete */
    {
        p.connecting = false;
        p.status = e[3];
        if (e[3] != 0)
        {
            logf("bt: connection failed, status %#x", e[3]);
            return;
        }
        p.connected = true;
        p.handle = le16(&e[4]) & 0x0fff;
        p.mtu = 23;
        p.l2cap.clear();
        p.l2cap_want = 0;
        const int interval = le16(&e[14]);
        logf("bt: %s: connected, handle %#x, interval %.2f ms, timeout %d ms", address_text(p.addr).c_str(), p.handle,
             interval * 1.25, le16(&e[18]) * 10);
        publish([&](Status &s) { s.interval_x125 = interval; });
    }
    else if (sub == 0x03 && e.size() >= 12) /* LE Connection Update Complete */
    {
        const int interval = le16(&e[6]);
        logf("bt: connection updated: status %#x, interval %.2f ms", e[3], interval * 1.25);
        if (e[3] == 0)
            publish([&](Status &s) { s.interval_x125 = interval; });
    }
}

/* The controller's own ATT requests to us (we serve nothing), and its
 * notifications. */
void handle_att(Adapter &a, Peer &p, const std::uint8_t *d, std::size_t n)
{
    if (n == 0)
        return;
    const std::uint8_t op = d[0];
    if (op == 0x1b && n >= 3) /* Handle Value Notification */
    {
        const std::uint16_t h = le16(d + 1);
        ++p.notifications;
        if (h == 0x000e)
        {
            ++p.on_0e;
            if (!p.on_0a) /* only when 0x000A brings nothing */
                porpoise::ns2gc::bt_input(d + 3, int(n - 3));
        }
        else if (h == 0x000a)
        {
            ++p.on_0a;
            porpoise::ns2gc::bt_input(d + 3, int(n - 3), true);
        }
        else if (h == 0x001a && n >= 3 + 8)
        {
            if (p.cmd_wait && d[3] == p.cmd_wait && d[6] == p.sub_wait)
            {
                p.cmd_reply.assign(d + 3, d + n);
                p.cmd_got = true;
            }
        }
        else
        {
            static unsigned others = 0;
            if (others++ < 10)
                logf("bt: notification on %#06x: %s", h, hex(d + 3, int(std::min<std::size_t>(n - 3, 16))).c_str());
        }
        return;
    }
    if (op == 0x1d && n >= 3) /* Handle Value Indication: confirmed */
    {
        const std::uint8_t c = 0x1e;
        att_send(a, p, {c});
        return;
    }
    if (p.att_wait && (op == p.att_wait || op == 0x01))
    {
        p.att_reply.assign(d, d + n);
        p.att_got = true;
        p.att_error = op == 0x01;
        return;
    }
    if (op == 0x02 && n >= 3) /* Exchange MTU Request: ours */
    {
        p.mtu = std::max(23, std::min<int>(le16(d + 1), 185));
        att_send(a, p, {0x03, 185, 0});
        return;
    }
    if ((op & 0x01) == 0 && op != 0x52 && op != 0xd2) /* any other request: nothing here */
    {
        const bool discovery = op == 0x04 || op == 0x06 || op == 0x08 || op == 0x10;
        att_send(a, p, {0x01, op, 0x01, 0x00, std::uint8_t(discovery ? 0x0a : 0x06)});
    }
}

void handle_signaling(Adapter &a, Peer &p, const std::uint8_t *d, std::size_t n)
{
    if (n < 4)
        return;
    const std::uint8_t code = d[0], id = d[1];
    if (code == 0x12 && n >= 12) /* Connection Parameter Update Request: accepted */
    {
        const std::uint8_t ok[] = {0x13, id, 0x02, 0x00, 0x00, 0x00};
        send_l2cap(a, p, 0x0005, ok, sizeof ok);
        std::uint8_t params[14] = {std::uint8_t(p.handle), std::uint8_t(p.handle >> 8)};
        std::memcpy(params + 2, d + 4, 8);
        logf("bt: the controller asks for interval %d..%d, latency %d, timeout %d", le16(d + 4), le16(d + 6),
             le16(d + 8), le16(d + 10));
        std::vector<std::uint8_t> r;
        command(a, p, 0x2013, params, 14, r);
        return;
    }
    if (code & 1)
        return; /* a response */
    const std::uint8_t reject[] = {0x01, id, 0x02, 0x00, 0x00, 0x00};
    send_l2cap(a, p, 0x0005, reject, sizeof reject);
}

void handle_acl(Adapter &a, Peer &p, const std::uint8_t *d, std::size_t n)
{
    const std::uint16_t hf = le16(d);
    if (!p.connected || (hf & 0x0fff) != p.handle)
        return;
    const std::uint16_t pb = (hf >> 12) & 3;
    const std::uint8_t *data = d + 4;
    const std::size_t len = n - 4;
    if (pb != 1) /* a start */
    {
        p.l2cap.assign(data, data + len);
        p.l2cap_want = len >= 2 ? std::size_t(4 + le16(data)) : 0;
    }
    else
        p.l2cap.insert(p.l2cap.end(), data, data + len);
    if (p.l2cap.size() < 4 || p.l2cap.size() < p.l2cap_want)
        return;
    const std::uint16_t cid = le16(&p.l2cap[2]);
    const std::uint8_t *payload = p.l2cap.data() + 4;
    const std::size_t pl = p.l2cap_want - 4;
    std::vector<std::uint8_t> frame(payload, payload + pl);
    p.l2cap.clear();
    p.l2cap_want = 0;
    if (cid == 0x0004)
        handle_att(a, p, frame.data(), frame.size());
    else if (cid == 0x0005)
        handle_signaling(a, p, frame.data(), frame.size());
}

/* ---- finding the adapter ---------------------------------------------------------------------- */
std::vector<std::string> g_not_adapters; /* nodes looked at that aren't one */

bool find_adapter(Adapter &a, std::string &product, std::uint16_t &vid, std::uint16_t &pid)
{
    DIR *d = ps5_opendir("/dev");
    if (!d)
        return false;
    std::vector<std::string> names;
    while (const dirent *e = ps5_readdir(d))
        if (std::strncmp(e->d_name, "ugen", 4) == 0)
            names.emplace_back(e->d_name);
    ps5_closedir(d);
    std::sort(names.begin(), names.end());
    g_not_adapters.erase(std::remove_if(g_not_adapters.begin(), g_not_adapters.end(),
                                        [&](const std::string &n) {
                                            return std::find(names.begin(), names.end(), n) == names.end();
                                        }),
                         g_not_adapters.end());
    for (const std::string &n : names)
    {
        const std::size_t dot = n.find('.');
        if (n == kConsoleBluetooth || dot == std::string::npos || n.compare(dot, std::string::npos, ".1") == 0 ||
            std::find(g_not_adapters.begin(), g_not_adapters.end(), n) != g_not_adapters.end())
            continue;
        const int fd = ::open(("/dev/" + n).c_str(), O_RDWR | O_NONBLOCK);
        if (fd < 0)
            continue; /* busy (another of Porpoise's, or the console's): next time */
        DeviceInfo info{};
        bool hci = false, read = false;
        a.ep_events = a.ep_in = a.ep_out = 0;
        if (::ioctl(fd, kDeviceInfoPs5, &info) == 0 || ::ioctl(fd, kDeviceInfo, &info) == 0)
        {
            std::uint8_t cfg[512];
            GenDescriptor g{};
            g.ugd_data = cfg;
            g.ugd_maxlen = sizeof cfg;
            g.ugd_config_index = 0xff;
            if (::ioctl(fd, kGetFullDesc, &g) == 0)
            {
                read = true;
                int iface = -1;
                bool want = info.cls == 0xe0 && info.subclass == 1 && info.protocol == 1;
                for (std::size_t off = 0; off + 2 <= g.ugd_actlen;)
                {
                    const std::uint8_t bl = cfg[off], type = cfg[off + 1];
                    if (bl < 2 || off + bl > g.ugd_actlen)
                        break;
                    if (type == 4 && bl >= 9)
                    {
                        iface = -1;
                        /* the first interface's alternate setting 0: HCI commands, events, ACL */
                        if (cfg[off + 3] == 0 && cfg[off + 5] == 0xe0 && cfg[off + 6] == 1 && cfg[off + 7] == 1 &&
                            !a.ep_events)
                        {
                            iface = cfg[off + 2];
                            want = true;
                        }
                    }
                    else if (type == 5 && bl >= 7 && iface >= 0)
                    {
                        const std::uint8_t ep = cfg[off + 2], attr = cfg[off + 3] & 3;
                        const std::uint16_t size = std::uint16_t(le16(cfg + off + 4) & 0x7ff);
                        a.iface = iface;
                        if (attr == 3 && (ep & 0x80))
                            a.ep_events = ep, a.event_size = std::max<std::uint16_t>(size, 1);
                        else if (attr == 2 && (ep & 0x80))
                            a.ep_in = ep;
                        else if (attr == 2)
                            a.ep_out = ep, a.out_size = std::max<std::uint16_t>(size, 8);
                    }
                    off += bl;
                }
                hci = want && a.ep_events && a.ep_in && a.ep_out;
                if (hci)
                    logf("bt: %s: %04x:%04x \"%s\": HCI on interface %d, events %#x (%u), ACL %#x / %#x", n.c_str(),
                         info.vendorNo, info.productNo, info.product, a.iface, a.ep_events, a.event_size, a.ep_in,
                         a.ep_out);
            }
        }
        ::close(fd);
        if (!hci)
        {
            if (read)
                g_not_adapters.push_back(n);
            continue;
        }
        info.product[sizeof info.product - 1] = 0;
        product = info.product;
        vid = info.vendorNo;
        pid = info.productNo;
        a.node = n;
        return true;
    }
    return false;
}

bool open_adapter(Adapter &a)
{
    a.fd = ::open(("/dev/" + a.node).c_str(), O_RDWR);
    if (a.fd < 0)
        return false;
    std::memset(a.eps, 0, sizeof a.eps);
    for (int t = 0; t < kTransfers; ++t)
    {
        a.buf_ptr[t] = a.buf[t];
        a.buf_len[t] = kBuf;
        a.eps[t].ppBuffer = &a.buf_ptr[t];
        a.eps[t].pLength = &a.buf_len[t];
        a.eps[t].nFrames = 1;
    }
    FsInit init{};
    init.pEndpoints = a.eps;
    init.ep_index_max = kTransfers;
    if (::ioctl(a.fd, kFsInit, &init) != 0)
        return false;
    auto open_ep = [&](int t, std::uint8_t ep) {
        FsOpen o{};
        o.max_bufsize = kBuf;
        o.max_frames = 1;
        o.ep_index = std::uint8_t(t);
        o.ep_no = ep;
        if (::ioctl(a.fd, kFsOpen, &o) != 0)
        {
            logf("bt: %s: USB_FS_OPEN %#x errno %d", a.node.c_str(), ep, errno);
            return false;
        }
        a.opened[t] = true;
        return true;
    };
    return open_ep(kEvents, a.ep_events) && open_ep(kAclIn, a.ep_in) && open_ep(kAclOut, a.ep_out);
}

void close_adapter(Adapter &a, Peer &p)
{
    if (a.fd < 0)
        return;
    a.stopping = true;
    if (!a.dead)
    {
        for (int t = 1; t < kTransfers; ++t)
            if (a.pending[t])
            {
                FsIndex s{std::uint8_t(t)};
                ::ioctl(a.fd, kFsStop, &s);
            }
        const std::uint64_t until = now_ms() + 500;
        auto running = [&] {
            for (int t = 1; t < kTransfers; ++t)
                if (a.pending[t])
                    return true;
            return false;
        };
        while (running() && !a.dead && now_ms() < until)
        {
            pump(a, p);
            if (running())
                ::usleep(1000);
        }
        if (running() && !a.dead)
        {
            logf("bt: %s: its transfers didn't stop: the node is left open", a.node.c_str());
            return;
        }
        FsIndex u{0};
        ::ioctl(a.fd, kFsUninit, &u);
    }
    ::close(a.fd);
    a.fd = -1;
}

bool read_file(const std::string &path, std::vector<std::uint8_t> &out)
{
    std::FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    std::uint8_t b[4096];
    std::size_t n;
    out.clear();
    while ((n = std::fread(b, 1, sizeof b, f)) > 0)
        out.insert(out.end(), b, b + n);
    std::fclose(f);
    return true;
}

/* ---- bringing the adapter up ------------------------------------------------------------------ */
bool bring_up(Adapter &a, Peer &p, std::string &step)
{
    std::vector<std::uint8_t> r;
    auto version = [&](int &hv, int &hr, int &lv, int &ls, int &mf) {
        if (!command(a, p, 0x1001, nullptr, 0, r) || r.size() < 9)
            return false;
        hv = r[1];
        hr = le16(&r[2]);
        lv = r[4];
        mf = le16(&r[5]);
        ls = le16(&r[7]);
        return true;
    };
    int hv = 0, hr = 0, lv = 0, ls = 0, mf = 0;
    if (!version(hv, hr, lv, ls, mf))
    {
        command(a, p, 0x0c03, nullptr, 0, r, 3000);
        if (!version(hv, hr, lv, ls, mf))
        {
            step = "Read Local Version";
            return false;
        }
    }
    const std::string chip = chip_name(mf, ls, hr);
    logf("bt: %s: hci_ver %02x hci_rev %04x lmp_ver %02x lmp_subver %04x manufacturer %d: %s", a.node.c_str(), hv,
         hr, lv, ls, mf, chip.c_str());
    publish([&](Status &s) {
        s.have_version = true;
        s.hci_ver = hv;
        s.hci_rev = hr;
        s.lmp_ver = lv;
        s.lmp_subver = ls;
        s.manufacturer = mf;
        s.chip = chip;
    });
    /* A Realtek RTL8761 runs from its ROM until it's given its patch. */
    if (mf == 93 && ls == 0x8761)
    {
        int rom = -1;
        if (command(a, p, 0xfc6d, nullptr, 0, r) && r.size() >= 2)
            rom = r[1];
        logf("bt: %s: ROM version %d", a.node.c_str(), rom);
        publish([&](Status &s) { s.rom_version = rom; });
        const std::string dir = PORPOISE_APP "/assets/bluetooth/";
        const char *names[2][2] = {{"rtl8761bu_fw.bin", "rtl8761bu_config.bin"},
                                   {"rtl8761cu_fw.bin", "rtl8761cu_config.bin"}};
        const int first = hr == 0x000e ? 1 : 0;
        std::vector<std::uint8_t> patch;
        std::string used, why_all;
        for (int k = 0; k < 2 && patch.empty() && rom >= 0; ++k)
        {
            const int i = (first + k) % 2;
            std::vector<std::uint8_t> fw, cfg;
            if (!read_file(dir + names[i][0], fw))
            {
                why_all += std::string(names[i][0]) + ": not found; ";
                continue;
            }
            read_file(dir + names[i][1], cfg);
            std::string why;
            patch = realtek_patch(fw, cfg, rom, why);
            if (patch.empty())
                why_all += std::string(names[i][0]) + ": " + why + "; ";
            else
                used = names[i][0];
        }
        if (patch.empty())
        {
            logf("bt: %s: no firmware for it: %s", a.node.c_str(), why_all.c_str());
            publish([&](Status &s) { s.firmware = "none fits: " + why_all; });
        }
        else
        {
            logf("bt: %s: uploading %s (%zu bytes with its config)", a.node.c_str(), used.c_str(), patch.size());
            const int frags = int(patch.size() / 252) + 1;
            bool ok = true;
            int index = 0;
            for (int i = 0; i < frags && ok; ++i)
            {
                std::uint8_t cmd[253];
                int len = 252;
                cmd[0] = std::uint8_t(index++);
                if (index == 0x80)
                    index = 1;
                if (i == frags - 1)
                {
                    cmd[0] |= 0x80;
                    len = int(patch.size() % 252);
                }
                std::memcpy(cmd + 1, patch.data() + std::size_t(i) * 252, std::size_t(len));
                ok = command(a, p, 0xfc20, cmd, len + 1, r, 3000);
            }
            int hv2 = 0, hr2 = 0, lv2 = 0, ls2 = 0, mf2 = 0;
            if (ok)
            {
                ::usleep(50000);
                ok = version(hv2, hr2, lv2, ls2, mf2);
            }
            char b[160];
            std::snprintf(b, sizeof b, ok ? "%s uploaded (%zu bytes), now %04x%04x" : "%s: the upload failed",
                          used.c_str(), patch.size(), hr2, ls2);
            logf("bt: %s: %s", a.node.c_str(), b);
            publish([&](Status &s) {
                s.firmware = b;
                s.firmware_ok = ok;
                s.fw_lmp_subver = ls2;
            });
        }
    }
    else if (mf == 93)
        publish([](Status &s) {
            s.firmware = "already running";
            s.firmware_ok = true;
        });
    else
        publish([](Status &s) { s.firmware = "not needed (not a Realtek)"; });

    command(a, p, 0x0c03, nullptr, 0, r, 3000); /* reset */
    if (command(a, p, 0x1009, nullptr, 0, r) && r.size() >= 7)
    {
        std::uint8_t addr[6];
        for (int i = 0; i < 6; ++i)
            addr[i] = r[6 - i];
        logf("bt: %s: address %s", a.node.c_str(), address_text(addr).c_str());
        publish([&](Status &s) {
            std::memcpy(s.bd_addr, addr, 6);
            std::memcpy(a.bd, addr, 6);
            s.have_addr = true;
        });
    }
    bool le = false;
    if (command(a, p, 0x1003, nullptr, 0, r) && r.size() >= 9)
        le = (r[1 + 4] & 0x40) != 0;
    publish([&](Status &s) { s.le = le; });
    if (command(a, p, 0x2002, nullptr, 0, r) && r.size() >= 4 && le16(&r[1]) > 0)
    {
        a.acl_len = le16(&r[1]);
        a.acl_num = std::max(1, int(r[3]));
    }
    else if (command(a, p, 0x1005, nullptr, 0, r) && r.size() >= 8)
    {
        a.acl_len = le16(&r[1]);
        a.acl_num = std::max(1, int(le16(&r[4])));
    }
    a.acl_len = std::min(a.acl_len, int(kBuf) - 4);
    logf("bt: %s: LE ACL buffers: %d of %d bytes", a.node.c_str(), a.acl_num, a.acl_len);
    const std::uint8_t mask[8] = {0xff, 0xff, 0xfb, 0xff, 0x07, 0xf8, 0xbf, 0x3d};
    command(a, p, 0x0c01, mask, 8, r);
    const std::uint8_t le_mask[8] = {0x1f, 0, 0, 0, 0, 0, 0, 0};
    command(a, p, 0x2001, le_mask, 8, r);
    /* passive, 30 ms of every 60: a controller advertises every few ms when paired */
    const std::uint8_t params[7] = {0x00, 0x60, 0x00, 0x30, 0x00, 0x00, 0x00};
    if (!command(a, p, 0x200b, params, 7, r))
    {
        step = "LE Set Scan Parameters";
        return false;
    }
    return true;
}

bool scan(Adapter &a, Peer &p, bool on)
{
    std::vector<std::uint8_t> r;
    const std::uint8_t v[2] = {std::uint8_t(on ? 1 : 0), 0x00};
    return command(a, p, 0x200c, v, 2, r);
}

/* ---- the controller --------------------------------------------------------------------------- */
bool connect(Adapter &a, Peer &p, const Found &f)
{
    std::memcpy(p.addr, f.addr, 6);
    p.random = f.random;
    p.connecting = true;
    p.connected = false;
    p.ready = false;
    p.notifications = p.on_0e = p.on_0a = 0;
    publish([&](Status &s) {
        s.link = Link::Connecting;
        std::memcpy(s.peer, f.addr, 6);
        s.link_step = "connecting";
        s.commands_ok = s.commands_sent = 0;
        s.notifications = 0;
    });
    std::uint8_t c[25] = {};
    c[0] = 0x60; /* scan interval */
    c[2] = 0x30; /* window */
    c[4] = 0x00; /* the peer's address, not the filter list */
    c[5] = f.random ? 1 : 0;
    for (int i = 0; i < 6; ++i)
        c[6 + i] = f.addr[5 - i];
    c[12] = 0x00; /* our public address */
    c[13] = 6;    /* interval 7.5 .. 15 ms */
    c[15] = 12;
    c[17] = 0;    /* latency */
    c[19] = 200;  /* supervision timeout 2 s */
    std::vector<std::uint8_t> r;
    logf("bt: %s: connecting", address_text(f.addr).c_str());
    if (!command(a, p, 0x200d, c, 25, r))
    {
        p.connecting = false;
        return false;
    }
    const std::uint64_t until = now_ms() + 5000;
    while (p.connecting && !a.dead && g_run && now_ms() < until)
    {
        pump(a, p);
        ::usleep(1000);
    }
    if (p.connecting)
    {
        logf("bt: %s: no connection in 5 s", address_text(f.addr).c_str());
        command(a, p, 0x200e, nullptr, 0, r); /* LE Create Connection Cancel */
        const std::uint64_t wait = now_ms() + 1000;
        while (p.connecting && !a.dead && now_ms() < wait)
        {
            pump(a, p);
            ::usleep(1000);
        }
        p.connecting = false;
    }
    return p.connected;
}

void step(const char *what)
{
    publish([&](Status &s) { s.link_step = what; });
}

/* Its set-up as Nitrama/switch2-controllers-linux does it (tested there with
 * this controller over a raw ATT channel, as here): command responses on,
 * its calibration read, player 1's light, a short rumble, its features on
 * (buttons, sticks, motion: 0x07), then the input report's notifications
 * (handle 0x000A, the common report 0x05). The console's own sequence
 * (ndeadly's notes) brought a connection but no input. */
bool set_up(Adapter &a, Peer &p, porpoise::ns2gc::Calibration &cal, bool &have_cal, bool pair)
{
    publish([](Status &s) { s.link = Link::SettingUp; });
    std::vector<std::uint8_t> r;
    step("MTU");
    if (att_request(a, p, {0x02, 247, 0}, 0x03, r) && r.size() >= 3)
        p.mtu = std::max(23, std::min<int>(le16(&r[1]), 247));
    logf("bt: ATT MTU %d", p.mtu);
    publish([&](Status &s) { s.att_mtu = p.mtu; });
    step("command responses");
    if (!write_request(a, p, 0x001b, {0x01, 0x00}))
        return false;
    int ok = 0, sent = 0;
    auto cmd = [&](std::uint8_t c, std::uint8_t s, const std::vector<std::uint8_t> &d) {
        ++sent;
        const bool got = gc_command(a, p, c, s, d, r);
        if (got)
            ++ok;
        publish([&](Status &st) {
            st.commands_ok = ok;
            st.commands_sent = sent;
        });
        return got;
    };
    auto flash = [&](std::uint32_t address, std::uint8_t len, std::uint8_t *out) {
        const std::vector<std::uint8_t> d = {len, 0x7e, 0, 0, std::uint8_t(address), std::uint8_t(address >> 8),
                                             std::uint8_t(address >> 16), std::uint8_t(address >> 24)};
        if (!cmd(0x02, 0x04, d) || r.size() < std::size_t(16 + len))
            return false;
        std::memcpy(out, r.data() + 16, len);
        return true;
    };
    std::uint8_t b[0x50];
    step("its set-up");
    flash(0x13000, 0x40, b);
    bool sticks = false;
    if (flash(0x13080, 0x40, b))
        sticks = porpoise::ns2gc::parse_stick_calibration(b + 0x28, cal, 0);
    if (flash(0x130c0, 0x40, b))
        sticks = porpoise::ns2gc::parse_stick_calibration(b + 0x28, cal, 2) && sticks;
    if (flash(0x1fc040, 0x40, b) && b[0] == 0xb2 && b[1] == 0xa1)
        porpoise::ns2gc::parse_stick_calibration(b + 2, cal, 0);
    if (flash(0x13140, 0x02, b) && b[0] > 0 && b[0] < 0x80 && b[1] < 0x80)
    {
        cal.trigger_zero[0] = b[0];
        cal.trigger_zero[1] = b[1];
    }
    cmd(0x09, 0x07, {0x01, 0, 0, 0});       /* player 1's light */
    cmd(0x0a, 0x02, {0x03, 0, 0, 0});       /* a short, soft rumble: connected */
    cmd(0x0c, 0x02, {0x07, 0, 0, 0});       /* features: buttons, sticks, motion */
    cmd(0x0c, 0x04, {0x07, 0, 0, 0});
    have_cal = sticks;
    logf("bt: set-up: %d of %d commands answered; sticks %s: centre %d %d / %d %d; triggers rest %d %d", ok, sent,
         sticks ? "calibrated" : "NOT calibrated", cal.centre[0], cal.centre[1], cal.centre[2], cal.centre[3],
         cal.trigger_zero[0], cal.trigger_zero[1]);
    if (!p.connected)
        return false;
    step("input");
    if (!write_request(a, p, 0x000b, {0x01, 0x00}))
        return false;
    if (pair)
    {
        /* Paired to this adapter (Nitrama's bond(): its address twice, the
         * protocol's two key halves, done): from now on a button press makes
         * it look for us, no pair button needed. */
        step("pairing");
        std::vector<std::uint8_t> mac = {0x00, 0x02};
        for (int k = 0; k < 2; ++k)
            for (int i = 5; i >= 0; --i)
                mac.push_back(a.bd[i]);
        const bool paired =
            cmd(0x15, 0x01, mac) &&
            cmd(0x15, 0x04, {0x00, 0xea, 0xbd, 0x47, 0x13, 0x89, 0x35, 0x42, 0xc6, 0x79, 0xee, 0x07, 0xf2, 0x53, 0x2c,
                             0x6c, 0x31}) &&
            cmd(0x15, 0x02, {0x00, 0x40, 0xb0, 0x8a, 0x5f, 0xcd, 0x1f, 0x9b, 0x41, 0x12, 0x5c, 0xac, 0xc6, 0x3f, 0x38,
                             0xa0, 0x73}) &&
            cmd(0x15, 0x03, {0x00});
        logf("bt: paired to %s: %s", address_text(a.bd).c_str(), paired ? "done" : "not answered");
    }
    return p.connected;
}

/* The game's (or the test's) rumble. The controller has no HD rumble motor
 * by Bluetooth (writing the motor's characteristic switches it off, says
 * Nitrama): it plays its own vibration presets, the soft one (3) again
 * every 220 ms while the rumble is on. */
void service_rumble(Adapter &a, Peer &p)
{
    const std::uint64_t t = now_ms();
    const bool want = porpoise::ns2gc::rumble_wanted();
    if (want && (!p.rumble_on || t >= p.rumble_next))
    {
        const std::uint8_t c[12] = {0x0a, 0x91, 0x01, 0x02, 0x00, 0x04, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00};
        if (!write_command(a, p, 0x0014, c, sizeof c))
            logf("bt: rumble not sent");
        p.rumble_next = t + 220;
    }
    if (want != p.rumble_on)
    {
        static unsigned logged = 0;
        if (logged++ < 20)
            logf("bt: rumble %s", want ? "on" : "off");
    }
    p.rumble_on = want;
}

/* ---- the thread ------------------------------------------------------------------------------- */
void run()
{
    std::uint64_t next_look = 0;
    while (g_run)
    {
        if (now_ms() < next_look)
        {
            ::usleep(50000);
            continue;
        }
        Adapter a;
        Peer p;
        std::string product;
        std::uint16_t vid = 0, pid = 0;
        if (!find_adapter(a, product, vid, pid))
        {
            if (status().phase != Phase::NotFound)
                publish([](Status &s) { s.phase = Phase::NotFound; });
            next_look = now_ms() + 3000;
            continue;
        }
        porpoise::usbhid::set_node_in_use(a.node, true);
        publish([&](Status &s) {
            const std::uint32_t gen = s.generation + 1;
            std::vector<Found> keep = std::move(s.found);
            s = Status{};
            s.generation = gen;
            s.found = std::move(keep);
            s.phase = Phase::Starting;
            s.node = a.node;
            s.product = product;
            s.vid = vid;
            s.pid = pid;
        });
        std::string failed;
        if (!open_adapter(a))
            failed = "opening it";
        else
        {
            int n = a.iface;
            const int driver = ::ioctl(a.fd, kIfaceDriverActive, &n) == 0 ? 1 : 0;
            if (driver == 1)
            {
                n = a.iface;
                logf("bt: %s: the console's driver had interface %d: detached %s", a.node.c_str(), a.iface,
                     ::ioctl(a.fd, kIfaceDriverDetach, &n) == 0 ? "ok" : "failed");
            }
            publish([&](Status &s) { s.kernel_driver = driver; });
            arm(a);
            if (!bring_up(a, p, failed) || !scan(a, p, true))
                failed = failed.empty() ? "LE Set Scan Enable" : failed;
        }
        if (!failed.empty())
        {
            logf("bt: %s: %s failed", a.node.c_str(), failed.c_str());
            publish([&](Status &s) {
                s.phase = Phase::Failed;
                s.step = failed;
                s.error = a.dead ? ENXIO : errno;
            });
            close_adapter(a, p);
            porpoise::usbhid::set_node_in_use(a.node, false);
            next_look = now_ms() + 10000;
            continue;
        }
        publish([](Status &s) {
            s.phase = Phase::Scanning;
            s.link = Link::None;
        });
        logf("bt: %s: ready, listening for a Switch 2 GameCube controller", a.node.c_str());
        g_have_candidate = false;
        std::uint64_t retry_after = 0;
        while (g_run && !a.dead)
        {
            if (g_clear.exchange(false))
                publish([](Status &s) { s.found.clear(); });
            pump(a, p);
            if (g_have_candidate && !p.connected && now_ms() >= retry_after)
            {
                const Found f = g_candidate;
                scan(a, p, false);
                bool up = connect(a, p, f);
                porpoise::ns2gc::Calibration cal;
                bool have_cal = false;
                if (up)
                {
                    /* advertising for any host (its pair button): pair it to us */
                    bool any = true;
                    for (std::size_t i = 10; i < 16 && i < f.maker.size(); ++i)
                        any = any && f.maker[i] == 0;
                    up = set_up(a, p, cal, have_cal, any);
                }
                if (up)
                {
                    p.ready = true;
                    porpoise::ns2gc::bt_link(true, address_text(f.addr), have_cal ? &cal : nullptr);
                    publish([](Status &s) {
                        s.link = Link::Ready;
                        s.link_step.clear();
                    });
                    logf("bt: %s: ready", address_text(f.addr).c_str());
                    std::uint64_t last_status = 0, last_log = now_ms();
                    const std::uint64_t since = now_ms();
                    bool fallback = false;
                    std::uint32_t logged_n = 0, logged_0e = 0, logged_0a = 0;
                    while (p.connected && g_run && !a.dead)
                    {
                        pump(a, p);
                        service_rumble(a, p);
                        /* nothing on 0x000A after a second and a half: the GameCube report too */
                        if (!fallback && !p.on_0a && now_ms() - since > 1500)
                        {
                            fallback = true;
                            logf("bt: no input on 0x000a: 0x000e's notifications on too");
                            write_request(a, p, 0x000f, {0x01, 0x00});
                        }
                        if (now_ms() - last_log >= 5000)
                        {
                            logf("bt: 5 s: %u notifications (0x000e %u, 0x000a %u, other %u)",
                                 p.notifications - logged_n, p.on_0e - logged_0e, p.on_0a - logged_0a,
                                 (p.notifications - logged_n) - (p.on_0e - logged_0e) - (p.on_0a - logged_0a));
                            logged_n = p.notifications;
                            logged_0e = p.on_0e;
                            logged_0a = p.on_0a;
                            last_log = now_ms();
                        }
                        if (now_ms() - last_status >= 500)
                        {
                            last_status = now_ms();
                            const std::uint32_t nn = p.notifications;
                            publish([&](Status &s) { s.notifications = nn; });
                        }
                        ::usleep(1000);
                    }
                    porpoise::ns2gc::bt_link(false, address_text(f.addr), nullptr);
                }
                if (p.connected && (!g_run || !up))
                {
                    /* set-up failed, or Porpoise is closing: let it go */
                    std::vector<std::uint8_t> r;
                    const std::uint8_t d[3] = {std::uint8_t(p.handle), std::uint8_t(p.handle >> 8), 0x13};
                    command(a, p, 0x0406, d, 3, r);
                    const std::uint64_t wait = now_ms() + 1000;
                    while (p.connected && !a.dead && now_ms() < wait)
                    {
                        pump(a, p);
                        ::usleep(1000);
                    }
                    p.connected = false;
                }
                const int reason = p.disconnect_reason;
                publish([&](Status &s) {
                    s.link = Link::None;
                    s.last_disconnect = reason;
                    if (!up)
                        s.link_step = "the last connection failed (" + s.link_step + ")";
                });
                g_have_candidate = false;
                retry_after = now_ms() + 1500;
                if (g_run && !a.dead)
                    scan(a, p, true);
                continue;
            }
            ::usleep(2000);
        }
        close_adapter(a, p);
        porpoise::usbhid::set_node_in_use(a.node, false);
        if (a.dead)
        {
            g_not_adapters.clear();
            publish([](Status &s) {
                s.phase = Phase::NotFound;
                s.link = Link::None;
            });
        }
        next_look = now_ms() + 2000;
    }
}

void *thread_main(void *)
{
    run();
    g_finished = true;
    return nullptr;
}
} // namespace

Status status()
{
    std::lock_guard<std::mutex> g(g_lock);
    return g_status;
}

void start()
{
    if (g_started.exchange(true))
        return;
    g_run = true;
    g_finished = false;
    publish([](Status &s) { s.phase = Phase::Searching; });
    pthread_t t{};
    if (create_title_thread(&t, &thread_main, nullptr) != 0)
    {
        g_finished = true;
        return;
    }
    pthread_detach(t);
}

void clear_found() { g_clear = true; }

bool close_for_exit(unsigned max_ms)
{
    if (!g_started)
        return true;
    g_run = false;
    const std::uint64_t until = now_ms() + max_ms;
    while (!g_finished && now_ms() < until)
        ::usleep(2000);
    return g_finished;
}
} // namespace porpoise::bt
#endif
