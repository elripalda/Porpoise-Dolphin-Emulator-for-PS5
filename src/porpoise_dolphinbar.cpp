/* Porpoise - the Mayflash DolphinBar's monitor for Porpoise's own menus, and
 * the Wii Remote's reports read for its controller test
 * (porpoise_dolphinbar.hpp says what and why).
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The protocol is the Wii Remote's own as WiiBrew documents it (wiibrew.org,
 * "Wiimote" and "Wiimote/Extension Controllers"); Dolphin speaks it to the
 * same Remotes in a game. One manager thread finds the DolphinBar and opens
 * its interfaces; one thread per slot then owns that interface: it asks
 * whether a Remote is there, sets it up and reads it, the way Dolphin's own
 * per-Remote threads do. */
#include "porpoise_dolphinbar.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <functional>
#include <memory>
#include <pthread.h>
#include <thread>

#include "porpoise_usbhid.hpp"
#if !defined(PORPOISE_DESKTOP)
#include "porpoise_hidapi.h"
#endif
#if !defined(PORPOISE_DESKTOP) && !defined(PORPOISE_USBHID_HOST_TEST)
#include "porpoise_paths.hpp"
#include "title_threads.hpp"
#endif

namespace porpoise::dolphinbar
{
/* ---- the log ------------------------------------------------------------------------------- */
namespace
{
std::mutex g_log_lock;
bool g_log_started = false;
#if defined(PORPOISE_USBHID_HOST_TEST) || defined(PORPOISE_DESKTOP)
std::string g_log_path;
#else
std::string g_log_path = PORPOISE_DATA "/dolphinbar.log";
#endif

double seconds_now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

void set_log_path(const std::string &path)
{
    std::lock_guard<std::mutex> lock(g_log_lock);
    g_log_path = path;
    g_log_started = false;
}

void log(const char *fmt, ...)
{
    char line[600];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    std::lock_guard<std::mutex> lock(g_log_lock);
    if (g_log_path.empty())
        return;
    std::FILE *f = std::fopen(g_log_path.c_str(), g_log_started ? "a" : "w");
    if (!f)
        return;
    g_log_started = true;
    const long long ms = (long long)(seconds_now() * 1000.0);
    std::fprintf(f, "%6lld.%03lld %s\n", ms / 1000, ms % 1000, line);
    std::fclose(f);
}

void start_stuck_watch();

void route_usbhid_log()
{
    porpoise::usbhid::set_log([](const char *line) { log("%s", line); });
    start_stuck_watch();
}

const char *extension_name(Extension e)
{
    switch (e)
    {
    case Extension::None: return "None";
    case Extension::Unknown: return "Not recognized";
    case Extension::Nunchuk: return "Nunchuk";
    case Extension::Classic: return "Classic Controller";
    case Extension::MotionPlus: return "Wii MotionPlus";
    case Extension::MotionPlusNunchuk: return "Wii MotionPlus + Nunchuk";
    case Extension::MotionPlusClassic: return "Wii MotionPlus + Classic Controller";
    case Extension::Other: return "Other attachment";
    }
    return "?";
}

/* ---- the protocol ------------------------------------------------------------------------- */
Extension identify(const std::uint8_t id[6])
{
    const bool all_ff = std::all_of(id, id + 6, [](std::uint8_t b) { return b == 0xff; });
    const bool all_00 = std::all_of(id, id + 6, [](std::uint8_t b) { return b == 0x00; });
    if (all_ff || all_00)
        return Extension::Unknown; /* not seated, or not answering */
    if (id[2] == 0xa4 && id[3] == 0x20)
    {
        if (id[4] == 0x00 && id[5] == 0x00)
            return Extension::Nunchuk;
        if (id[4] == 0x01 && id[5] == 0x01)
            return Extension::Classic;
        if (id[4] == 0x04 && id[5] == 0x05)
            return Extension::MotionPlus;
        if (id[4] == 0x05 && id[5] == 0x05)
            return Extension::MotionPlusNunchuk;
        if (id[4] == 0x07 && id[5] == 0x05)
            return Extension::MotionPlusClassic;
    }
    return Extension::Other;
}

bool parse_calibration(const std::uint8_t d[8], int zero[3], int one[3])
{
    int z[3], o[3];
    for (int i = 0; i < 3; ++i)
    {
        const int shift = 4 - 2 * i; /* the low bits: x in 5-4, y in 3-2, z in 1-0 */
        z[i] = (d[i] << 2) | ((d[3] >> shift) & 3);
        o[i] = (d[4 + i] << 2) | ((d[7] >> shift) & 3);
        if (o[i] - z[i] < 20 || o[i] - z[i] > 400) /* not a real calibration (blank or garbled) */
            return false;
    }
    for (int i = 0; i < 3; ++i)
    {
        zero[i] = z[i];
        one[i] = o[i];
    }
    return true;
}

namespace
{
float to_g(int raw, int zero, int one) { return one != zero ? float(raw - zero) / float(one - zero) : 0.0f; }

void decode_nunchuk(Remote &r, const std::uint8_t *d, bool passthrough)
{
    r.nc_stick[0] = d[0];
    r.nc_stick[1] = d[1];
    if (passthrough)
    {
        r.nc_accel_raw[0] = (d[2] << 2) | (((d[5] >> 4) & 1) << 1);
        r.nc_accel_raw[1] = (d[3] << 2) | (((d[5] >> 5) & 1) << 1);
        r.nc_accel_raw[2] = ((d[4] >> 1) << 3) | (((d[5] >> 6) & 3) << 1);
        r.nc_c = !(d[5] & 0x08);
        r.nc_z = !(d[5] & 0x04);
    }
    else
    {
        r.nc_accel_raw[0] = (d[2] << 2) | ((d[5] >> 2) & 3);
        r.nc_accel_raw[1] = (d[3] << 2) | ((d[5] >> 4) & 3);
        r.nc_accel_raw[2] = (d[4] << 2) | ((d[5] >> 6) & 3);
        r.nc_z = !(d[5] & 0x01);
        r.nc_c = !(d[5] & 0x02);
    }
    for (int i = 0; i < 3; ++i)
        r.nc_accel_g[i] = to_g(r.nc_accel_raw[i], r.nc_zero[i], r.nc_one[i]);
}

void decode_classic(Remote &r, const std::uint8_t *in, bool passthrough)
{
    std::uint8_t d[6];
    std::memcpy(d, in, 6);
    if (passthrough)
    {
        /* Through a MotionPlus: Up and Left move to bit 0 of bytes 0 and 1,
         * and the left stick loses its lowest bit. */
        d[5] = std::uint8_t((d[5] & 0xfc) | ((in[1] & 1) << 1) | (in[0] & 1));
        d[4] |= 1;
        d[0] &= 0xfe;
        d[1] &= 0xfe;
    }
    r.cc_left[0] = d[0] & 0x3f;
    r.cc_left[1] = d[1] & 0x3f;
    r.cc_right[0] = (((d[0] >> 6) & 3) << 3) | (((d[1] >> 6) & 3) << 1) | ((d[2] >> 7) & 1);
    r.cc_right[1] = d[2] & 0x1f;
    r.cc_lt = (((d[2] >> 5) & 3) << 3) | ((d[3] >> 5) & 7);
    r.cc_rt = d[3] & 0x1f;
    r.cc_buttons = std::uint16_t(((~d[4]) & 0xfe) | (((~d[5]) & 0xff) << 8));
}

void decode_motion_plus(Remote &r, const std::uint8_t *d)
{
    r.mp_raw[0] = d[0] | ((d[3] >> 2) << 8);
    r.mp_raw[1] = d[1] | ((d[4] >> 2) << 8);
    r.mp_raw[2] = d[2] | ((d[5] >> 2) << 8);
    r.mp_slow[0] = d[3] & 0x02;
    r.mp_slow[1] = d[4] & 0x02;
    r.mp_slow[2] = d[3] & 0x01;
    r.mp_ext_connected = d[4] & 0x01; /* something plugged into the MotionPlus */
    for (int i = 0; i < 3; ++i) /* about 20 counts per degree a second slow, 4.4 fast (WiiBrew) */
        r.mp_dps[i] = float(r.mp_raw[i] - 8192) / (r.mp_slow[i] ? 20.0f : 20.0f / 4.545f);
    ++r.mp_reports;
}
} // namespace

void decode_extension(Remote &r, const std::uint8_t *d)
{
    switch (r.ext)
    {
    case Extension::Nunchuk:
        decode_nunchuk(r, d, false);
        ++r.ext_reports;
        break;
    case Extension::Classic:
        decode_classic(r, d, false);
        ++r.ext_reports;
        break;
    case Extension::MotionPlus:
        if (d[5] & 0x02)
            decode_motion_plus(r, d);
        break;
    case Extension::MotionPlusNunchuk:
    case Extension::MotionPlusClassic:
        if (d[5] & 0x02)
            decode_motion_plus(r, d);
        else
        {
            if (r.ext == Extension::MotionPlusNunchuk)
                decode_nunchuk(r, d, true);
            else
                decode_classic(r, d, true);
            ++r.ext_reports;
        }
        break;
    default: break;
    }
}

bool parse_stick_calibration(const std::uint8_t d[16], Remote &r)
{
    /* WiiBrew "Nunchuck": bytes 8..13 are X max, X min, X centre, Y max,
     * Y min, Y centre. A block of 0x00 or 0xff (an unprogrammed copy) is
     * left alone. */
    int mx[2] = {d[8], d[11]}, mn[2] = {d[9], d[12]}, md[2] = {d[10], d[13]};
    for (int a = 0; a < 2; ++a)
        if (!(mn[a] < md[a] && md[a] < mx[a] && mx[a] - mn[a] >= 64))
            return false;
    for (int a = 0; a < 2; ++a)
    {
        r.nc_stick_min[a] = mn[a];
        r.nc_stick_mid[a] = md[a];
        r.nc_stick_max[a] = mx[a];
    }
    return true;
}

float nunchuk_stick(const Remote &r, int axis)
{
    const int v = r.nc_stick[axis], mid = r.nc_stick_mid[axis];
    const int span = v >= mid ? r.nc_stick_max[axis] - mid : mid - r.nc_stick_min[axis];
    float f = span > 0 ? float(v - mid) / float(span) : 0.0f;
    f = std::clamp(f, -1.0f, 1.0f);
    constexpr float kDead = 0.12f;
    if (std::abs(f) < kDead)
        return 0.0f;
    return (f - std::copysign(kDead, f)) / (1.0f - kDead);
}

void decode_report(Remote &r, const std::uint8_t *b, int n)
{
    if (n < 3)
        return;
    const std::uint8_t id = b[0];
    ++r.reports;
    if ((id >= 0x20 && id <= 0x22) || (id >= 0x30 && id <= 0x37))
        r.buttons = std::uint16_t(((b[1] << 8) | b[2]) & 0x1f9f);
    if (id == 0x20 && n >= 7)
    {
        r.have_status = true;
        r.battery_low = b[3] & 0x01;
        r.leds = (b[3] >> 4) & 0x0f;
        r.battery_percent = std::min(100, int(b[6]) * 100 / 0xc8);
        return;
    }
    const bool accel = id == 0x31 || id == 0x33 || id == 0x35 || id == 0x37;
    if (accel && n >= 6)
    {
        r.accel_raw[0] = (b[3] << 2) | ((b[1] >> 5) & 3);
        r.accel_raw[1] = (b[4] << 2) | (((b[2] >> 5) & 1) << 1);
        r.accel_raw[2] = (b[5] << 2) | (((b[2] >> 6) & 1) << 1);
        for (int i = 0; i < 3; ++i)
            r.accel_g[i] = to_g(r.accel_raw[i], r.cal_zero[i], r.cal_one[i]);
        const float gx = r.accel_g[0], gy = r.accel_g[1], gz = r.accel_g[2];
        r.pitch = std::atan2(gy, std::sqrt(gx * gx + gz * gz)) * 57.29578f;
        r.roll = std::atan2(-gx, gz) * 57.29578f;
    }
    if (id == 0x33 && n >= 18)
        for (int i = 0; i < 4; ++i)
        {
            const std::uint8_t *p = b + 6 + 3 * i;
            const int x = p[0] | (((p[2] >> 4) & 3) << 8), y = p[1] | (((p[2] >> 6) & 3) << 8);
            r.ir[i].visible = !(p[0] == 0xff && p[1] == 0xff && p[2] == 0xff) && x < 1023 && y < 1023;
            r.ir[i].x = x;
            r.ir[i].y = y;
        }
    if (id == 0x37 && n >= 16)
        for (int pair = 0; pair < 2; ++pair)
        {
            const std::uint8_t *p = b + 6 + 5 * pair;
            const int x1 = p[0] | (((p[2] >> 4) & 3) << 8), y1 = p[1] | (((p[2] >> 6) & 3) << 8);
            const int x2 = p[3] | ((p[2] & 3) << 8), y2 = p[4] | (((p[2] >> 2) & 3) << 8);
            IrDot &a = r.ir[pair * 2], &c = r.ir[pair * 2 + 1];
            a.visible = x1 < 1023 && y1 < 1023;
            a.x = x1;
            a.y = y1;
            c.visible = x2 < 1023 && y2 < 1023;
            c.x = x2;
            c.y = y2;
        }
    if (id == 0x32 && n >= 11)
        decode_extension(r, b + 3);
    if (id == 0x35 && n >= 12)
        decode_extension(r, b + 6);
    if (id == 0x37 && n >= 22)
        decode_extension(r, b + 16);
    if (id >= 0x30 && id <= 0x37)
        r.reporting = true;
}

/* ---- the speaker's format ------------------------------------------------------------------- */
namespace
{
/* Yamaha ADPCM as the Remote decodes it (Dolphin: Core/HW/WiimoteEmu/Speaker.cpp). */
constexpr int kDiff[16] = {1, 3, 5, 7, 9, 11, 13, 15, -1, -3, -5, -7, -9, -11, -13, -15};
constexpr int kScale[16] = {230, 230, 230, 230, 307, 409, 512, 614, 230, 230, 230, 230, 307, 409, 512, 614};

std::uint8_t encode_nibble(Adpcm &s, int sample)
{
    /* Every nibble tried against the decoder itself: the one landing nearest. */
    int best = 0, best_err = 1 << 30;
    for (int nib = 0; nib < 16; ++nib)
    {
        const int p = std::clamp(s.predictor + (s.step * kDiff[nib]) / 8, -32768, 32767);
        const int err = std::abs(p - sample);
        if (err < best_err)
        {
            best_err = err;
            best = nib;
        }
    }
    adpcm_decode_nibble(s, std::uint8_t(best));
    return std::uint8_t(best);
}
} // namespace

std::int16_t adpcm_decode_nibble(Adpcm &s, std::uint8_t nibble)
{
    s.predictor = std::clamp(s.predictor + (s.step * kDiff[nibble & 15]) / 8, -32768, 32767);
    s.step = std::clamp((s.step * kScale[nibble & 15]) >> 8, 127, 24576);
    return std::int16_t(s.predictor);
}

std::uint8_t adpcm_encode_pair(Adpcm &s, std::int16_t first, std::int16_t second)
{
    const std::uint8_t hi = encode_nibble(s, first);
    const std::uint8_t lo = encode_nibble(s, second);
    return std::uint8_t((hi << 4) | lo);
}

const char *speaker_tone_name(int tone)
{
    switch (tone)
    {
    case 0: return "Low tone (440 Hz)";
    case 1: return "Middle tone (660 Hz)";
    case 2: return "High tone (880 Hz)";
    default: return "Four notes (C E G C)";
    }
}

bool speaker_tone_samples(int tone, int from, std::int16_t *out, int n)
{
    /* 0.8 s at a third of full scale, faded in and out over 10 ms (no clicks). */
    const int total = kSpeakerRate * 8 / 10;
    if (from >= total)
        return false;
    static const float kNotes[4] = {523.25f, 659.25f, 783.99f, 1046.5f};
    for (int k = 0; k < n; ++k)
    {
        const int i = from + k;
        float v = 0;
        if (i < total)
        {
            const float t = float(i) / float(kSpeakerRate);
            const float f = tone == 0 ? 440.0f : tone == 1 ? 660.0f : tone == 2 ? 880.0f : kNotes[std::min(3, i * 4 / total)];
            const int fade = kSpeakerRate / 100;
            const int pos = tone >= 3 ? i % (total / 4) : i, len = tone >= 3 ? total / 4 : total;
            const float env = std::min(1.0f, std::min(float(pos) / float(fade), float(len - pos) / float(fade)));
            v = std::sin(2.0f * 3.14159265f * f * t) * env * 0.33f;
        }
        out[k] = std::int16_t(v * 32767.0f);
    }
    return true;
}

/* ---- notices --------------------------------------------------------------------------------- */
namespace
{
struct Shown
{
    int port;
    bool connected;
    double since; /* < 0: waiting */
    bool attachment = false;
    std::string what;
};
std::mutex g_notice_lock;
std::deque<Shown> g_notices;
} // namespace

void push_notice(int port, bool connected)
{
    if (port < 1 || port > 4)
        return;
    std::lock_guard<std::mutex> lock(g_notice_lock);
    /* The same port's latest change waiting already: it says the same. */
    for (const Shown &s : g_notices)
        if (s.since < 0 && s.port == port && s.connected == connected)
            return;
    if (g_notices.size() < 16)
        g_notices.push_back({port, connected, -1, false, {}});
}

void push_attachment_notice(int port, bool plugged, const std::string &what)
{
    if (port < 1 || port > 4)
        return;
    std::lock_guard<std::mutex> lock(g_notice_lock);
    for (const Shown &s : g_notices)
        if (s.since < 0 && s.attachment && s.port == port && s.connected == plugged && s.what == what)
            return;
    if (g_notices.size() < 16)
        g_notices.push_back({port, plugged, -1, true, what});
}

void clear_notices()
{
    std::lock_guard<std::mutex> lock(g_notice_lock);
    g_notices.clear();
}

std::vector<Notice> notices()
{
    std::lock_guard<std::mutex> lock(g_notice_lock);
    const double t = seconds_now();
    while (!g_notices.empty() && g_notices.front().since >= 0 && t - g_notices.front().since > kNoticeSeconds)
        g_notices.pop_front();
    std::vector<Notice> out;
    for (Shown &s : g_notices)
    {
        if (out.size() == 4)
            break;
        if (s.since < 0)
            s.since = t;
        out.push_back({s.port, s.connected, t - s.since, s.attachment, s.what});
    }
    return out;
}

/* ---- the monitor ---------------------------------------------------------------------------- */
#if defined(PORPOISE_DESKTOP)
#ifdef PORPOISE_UI_PREVIEW
namespace
{
const Status *g_preview = nullptr;
}
void preview_status(const Status *s) { g_preview = s; }
Status status() { return g_preview ? *g_preview : Status{}; }
#else
Status status() { return {}; }
#endif
void monitor(bool) {}
void hold_for_game() {}
void release_after_game() {}
bool held_for_game() { return false; }
void rumble(int, int) {}
void refresh(int) {}
void speaker_play(int, int) {}
void set_full_detail(bool) {}
int menu_remote() { return -1; }
std::uint16_t menu_buttons() { return 0; }
MenuNunchuk menu_nunchuk() { return {}; }
bool menu_sideways() { return false; }
void speaker_power(int, bool) {}
void speaker_volume(int, int) {}
void start_stuck_watch() {}
bool close_for_exit(unsigned) { return true; }
#else
namespace
{
/* A thread with the title's stack (title_threads.hpp: the console's default
 * of 64 KiB is too little for libc's formatting). */
class Thread
{
public:
    template <typename F> void start(F f)
    {
        fn_ = std::move(f);
        finished_ = false;
#if defined(PORPOISE_USBHID_HOST_TEST)
        running_ = pthread_create(&t_, nullptr, &Thread::entry, this) == 0;
#else
        running_ = create_title_thread(&t_, &Thread::entry, this) == 0;
#endif
    }
    bool joinable() const { return running_; }
    void detach() /* one that's never waited for */
    {
        if (running_)
            pthread_detach(t_);
        running_ = false;
    }
    void join()
    {
        if (running_)
            pthread_join(t_, nullptr);
        running_ = false;
    }
    bool finished() const { return finished_; }

private:
    static void *entry(void *self)
    {
        Thread *t = static_cast<Thread *>(self);
        t->fn_();
        t->finished_ = true;
        return nullptr;
    }
    pthread_t t_{};
    bool running_ = false;
    std::atomic<bool> finished_{false};
    std::function<void()> fn_;
};

std::mutex g_lock;          /* g_status and the commands */
Status g_status;
std::mutex g_control_lock;  /* starting and stopping */
Thread g_manager;
std::atomic<bool> g_run{false};
std::atomic<bool> g_held{false};
int g_rumble_ms[4] = {};
bool g_refresh[4] = {};
int g_spk_play[4] = {-1, -1, -1, -1};  /* a tone to play */
int g_spk_power[4] = {-1, -1, -1, -1}; /* 0 off, 1 on */
int g_spk_volume[4] = {-1, -1, -1, -1};
std::atomic<bool> g_full{false};
/* when each slot's Remote connected (a running count; 0: not connected): the
 * earliest connected is the menu Remote */
unsigned g_connect_seq[4] = {};
unsigned g_seq_counter = 0;

double now() { return seconds_now(); }

void bump() { ++g_status.generation; }

/* One slot's interface, owned by its thread. */
class Slot
{
public:
    Slot(int index, hid_device *h, int number) : index_(index), h_(h), number_(number) {}
    void run();

private:
    struct Pending
    {
        int want = 0;        /* 0x21 or 0x22 */
        std::uint32_t addr = 0;
        bool done = false;
        int error = 0;
        std::uint8_t data[16] = {};
    };

    bool write(std::initializer_list<std::uint8_t> bytes);
    bool write_raw(const std::uint8_t *b, std::size_t n);
    /* Reads for ms, handing every report to handle(); false when the device is gone. */
    bool pump(int ms);
    void handle(const std::uint8_t *b, int n);
    /* Register access; false on failure, with reg_error_: the Remote's error
     * code (7: nothing there, 8: write-only), -1 no answer, -2 not sent. */
    bool write_register(std::uint8_t space, std::uint32_t addr, const std::uint8_t *data, int size);
    bool read_register(std::uint8_t space, std::uint32_t addr, int size, std::uint8_t *out);
    bool set_up();
    void identify_attachment(bool restart_motion_plus = false);
    /* Our own MotionPlus switch: reads until its status reports have come and
     * asks for the status again, all while switching_ keeps them from being
     * taken for a plug. */
    void settle_after_switch(int ms);
    void set_mode();
    void ir_on();
    void wind_down();
    void publish(SlotState s);
    void forget_remote();

    int index_;
    hid_device *h_;
    int number_;
    Remote r_;          /* this thread's copy; published to g_status */
    Pending pending_;
    bool dead_ = false;
    std::uint8_t rumble_ = 0;       /* 0x01 while rumbling */
    double rumble_until_ = 0;
    bool ext_flag_ = false;         /* the last status said: something is plugged in */
    bool need_identify_ = false;
    bool need_mp_restart_ = false;  /* the MotionPlus's own attachment changed */
    bool need_mode_ = false;
    double last_report_ = 0;
    int reg_error_ = 0;
    /* The MotionPlus as we left it. Turning it on or off makes the Remote
     * report its attachment gone and back (two status reports): while
     * switching_, those are ours, not a plug (the first PS5 test's bug:
     * they were taken for one, the identification that followed turned the
     * MotionPlus off again, and so on). */
    bool switching_ = false;
    bool mp_active_ = false;
    bool mp_checked_ = false;       /* the switched-off MotionPlus was looked for */
    int mp_restart_for_ = -1;       /* the pass-through state the last restart was for */
    bool full_ = false;             /* set up for the controller test (else for the menus) */
    bool resolved_ = false;         /* the slot's first state is known: changes after it are notices */
    Extension base_ext_ = Extension::None; /* what's plugged in, without the MotionPlus, as last told */
    bool have_base_ = false;
    void lighten();

    /* the speaker test */
    bool speaker_power(bool on);
    void speaker_start(int tone);
    void speaker_send_due();
    void speaker_finish();
    double spk_t0_ = 0, spk_last_ = 0;
    int spk_index_ = 0;
    Adpcm spk_adpcm_;
    double spk_int_min_ = 0, spk_int_max_ = 0, spk_int_sum_ = 0;
    std::uint32_t spk_dropped_before_ = 0, spk_write_max_before_ = 0;
};

void Slot::settle_after_switch(int ms)
{
    switching_ = true;
    pump(ms);
    write({0x15, 0x00}); /* where the attachment flag now stands */
    pump(250);
    switching_ = false;
    need_identify_ = false;
}

void Slot::publish(SlotState s)
{
    r_.state = s;
    r_.interface_number = number_;
    r_.last_report_age = last_report_ > 0 ? now() - last_report_ : -1;
    if (r_.last_report_age > 2.0)
        r_.reporting = false;
    std::lock_guard<std::mutex> g(g_lock);
    Remote &out = g_status.remote[index_];
    if (out.state != r_.state || out.ext != r_.ext)
        bump();
    out = r_;
}

void Slot::forget_remote()
{
    const int number = number_;
    r_ = Remote{};
    r_.interface_number = number;
    ext_flag_ = false;
    rumble_ = 0;
    last_report_ = 0;
    switching_ = mp_active_ = mp_checked_ = false;
    need_identify_ = need_mp_restart_ = false;
    have_base_ = false;
    mp_restart_for_ = -1;
}

bool Slot::write_raw(const std::uint8_t *b, std::size_t n)
{
    const int w = hid_write(h_, b, n);
    if (w < 0 && errno == ENXIO)
        dead_ = true;
    return w >= 0;
}

bool Slot::write(std::initializer_list<std::uint8_t> bytes)
{
    std::uint8_t b[22] = {};
    std::size_t n = 0;
    for (std::uint8_t x : bytes)
        b[n++] = x;
    if (n > 1)
        b[1] |= rumble_; /* every output report carries the rumble bit */
    return write_raw(b, n);
}

void Slot::handle(const std::uint8_t *b, int n)
{
    last_report_ = now();
    decode_report(r_, b, n);
    if (b[0] == 0x20 && n >= 7)
    {
        const bool ext = b[3] & 0x02;
        if (ext != ext_flag_ && !switching_)
        {
            log("Wii Remote %d: status report: attachment %s", index_ + 1, ext ? "plugged in" : "pulled out");
            need_identify_ = true;
            mp_checked_ = false; /* a MotionPlus may have come with it */
            if (!ext)
                mp_active_ = false; /* pulled out: a MotionPlus too, if one was on */
        }
        ext_flag_ = ext;
        need_mode_ = true; /* a status report stops the data reports until the mode is set again */
    }
    else if (b[0] >= 0x30 && mp_active_ && !switching_ && r_.mp_reports > 0 &&
             (r_.ext == Extension::MotionPlus || r_.ext == Extension::MotionPlusNunchuk ||
              r_.ext == Extension::MotionPlusClassic))
    {
        /* A Nunchuk or Classic Controller plugged into (or out of) the
         * MotionPlus shows only in its data; it needs the other mode. Once
         * for each change, whatever the result. */
        const bool through = r_.ext != Extension::MotionPlus;
        const int seen = r_.mp_ext_connected ? 1 : 0;
        if ((seen == 1) != through && seen != mp_restart_for_)
        {
            mp_restart_for_ = seen;
            need_mp_restart_ = true;
            log("Wii Remote %d: something was %s the MotionPlus", index_ + 1, seen ? "plugged into" : "pulled out of");
        }
    }
    else if (b[0] == 0x21 && n >= 22 && pending_.want == 0x21 && !pending_.done)
    {
        const std::uint32_t addr = std::uint32_t((b[4] << 8) | b[5]);
        if (addr == (pending_.addr & 0xffff))
        {
            pending_.error = b[3] & 0x0f;
            std::memcpy(pending_.data, b + 6, 16);
            pending_.done = true;
        }
    }
    else if (b[0] == 0x22 && n >= 5 && pending_.want == 0x22 && !pending_.done && b[3] == 0x16)
    {
        pending_.error = b[4];
        pending_.done = true;
    }
}

bool Slot::pump(int ms)
{
    const double until = now() + ms / 1000.0;
    std::uint8_t b[64];
    do
    {
        const int n = hid_read_timeout(h_, b, sizeof b, std::max(1, std::min(ms, 20)));
        if (n < 0)
        {
            dead_ = true;
            return false;
        }
        if (n > 0)
            handle(b, n);
        if (pending_.want && pending_.done)
            return true;
    } while (now() < until && g_run);
    return true;
}

bool Slot::write_register(std::uint8_t space, std::uint32_t addr, const std::uint8_t *data, int size)
{
    std::uint8_t b[22] = {0x16, std::uint8_t(space | rumble_), std::uint8_t(addr >> 16), std::uint8_t(addr >> 8),
                          std::uint8_t(addr), std::uint8_t(size)};
    std::memcpy(b + 6, data, std::size_t(size));
    /* No answer: once more. The console's own driver can take a report of an
     * interface it's attached to (the PS5 log showed it on interface 0), and a
     * register write is the same done twice. */
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        pending_ = Pending{0x22, addr};
        if (!write_raw(b, sizeof b))
        {
            pending_ = Pending{};
            reg_error_ = -2;
            return false;
        }
        pump(400);
        reg_error_ = pending_.done ? pending_.error : -1;
        pending_ = Pending{};
        if (reg_error_ != -1)
            break;
    }
    return reg_error_ == 0;
}

bool Slot::read_register(std::uint8_t space, std::uint32_t addr, int size, std::uint8_t *out)
{
    const std::uint8_t b[7] = {0x17, std::uint8_t(space | rumble_), std::uint8_t(addr >> 16), std::uint8_t(addr >> 8),
                               std::uint8_t(addr), 0, std::uint8_t(size)};
    for (int attempt = 0; attempt < 2; ++attempt) /* no answer: once more, as above */
    {
        pending_ = Pending{0x21, addr};
        if (!write_raw(b, sizeof b))
        {
            pending_ = Pending{};
            reg_error_ = -2;
            return false;
        }
        pump(500);
        reg_error_ = pending_.done ? pending_.error : -1;
        if (reg_error_ == 0)
            std::memcpy(out, pending_.data, std::size_t(size));
        pending_ = Pending{};
        if (reg_error_ != -1)
            break;
    }
    return reg_error_ == 0;
}

namespace
{
bool is_motion_plus(Extension e)
{
    return e == Extension::MotionPlus || e == Extension::MotionPlusNunchuk || e == Extension::MotionPlusClassic;
}

std::string hex_id(const std::uint8_t id[6])
{
    char t[24];
    std::snprintf(t, sizeof t, "%02x %02x %02x %02x %02x %02x", id[0], id[1], id[2], id[3], id[4], id[5]);
    return t;
}
} // namespace

/* What's plugged in (WiiBrew "Wiimote/Extension Controllers"):
 *  - An active MotionPlus answers at 0xa400fa as it is; the initialisation
 *    below would turn it off, so it's asked first.
 *  - Anything else is initialised unencrypted (0x55 to 0xa400f0, 0x00 to
 *    0xa400fb) and read at 0xa400fa. A freshly plugged attachment may not
 *    answer at once: a few tries.
 *  - A MotionPlus that's off answers at 0xa600fa; it's turned on once per
 *    connection with the mode for what's plugged into it, and its own status
 *    reports while it switches are waited for (settle_after_switch).
 * Every step goes into one log line. */
void Slot::identify_attachment(bool restart_motion_plus)
{
    need_identify_ = false;
    need_mp_restart_ = false;
    r_.ext_identifying = true;
    publish(r_.state);
    std::string steps;
    char t[160];
    auto note = [&](const std::string &what) {
        if (!steps.empty())
            steps += "; ";
        steps += what;
    };
    auto note_error = [&](const char *what) {
        note(std::string(what) + ": " +
             (reg_error_ == -1 ? std::string("no answer")
              : reg_error_ == -2 ? std::string("not sent")
                                 : "error " + std::to_string(reg_error_)));
    };
    const std::uint8_t x55 = 0x55, x00 = 0x00;

    if (restart_motion_plus && mp_active_)
    {
        /* Something was plugged into or out of the MotionPlus: off, so that
         * it shows directly, then on again in the mode for it. */
        switching_ = true;
        if (write_register(0x04, 0xa400f0, &x55, 1))
            note("MotionPlus off for its attachment");
        else
            note_error("MotionPlus off");
        mp_active_ = false;
        mp_checked_ = false;
        settle_after_switch(600);
    }

    Extension found = ext_flag_ ? Extension::Unknown : Extension::None;
    std::uint8_t id[6] = {};
    bool id_read = false;
    int last_error = 0;
    if (ext_flag_)
    {
        if (read_register(0x04, 0xa400fa, 6, id) && is_motion_plus(identify(id)))
        {
            found = identify(id);
            id_read = true;
            mp_active_ = true;
            note("MotionPlus already on");
        }
        for (int attempt = 1; attempt <= 3 && !mp_active_; ++attempt)
        {
            if (attempt > 1)
                pump(200); /* a freshly plugged attachment: give it a moment */
            std::snprintf(t, sizeof t, "try %d", attempt);
            note(t);
            if (!write_register(0x04, 0xa400f0, &x55, 1))
            {
                note_error("init 0xa400f0");
                last_error = reg_error_;
                continue;
            }
            if (!write_register(0x04, 0xa400fb, &x00, 1))
            {
                note_error("init 0xa400fb");
                last_error = reg_error_;
                continue;
            }
            if (!read_register(0x04, 0xa400fa, 6, id))
            {
                note_error("read 0xa400fa");
                last_error = reg_error_;
                continue;
            }
            id_read = true;
            found = identify(id);
            last_error = 0;
            if (found != Extension::Unknown)
                break;
            note("id " + hex_id(id));
        }
    }
    else
        note("status: nothing plugged in");

    /* A MotionPlus while it's off (the Wii Remote Plus has one inside). */
    if (!mp_active_ && !mp_checked_ &&
        (found == Extension::None || found == Extension::Nunchuk || found == Extension::Classic))
    {
        mp_checked_ = true;
        std::uint8_t mp[6];
        if (read_register(0x04, 0xa600fa, 6, mp) && mp[2] == 0xa6 && mp[3] == 0x20)
        {
            r_.motion_plus_seen = true;
            const std::uint8_t mode = found == Extension::Nunchuk ? 0x05 : found == Extension::Classic ? 0x07 : 0x04;
            switching_ = true;
            if (write_register(0x04, 0xa600f0, &x55, 1) && write_register(0x04, 0xa600fe, &mode, 1))
            {
                /* It drops the attachment and comes back as it. */
                std::uint8_t mid[6];
                bool on = false;
                for (int k = 0; k < 8 && !on; ++k)
                {
                    pump(100);
                    on = read_register(0x04, 0xa400fa, 6, mid) && is_motion_plus(identify(mid));
                }
                if (on)
                {
                    std::memcpy(id, mid, 6);
                    id_read = true;
                    found = identify(mid);
                    mp_active_ = true;
                    mp_restart_for_ = found == Extension::MotionPlus ? 0 : 1;
                    std::snprintf(t, sizeof t, "MotionPlus on (mode %02x)", mode);
                    note(t);
                }
                else
                    note_error("MotionPlus on, but its id");
            }
            else
                note_error("MotionPlus on");
            settle_after_switch(200);
        }
        else
            note(reg_error_ == 7 ? "no MotionPlus" : "MotionPlus check: no answer");
    }

    if (found == Extension::Nunchuk)
    {
        std::uint8_t cal[16];
        r_.nc_calibrated = read_register(0x04, 0xa40020, 16, cal) && parse_calibration(cal, r_.nc_zero, r_.nc_one);
        if (r_.nc_calibrated)
            parse_stick_calibration(cal, r_);
    }
    /* A notice when what's plugged in changed (not for the first look after
     * connecting, nor for the MotionPlus turned on or off around it). */
    {
        auto base = [](Extension e) {
            switch (e)
            {
            case Extension::Nunchuk:
            case Extension::MotionPlusNunchuk: return Extension::Nunchuk;
            case Extension::Classic:
            case Extension::MotionPlusClassic: return Extension::Classic;
            case Extension::Unknown: return Extension::Unknown;
            default: return Extension::None;
            }
        };
        const Extension now_base = base(found);
        if (now_base != Extension::Unknown)
        {
            if (have_base_ && now_base != base_ext_)
            {
                if (base_ext_ != Extension::None)
                    push_attachment_notice(index_ + 1, false, extension_name(base_ext_));
                if (now_base != Extension::None)
                    push_attachment_notice(index_ + 1, true, extension_name(now_base));
            }
            base_ext_ = now_base;
            have_base_ = true;
        }
    }
    r_.ext = found;
    r_.ext_id_read = id_read;
    std::memcpy(r_.ext_id, id, 6);
    r_.ext_error = found == Extension::Unknown ? last_error : 0;
    r_.ext_identifying = false;
    log("Wii Remote %d: attachment %s%s%s (%s)", index_ + 1, extension_name(found), id_read ? ", id " : "",
        id_read ? hex_id(id).c_str() : "", steps.c_str());
    need_mode_ = true;
}
void Slot::ir_on()
{
    /* The camera in basic mode (report 0x37), sensitivity level 3 (WiiBrew "IR Camera"). */
    static const std::uint8_t block1[9] = {0x02, 0x00, 0x00, 0x71, 0x01, 0x00, 0xaa, 0x00, 0x64};
    static const std::uint8_t block2[2] = {0x63, 0x03};
    const std::uint8_t x08 = 0x08, basic = 0x01;
    write({0x13, 0x04});
    write({0x1a, 0x04});
    r_.ir_on = write_register(0x04, 0xb00030, &x08, 1) && write_register(0x04, 0xb00000, block1, 9) &&
               write_register(0x04, 0xb0001a, block2, 2) && write_register(0x04, 0xb00033, &basic, 1) &&
               write_register(0x04, 0xb00030, &x08, 1);
    log("Wii Remote %d: IR camera %s", index_ + 1, r_.ir_on ? "on" : "could not be set up");
}

void Slot::set_mode()
{
    need_mode_ = false;
    if (full_)
        write({0x12, 0x04, 0x37}); /* continuous: buttons, motion, IR (basic) and 6 attachment bytes */
    /* The menus: continuous too. Reported only on a change, a report the
     * console's own driver took (it can be on every interface) was a button
     * let go that never arrived, and the menu kept moving (a PS5 test). */
    else if (r_.ext == Extension::Nunchuk)
        write({0x12, 0x04, 0x32}); /* buttons and the Nunchuk's 8 bytes */
    else
        write({0x12, 0x04, 0x30}); /* buttons */
}

/* From the controller test back to the menus: the camera, the MotionPlus and
 * the speaker off, reports only when a button changes. */
void Slot::lighten()
{
    full_ = false;
    if (r_.speaker_on)
        speaker_power(false);
    write({0x13, 0x00});
    write({0x1a, 0x00});
    r_.ir_on = false;
    if (mp_active_)
    {
        const std::uint8_t x55 = 0x55;
        switching_ = true;
        write_register(0x04, 0xa400f0, &x55, 1);
        mp_active_ = false;
        settle_after_switch(300);
    }
    mp_checked_ = false;
    /* What's plugged in, as the menus read it (the Nunchuk directly, not
     * through the MotionPlus the test had on): its mode follows. */
    if (ext_flag_)
        need_identify_ = true;
    else
        set_mode();
}

bool Slot::set_up()
{
    write({0x11, std::uint8_t(0x10 << index_)}); /* the player's LED */
    if (!full_)
    {
        /* The menus read a Nunchuk too (C, Z, its stick); a MotionPlus stays off. */
        if (ext_flag_)
        {
            mp_checked_ = true;
            identify_attachment();
        }
        set_mode();
        return !dead_;
    }
    std::uint8_t cal[10];
    r_.calibrated = read_register(0x00, 0x000016, 10, cal) && parse_calibration(cal, r_.cal_zero, r_.cal_one);
    log("Wii Remote %d: calibration %s (zero %d %d %d, 1 g %d %d %d)", index_ + 1,
        r_.calibrated ? "read" : "not read, estimated", r_.cal_zero[0], r_.cal_zero[1], r_.cal_zero[2],
        r_.cal_one[0], r_.cal_one[1], r_.cal_one[2]);
    identify_attachment();
    ir_on();
    set_mode();
    return !dead_;
}

/* The speaker on or off (WiiBrew "Wiimote", Speaker): enable, mute, the
 * registers in their order (0xa20009 = 1, 0xa20001 = 8, the configuration:
 * 4-bit ADPCM, rate 2000 for 3000 Hz, the volume; 0xa20008 = 1), unmute.
 * Setting it up again resets its decoder, so each tone starts clean. */
bool Slot::speaker_power(bool on)
{
    if (!on)
    {
        write({0x19, 0x04});
        write({0x14, 0x00});
        if (r_.speaker_on)
            log("Wii Remote %d: speaker off", index_ + 1);
        r_.speaker_on = r_.speaker_playing = false;
        return true;
    }
    const std::uint8_t x01 = 0x01, x08 = 0x08;
    const std::uint8_t config[7] = {0x00, 0x00, 0xd0, 0x07, kSpeakerVolume[std::clamp(r_.speaker_volume, 0,
                                                                                           kSpeakerVolumes - 1)],
                                    0x00, 0x00};
    write({0x14, 0x04});
    write({0x19, 0x04});
    const bool ok = write_register(0x04, 0xa20009, &x01, 1) && write_register(0x04, 0xa20001, &x08, 1) &&
                    write_register(0x04, 0xa20001, config, 7) && write_register(0x04, 0xa20008, &x01, 1);
    write({0x19, 0x00});
    if (!r_.speaker_on || !ok)
        log("Wii Remote %d: speaker %s (volume %02x)", index_ + 1, ok ? "on" : "set-up not acknowledged",
            config[4]);
    r_.speaker_on = true;
    r_.speaker_setup_failed = !ok;
    return ok;
}

void Slot::speaker_start(int tone)
{
    if (!porpoise::usbhid::speaker_enabled())
    {
        log("Wii Remote %d: speaker test not played: the Wii Remote speaker is off in the settings", index_ + 1);
        return;
    }
    speaker_power(true);
    r_.speaker_tone = std::clamp(tone, 0, kSpeakerTones - 1);
    r_.speaker_playing = true;
    r_.spk_sent = r_.spk_refused = 0;
    r_.spk_rate = r_.spk_interval_min = r_.spk_interval_avg = r_.spk_interval_max = 0;
    r_.spk_write_max_ms = 0;
    spk_adpcm_ = Adpcm{};
    spk_index_ = 0;
    spk_t0_ = now();
    spk_last_ = 0;
    spk_int_min_ = 1e9;
    spk_int_max_ = spk_int_sum_ = 0;
    porpoise::usbhid::InterfaceStatus st;
    if (porpoise::usbhid::interface_stats(h_, st))
    {
        spk_dropped_before_ = st.speaker_dropped;
        spk_write_max_before_ = st.write_max_ms;
    }
}

/* The reports due by now, at 75 a second from the start (a few at once at
 * most, to catch up after a slow write). */
void Slot::speaker_send_due()
{
    const double t = now();
    const int due = int((t - spk_t0_) * (double(kSpeakerRate) / kSpeakerSamplesPerReport)) + 1;
    for (int burst = 0; burst < 3 && spk_index_ < due; ++burst)
    {
        std::int16_t pcm[kSpeakerSamplesPerReport];
        if (!speaker_tone_samples(r_.speaker_tone, spk_index_ * kSpeakerSamplesPerReport, pcm,
                                  kSpeakerSamplesPerReport))
        {
            speaker_finish();
            return;
        }
        std::uint8_t b[22] = {0x18, std::uint8_t((20 << 3) | rumble_)};
        for (int k = 0; k < 20; ++k)
            b[2 + k] = adpcm_encode_pair(spk_adpcm_, pcm[2 * k], pcm[2 * k + 1]);
        const double sent_at = now();
        write_raw(b, sizeof b);
        if (spk_last_ > 0)
        {
            const double gap = (sent_at - spk_last_) * 1000.0;
            spk_int_min_ = std::min(spk_int_min_, gap);
            spk_int_max_ = std::max(spk_int_max_, gap);
            spk_int_sum_ += gap;
        }
        spk_last_ = sent_at;
        ++spk_index_;
        ++r_.spk_sent;
    }
    porpoise::usbhid::InterfaceStatus st;
    if (porpoise::usbhid::interface_stats(h_, st))
    {
        r_.spk_refused = st.speaker_dropped - spk_dropped_before_;
        r_.spk_write_max_ms = st.write_max_ms;
    }
    const double elapsed = now() - spk_t0_;
    r_.spk_rate = elapsed > 0.05 ? float(r_.spk_sent / elapsed) : 0.0f;
    if (r_.spk_sent > 1)
    {
        r_.spk_interval_min = float(spk_int_min_);
        r_.spk_interval_max = float(spk_int_max_);
        r_.spk_interval_avg = float(spk_int_sum_ / double(r_.spk_sent - 1));
    }
}

void Slot::speaker_finish()
{
    r_.speaker_playing = false;
    write({0x19, 0x04}); /* silent between tones; still on */
    log("Wii Remote %d: speaker test \"%s\": %u reports, %u refused, %.1f a second, %.1f / %.1f / %.1f ms apart "
        "(min / avg / max), slowest write %u ms",
        index_ + 1, speaker_tone_name(r_.speaker_tone), r_.spk_sent, r_.spk_refused, r_.spk_rate,
        r_.spk_interval_min, r_.spk_interval_avg, r_.spk_interval_max, r_.spk_write_max_ms);
}

/* Leaves the Remote as Dolphin will want to find it: no rumble, the camera
 * off, the MotionPlus off, no continuous reports. */
void Slot::wind_down()
{
    if (dead_)
        return;
    rumble_ = 0;
    write({0x10, 0x00});
    if (r_.speaker_on)
        speaker_power(false); /* muted and off */
    if (r_.state == SlotState::Connected)
    {
        write({0x13, 0x00});
        write({0x1a, 0x00});
        if (mp_active_) /* the MotionPlus off: Dolphin and the game find the Remote as it starts */
        {
            /* Checked, not assumed: a MotionPlus left on hands a game that
             * knows only the Nunchuk its pass-through data, every other report
             * the MotionPlus's (a PS5 test: a game's menu cursor ran up and
             * down by itself). Off, the MotionPlus is at 0xa600fa again. */
            const std::uint8_t x55 = 0x55;
            bool off = false;
            for (int k = 0; k < 3 && !off; ++k)
            {
                switching_ = true;
                if (write_register(0x04, 0xa400f0, &x55, 1))
                {
                    std::uint8_t mp[6];
                    for (int w = 0; w < 5 && !off; ++w)
                    {
                        pump(100);
                        off = read_register(0x04, 0xa600fa, 6, mp) && mp[2] == 0xa6 && mp[3] == 0x20;
                    }
                }
            }
            switching_ = false;
            log("Wii Remote %d: MotionPlus %s for the game", index_ + 1,
                off ? "switched off" : "could not be confirmed off (the game may see its data)");
            mp_active_ = false;
        }
        write({0x12, 0x00, 0x30});
    }
}

void Slot::run()
{
    double next_probe = 0, next_alive = 0, next_status = 0;
    SlotState state = SlotState::NoRemote;
    bool answered_once = false;
    publish(state);
    while (g_run && !dead_)
    {
        /* the test screen's requests */
        int rumble_ms = 0, spk_play = -1, spk_power = -1, spk_volume = -1;
        bool refresh = false;
        {
            std::lock_guard<std::mutex> g(g_lock);
            std::swap(rumble_ms, g_rumble_ms[index_]);
            std::swap(refresh, g_refresh[index_]);
            std::swap(spk_play, g_spk_play[index_]);
            std::swap(spk_power, g_spk_power[index_]);
            std::swap(spk_volume, g_spk_volume[index_]);
        }
        const double t = now();
        if (state == SlotState::NoRemote || state == SlotState::Lost || state == SlotState::Waiting)
        {
            if (t >= next_probe)
            {
                next_probe = t + 1.0;
                std::uint8_t req[2] = {0x15, 0x00};
                if (write_raw(req, 2))
                {
                    if (state != SlotState::Waiting)
                        log("Wii Remote %d: the DolphinBar takes reports for this slot; asking the Remote", index_ + 1);
                    state = SlotState::Waiting;
                    publish(state);
                    pump(800);
                    if (r_.have_status)
                    {
                        state = SlotState::Connected;
                        answered_once = true;
                        if (resolved_)
                            push_notice(index_ + 1, true);
                        resolved_ = true;
                        {
                            std::lock_guard<std::mutex> g(g_lock);
                            g_connect_seq[index_] = ++g_seq_counter;
                        }
                        full_ = g_full.load();
                        log("Wii Remote %d: connected (it answered: battery %d%%%s)", index_ + 1, r_.battery_percent,
                            ext_flag_ ? ", something plugged in" : "");
                        publish(state);
                        set_up();
                        next_alive = now() + 1.0;
                        next_status = now() + 15.0;
                    }
                }
                else if (!dead_)
                {
                    resolved_ = true; /* nothing here at the start: what comes later is news */
                    if (state == SlotState::Waiting)
                    {
                        state = answered_once ? SlotState::Lost : SlotState::NoRemote;
                        log("Wii Remote %d: the DolphinBar refuses reports again", index_ + 1);
                    }
                }
            }
            else
                pump(50);
            publish(state);
            continue;
        }
        /* Connected */
        if (g_full.load() != full_)
        {
            if (!full_)
            {
                full_ = true;
                mp_checked_ = false; /* the test looks for a MotionPlus */
                log("Wii Remote %d: set up for the controller test", index_ + 1);
                set_up();
            }
            else
                lighten();
        }
        if (!full_)
        {
            /* the menus: the speaker test and attachments are the test's */
            spk_play = spk_power = spk_volume = -1;
            refresh = false;
        }
        if (rumble_ms > 0)
        {
            rumble_ = 0x01;
            rumble_until_ = t + rumble_ms / 1000.0;
            write({0x10, 0x01});
            log("Wii Remote %d: rumble test %d ms", index_ + 1, rumble_ms);
        }
        if (rumble_ && t >= rumble_until_)
        {
            rumble_ = 0;
            write({0x10, 0x00});
        }
        if (refresh)
        {
            log("Wii Remote %d: check again (Triangle)", index_ + 1);
            write({0x15, 0x00});
            pump(150);
            mp_checked_ = false;
            need_identify_ = true;
        }
        if (spk_volume >= 0)
        {
            r_.speaker_volume = std::clamp(spk_volume, 0, kSpeakerVolumes - 1);
            if (r_.speaker_on && !r_.speaker_playing)
                speaker_power(true);
        }
        if (spk_power == 0)
            speaker_power(false);
        else if (spk_power == 1 && !r_.speaker_on)
            speaker_power(true);
        if (spk_play >= 0)
            speaker_start(spk_play);
        if (r_.speaker_playing)
            speaker_send_due();
        if (need_identify_)
        {
            if (!full_)
                mp_checked_ = true; /* the menus: a Nunchuk read directly, a MotionPlus left off */
            identify_attachment();
        }
        else if (full_ && need_mp_restart_)
            identify_attachment(true);
        if (need_mode_)
            set_mode();
        if (t >= next_alive)
        {
            /* Dolphin's own once-a-second write (WiimoteReal.cpp): the
             * DolphinBar refuses it once the Remote is gone. */
            next_alive = t + 1.0;
            if (!write({0x10, rumble_}))
            {
                if (dead_)
                    break;
                log("Wii Remote %d: lost (the DolphinBar refuses reports for it)", index_ + 1);
                push_notice(index_ + 1, false);
                {
                    std::lock_guard<std::mutex> g(g_lock);
                    g_connect_seq[index_] = 0;
                }
                forget_remote();
                r_.state = state = SlotState::Lost;
                next_probe = t + 1.0;
                publish(state);
                continue;
            }
        }
        if (t >= next_status)
        {
            next_status = t + 15.0;
            write({0x15, 0x00}); /* the battery, now and then */
        }
        if (full_ && last_report_ > 0 && t - last_report_ > 3.0 && !need_mode_)
        {
            /* Taken reports but nothing back: ask for the reports again. */
            need_mode_ = true;
            last_report_ = t - 1.0; /* not every frame */
        }
        pump(r_.speaker_playing ? 2 : 30); /* 75 speaker reports a second want a finer step */
        publish(state);
    }
    wind_down();
    {
        std::lock_guard<std::mutex> g(g_lock);
        g_connect_seq[index_] = 0;
    }
    if (dead_)
        log("Wii Remote %d: the interface is gone (unplugged?)", index_ + 1);
}

struct OpenSlot
{
    hid_device *h = nullptr;
    std::unique_ptr<Thread> t;
    std::unique_ptr<Slot> slot;
};

void close_slots(std::vector<OpenSlot> &slots)
{
    for (OpenSlot &s : slots)
        if (s.t)
            s.t->join();
    for (OpenSlot &s : slots)
        if (s.h)
            hid_close(s.h);
    slots.clear();
}

void manager()
{
    log("Controller test: looking for the Mayflash DolphinBar (057e:0306)");
    hid_init();
    std::vector<OpenSlot> slots;
    std::string open_node;
    while (g_run)
    {
        hid_device_info *list = hid_enumerate(0x057e, 0);
        std::string node;
        std::vector<std::pair<std::string, int>> paths; /* path, interface number */
        for (hid_device_info *i = list; i; i = i->next)
        {
            const std::string p = i->path;
            const std::string n = p.substr(0, p.find(':'));
            if (node.empty())
                node = n;
            if (n == node && paths.size() < 4)
                paths.emplace_back(p, i->interface_number);
        }
        hid_free_enumeration(list);

        /* What the hidapi layer knows about the device. */
        porpoise::usbhid::DeviceStatus dev;
        bool have_dev = false;
        for (const auto &d : porpoise::usbhid::status())
            if (d.node == node)
            {
                dev = d;
                have_dev = true;
            }
        const auto unreadable = porpoise::usbhid::unreadable();

        /* A slot's thread ends only when its interface is gone (or we stop):
         * all of them ended, or the node gone from the list, is an unplug. */
        const bool slots_ended = !slots.empty() && std::all_of(slots.begin(), slots.end(), [](const OpenSlot &s) {
            return !s.t || s.t->finished();
        });
        if (!slots.empty() && (node != open_node || slots_ended))
        {
            log("Controller test: the DolphinBar %s is gone", open_node.c_str());
            close_slots(slots);
            open_node.clear();
            std::lock_guard<std::mutex> g(g_lock);
            for (Remote &r : g_status.remote)
                r = Remote{};
            bump();
        }

        int open_errno = 0;
        if (slots.empty() && !node.empty())
        {
            log("Controller test: DolphinBar found at %s (%04x:%04x \"%s\"), %zu interface(s)", node.c_str(), dev.vid,
                dev.pid, dev.product.c_str(), paths.size());
            int opened = 0;
            for (std::size_t k = 0; k < paths.size(); ++k)
            {
                OpenSlot s;
                errno = 0;
                s.h = hid_open_path(paths[k].first.c_str());
                if (!s.h)
                {
                    open_errno = errno;
                    log("Controller test: %s could not be opened (errno %d: %s)", paths[k].first.c_str(), errno,
                        std::strerror(errno));
                    std::lock_guard<std::mutex> g(g_lock);
                    g_status.remote[k] = Remote{};
                    g_status.remote[k].interface_number = paths[k].second;
                    g_status.remote[k].open_errno = open_errno;
                    slots.push_back(std::move(s));
                    continue;
                }
                ++opened;
                s.slot = std::make_unique<Slot>(int(k), s.h, paths[k].second);
                Slot *raw = s.slot.get();
                s.t = std::make_unique<Thread>();
                s.t->start([raw] { raw->run(); });
                slots.push_back(std::move(s));
            }
            open_node = node;
            std::lock_guard<std::mutex> g(g_lock);
            g_status.bar = opened > 0 ? BarState::Ready : BarState::AccessFailed;
            bump();
            if (!opened)
                log("Controller test: no interface of the DolphinBar could be opened");
        }
        {
            std::lock_guard<std::mutex> g(g_lock);
            Status &s = g_status;
            if (node.empty())
                s.bar = BarState::NotConnected;
            s.node = node;
            s.vid = have_dev ? dev.vid : 0;
            s.pid = have_dev ? dev.pid : 0;
            s.product = have_dev ? dev.product : "";
            s.descriptors_read = have_dev && dev.descriptors_read;
            s.interfaces.clear();
            s.kernel_driver.clear();
            if (have_dev)
                for (const auto &i : dev.interfaces)
                {
                    char line[240];
                    std::snprintf(line, sizeof line, "%d|%02x|%02x|%d|%d|%u|%u|%u|%u|%d|%u|%.1f|%.1f|%u|%u|%u", i.number,
                                  i.ep_in, i.ep_out, i.kernel_driver, i.open ? 1 : 0, i.reports_in, i.writes_ok,
                                  i.writes_failed, i.read_errors, i.last_read_status, i.read_restarts,
                                  i.last_input_age, i.last_output_age, i.speaker_dropped, i.write_max_ms,
                                  i.write_retries);
                    s.interfaces.push_back(line);
                    s.kernel_driver.push_back(i.kernel_driver);
                }
            s.unreadable = unreadable;
        }
        for (int k = 0; k < 10 && g_run; ++k)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    close_slots(slots);
    hid_exit();
    {
        std::lock_guard<std::mutex> g(g_lock);
        g_status.monitoring = false;
        if (g_status.bar == BarState::Ready || g_status.bar == BarState::AccessFailed)
            g_status.bar = g_status.node.empty() ? BarState::NotConnected : BarState::UsbDetected;
        for (Remote &r : g_status.remote)
            r = Remote{};
        bump();
    }
    log("Controller test: stopped; the DolphinBar's interfaces are closed");
}

void stop_locked()
{
    if (!g_manager.joinable())
        return;
    g_run = false;
    g_manager.join();
}
} // namespace

Status status()
{
    std::lock_guard<std::mutex> g(g_lock);
    Status s = g_status;
    if (g_held)
        s.bar = s.node.empty() ? BarState::NotConnected : BarState::HeldByGame;
    return s;
}

void monitor(bool wanted)
{
    std::lock_guard<std::mutex> c(g_control_lock);
    if (wanted && !g_held && !g_manager.joinable())
    {
        route_usbhid_log();
        g_run = true;
        {
            std::lock_guard<std::mutex> g(g_lock);
            g_status.monitoring = true;
        }
        g_manager.start(manager);
    }
    else if (!wanted && g_manager.joinable())
        stop_locked();
}

void hold_for_game()
{
    std::lock_guard<std::mutex> c(g_control_lock);
    g_held = true;
    if (g_manager.joinable())
    {
        log("A game starts: the controller test hands the DolphinBar over");
        stop_locked();
    }
}

void release_after_game()
{
    g_held = false;
}

bool held_for_game() { return g_held; }

void rumble(int slot, int ms)
{
    if (slot < 0 || slot > 3)
        return;
    std::lock_guard<std::mutex> g(g_lock);
    g_rumble_ms[slot] = std::clamp(ms, 50, 2000);
}

void set_full_detail(bool full) { g_full = full; }

int menu_remote()
{
    std::lock_guard<std::mutex> g(g_lock);
    if (g_held)
        return -1;
    int best = -1;
    for (int k = 0; k < 4; ++k)
        if (g_connect_seq[k] && g_status.remote[k].state == SlotState::Connected &&
            (best < 0 || g_connect_seq[k] < g_connect_seq[best]))
            best = k;
    return best;
}

namespace
{
/* The menu Remote as its latest report has it, straight from the hidapi
 * layer: its slot's thread can be in a register read or a write for a while,
 * and what it last published then was a button still held (the menu moved
 * on by itself in a PS5 test). A report older than 250 ms is no button: the
 * menus ask for continuous reports. */
bool menu_report(Remote &r)
{
    const int k = menu_remote();
    if (k < 0)
        return false;
    int number;
    {
        std::lock_guard<std::mutex> g(g_lock);
        const Remote &s = g_status.remote[k];
        number = s.interface_number;
        r.ext = s.ext;
        for (int a = 0; a < 2; ++a)
        {
            r.nc_stick_min[a] = s.nc_stick_min[a];
            r.nc_stick_mid[a] = s.nc_stick_mid[a];
            r.nc_stick_max[a] = s.nc_stick_max[a];
        }
    }
    std::uint8_t b[64];
    unsigned age = 0;
    const int n = porpoise::usbhid::latest_report(number, b, sizeof b, age);
    if (n < 3 || age > 250)
        return false;
    /* The Nunchuk's bytes only from the menus' own report for it: in any
     * other (the test's, a MotionPlus passing it through) they are something
     * else, and read as the stick they moved the menu (a PS5 test). */
    if (b[0] != 0x32)
        r.ext = Extension::None;
    decode_report(r, b, n);
    return true;
}
} // namespace

std::uint16_t menu_buttons()
{
    Remote r;
    return menu_report(r) ? r.buttons : 0;
}

bool menu_sideways()
{
    const int k = menu_remote();
    if (k < 0)
        return false;
    std::lock_guard<std::mutex> g(g_lock);
    const Remote &r = g_status.remote[k];
    return r.ext == Extension::None && !r.ext_identifying;
}

MenuNunchuk menu_nunchuk()
{
    MenuNunchuk out;
    Remote r;
    if (!menu_report(r) || r.ext != Extension::Nunchuk || r.ext_reports == 0)
        return out;
    out.present = true;
    out.c = r.nc_c;
    out.z = r.nc_z;
    out.x = nunchuk_stick(r, 0);
    out.y = nunchuk_stick(r, 1); /* up is +1 */
    return out;
}

void speaker_play(int slot, int tone)
{
    if (slot < 0 || slot > 3)
        return;
    std::lock_guard<std::mutex> g(g_lock);
    g_spk_play[slot] = std::clamp(tone, 0, kSpeakerTones - 1);
}

void speaker_power(int slot, bool on)
{
    if (slot < 0 || slot > 3)
        return;
    std::lock_guard<std::mutex> g(g_lock);
    g_spk_power[slot] = on ? 1 : 0;
}

void speaker_volume(int slot, int level)
{
    if (slot < 0 || slot > 3)
        return;
    std::lock_guard<std::mutex> g(g_lock);
    g_spk_volume[slot] = std::clamp(level, 0, kSpeakerVolumes - 1);
}

void refresh(int slot)
{
    if (slot < 0 || slot > 3)
        return;
    std::lock_guard<std::mutex> g(g_lock);
    g_refresh[slot] = true;
}

/* A call into the console's USB stack that doesn't come back can't say so
 * itself (usbhid's stuck_calls): this asks every second, and logs each one
 * once, after 2 s. */
void start_stuck_watch()
{
    static std::once_flag once;
    static Thread watch;
    std::call_once(once, [] {
        watch.start([] {
            std::vector<std::string> said;
            for (;;)
            {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                std::vector<std::string> now_stuck;
                for (const std::string &line : porpoise::usbhid::stuck_calls(2000))
                {
                    const std::string what = line.substr(0, line.find(" for "));
                    now_stuck.push_back(what);
                    if (std::find(said.begin(), said.end(), what) == said.end())
                        log("USB: %s and not back yet (the console's USB stack is still in it)", line.c_str());
                }
                said = std::move(now_stuck);
            }
        });
        watch.detach();
    });
}

bool close_for_exit(unsigned max_ms)
{
    static std::atomic<bool> done{false};
    static std::atomic<bool> started{false};
    static Thread closer;
    if (started.exchange(true))
        return done;
    log("Porpoise closes: the DolphinBar is let go");
    closer.start([] {
        {
            std::lock_guard<std::mutex> c(g_control_lock);
            g_held = true;
            stop_locked();
        }
        porpoise::usbhid::shutdown();
        done = true;
    });
    closer.detach();
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(max_ms);
    while (!done && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (!done)
    {
        std::string stuck;
        for (const std::string &line : porpoise::usbhid::stuck_calls(0))
            stuck += (stuck.empty() ? "" : "; ") + line;
        log("Porpoise closes anyway after %u ms: %s", max_ms, stuck.empty() ? "the monitor hasn't stopped" : stuck.c_str());
    }
    return done;
}
#endif
} // namespace porpoise::dolphinbar
