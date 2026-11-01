/* Porpoise - the emulator core, hosted through the libretro API.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Dolphin is built as a libretro core (libretro/dolphin with Mihawk's PS5 port
 * patch). The API is a plugin interface, not a user interface: this file is the
 * whole host side of it, and nothing of RetroArch runs.
 *
 * The core is loaded with the title's own ELF loader (src/core_loader_ps5.cpp),
 * because this console refuses a title's dlopen, and its imports are bound to
 * the title's symbols (build/core_imports.inc). */
#include "porpoise_paths.hpp"
#include "porpoise_core.hpp"
#include "porpoise_atomic.hpp"
#ifdef PORPOISE_DESKTOP
#include "porpoise_platform.hpp"
#endif
#include "porpoise_mic.hpp"
#include "porpoise_netfs.hpp"
#include "porpoise_ra.hpp"
#include "porpoise_speaker.hpp"
#include "porpoise_states.hpp"
#include "porpoise_dolphinbar.hpp"
#include "porpoise_ns2gc.hpp"
#include "porpoise_usbhid.hpp"

#include <ps5platform/libc.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "libretro.h"
#include "porpoise_audio.hpp"
#include "porpoise_framegen.hpp"
#include "porpoise_pacer.hpp"
#include "porpoise_aim.hpp"
#include "porpoise_pad.hpp"
#include "porpoise_vk.hpp"
#include "trace.hpp"

extern "C"
{
    void *ps5_core_dlopen(const char *path, int mode);
    void *ps5_core_dlsym(void *handle, const char *name);
    int ps5_core_dlclose(void *handle);
    char *ps5_core_dlerror();
}

namespace
{
using porpoise::core::Paths;

struct CoreApi
{
    void (*init)();
    void (*deinit)();
    void (*get_system_info)(retro_system_info *);
    void (*get_system_av_info)(retro_system_av_info *);
    void (*set_environment)(retro_environment_t);
    void (*set_video_refresh)(retro_video_refresh_t);
    void (*set_audio_sample)(retro_audio_sample_t);
    void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*set_input_poll)(retro_input_poll_t);
    void (*set_input_state)(retro_input_state_t);
    void (*set_controller_port_device)(unsigned, unsigned);
    bool (*load_game)(const retro_game_info *);
    void (*unload_game)();
    void (*run)();
    void (*reset)();
    std::size_t (*serialize_size)();
    bool (*serialize)(void *, std::size_t);
    bool (*unserialize)(const void *, std::size_t);
};

struct Option
{
    std::string key;
    std::string default_value;
    std::string value;
    std::string description;
    std::vector<std::string> choices;
};

struct Host
{
    Paths paths;
    void *library = nullptr;
    CoreApi api{};
    std::vector<Option> options;
    bool options_updated = false;
    retro_hw_render_callback hw{};
    bool hw_requested = false;
    const retro_hw_render_context_negotiation_interface_vulkan *negotiation = nullptr;
    float aspect = 4.0f / 3.0f;
    double sample_rate = 32000.0;
    double fps = 60.0;
    std::atomic<unsigned long long> samples_in{0}; /* audio frames the core has pushed (any thread) */
    bool presented = false;
    unsigned last_width = 0, last_height = 0;
    int invert_main = 0, invert_c = 0; /* set_stick_invert */
    float stick_power = 1.0f, stick_dead = 0.0f; /* set_stick_shape */
    int filter = 0;        /* the screen filter (porpoise_vk.hpp) */
    int fast_forward = 1;  /* frames run per frame shown */
    std::string pending_state; /* a save state to load once the game shows its first pictures */
    unsigned frames_with_picture = 0;
    bool running = false;  /* a game is loaded: save states are possible */
    float strength = 0.6f; /* its strength, 0..1 */
    bool have_frame = false;
    bool new_frame = false; /* 3.1: this run handed over a new frame (frame generation) */
    /* 3.1: the core's disc changer (a game on two discs: Change Disc). */
    retro_disk_control_ext_callback disks{};
    bool have_disks = false;
    /* 4.0: Change Disc to another game's disc (a game asking for its
     * expansion's): the game's own discs, the files added to the core's list
     * after them, and what the menu chose for the next Change Disc. */
    unsigned own_discs = 0;
    std::vector<std::string> added_discs;
    int next_disc = -1;
    std::string next_disc_path;
    bool hold_input = false; /* after the menu: the game sees no buttons until all are let go */
    porpoise::pad::WiiConfig wii;   /* a Wii game's Remote */
    bool wii_changed = false;       /* the in-game menu changed it: the ports again */
    std::FILE *motion_log = nullptr; /* the debug folder's motion-<time>.csv */
    /* Two-controller play: getting the Nunchuk its motion (nunchuk_motion_step). */
    int nunchuk_stage = 0;
    unsigned long long nunchuk_frame = 0;
    int nunchuk_tries = 0;
    std::string debug_dir; /* the debug folder, "" when off */
    long long motion_log_bytes = 0;
    unsigned long long frame_number = 0;
    bool plugged[porpoise::pad::kMaxPlayers] = {}; /* GameCube ports with a controller in */
    bool ports_real = false; /* the ports were given real Wii Remotes (WiiConfig::real_remotes) */
    /* 4.0: player 1's GameCube port (Settings > Controls); the player whose
     * port that is takes port 1. Wii games keep 0. */
    std::atomic<int> p1_port_want{0};
    int p1_port = 0;
    std::FILE *log = nullptr;
};

Host h;

/* The first alert and the last error Dolphin logged this run, for a game that
 * doesn't start (core_log, any thread). */
using porpoise::core::Failure;
std::mutex g_reason_lock;
std::string g_first_alert, g_last_error;
Failure g_failure = Failure::None;
std::string g_failure_reason;

std::string clean_reason(std::string s)
{
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    /* Dolphin's lines: "12:34:567 Core/Boot/Boot.cpp:123 E[BOOT]: text". */
    const std::size_t tag = s.find("]: ");
    if (tag != std::string::npos && tag < 80)
        s = s.substr(tag + 3);
    /* Paths as their file names: a long one has no spaces to wrap at. */
    std::string out;
    std::size_t i = 0;
    while (i < s.size())
    {
        const std::size_t sp = s.find(' ', i);
        std::string word = s.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
        const std::size_t slash = word.find_last_of('/');
        if (slash != std::string::npos && slash + 1 < word.size() && word.find('/') != slash)
            word = word.substr(slash + 1); /* "/mnt/usb0/games/x.rvz" (or quoted) -> "x.rvz" */
        if (!out.empty())
            out += ' ';
        out += word;
        if (sp == std::string::npos)
            break;
        i = sp + 1;
    }
    s = out;
    if (s.size() > 200)
        s = s.substr(0, 197) + "...";
    return s;
}

void fail(Failure what, const std::string &reason)
{
    g_failure = what;
    std::string r = reason;
    if (r.empty())
    {
        std::lock_guard<std::mutex> lock(g_reason_lock);
        r = !g_first_alert.empty() ? g_first_alert : g_last_error;
    }
    g_failure_reason = clean_reason(r);
    ps5::debug::mark(("core: did not start: " + g_failure_reason).c_str());
}

/* The Wii Remote's motion (below). */
bool RETRO_CALLCONV set_sensor_state(unsigned port, enum retro_sensor_action action, unsigned rate);
float RETRO_CALLCONV get_sensor_input(unsigned port, unsigned id);

/* ---- options --------------------------------------------------------------------------- */

std::string trim(const std::string &s)
{
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
        return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

/* The running game's hooks, for what the core reports while it loads. */
const porpoise::core::Hooks *g_run_hooks = nullptr;
/* The core said a Wii System Update didn't finish (take_update_failed). */
bool g_update_failed = false;

Option *find_option(const char *key)
{
    for (Option &o : h.options)
        if (o.key == key)
            return &o;
    return nullptr;
}

/* Porpoise's own choices, before the player's: things that suit a television
 * and a DualSense better than the core's desktop defaults. */
void apply_porpoise_defaults()
{
    /* Nothing is overridden yet; this is where Porpoise's video/controls
     * presets will land once the settings screens exist. */
}

void apply_user_overrides()
{
    std::FILE *f = std::fopen(h.paths.options, "r");
    if (!f)
        return;
    char line[512];
    int applied = 0;
    while (std::fgets(line, sizeof line, f))
    {
        std::string text = trim(line);
        if (text.empty() || text[0] == '#' || text[0] == ';')
            continue;
        const auto eq = text.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = trim(text.substr(0, eq));
        std::string value = trim(text.substr(eq + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);
        if (Option *o = find_option(key.c_str()))
        {
            o->value = value;
            ++applied;
        }
    }
    std::fclose(f);
    ps5::debug::mark_value("options: overrides applied from options.ini", applied);
}

void write_options_reference()
{
    mkdir(PORPOISE_APP "/porpoise", 0777);
    std::FILE *f = std::fopen(h.paths.options_reference, "w");
    if (!f)
        return;
    std::fprintf(f, "# Every option the Dolphin core offers, written by Porpoise at each launch.\n"
                    "# To change one, copy its line to options.ini and edit the value.\n\n");
    for (const Option &o : h.options)
    {
        std::fprintf(f, "# %s\n# choices:", o.description.c_str());
        for (const std::string &c : o.choices)
            std::fprintf(f, " %s |", c.c_str());
        std::fprintf(f, "\n%s = %s\n\n", o.key.c_str(), o.default_value.c_str());
    }
    std::fclose(f);
}

void define_options_v2(const retro_core_options_v2 *opts)
{
    h.options.clear();
    if (!opts || !opts->definitions)
        return;
    for (const retro_core_option_v2_definition *d = opts->definitions; d->key; ++d)
    {
        Option o;
        o.key = d->key;
        o.description = d->desc ? d->desc : "";
        for (unsigned i = 0; i < RETRO_NUM_CORE_OPTION_VALUES_MAX && d->values[i].value; ++i)
            o.choices.emplace_back(d->values[i].value);
        o.default_value = d->default_value ? d->default_value
                                           : (o.choices.empty() ? "" : o.choices.front());
        o.value = o.default_value;
        h.options.push_back(std::move(o));
    }
}

void define_options_v1(const retro_core_option_definition *defs)
{
    h.options.clear();
    for (const retro_core_option_definition *d = defs; d && d->key; ++d)
    {
        Option o;
        o.key = d->key;
        o.description = d->desc ? d->desc : "";
        for (unsigned i = 0; i < RETRO_NUM_CORE_OPTION_VALUES_MAX && d->values[i].value; ++i)
            o.choices.emplace_back(d->values[i].value);
        o.default_value = d->default_value ? d->default_value
                                           : (o.choices.empty() ? "" : o.choices.front());
        o.value = o.default_value;
        h.options.push_back(std::move(o));
    }
}

void define_options_v0(const retro_variable *vars)
{
    /* "Description; first|second|third", the first choice being the default. */
    h.options.clear();
    for (const retro_variable *v = vars; v && v->key; ++v)
    {
        Option o;
        o.key = v->key;
        std::string spec = v->value ? v->value : "";
        const auto semi = spec.find(';');
        o.description = trim(spec.substr(0, semi));
        std::string list = semi == std::string::npos ? "" : trim(spec.substr(semi + 1));
        std::size_t start = 0;
        while (start <= list.size() && !list.empty())
        {
            const auto bar = list.find('|', start);
            o.choices.push_back(trim(list.substr(start, bar - start)));
            if (bar == std::string::npos)
                break;
            start = bar + 1;
        }
        o.default_value = o.choices.empty() ? "" : o.choices.front();
        o.value = o.default_value;
        h.options.push_back(std::move(o));
    }
}

void options_defined()
{
    apply_porpoise_defaults();
    apply_user_overrides();
    write_options_reference();
    h.options_updated = true;
    ps5::debug::mark_value("options: defined by the core", static_cast<long long>(h.options.size()));
}

/* ---- logging and perf ------------------------------------------------------------------ */

void core_log(enum retro_log_level level, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if (level >= RETRO_LOG_WARN)
    {
        /* Kept for a game that doesn't start: Dolphin's errors, and its alerts
         * (logged as warnings: "Suppressed popup: caption - text"). */
        va_list copy;
        va_copy(copy, args);
        char line[512];
        std::vsnprintf(line, sizeof line, fmt, copy);
        va_end(copy);
        const char *alert = std::strstr(line, "Suppressed popup: ");
        std::lock_guard<std::mutex> lock(g_reason_lock);
        if (alert && g_first_alert.empty())
            g_first_alert = alert + 18;
        else if (level >= RETRO_LOG_ERROR)
            g_last_error = line;
    }
    if (h.log)
    {
        static const char *const names[] = {"debug", "info", "warn", "error"};
        std::fprintf(h.log, "[%s] ", names[level <= RETRO_LOG_ERROR ? level : RETRO_LOG_ERROR]);
        std::vfprintf(h.log, fmt, args);
        /* Before the game runs, each line is written out at once: a start
         * that hangs (a long download) still leaves its log. */
        if (!h.running)
            std::fflush(h.log);
    }
    va_end(args);
}

retro_time_t perf_time_usec()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return retro_time_t(ts.tv_sec) * 1000000 + ts.tv_nsec / 1000;
}

uint64_t perf_cpu_features()
{
    /* Zen 2: SSE through AVX2. */
    return RETRO_SIMD_SSE | RETRO_SIMD_SSE2 | RETRO_SIMD_SSE3 | RETRO_SIMD_SSSE3 | RETRO_SIMD_SSE4 |
           RETRO_SIMD_SSE42 | RETRO_SIMD_AVX | RETRO_SIMD_AVX2 | RETRO_SIMD_POPCNT | RETRO_SIMD_CMOV;
}

retro_perf_tick_t perf_counter()
{
    return static_cast<retro_perf_tick_t>(perf_time_usec());
}

void perf_register(retro_perf_counter *counter)
{
    if (counter)
        counter->registered = true;
}

void perf_start(retro_perf_counter *counter)
{
    if (counter)
        counter->start = perf_counter();
}

void perf_stop(retro_perf_counter *counter)
{
    if (counter)
    {
        counter->total += perf_counter() - counter->start;
        counter->call_cnt++;
    }
}

void perf_log() {}

/* 4.0: whose controller is in a GameCube port: player 1's port and port 1
 * swap players (Player 1's Port); Wii games as they are. */
int player_of(unsigned port)
{
    if (h.wii.active || h.p1_port == 0)
        return int(port);
    if (int(port) == h.p1_port)
        return 0;
    if (port == 0)
        return h.p1_port;
    return int(port);
}

/* Player 1 last played with the Switch 2's GameCube controller (else the
 * DualSense): it gets the rumble (merge_ns2gc). */
std::atomic<bool> g_p1_on_gc{true};

/* The Switch 2's GameCube controller plays a GameCube controller: in a
 * GameCube game, or a Wii game played with one. */
bool gamecube_controls()
{
    return !h.wii.active || h.wii.controller == porpoise::pad::WiiGameCube;
}

bool set_rumble(unsigned port, enum retro_rumble_effect effect, uint16_t strength)
{
    if (port >= unsigned(porpoise::pad::kMaxPlayers))
        return false;
    /* Player 1 with the Switch 2's GameCube controller plugged in: the rumble
     * goes to whichever of it and the DualSense was played with last (its
     * one motor runs while either of the game's is on), the other is still. */
    if (player_of(port) == 0 && porpoise::ns2gc::connected() && gamecube_controls())
    {
        static std::atomic<std::uint16_t> strong{0}, weak{0};
        (effect == RETRO_RUMBLE_STRONG ? strong : weak) = strength;
        const bool gc = g_p1_on_gc.load();
        porpoise::ns2gc::set_rumble(gc && (strong.load() || weak.load()));
        porpoise::pad::set_rumble(0, effect == RETRO_RUMBLE_STRONG, gc ? 0 : strength);
        if (gc)
            porpoise::pad::set_rumble(0, effect != RETRO_RUMBLE_STRONG, 0);
    }
    else
    {
        porpoise::pad::set_rumble(player_of(port), effect == RETRO_RUMBLE_STRONG, strength);
        if (player_of(port) == 0)
            porpoise::ns2gc::set_rumble(false);
    }
    /* Two-controller play: the Remote's rumble is felt in both hands. */
    if (port == 0 && h.wii.active && h.wii.controller == porpoise::pad::WiiTwoControllers)
        porpoise::pad::set_rumble(1, effect == RETRO_RUMBLE_STRONG, strength);
    return true;
}

/* ---- environment ----------------------------------------------------------------------- */

bool environment(unsigned cmd, void *data)
{
    /* Matched whole: several IDs carry RETRO_ENVIRONMENT_EXPERIMENTAL as part of
     * their value (the Vulkan handshake among them), so the bit must not be
     * stripped before the switch. */
    switch (cmd)
    {
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *static_cast<const char **>(data) = h.paths.system;
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *static_cast<const char **>(data) = h.paths.saves;
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
        *static_cast<const char **>(data) = h.paths.assets;
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        static_cast<retro_log_callback *>(data)->log = core_log;
        return true;
    case RETRO_ENVIRONMENT_GET_PERF_INTERFACE:
    {
        auto *perf = static_cast<retro_perf_callback *>(data);
        perf->get_time_usec = perf_time_usec;
        perf->get_cpu_features = perf_cpu_features;
        perf->get_perf_counter = perf_counter;
        perf->perf_register = perf_register;
        perf->perf_start = perf_start;
        perf->perf_stop = perf_stop;
        perf->perf_log = perf_log;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return true;
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
        *static_cast<unsigned *>(data) = RETRO_HW_CONTEXT_VULKAN;
        return true;
    case RETRO_ENVIRONMENT_SET_HW_RENDER:
    {
        auto *hw = static_cast<retro_hw_render_callback *>(data);
        if (hw->context_type != RETRO_HW_CONTEXT_VULKAN)
            return false;
        h.hw = *hw;
        h.hw_requested = true;
        ps5::debug::mark("core: asked for Vulkan");
        return true;
    }
    case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE:
        h.negotiation =
            static_cast<const retro_hw_render_context_negotiation_interface_vulkan *>(data);
        return true;
    case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE:
    {
        retro_hw_render_interface_vulkan *iface = porpoise::vk::render_interface();
        *static_cast<const retro_hw_render_interface **>(data) =
            reinterpret_cast<const retro_hw_render_interface *>(iface);
        return iface != nullptr;
    }
    case RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT:
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *static_cast<unsigned *>(data) = 2;
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        define_options_v2(static_cast<const retro_core_options_v2 *>(data));
        options_defined();
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        define_options_v1(static_cast<const retro_core_option_definition *>(data));
        options_defined();
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        define_options_v0(static_cast<const retro_variable *>(data));
        options_defined();
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:
    {
        auto *var = static_cast<retro_variable *>(data);
        if (!var || !var->key)
            return false;
        Option *o = find_option(var->key);
        if (!o)
        {
            var->value = nullptr;
            return false;
        }
        var->value = o->value.c_str();
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *static_cast<bool *>(data) = h.options_updated;
        h.options_updated = false;
        return true;
    case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
        static_cast<retro_rumble_interface *>(data)->set_rumble_state = set_rumble;
        return true;
    case RETRO_ENVIRONMENT_GET_MICROPHONE_INTERFACE:
    {
        /* The DualSense's microphone (porpoise_mic): the GameCube Microphone
         * and the Wii Speak, when the core's options turn them on. */
        auto *mic = static_cast<retro_microphone_interface *>(data);
        if (!mic || mic->interface_version != RETRO_MICROPHONE_INTERFACE_VERSION)
            return false;
        porpoise::mic::fill(*mic);
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SENSOR_INTERFACE:
    {
        /* The DualSense's gyroscope and accelerometer, as the Wii Remote's. */
        auto *sensors = static_cast<retro_sensor_interface *>(data);
        sensors->set_sensor_state = set_sensor_state;
        sensors->get_sensor_input = get_sensor_input;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    {
        const auto *geometry = static_cast<const retro_game_geometry *>(data);
        if (geometry->aspect_ratio > 0.0f)
            h.aspect = geometry->aspect_ratio;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
    {
        const auto *av = static_cast<const retro_system_av_info *>(data);
        if (av->geometry.aspect_ratio > 0.0f)
            h.aspect = av->geometry.aspect_ratio;
        if (av->timing.sample_rate > 1000.0)
        {
            h.sample_rate = av->timing.sample_rate;
            porpoise::audio::set_source_rate(h.sample_rate);
        }
        h.fps = av->timing.fps;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE:
        *static_cast<float *>(data) = static_cast<float>(porpoise::vk::refresh_hz());
        return true;
    case RETRO_ENVIRONMENT_GET_JIT_CAPABLE:
        *static_cast<bool *>(data) = true;
        return true;
    case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
        *static_cast<bool *>(data) = false;
        return true;
    case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
        *static_cast<unsigned *>(data) = 1;
        return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE:
    {
        const auto *msg = static_cast<const retro_message *>(data);
        if (msg && msg->msg)
        {
            /* Porpoise's core: a step's measure while the game loads
             * ("porpoise-progress:<done>/<total>"), for the launch screen. */
            unsigned done = 0, total = 0;
            /* 3.1: the core's boot is taking a while (the disc's
             * achievements hash): the launch screen goes on moving. */
            if (std::strcmp(msg->msg, "porpoise-wait") == 0)
            {
                if (g_run_hooks && g_run_hooks->progress)
                    g_run_hooks->progress("Starting the game", 0, 0, g_run_hooks->user);
                return true;
            }
            if (std::strncmp(msg->msg, "porpoise-update-failed", 22) == 0)
            {
                g_update_failed = true;
                core_log(RETRO_LOG_WARN, "message: %s\n", msg->msg);
                return true;
            }
            if (std::sscanf(msg->msg, "porpoise-progress:%u/%u", &done, &total) == 2 && total > 0)
            {
                char line[80];
                std::snprintf(line, sizeof line, "core: Wii System Update, title %u of %u", done, total);
                ps5::debug::mark(line);
                if (g_run_hooks && g_run_hooks->progress)
                    g_run_hooks->progress("Updating the Wii's system software", done, total, g_run_hooks->user);
                return true;
            }
            core_log(RETRO_LOG_INFO, "message: %s\n", msg->msg);
        }
        return true;
    }
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
    {
        const auto *msg = static_cast<const retro_message_ext *>(data);
        if (msg && msg->msg)
            core_log(RETRO_LOG_INFO, "message: %s\n", msg->msg);
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:
    {
        /* Games on a network share (porpoise_netfs): the core reads /net/
         * paths through this; every other file it opens is its own, as before. */
        auto *info = static_cast<retro_vfs_interface_info *>(data);
        const retro_vfs_interface *iface = porpoise::netfs::vfs_interface();
        if (!info || !iface || info->required_interface_version > 4)
            return false;
        info->required_interface_version = 4;
        info->iface = const_cast<retro_vfs_interface *>(iface);
        return true;
    }
    /* Accepted and not needed by Porpoise yet. */
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        return true;
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
        /* 3.1: kept for the in-game menu's Change Disc. */
        if (data)
        {
            h.disks = *static_cast<const retro_disk_control_ext_callback *>(data);
            h.have_disks = h.disks.get_num_images && h.disks.set_eject_state && h.disks.set_image_index;
        }
        return true;
    /* Refused on purpose: the core then pushes audio itself each frame, paced
     * by the display (SET_AUDIO_CALLBACK, SET_FRAME_TIME_CALLBACK), and leaves out the motion sensor and
     * microphone. EXEC_MEM_ALLOC is an iOS path; on PS5 the core takes JIT
     * memory from the platform layer directly. */
    default:
        return false;
    }
}

/* ---- frame callbacks ------------------------------------------------------------------- */

void video_refresh(const void *data, unsigned width, unsigned height, std::size_t)
{
    /* The frame is presented by the run loop after retro_run returns, once per
     * iteration; data == nullptr is a duplicate frame and keeps the last one. */
    if (data == RETRO_HW_FRAME_BUFFER_VALID && width && height)
    {
        h.last_width = width;
        h.last_height = height;
        h.have_frame = true;
        h.new_frame = true;
    }
}

void audio_sample(int16_t left, int16_t right)
{
    const int16_t frame[2] = {left, right};
    ++h.samples_in;
    porpoise::audio::push(frame, 1);
}

std::size_t audio_batch(const int16_t *data, std::size_t frames)
{
    h.samples_in += frames;
    porpoise::audio::push(data, frames);
    return frames;
}

/* ---- the Wii Remote's motion ---------------------------------------------------------- */

/* The DualSense whose motion a port's sensors give. In two-controller play
 * port 0 is the first (the Remote) and port 1 the second: the Nunchuk, which
 * Porpoise binds to that port's accelerometer (nunchuk_motion_step). */
int remote_player(unsigned port)
{
    if (h.wii.controller == porpoise::pad::WiiTwoControllers)
        return port <= 1 ? int(port) : -1;
    return int(port);
}

bool RETRO_CALLCONV set_sensor_state(unsigned port, enum retro_sensor_action action, unsigned)
{
    const bool enable = action == RETRO_SENSOR_ACCELEROMETER_ENABLE || action == RETRO_SENSOR_GYROSCOPE_ENABLE;
    if (!enable)
        return true;
    const bool ok = h.wii.active && h.wii.motion && port < unsigned(porpoise::pad::kMaxPlayers);
    if (h.log)
        std::fprintf(h.log, "[porpoise] motion sensor %s for port %u: %s\n",
                     action == RETRO_SENSOR_ACCELEROMETER_ENABLE ? "accelerometer" : "gyroscope", port,
                     ok ? "on" : "off");
    return ok;
}

/* 4.0: the controllers as this frame's poll read them, for the core's input
 * calls on the emulated CPU's thread (many thousand a second for a Wii
 * Remote): two copies, the one being written never the one being read, so no
 * lock and no copy per call. */
porpoise::pad::State g_frame_pads[2][porpoise::pad::kMaxPlayers];
std::atomic<int> g_frame_pads_now{0};

/* The Switch 2's GameCube controller (porpoise_ns2gc.hpp), plugged in, is
 * player 1's beside the DualSense: both work, a button pressed on either is
 * pressed, and each stick and trigger is whichever is pushed further. */
void merge_ns2gc(porpoise::pad::State &p)
{
    const porpoise::ns2gc::Pad gc = porpoise::ns2gc::pad();
    static bool was_connected = false;
    if (!gc.connected || !gamecube_controls())
    {
        was_connected = false;
        return;
    }
    /* Which of the two player 1 is playing with: the one whose buttons
     * change or whose stick or trigger is pushed (plugged in: this one). */
    {
        static std::uint32_t gc_prev = 0, ds_prev = 0;
        auto pushed = [](int v) { return std::abs(v) > 16000; };
        const bool gc_moved = (gc.buttons & ~gc_prev) || pushed(gc.lx) || pushed(gc.ly) || pushed(gc.rx) ||
                              pushed(gc.ry) || gc.lt > 16000 || gc.rt > 16000;
        const bool ds_moved = (p.buttons & ~ds_prev) || pushed(p.left_x) || pushed(p.left_y) ||
                              pushed(p.right_x) || pushed(p.right_y) || p.l2 > 16000 || p.r2 > 16000;
        bool on_gc = g_p1_on_gc.load();
        if (!was_connected || gc_moved)
            on_gc = true;
        else if (ds_moved)
            on_gc = false;
        if (on_gc != g_p1_on_gc.load())
        {
            g_p1_on_gc = on_gc;
            /* the one put down stops at once */
            if (on_gc)
            {
                porpoise::pad::set_rumble(0, true, 0);
                porpoise::pad::set_rumble(0, false, 0);
            }
            else
                porpoise::ns2gc::set_rumble(false);
        }
        gc_prev = gc.buttons;
        ds_prev = p.buttons;
        was_connected = true;
    }
    namespace n = porpoise::ns2gc;
    static const struct
    {
        std::uint32_t button;
        unsigned retro;
    } kMap[] = {
        /* as the core reads a GameCube controller (porpoise_pad.cpp, kGcRetro) */
        {n::GcA, RETRO_DEVICE_ID_JOYPAD_A},         {n::GcB, RETRO_DEVICE_ID_JOYPAD_B},
        {n::GcX, RETRO_DEVICE_ID_JOYPAD_X},         {n::GcY, RETRO_DEVICE_ID_JOYPAD_Y},
        {n::GcZ, RETRO_DEVICE_ID_JOYPAD_R},         {n::GcL, RETRO_DEVICE_ID_JOYPAD_L2},
        {n::GcR, RETRO_DEVICE_ID_JOYPAD_R2},        {n::GcStart, RETRO_DEVICE_ID_JOYPAD_START},
        {n::GcUp, RETRO_DEVICE_ID_JOYPAD_UP},       {n::GcDown, RETRO_DEVICE_ID_JOYPAD_DOWN},
        {n::GcLeft, RETRO_DEVICE_ID_JOYPAD_LEFT},   {n::GcRight, RETRO_DEVICE_ID_JOYPAD_RIGHT},
    };
    for (const auto &m : kMap)
        if (gc.buttons & m.button)
            p.joypad = std::uint16_t(p.joypad | (1u << m.retro));
    auto further = [](std::int16_t a, std::int16_t b) { return std::abs(int(b)) > std::abs(int(a)) ? b : a; };
    p.left_x = further(p.left_x, gc.lx);
    p.left_y = further(p.left_y, gc.ly);
    p.right_x = further(p.right_x, gc.rx);
    p.right_y = further(p.right_y, gc.ry);
    p.l2 = std::max(p.l2, gc.lt);
    p.r2 = std::max(p.r2, gc.rt);
    p.connected = true;
}

void publish_frame_pads()
{
    const int next = 1 - g_frame_pads_now.load(std::memory_order_relaxed);
    for (int player = 0; player < porpoise::pad::kMaxPlayers; ++player)
        g_frame_pads[next][player] = porpoise::pad::snapshot(player);
    merge_ns2gc(g_frame_pads[next][0]);
    g_frame_pads_now.store(next, std::memory_order_release);
}

const porpoise::pad::State &frame_pad(int player)
{
    return g_frame_pads[g_frame_pads_now.load(std::memory_order_acquire)][player];
}

float RETRO_CALLCONV get_sensor_input(unsigned port, unsigned id)
{
    if (!h.wii.active || !h.wii.motion || port >= unsigned(porpoise::pad::kMaxPlayers))
        return 0.0f;
    const int player = remote_player(port);
    if (player < 0)
        return 0.0f;
    const porpoise::pad::Motion &m = frame_pad(player).motion;
    switch (id)
    {
    case RETRO_SENSOR_ACCELEROMETER_X: return m.accel[0];
    case RETRO_SENSOR_ACCELEROMETER_Y: return m.accel[1];
    case RETRO_SENSOR_ACCELEROMETER_Z: return m.accel[2];
    case RETRO_SENSOR_GYROSCOPE_X: return m.gyro[0];
    case RETRO_SENSOR_GYROSCOPE_Y: return m.gyro[1];
    case RETRO_SENSOR_GYROSCOPE_Z: return m.gyro[2];
    default: return 0.0f;
    }
}

/* ---- real Wii Remotes (a DolphinBar) --------------------------------------------------
 * The Dolphin core's Wii Remote scanner and each Remote's threads call the
 * title's hidapi (porpoise_usbhid.cpp). What it finds and loses goes to
 * /data/porpoise/dolphinbar.log (porpoise_dolphinbar.cpp), a file of its own:
 * those threads outlive the core's log. /data/porpoise/dolphinbar-detach (an
 * empty file the player makes) has the console's driver detached from the
 * DolphinBar's interfaces, should it have them; nothing does that unasked. */
void dolphinbar_log(const char *line) { porpoise::dolphinbar::log("%s", line); }

void start_dolphinbar_log()
{
    porpoise::dolphinbar::route_usbhid_log();
    const bool detach = access(PORPOISE_DATA "/dolphinbar-detach", F_OK) == 0;
    porpoise::usbhid::set_detach_kernel_driver(detach);
    dolphinbar_log(detach ? "Porpoise: real Wii Remotes; the console's driver is detached from the DolphinBar "
                            "(dolphinbar-detach)"
                          : "Porpoise: real Wii Remotes");
}
/* Which device each port gets: GameCube pads, or the chosen Wii controller. */
unsigned port_device(int)
{
    if (!h.wii.active)
        return RETRO_DEVICE_JOYPAD;
    if (h.wii.real_remotes)
        return porpoise::pad::kRealWiiRemoteDevice;
    return h.wii.motion_plus ? porpoise::pad::wii_device_with_motion_plus(h.wii.controller)
                             : porpoise::pad::wii_device(h.wii.controller);
}

/* A line for the debug folder's motion.csv: what the controller felt and
 * what the game was given. */
void log_motion()
{
    if (!h.motion_log || h.motion_log_bytes > (40LL << 20))
        return;
    const porpoise::pad::State p = porpoise::pad::snapshot(0);
    const porpoise::pad::Motion &m = p.motion;
    const porpoise::pad::Motion n2 = porpoise::pad::snapshot(1).motion; /* the second controller */
    porpoise::aim::Dot dots[2];
    porpoise::aim::sensor_bar(m.aim_x, m.aim_y, m.roll, dots, h.wii.range, h.wii.bar_above);
    auto cam = [](const porpoise::aim::Dot &d, bool y) {
        return d.visible ? static_cast<int>((y ? d.y * 767 : d.x * 1023) + 0.5f) : -1;
    };
    const int n = std::fprintf(
        h.motion_log,
        "%llu,%u,%08x,%04x,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%d,%.3f,%.3f,%.1f,%.4f,%.4f,%.4f,%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d\n",
        h.frame_number, m.samples, p.buttons, p.joypad, m.raw_accel[0], m.raw_accel[1], m.raw_accel[2],
        m.raw_gyro[0], m.raw_gyro[1], m.raw_gyro[2], m.orientation[0], m.orientation[1], m.orientation[2],
        m.orientation[3], m.accel[0], m.accel[1], m.accel[2], m.gyro[0], m.gyro[1], m.gyro[2], m.pointer_x,
        m.pointer_y, m.touching ? 1 : 0, m.touch_x, m.touch_y, m.shaking ? 1 : 0, cam(dots[0], false),
        cam(dots[0], true), cam(dots[1], false), cam(dots[1], true), m.aim_x, m.aim_y, m.roll * 57.29578f,
        m.gyro_bias[0], m.gyro_bias[1], m.gyro_bias[2], m.pose, n2.valid ? 1 : 0, n2.raw_accel[0], n2.raw_accel[1],
        n2.raw_accel[2], n2.accel[0], n2.accel[1], n2.accel[2], n2.pose);
    if (n > 0)
        h.motion_log_bytes += n;
}

/* ---- motion for the Nunchuk devices ---------------------------------------------------
 *
 * Dolphin's libretro core binds the DualSense's motion to the Remote only when
 * the Remote is alone or sideways: with the Nunchuk plugged in (Remote +
 * Nunchuk, two-controller play) the Remote gets no accelerometer or gyroscope
 * at all, and the Nunchuk never gets its accelerometer. Dolphin itself can do
 * both; they are only unbound. The core writes its controller set-up to
 * User/Config/WiimoteNew.ini each time a Remote is plugged in, and with
 * dolphin_save_load_settings on it reads that file back instead. So, once the
 * game runs: add the bindings to each Nunchuk Remote's section of the core's
 * own file - the Remote's motion from its port's sensor, and in two-controller
 * play the Nunchuk's from port 1's (the second DualSense, in the Nunchuk's
 * axes) - turn the option on and plug the Remotes in again. Any change of
 * controller turns it off and plugs them in again first, then starts over. */
std::string wiimote_ini_path()
{
    return std::string(h.paths.saves) + "/User/Config/WiimoteNew.ini";
}

bool nunchuk_device(int controller)
{
    return controller == porpoise::pad::WiiRemoteNunchuk || controller == porpoise::pad::WiiTwoControllers;
}

/* The lines binding a motion group to a port's sensor, as the core writes the
 * Remote's own (Input.cpp: Up AccelZ+, Left AccelX+, Forward AccelY-;
 * Pitch Up GyroX-, Roll Left GyroY+, Yaw Left GyroZ+). */
void motion_lines(std::vector<std::string> &out, const std::string &prefix, int port, bool gyro)
{
    const std::string dev = "`Libretro/" + std::to_string(port) + "/Sensor:";
    static const char *const kAccel[6][2] = {{"Up", "AccelZ+"},   {"Down", "AccelZ-"},    {"Left", "AccelX+"},
                                             {"Right", "AccelX-"}, {"Forward", "AccelY-"}, {"Backward", "AccelY+"}};
    static const char *const kGyro[6][2] = {{"Pitch Up", "GyroX-"},  {"Pitch Down", "GyroX+"}, {"Roll Left", "GyroY+"},
                                            {"Roll Right", "GyroY-"}, {"Yaw Left", "GyroZ+"},   {"Yaw Right", "GyroZ-"}};
    for (const auto &a : kAccel)
        out.push_back(prefix + "IMUAccelerometer/" + a[0] + " = " + dev + a[1] + "`");
    if (gyro)
        for (const auto &g : kGyro)
            out.push_back(prefix + "IMUGyroscope/" + g[0] + " = " + dev + g[1] + "`");
}

/* Returns how many Remotes' sections were given motion (0: not ready yet). */
int add_nunchuk_motion(bool two)
{
    std::ifstream in(wiimote_ini_path());
    if (!in)
        return 0;
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);)
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }
    in.close();
    auto starts = [](const std::string &l, const char *p) { return l.compare(0, std::strlen(p), p) == 0; };
    std::vector<std::string> out;
    int patched = 0;
    std::size_t i = 0;
    while (i < lines.size())
    {
        /* Copy anything outside a [WiimoteN] section as it is. */
        int port = -1;
        if (lines[i].size() == 10 && starts(lines[i], "[Wiimote") && lines[i][8] >= '1' && lines[i][8] <= '4' &&
            lines[i][9] == ']')
            port = lines[i][8] - '1';
        out.push_back(lines[i++]);
        if (port < 0)
            continue;
        std::vector<std::string> raw;
        bool nunchuk = false;
        while (i < lines.size() && !(starts(lines[i], "[")))
        {
            raw.push_back(lines[i++]);
            if (starts(raw.back(), "Extension = ") && raw.back().find("Nunchuk") != std::string::npos)
                nunchuk = true;
        }
        std::vector<std::string> section;
        for (const std::string &l : raw)
            /* A Remote with the Nunchuk: its motion lines (ours from before)
             * are written again below. Other Remotes keep the core's. */
            if (!nunchuk || !(starts(l, "IMUAccelerometer/") || starts(l, "IMUGyroscope/") ||
                              starts(l, "Nunchuk/IMUAccelerometer/") ||
                              starts(l, "Extension/Nunchuk/IMUAccelerometer/") /* test 5-6's wrong name */))
                section.push_back(l);
        while (!section.empty() && section.back().empty())
            section.pop_back();
        if (nunchuk)
        {
            motion_lines(section, "", port, true);
            /* An attachment's groups go under its own name (Attachments::SaveConfig:
             * base + "Nunchuk/"), not under "Extension/". */
            if (two && port == 0)
                motion_lines(section, "Nunchuk/", 1, false);
            ++patched;
        }
        out.insert(out.end(), section.begin(), section.end());
        out.push_back("");
    }
    if (patched == 0)
        return 0;
    const std::string tmp = wiimote_ini_path() + ".porpoise";
    {
        std::ofstream o(tmp, std::ios::trunc);
        if (!o)
            return 0;
        for (const std::string &l : out)
            o << l << "\n";
        if (!o.good())
            return 0;
    }
    if (std::rename(tmp.c_str(), wiimote_ini_path().c_str()) != 0)
        return 0;
    /* A copy in the debug folder, to see what the core was given. */
    if (!h.debug_dir.empty())
    {
        std::ofstream copy(h.debug_dir + "/WiimoteNew.ini", std::ios::trunc);
        for (const std::string &l : out)
            copy << l << "\n";
    }
    return patched;
}

void set_core_option(const char *key, const char *value)
{
    if (Option *o = find_option(key))
    {
        o->value = value;
        h.options_updated = true;
    }
}

/* Called once a frame; leave = a change of controller: start over. */
void nunchuk_motion_step(bool leave = false)
{
    const bool want = h.wii.active && h.wii.motion && nunchuk_device(h.wii.controller);
    if (leave && (h.nunchuk_stage == 1 || h.nunchuk_stage == 2))
    {
        set_core_option("dolphin_save_load_settings", "disabled");
        h.nunchuk_stage = 3;
        h.nunchuk_frame = h.frame_number + 2;
        porpoise::pad::set_nunchuk_motion(false);
        return;
    }
    switch (h.nunchuk_stage)
    {
    case 0: /* the core's own set-up is in the file: add the motion */
        if (want && h.frame_number >= h.nunchuk_frame)
        {
            const bool two = h.wii.controller == porpoise::pad::WiiTwoControllers;
            if (const int n = add_nunchuk_motion(two))
            {
                set_core_option("dolphin_save_load_settings", "enabled");
                h.nunchuk_stage = 1;
                h.nunchuk_frame = h.frame_number + 2; /* the core reads options as a frame starts */
                if (h.log)
                    std::fprintf(h.log, "[porpoise] motion for %d Remote%s with the Nunchuk%s\n", n, n == 1 ? "" : "s",
                                 two ? ", and the Nunchuk's from the second controller" : "");
            }
            else if (++h.nunchuk_tries > 20)
            {
                h.nunchuk_stage = 4;
                if (h.log)
                {
                    struct stat st{};
                    const bool there = stat(wiimote_ini_path().c_str(), &st) == 0;
                    std::fprintf(h.log, "[porpoise] Nunchuk motion left off: %s %s\n", wiimote_ini_path().c_str(),
                                 there ? "has no Remote with the Nunchuk" : "isn't there");
                }
            }
            else
                h.nunchuk_frame = h.frame_number + 30;
        }
        break;
    case 1: /* plug the Remotes in again: the core reads the file */
        if (h.frame_number >= h.nunchuk_frame)
        {
            for (int port = 0; port < porpoise::pad::kMaxPlayers; ++port)
                if (h.plugged[port])
                    h.api.set_controller_port_device(unsigned(port), port_device(port));
            h.nunchuk_stage = 2;
            porpoise::pad::set_nunchuk_motion(true);
            ps5::debug::mark("core: motion on for the Remote with the Nunchuk");
        }
        break;
    case 2: /* in use */
        if (!want)
            nunchuk_motion_step(true);
        break;
    case 3: /* the option is off again: every port gets the core's own set-up */
        if (h.frame_number >= h.nunchuk_frame)
        {
            for (int port = 0; port < porpoise::pad::kMaxPlayers; ++port)
                if (h.plugged[port])
                    h.api.set_controller_port_device(unsigned(port), port_device(port));
            h.nunchuk_stage = 0;
            h.nunchuk_tries = 0;
            h.nunchuk_frame = h.frame_number + 10;
        }
        break;
    default: /* gave up until the controller changes */
        if (!want)
        {
            h.nunchuk_stage = 0;
            h.nunchuk_tries = 0;
        }
        break;
    }
}

void input_poll()
{
    /* The pad is read once per frame by the run loop, before retro_run. */
}


int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (port >= unsigned(porpoise::pad::kMaxPlayers))
        return 0;
    const int player = player_of(port);
    if (player == 0 && h.hold_input)
        return 0;
    const bool two = h.wii.active && h.wii.controller == porpoise::pad::WiiTwoControllers;
    if (two && port > 0)
        return 0; /* both controllers are player 1's Remote and Nunchuk */
    const porpoise::pad::State &pad = frame_pad(player);
    /* With two controllers the second is the Nunchuk: its buttons and stick. */
    const porpoise::pad::State &nunchuk_pad = frame_pad(1);
    const std::uint16_t joypad = std::uint16_t(pad.joypad | (two ? nunchuk_pad.nunchuk : 0));
    switch (device)
    {
    case RETRO_DEVICE_JOYPAD:
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
            return static_cast<int16_t>(joypad);
        return id < 16 && (joypad & (1u << id)) ? 1 : 0;
    case RETRO_DEVICE_POINTER:
    {
        /* The Wii Remote's camera: the two sensor-bar lights it sees when it
         * points where Porpoise's pointer is (dolphin_ir_passthrough), as
         * indices 0 and 1, x and y over 0..0x7fff. Only this, so Dolphin's own
         * gyro cursor can't add a second, different aim on top. */
        if (index > 1 || !h.wii.active)
            return 0;
        porpoise::aim::Dot dots[2];
        porpoise::aim::sensor_bar(pad.motion.aim_x, pad.motion.aim_y, pad.motion.roll, dots, h.wii.range, h.wii.bar_above);
        const porpoise::aim::Dot &dot = dots[index];
        if (!dot.visible)
            return 0;
        if (id == RETRO_DEVICE_ID_POINTER_X)
            return static_cast<int16_t>(std::lround(dot.x * 32767.0f));
        if (id == RETRO_DEVICE_ID_POINTER_Y)
            return static_cast<int16_t>(std::lround(dot.y * 32767.0f));
        if (id == RETRO_DEVICE_ID_POINTER_PRESSED)
            return 1;
        return 0;
    }
    case RETRO_DEVICE_ANALOG:
        if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT && two)
            return id == RETRO_DEVICE_ID_ANALOG_X ? nunchuk_pad.left_x : nunchuk_pad.left_y;
        {
            /* A stick turned around, for a game that moves the other way. */
            auto turned = [](std::int16_t v, int invert, bool x) -> std::int16_t {
                const bool flip = x ? (invert & 2) != 0 : (invert & 1) != 0;
                return !flip ? v : v == -32768 ? std::int16_t(32767) : std::int16_t(-v);
            };
            const bool x = id == RETRO_DEVICE_ID_ANALOG_X;
            if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
            {
                if (h.stick_power == 1.0f && h.stick_dead == 0.0f)
                    return turned(x ? pad.left_x : pad.left_y, h.invert_main, x);
                /* Its feel: the push's length past the dead zone, on a curve
                 * (a small push moves less, a full one still all the way),
                 * in the same direction. */
                const float sx = pad.left_x / 32767.0f, sy = pad.left_y / 32767.0f;
                const float len = std::min(1.0f, std::sqrt(sx * sx + sy * sy));
                float out = 0.0f;
                if (len > h.stick_dead)
                    out = std::pow((len - h.stick_dead) / (1.0f - h.stick_dead), h.stick_power);
                const float k = len > 0.0f ? out / len : 0.0f;
                const float v = std::clamp((x ? sx : sy) * k, -1.0f, 1.0f);
                return turned(std::int16_t(std::lround(v * 32767.0f)), h.invert_main, x);
            }
            if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT)
                return turned(x ? pad.right_x : pad.right_y, h.invert_c, x);
        }
        if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON)
        {
            if (id == RETRO_DEVICE_ID_JOYPAD_L2)
                return pad.l2;
            if (id == RETRO_DEVICE_ID_JOYPAD_R2)
                return pad.r2;
            return id < 16 && (joypad & (1u << id)) ? 0x7fff : 0;
        }
        return 0;
    default:
        return 0;
    }
}

using porpoise::pacer::now_ns;
long long monotonic_ns()
{
    return now_ns();
}

/* ---- loading --------------------------------------------------------------------------- */

template <typename F> bool symbol(F &fn, const char *name)
{
    fn = reinterpret_cast<F>(ps5_core_dlsym(h.library, name));
    if (!fn)
        ps5::debug::mark(name);
    return fn != nullptr;
}

bool load_core()
{
    h.library = ps5_core_dlopen(h.paths.core, 0);
    if (!h.library)
    {
        const char *error = ps5_core_dlerror();
        ps5::debug::mark(error ? error : "core: load failed");
        return false;
    }
    bool ok = true;
    ok &= symbol(h.api.init, "retro_init");
    ok &= symbol(h.api.deinit, "retro_deinit");
    ok &= symbol(h.api.get_system_info, "retro_get_system_info");
    ok &= symbol(h.api.get_system_av_info, "retro_get_system_av_info");
    ok &= symbol(h.api.set_environment, "retro_set_environment");
    ok &= symbol(h.api.set_video_refresh, "retro_set_video_refresh");
    ok &= symbol(h.api.set_audio_sample, "retro_set_audio_sample");
    ok &= symbol(h.api.set_audio_sample_batch, "retro_set_audio_sample_batch");
    ok &= symbol(h.api.set_input_poll, "retro_set_input_poll");
    ok &= symbol(h.api.set_input_state, "retro_set_input_state");
    ok &= symbol(h.api.set_controller_port_device, "retro_set_controller_port_device");
    ok &= symbol(h.api.load_game, "retro_load_game");
    ok &= symbol(h.api.unload_game, "retro_unload_game");
    ok &= symbol(h.api.run, "retro_run");
    ok &= symbol(h.api.reset, "retro_reset");
    ok &= symbol(h.api.serialize_size, "retro_serialize_size");
    ok &= symbol(h.api.serialize, "retro_serialize");
    ok &= symbol(h.api.unserialize, "retro_unserialize");
    return ok;
}

/* The core is unmapped after each game (once its threads have ended), so the
 * next game starts with Dolphin's state fresh. */
void unload_core()
{
    if (h.library)
        ps5_core_dlclose(h.library);
    h.library = nullptr;
}

bool read_whole_file(const char *path, std::vector<unsigned char> &out)
{
    return porpoise::read_whole(path, out);
}
} // namespace

/* libretro-common's rtime_localtime, which the core imports from its host
 * (RetroArch provided it): a thread-safe localtime. */
#ifndef PORPOISE_DESKTOP
extern "C" int ps5_sampler_hot(unsigned long long *rips, unsigned *counts, int max);
#endif

extern "C" struct tm *rtime_localtime(const time_t *timep, struct tm *result)
{
    return localtime_r(timep, result);
}

namespace porpoise::core
{
void set_option(const char *key, const char *value)
{
    if (Option *o = find_option(key))
    {
        o->value = value;
        h.options_updated = true;
    }
}

void set_wii(const porpoise::pad::WiiConfig &config)
{
    if (!h.wii.active)
        return;
    const bool device_changed = config.controller != h.wii.controller || config.motion_plus != h.wii.motion_plus ||
                                config.real_remotes != h.wii.real_remotes;
    if (config.real_remotes && !h.wii.real_remotes)
        start_dolphinbar_log();
    /* (the same device for the GameCube controller either way: re-plugged all the same, harmlessly) */
    h.wii = config;
    h.wii.active = true;
    porpoise::pad::set_wii(h.wii);
    h.wii_changed |= device_changed;
    if (h.log)
        std::fprintf(h.log,
                     "[porpoise] Wii controller now %d%s, pointer %d, grip %d, speed %d, screen %.1f x %.1f deg, "
                     "smoothing %d, reach %d%%\n",
                     h.wii.controller, h.wii.motion_plus ? " with MotionPlus" : "", h.wii.pointer, h.wii.grip,
                     h.wii.speed, h.wii.half_x * 57.29578f,
                     h.wii.half_y * 57.29578f, h.wii.smooth, h.wii.reach);
}

int fast_forward()
{
    return h.fast_forward;
}

unsigned disc_count()
{
    return h.have_disks && h.running ? h.disks.get_num_images() : 0;
}

unsigned disc_index()
{
    return h.have_disks && h.disks.get_image_index ? h.disks.get_image_index() : 0;
}

unsigned own_disc_count()
{
    if (!h.have_disks || !h.running)
        return 0;
    if (h.own_discs == 0)
        h.own_discs = h.disks.get_num_images(); /* before any other game's disc was added */
    return h.own_discs;
}

std::string disc_path_in()
{
    const unsigned i = disc_index();
    const unsigned own = own_disc_count();
    return i >= own && i - own < h.added_discs.size() ? h.added_discs[i - own] : std::string();
}

void choose_disc(int index, const std::string &path)
{
    h.next_disc = index;
    h.next_disc_path = path;
}

void set_fast_forward(int factor)
{
    h.fast_forward = std::clamp(factor, 1, 8);
}

bool serialize(std::vector<unsigned char> &data)
{
    if (!h.running || !h.api.serialize_size || !h.api.serialize)
        return false;
    /* The player saved by hand before a slot chosen in Details had loaded:
     * that slot no longer loads over what they're doing. */
    h.pending_state.clear();
    timespec t0{}, t1{}, t2{};
    clock_gettime(CLOCK_MONOTONIC, &t0);
    const std::size_t size = h.api.serialize_size();
    clock_gettime(CLOCK_MONOTONIC, &t1);
    if (size == 0)
        return false;
    data.resize(size);
    const bool ok = h.api.serialize(data.data(), size);
    clock_gettime(CLOCK_MONOTONIC, &t2);
    auto ms = [](const timespec &a, const timespec &b) {
        return (long long)(b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000;
    };
    char line[160];
    std::snprintf(line, sizeof line, "core: state %zu MB, measured in %lld ms, taken in %lld ms", size >> 20,
                  ms(t0, t1), ms(t1, t2));
    ps5::debug::mark(line);
    if (!ok)
        ps5::debug::mark("core: the game would not save its state");
    return ok;
}

bool save_state(const char *path)
{
    std::vector<unsigned char> data;
    if (!serialize(data))
        return false;
    const std::size_t size = data.size();
    /* All of it on disk before it takes the old one's place: a full disk
     * leaves the old state as it was. */
    if (!porpoise::write_whole(path, data.data(), size))
        return false;
    char line[160];
    std::snprintf(line, sizeof line, "core: state saved (%zu MB)", size >> 20);
    ps5::debug::mark(line);
    return true;
}

bool load_state(const char *path)
{
    if (!h.running || !h.api.unserialize)
        return false;
    h.pending_state.clear(); /* an explicit load wins over the one from Details */
    std::vector<unsigned char> data;
    if (!read_whole_file(path, data) || data.empty())
        return false;
    const bool ok = h.api.unserialize(data.data(), data.size());
    ps5::debug::mark(ok ? "core: state loaded" : "core: the game would not take the state");
    porpoise::audio::flush();
    return ok;
}

bool capture_picture(std::vector<unsigned char> &rgba, unsigned &width, unsigned &height)
{
    if (!h.running || !h.have_frame)
        return false;
    width = h.last_width;
    height = h.last_height;
    return porpoise::vk::capture_picture(width, height, rgba);
}

float picture_aspect()
{
    return h.aspect;
}

void set_stick_shape(int sensitivity, int deadzone)
{
    static constexpr float kPower[4] = {1.0f, 1.4f, 1.8f, 2.3f};
    static constexpr float kDead[4] = {0.0f, 0.06f, 0.12f, 0.20f};
    h.stick_power = kPower[std::clamp(sensitivity, 0, 3)];
    h.stick_dead = kDead[std::clamp(deadzone, 0, 3)];
}

void set_player1_port(int port)
{
    h.p1_port_want.store(std::clamp(port, 0, porpoise::pad::kMaxPlayers - 1));
}

void set_stick_invert(int main_stick, int c_stick)
{
    h.invert_main = main_stick;
    h.invert_c = c_stick;
}

void set_picture(int filter, float strength)
{
    h.filter = filter;
    h.strength = strength;
}

Failure last_failure()
{
    return g_failure;
}

std::string last_failure_reason()
{
    return g_failure_reason;
}

bool take_update_failed()
{
    const bool r = g_update_failed;
    g_update_failed = false;
    return r;
}

Exit run_game(const char *game_path, const Paths &paths, const Hooks &hooks, const Playback &playback)
{
    const long long launch_ns = now_ns(); /* for the trace: how long each step of a launch takes */
    auto since_launch = [&](const char *what) { ps5::debug::mark_value(what, (now_ns() - launch_ns) / 1000000); };
    /* A fresh start: the previous game's core state is gone with its core. */
    h.paths = paths;
#ifdef PORPOISE_DESKTOP
    /* Dolphin is given whole paths: the program's folder, wherever it was started from. */
    static std::string full_system, full_saves, full_assets;
    full_system = porpoise::platform::absolute(paths.system);
    full_saves = porpoise::platform::absolute(paths.saves);
    full_assets = porpoise::platform::absolute(paths.assets);
    h.paths.system = full_system.c_str();
    h.paths.saves = full_saves.c_str();
    h.paths.assets = full_assets.c_str();
#endif
    h.library = nullptr;
    h.api = CoreApi{};
    h.options.clear();
    h.options_updated = false;
    h.hw = retro_hw_render_callback{};
    h.hw_requested = false;
    h.negotiation = nullptr;
    h.aspect = 4.0f / 3.0f;
    h.sample_rate = 32000.0;
    h.fps = 60.0;
    h.presented = false;
    h.last_width = h.last_height = 0;
    h.hold_input = false;
    h.fast_forward = 1;
    h.pending_state = playback.load_state ? playback.load_state : "";
    /* A Wii game's Remote; undone however the game ends. */
    h.wii = playback.wii;
    h.wii_changed = false;
    h.frame_number = 0;
    porpoise::pad::set_wii(h.wii);
    struct WiiOff
    {
        ~WiiOff()
        {
            porpoise::pad::set_wii(porpoise::pad::WiiConfig{});
            h.wii = porpoise::pad::WiiConfig{};
            if (h.motion_log)
                std::fclose(h.motion_log);
            h.motion_log = nullptr;
        }
    } wii_off;
    h.nunchuk_stage = 0;
    h.nunchuk_frame = 10;
    h.nunchuk_tries = 0;
    porpoise::pad::set_nunchuk_motion(false);
    h.debug_dir = playback.debug_dir ? playback.debug_dir : "";
    if (playback.debug_dir && playback.motion_log && h.wii.active)
    {
        mkdir(playback.debug_dir, 0777);
        /* One file per launch (motion-<date>-<time>.csv), the newest eight kept. */
        {
            std::vector<std::string> old;
            if (DIR *d = opendir(playback.debug_dir))
            {
                while (const dirent *e = readdir(d))
                {
                    const std::string name = e->d_name;
                    if (name.compare(0, 7, "motion-") == 0 && name.size() > 11 &&
                        name.compare(name.size() - 4, 4, ".csv") == 0)
                        old.push_back(name);
                }
                closedir(d);
            }
            std::sort(old.begin(), old.end());
            for (std::size_t i = 0; i + 7 < old.size(); ++i)
                std::remove((std::string(playback.debug_dir) + "/" + old[i]).c_str());
            std::remove((std::string(playback.debug_dir) + "/motion.csv").c_str()); /* test builds' single file */
        }
        char stamp[32] = "motion";
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
        if (localtime_r(&now, &tm))
            std::strftime(stamp, sizeof stamp, "motion-%Y%m%d-%H%M%S", &tm);
        const std::string path = std::string(playback.debug_dir) + "/" + stamp + ".csv";
        h.motion_log = std::fopen(path.c_str(), "w");
        h.motion_log_bytes = 0;
        if (h.motion_log)
            std::fprintf(h.motion_log,
                         "# Porpoise motion log: one line per frame. raw = the DualSense as the console gives it "
                         "(g, rad/s; x right, y out of the face, z toward the player); remote = turned into the "
                         "Wii Remote's axes for the grip (x left, y back, z up); dots = the sensor bar in the Remote's "
                         "1024x768 camera (-1 out of view); aim = the pointer past the screen's edges; roll in "
                         "degrees; bias = the gyroscope drift learnt (rad/s); pose = how it is held (0 flat, 1/2 upright "
                         "trigger to the TV right/left hand, 3/4 upright facing you right/left); p2 = the second "
                         "controller (two-controller play: the Nunchuk, its axes). controller %d pointer %d grip %d "
                         "speed %d motion %d shake %d\n"
                         "frame,samples,buttons,joypad,raw_ax,raw_ay,raw_az,raw_gx,raw_gy,raw_gz,qx,qy,qz,qw,"
                         "remote_ax,remote_ay,remote_az,remote_gx,remote_gy,remote_gz,pointer_x,pointer_y,touching,"
                         "touch_x,touch_y,shake,dot1_x,dot1_y,dot2_x,dot2_y,aim_x,aim_y,roll,bias_x,bias_y,bias_z,pose,"
                         "p2_valid,p2_raw_ax,p2_raw_ay,p2_raw_az,p2_ax,p2_ay,p2_az,p2_pose\n",
                         h.wii.controller, h.wii.pointer, h.wii.grip, h.wii.speed, h.wii.motion ? 1 : 0,
                         h.wii.shake ? 1 : 0);
    }
    h.frames_with_picture = 0;
    h.running = false;
    h.filter = playback.filter;
    h.strength = playback.strength;
    h.have_frame = false;
    h.have_disks = false;
    h.own_discs = 0;
    h.added_discs.clear();
    h.next_disc = -1;
    h.next_disc_path.clear();
    h.samples_in = 0;
    porpoise::audio::set_volume(playback.volume);
    porpoise::audio::set_muted(playback.muted);
    auto status = [&](const char *text) {
        if (hooks.status)
            hooks.status(text, hooks.user);
    };
    g_run_hooks = &hooks;
    struct ForgetHooks
    {
        ~ForgetHooks() { g_run_hooks = nullptr; }
    } forget_hooks;
    /* The menus' controller test lets go of the DolphinBar before Dolphin can
     * look for Wii Remotes, and may have it again once the game has ended. */
    porpoise::dolphinbar::hold_for_game();
    /* A real Wii Remote's HOME held for a second: the in-game menu. */
    porpoise::usbhid::set_home_menu(true);
    struct ReleaseDolphinBar
    {
        ~ReleaseDolphinBar()
        {
            porpoise::usbhid::set_home_menu(false);
            porpoise::dolphinbar::release_after_game();
        }
    } release_dolphinbar;
    status("Loading Dolphin");
    mkdir(PORPOISE_APP "/porpoise", 0777);
    mkdir(paths.saves, 0777);
    h.log = std::fopen(paths.log, "w");
    if (h.wii.real_remotes)
        start_dolphinbar_log();

    ps5::debug::mark(game_path);
    {
        std::lock_guard<std::mutex> lock(g_reason_lock);
        g_first_alert.clear();
        g_last_error.clear();
    }
    g_failure = Failure::None;
    g_failure_reason.clear();
    /* Every way out before the game runs closes the core's log. */
    auto close_log = [] {
        if (h.log)
            std::fclose(h.log);
        h.log = nullptr;
    };
    if (!load_core())
    {
        const char *why = ps5_core_dlerror();
        fail(Failure::Core, why ? why : "a function Porpoise needs is missing from the core");
        unload_core(); /* loaded, but missing a function: it goes too */
        close_log();
        return Exit::Failed;
    }

    h.api.set_environment(environment);
    h.api.set_video_refresh(video_refresh);
    h.api.set_audio_sample(audio_sample);
    h.api.set_audio_sample_batch(audio_batch);
    h.api.set_input_poll(input_poll);
    h.api.set_input_state(input_state);
    h.api.init();
    ps5::debug::mark("core: retro_init done");

    retro_system_info system{};
    h.api.get_system_info(&system);
    {
        char line[256];
        std::snprintf(line, sizeof line, "core: %s %s, need_fullpath=%d",
                      system.library_name ? system.library_name : "?",
                      system.library_version ? system.library_version : "?",
                      system.need_fullpath ? 1 : 0);
        ps5::debug::mark(line);
    }

    status("Reading the disc");
    retro_game_info game{};
    game.path = game_path;
    std::vector<unsigned char> bytes;
    if (!system.need_fullpath)
    {
        if (!read_whole_file(game_path, bytes))
        {
            const int e = errno;
            ps5::debug::mark("core: could not read the game file");
            fail(Failure::File, std::strerror(e));
            h.api.deinit();
            unload_core();
            close_log();
            return Exit::Failed;
        }
        game.data = bytes.data();
        game.size = bytes.size();
    }
    else if (porpoise::netfs::is_net(game_path) ? !porpoise::netfs::stat(game_path)
                                                : access(game_path, R_OK) != 0)
    {
        /* Dolphin opens the file itself: a drive unplugged, a file gone, a
         * network share out of reach. */
        const int e = errno;
        ps5::debug::mark("core: the game file can't be read");
        fail(Failure::File, porpoise::netfs::is_net(game_path) ? "the network share can't be reached" : std::strerror(e));
        h.api.deinit();
        unload_core();
        close_log();
        return Exit::Failed;
    }
    /* RetroAchievements: the account and Porpoise's network go to the core
     * before the game boots (it loads the game's set as it does). */
    porpoise::ra::start_game(h.library);
    if (!h.api.load_game(&game))
    {
        ps5::debug::mark("core: retro_load_game refused the game");
        fail(Failure::Game, "");
        porpoise::ra::end_game();
        h.api.deinit();
        unload_core();
        close_log();
        return Exit::Failed;
    }
    since_launch("core: game loaded, ms after the launch");
    /* Player 1's GameCube port always has a controller; the others get one
     * for each controller that is on, now or when it joins mid-game. A port
     * keeps its controller when a player leaves, so no game pauses for it. */
    porpoise::pad::poll();
    publish_frame_pads();
    h.p1_port = h.wii.active ? 0 : std::clamp(h.p1_port_want.load(), 0, porpoise::pad::kMaxPlayers - 1);
    for (int port = 0; port < porpoise::pad::kMaxPlayers; ++port)
    {
        const bool two = h.wii.active && h.wii.controller == porpoise::pad::WiiTwoControllers;
        const int player = player_of(unsigned(port));
        /* Real Wii Remotes: all four ports wait for one, whoever has a DualSense. */
        h.plugged[port] = player == 0 || (porpoise::pad::connected(player) && !two) || h.wii.real_remotes;
        if (h.plugged[port])
            h.api.set_controller_port_device(unsigned(port), port_device(port));
    }
    h.ports_real = h.wii.real_remotes;
    if (h.wii.real_remotes && h.log)
        std::fprintf(h.log, "[porpoise] real Wii Remotes in every port (DolphinBar)\n");
    if (h.wii.active && h.log)
        std::fprintf(h.log,
                     "[porpoise] Wii controller %d, pointer %d, grip %d, motion %s, shake %s, speed %d, screen %.1f x "
                     "%.1f deg, smoothing %d, reach %d%%\n",
                     h.wii.controller, h.wii.pointer, h.wii.grip, h.wii.motion ? "on" : "off",
                     h.wii.shake ? "on" : "off", h.wii.speed, h.wii.half_x * 57.29578f, h.wii.half_y * 57.29578f,
                     h.wii.smooth, h.wii.reach);
    ps5::debug::mark_value("core: controllers plugged in", porpoise::pad::connected_count());

    retro_system_av_info av{};
    h.api.get_system_av_info(&av);
    if (av.geometry.aspect_ratio > 0.0f)
        h.aspect = av.geometry.aspect_ratio;
    else if (av.geometry.base_height)
        h.aspect = float(av.geometry.base_width) / float(av.geometry.base_height);
    h.sample_rate = av.timing.sample_rate > 1000.0 ? av.timing.sample_rate : 32000.0;
    h.fps = av.timing.fps;
    porpoise::audio::set_source_rate(h.sample_rate);
    {
        char line[200];
        std::snprintf(line, sizeof line, "core: %ux%u aspect %.3f, %.3f fps, audio %.0f Hz",
                      av.geometry.base_width, av.geometry.base_height, h.aspect, h.fps, h.sample_rate);
        ps5::debug::mark(line);
    }

    if (!h.hw_requested)
    {
        ps5::debug::mark("core: did not ask for Vulkan; Porpoise needs it");
        fail(Failure::Graphics, "Dolphin didn't ask for Vulkan");
        porpoise::mic::close_all();
        h.api.unload_game();
        porpoise::ra::end_game();
        h.api.deinit();
        unload_core();
        close_log();
        return Exit::Failed;
    }
    /* The display changes hands: the launcher's device goes, Dolphin makes its
     * own with the features it needs, and the launcher's renderer is rebuilt
     * on it to keep the launch screen up until the game's first picture. */
    if (hooks.device_closing)
        hooks.device_closing(hooks.user);
    porpoise::vk::close_device();
    if (!porpoise::vk::open_device(h.negotiation))
    {
        fail(Failure::Graphics, "");
        porpoise::mic::close_all();
        h.api.unload_game();
        porpoise::ra::end_game();
        h.api.deinit();
        unload_core();
        close_log();
        return Exit::Failed;
    }
    if (hooks.device_ready)
        hooks.device_ready(hooks.user);
    status("Preparing graphics");
    if (h.hw.context_reset)
        h.hw.context_reset();
    ps5::debug::mark("core: context_reset done; running");
    /* Only now, with no early way out left before the game runs: the thread
     * must stop before the core goes (it does, after the loop). */
    /* Real Wii Remotes Dolphin connects to or takes off the game's ports (the
     * patched core keeps them): the notices, and dolphinbar.log. */
    using TakeWiimoteEvent = int (*)(int *, int *);
    const auto take_wiimote_event =
        reinterpret_cast<TakeWiimoteEvent>(ps5_core_dlsym(h.library, "porpoise_take_wiimote_event"));
    if (playback.controller_speakers)
    {
        /* The patched core hands each Remote's sound over; an older core
         * without it leaves the routing doing nothing worse than silence. */
        auto mix = reinterpret_cast<porpoise::speaker::MixFn>(ps5_core_dlsym(h.library, "porpoise_mix_wiimote_speaker"));
        auto rate = reinterpret_cast<porpoise::speaker::RateFn>(ps5_core_dlsym(h.library, "porpoise_wiimote_speaker_rate"));
        if (mix && rate)
            porpoise::speaker::start(mix, rate);
        else
            ps5::debug::mark("core: no Remote speaker export in this core");
    }
    /* 2.1.2: the game's sound pulled from Dolphin's own mixer (porpoise_audio.hpp). */
    if (playback.audio_pull)
    {
        auto pull = reinterpret_cast<porpoise::audio::PullFn>(ps5_core_dlsym(h.library, "porpoise_audio_pull"));
        auto queue = reinterpret_cast<porpoise::audio::QueueFn>(ps5_core_dlsym(h.library, "porpoise_audio_queue"));
        auto config =
            reinterpret_cast<porpoise::audio::ConfigFn>(ps5_core_dlsym(h.library, "porpoise_audio_pull_config"));
        if (pull && queue && config)
            porpoise::audio::start_pull(pull, queue, config, playback.audio_buffer_ms, playback.audio_fill);
        else
            ps5::debug::mark("core: no audio pull in this core: its sound is pushed (Classic)");
    }
    else
        ps5::debug::mark("core: Classic sound (pushed each frame)");
    const bool pulling = porpoise::audio::pulling();
    /* Where a frame's time goes (the log every 10 s). */
    using PerfFn = unsigned (*)(double *, double *, double *);
    const auto perf_take = reinterpret_cast<PerfFn>(ps5_core_dlsym(h.library, "porpoise_perf_take"));
    if (perf_take)
        perf_take(nullptr, nullptr, nullptr); /* from now */
    if (const char *cpus = std::getenv("PORPOISE_VIDEO_THREAD_CPUS"))
    {
        /* Emulator on its own cores (porpoise_main.cpp, keep_cores): this
         * thread runs Dolphin's video loop; the threads the device needed are
         * made by now, on the other processors. */
        const unsigned long long mask = std::strtoull(cpus, nullptr, 16);
        if (mask != 0)
            ps5::debug::mark_value("core: the video loop keeps to its processor",
                                   ps5_pthread_setaffinity_np(pthread_self(), sizeof mask, &mask));
    }

    unsigned long long frames = 0;
    /* Pacing (porpoise_pacer.hpp). The display is the clock when it runs at
     * the game's rate and the swapchain holds each present to the vblank: one
     * new frame every vblank, the steadiest picture there is. Otherwise a
     * clock of Porpoise's own gives each frame 1/fps of wall time.
     *
     * The speakers are the backstop either way. Dolphin makes its sound as
     * it emulates, so the samples waiting to be played measure how far the
     * game has run ahead; past the high water the loop waits for them, as
     * RetroArch's audio sync does. Locked to the display, the resampler's
     * rate control keeps the queue near its target and the backstop sits
     * higher, out of the way.
     *
     * Dolphin reports 32 kHz audio before the game starts but its sound
     * stream runs at 48 kHz; the rate is asked for again once sound flows,
     * or the resampler would stretch the audio by half and overflow. */
    /* The own-clock backstop has to sit above what one frame pushes on top of
     * the resampler's target (2304 frames, ~48 ms): a frame adds about 800 at
     * 48 kHz. At 2304 + 512 (1.5) nearly every frame waited ~6 ms for the
     * speakers, and those waits, quantised to AudioOut's 5.3 ms grain, made
     * frame times uneven (Wii Sports logged 140-370 ms of waiting a second at
     * full speed). Two frames' worth above the target leaves pacing to the
     * clock and keeps the speakers as the catch for a runaway only. */
    /* Both follow Settings > Audio > Audio buffer (porpoise_audio's target
     * depth; 2304 frames for Normal, as these were tuned for). */
    const std::size_t target = porpoise::audio::target_frames();
    const std::size_t kAudioHighWater = target + 2 * std::size_t(48000.0 / (h.fps > 10.0 ? h.fps : 60.0)) + 256;
    const std::size_t kAudioBackstop = target + 2496;    /* ~100 ms at Normal: locked to the display */
    porpoise::pacer::Pacer pacer;
    auto content_hz = [] { return h.fps > 10.0 && h.fps < 60.5 ? h.fps : 60.0; };
    /* The rate the pacing follows: the core can change h.fps itself (its
     * SET_SYSTEM_AV_INFO when a PAL game goes 50 -> 60), so the check below
     * compares against this, not against h.fps. */
    double paced_hz = content_hz();
    pacer.start(paced_hz, "game");
    porpoise::framegen::begin_game(playback.debug_dir);
    /* The frame statistics' second (frame generation's and the detailed FPS
     * counter's): the loop's times, apart from the 10-second log's. */
    long long stat_start = monotonic_ns(), stat_run_ns = 0, stat_run_max_ns = 0, stat_present_ns = 0,
              stat_pace_ns = 0, stat_misses_at = 0;
    unsigned stat_frames = 0;
    long long window_start = monotonic_ns();
    unsigned long long window_frames = 0, window_samples = 0;
    long long window_audio_wait_us = 0;
    double window_present_wait_ms = 0;
    /* Each frame's parts, summed over the window (ns): the core's run, the
     * overlay, the present, and the pacing waits. */
    long long window_run_ns = 0, window_hook_ns = 0, window_present_ns = 0, window_pace_ns = 0;
    long long window_run_max_ns = 0;
    double measured_fps = 0;
    long long fps_start = window_start;
    unsigned fps_frames = 0;
    bool rate_checked = false;
    int rate_frames = 0; /* since the core's rate was last looked at */
    int jit_frames = 0, jit_described = 0; /* Performance report: the hot code, told now and then */
    Exit exit = Exit::Home;
    bool paused = false, combo_was = false;
    for (;;)
    {
        const porpoise::pad::State &pad = porpoise::pad::poll();
        for (int port = 0, connected = 0; take_wiimote_event && take_wiimote_event(&port, &connected);)
        {
            porpoise::dolphinbar::push_notice(port + 1, connected != 0);
            porpoise::dolphinbar::log("Dolphin: a real Wii Remote %s the game's port %d", connected ? "connected to" : "taken off", port + 1);
        }
        {
            /* An attachment plugged into or out of a real Remote: a notice. */
            int iface = 0;
            bool plugged = false;
            while (porpoise::usbhid::take_attachment_event(iface, plugged))
                porpoise::dolphinbar::push_attachment_notice(iface + 1, plugged, "");
        }
        publish_frame_pads();
        log_motion();
        /* A controller that joins mid-game is plugged into its GameCube port. */
        const bool two_controllers = h.wii.active && h.wii.controller == porpoise::pad::WiiTwoControllers;
        /* 4.0: Player 1's Port changed (the in-game menu): player 1's
         * controller goes into its new port, out of the old one. */
        if (!h.wii.active && h.p1_port_want.load() != h.p1_port)
        {
            h.p1_port = std::clamp(h.p1_port_want.load(), 0, porpoise::pad::kMaxPlayers - 1);
            for (int port = 0; port < porpoise::pad::kMaxPlayers; ++port)
            {
                const int player = player_of(unsigned(port));
                const bool want = player == 0 || porpoise::pad::connected(player);
                if (want != h.plugged[port])
                {
                    h.plugged[port] = want;
                    h.api.set_controller_port_device(unsigned(port), want ? port_device(port) : RETRO_DEVICE_NONE);
                }
            }
            ps5::debug::mark_value("core: player 1 in GameCube port", h.p1_port + 1);
        }
        for (int port = 0; port < porpoise::pad::kMaxPlayers; ++port)
            if (!h.plugged[port] && porpoise::pad::connected(player_of(unsigned(port))) && !two_controllers)
            {
                h.plugged[port] = true;
                h.api.set_controller_port_device(unsigned(port), port_device(port));
                ps5::debug::mark_value("core: a controller joined; player", port + 1);
                if (h.log)
                    std::fprintf(h.log, "[porpoise] player %d joined\n", port + 1);
            }
        /* Options + touch pad, or a real Wii Remote's HOME held: the in-game
         * menu, which pauses the game. */
        const bool combo = pad.ps_menu_combo || porpoise::usbhid::take_home_hold() || porpoise::ns2gc::take_home_press();
        const bool combo_pressed = combo && !combo_was;
        combo_was = combo;
        if (h.hold_input && pad.buttons == 0)
            h.hold_input = false;
        if (!paused && combo_pressed && hooks.paused)
        {
            paused = true;
            porpoise::framegen::paused();
            porpoise::audio::set_pull_paused(true);
            porpoise::audio::flush();
            if (hooks.opened)
                hooks.opened(hooks.user);
            ps5::debug::mark("core: paused (menu)");
        }
#ifdef PORPOISE_DESKTOP
        /* The window was closed: as Close Porpoise in the in-game menu, so the
         * game's saves and the quick resume state are written first. */
        const bool closing = porpoise::platform::quit_requested();
        if (closing)
            paused = true;
#else
        const bool closing = false;
#endif
        if (paused)
        {
            int answer = closing ? int(kMenuHome) : hooks.paused(hooks.user);
            if (answer == kMenuRestart)
            {
                /* Start over: a fresh boot, and no quick resume into where it was. */
                if (playback.resume_path)
                    std::remove(playback.resume_path);
                h.pending_state.clear();
                h.api.reset();
                porpoise::audio::flush();
                porpoise::audio::set_pull_paused(false);
                paused = false;
                h.hold_input = true;
                pacer.resync();
                ps5::debug::mark("core: started over");
                continue;
            }
            if (answer == kMenuLibrary || answer == kMenuHome || answer == kMenuRelaunch)
            {
                exit = answer == kMenuHome ? Exit::Home : Exit::Library;
                ps5::debug::mark(answer == kMenuLibrary ? "core: back to the library" : "core: closing Porpoise");
                /* The picture fades to black, Porpoise's mark appears, and the
                 * core closes behind it: nothing on screen stops half-way. The
                 * quick resume state is written after the fade, behind the
                 * mark (written first, it held the menu still on screen). */
                /* The disc in now: Disc 2's point goes in Disc 2's file. */
                const char *resume_here = playback.resume_path;
                if (resume_here)
                {
                    const unsigned disc = disc_count() > 1 ? disc_index() : 0;
                    if (disc > 0)
                        resume_here = playback.disc_resume_paths && disc <= playback.disc_resume_paths->size()
                                          ? (*playback.disc_resume_paths)[disc - 1].c_str()
                                          : nullptr;
                }
                const bool keep_resume = resume_here && h.have_frame;
                if (!resume_here && playback.resume_path && h.have_frame)
                {
                    /* Left with a disc in that has no resume file of its own
                     * (4.0: another game's): none is kept, and the launched
                     * disc's older point isn't where the game was left. */
                    const std::string old = playback.resume_path;
                    std::remove(old.c_str());
                    if (old.size() > 6)
                        std::remove((old.substr(0, old.size() - 6) + ".png").c_str());
                    ps5::debug::mark("core: left on another disc: no quick resume");
                }
                if (hooks.leaving)
                {
                    porpoise::audio::flush();
                    constexpr int kFadeFrames = 22;
                    for (int frame = 1; frame <= kFadeFrames; ++frame)
                    {
                        hooks.leaving(float(frame) / kFadeFrames, hooks.user);
                        if (hooks.frame)
                            hooks.frame(h.have_frame, measured_fps, hooks.user);
                        if (h.have_frame)
                            porpoise::vk::present_core_frame(h.last_width, h.last_height, h.aspect, h.filter,
                                                             h.strength);
                        else
                            porpoise::vk::present_clear(0, 0, 0);
                        pacer.frame_done();
                    }
                }
                /* Quick resume: the game as it was left, for next time. */
                if (keep_resume)
                {
                    const bool kept = save_state(resume_here);
                    if (kept)
                        porpoise::states::write_resume_picture(resume_here);
                    if (kept && resume_here != playback.resume_path)
                    {
                        /* Left on the other disc: the launched disc's older
                         * point isn't where the game was left any more. */
                        const std::string old = playback.resume_path;
                        std::remove(old.c_str());
                        if (old.size() > 6)
                            std::remove((old.substr(0, old.size() - 6) + ".png").c_str());
                    }
                    ps5::debug::mark(kept ? "core: quick resume kept" : "core: quick resume could not be kept");
                }
                break;
            }
            if (answer == kMenuChangeDisc && h.have_disks)
            {
                /* The disc chosen in: out, the new one chosen, in (Dolphin
                 * shows the game the lid opening and closing), and back to
                 * the game. One of the game's own discs, or (4.0) another
                 * game's disc file, added to the core's list once. */
                const unsigned own = own_disc_count();
                const unsigned n = h.disks.get_num_images();
                int target = -1;
                if (!h.next_disc_path.empty() && h.disks.add_image_index && h.disks.replace_image_index)
                {
                    for (std::size_t i = 0; i < h.added_discs.size(); ++i)
                        if (h.added_discs[i] == h.next_disc_path && own + i < n)
                            target = int(own + i);
                    if (target < 0 && h.disks.add_image_index())
                    {
                        retro_game_info info{};
                        info.path = h.next_disc_path.c_str();
                        const unsigned at = h.disks.get_num_images() - 1;
                        if (at >= own && h.disks.replace_image_index(at, &info))
                        {
                            h.added_discs.resize(at - own + 1);
                            h.added_discs[at - own] = h.next_disc_path;
                            target = int(at);
                        }
                    }
                }
                else if (h.next_disc >= 0 && unsigned(h.next_disc) < n)
                    target = h.next_disc;
                else if (h.next_disc < 0 && h.next_disc_path.empty() && n > 1)
                    target = int((disc_index() + 1) % n); /* (no choice made: the next one) */
                if (target >= 0 && unsigned(target) != disc_index())
                {
                    h.disks.set_eject_state(true);
                    h.disks.set_image_index(unsigned(target));
                    h.disks.set_eject_state(false);
                    ps5::debug::mark_value("core: changed to disc", target + 1);
                }
                h.next_disc = -1;
                h.next_disc_path.clear();
                answer = kMenuResume;
            }
            if (answer == kMenuResume)
            {
                paused = false;
                porpoise::audio::set_pull_paused(false);
                h.hold_input = true;
                ps5::debug::mark("core: resumed");
            }
            /* The paused picture, with the menu over it. */
            if (hooks.frame)
                hooks.frame(h.have_frame, measured_fps, hooks.user);
            if (h.have_frame)
                porpoise::vk::present_core_frame(h.last_width, h.last_height, h.aspect, h.filter, h.strength);
            else
                porpoise::vk::present_clear(0, 0, 0);
            pacer.frame_done();
            continue;
        }
        h.running = true;
        /* Counted, and the Wii controller changed, only while the game runs:
         * the core takes a changed option as a frame starts, so a change made
         * in the paused menu waits for it (plugged in again while paused, the
         * Remote was read back from the file as it was, its old attachment). */
        ++h.frame_number;
        if (h.wii_changed)
        {
            /* The in-game menu changed the Wii controller: every port again.
             * Two-controller play has only the Remote (port 0): the second
             * controller is its Nunchuk, not a second Remote. */
            h.wii_changed = false;
            const bool two = h.wii.active && h.wii.controller == porpoise::pad::WiiTwoControllers;
            /* Real Wii Remotes: every port takes one. Back to the DualSense
             * from them: the ports without a controller are emptied. */
            const bool was_real = h.ports_real;
            h.ports_real = h.wii.real_remotes;
            for (int port = 1; port < porpoise::pad::kMaxPlayers; ++port)
                if (h.wii.real_remotes)
                    h.plugged[port] = true; /* given its device below */
                else if (h.plugged[port] &&
                         (two || (was_real && !porpoise::pad::connected(player_of(unsigned(port))))))
                {
                    h.api.set_controller_port_device(unsigned(port), RETRO_DEVICE_NONE);
                    h.plugged[port] = false;
                }
            if (h.nunchuk_stage == 1 || h.nunchuk_stage == 2)
                nunchuk_motion_step(true); /* the option goes off first, then every port, then again */
            else
                for (int port = 0; port < porpoise::pad::kMaxPlayers; ++port)
                    if (h.plugged[port])
                        h.api.set_controller_port_device(unsigned(port), port_device(port));
            if (h.nunchuk_stage == 4)
            {
                h.nunchuk_stage = 0;
                h.nunchuk_tries = 0;
            }
            h.nunchuk_frame = std::max(h.nunchuk_frame, h.frame_number + 10);
        }
        nunchuk_motion_step();
        /* Fast forward: several emulated frames for each one shown, without
         * their sound. */
        const int runs = h.fast_forward;
        porpoise::audio::set_fast_forward(runs > 1);
        const long long run_start = monotonic_ns();
        h.new_frame = false;
        for (int i = 0; i < runs; ++i)
            h.api.run();
        {
            const long long run_ns = monotonic_ns() - run_start;
            window_run_ns += run_ns;
            window_run_max_ns = std::max(window_run_max_ns, run_ns);
        }
        if (runs > 1)
            porpoise::audio::flush();
        /* A save state chosen in Details loads once the game is up. */
        if (!h.pending_state.empty() && h.have_frame && ++h.frames_with_picture > 30)
        {
            const std::string path = h.pending_state;
            h.pending_state.clear();
            if (!load_state(path.c_str()) && h.log)
                std::fprintf(h.log, "[porpoise] could not load %s\n", path.c_str());
        }

        /* Pulled, the core pushes no sound: two seconds of frames instead. */
        if (!rate_checked && (h.samples_in > 4096 || (pulling && frames > 120)))
        {
            rate_checked = true;
            retro_system_av_info now_av{};
            h.api.get_system_av_info(&now_av);
            char line[160];
            std::snprintf(line, sizeof line, "core: sound is flowing; core now reports %.0f Hz (was %.0f), %.3f fps",
                          now_av.timing.sample_rate, h.sample_rate, now_av.timing.fps);
            ps5::debug::mark(line);
            if (h.log)
                std::fprintf(h.log, "[porpoise] %s\n", line);
            if (now_av.timing.sample_rate > 1000.0)
            {
                h.sample_rate = now_av.timing.sample_rate;
                porpoise::audio::set_source_rate(h.sample_rate);
            }
            if (now_av.timing.fps > 10.0 && now_av.timing.fps < 200.0 && std::fabs(now_av.timing.fps - h.fps) > 0.5)
                h.fps = now_av.timing.fps; /* a PAL game shows its 50 Hz only now */
            /* Judge the vblank on the game itself, not on its boot. */
            paced_hz = content_hz();
            pacer.start(paced_hz, "game");
        }
#ifndef PORPOISE_DESKTOP
        /* Performance report: every 20 seconds (the first after 30), the
         * emulated CPU's busiest places told as the game's own code (the core's
         * porpoise_jit_describe). Up to 30 of them, about ten minutes: a report
         * that stopped after four missed the slow part of a game reached later. */
        if (rate_checked && jit_described < 30 && ++jit_frames >= (jit_described == 0 ? 1800 : 1200))
        {
            jit_frames = 0;
            unsigned long long rips[48];
            unsigned counts[48];
            const int n = ps5_sampler_hot(rips, counts, 48);
            using DescribeFn = void (*)(const unsigned long long *, const unsigned *, int);
            const DescribeFn describe =
                n > 0 ? reinterpret_cast<DescribeFn>(ps5_core_dlsym(h.library, "porpoise_jit_describe")) : nullptr;
            if (n > 0 && describe)
            {
                ++jit_described;
                describe(rips, counts, n);
            }
        }
#endif
        /* A PAL game switched to 60 Hz in its own menu (Metroid Prime, Wind
         * Waker, F-Zero GX), or back: the core doesn't always say so, so its
         * rate is looked at every second and the pacing follows it. Paced at
         * the old 50, such a game ran slow with crackling sound. */
        if (rate_checked && ++rate_frames >= 60)
        {
            rate_frames = 0;
            retro_system_av_info now_av{};
            h.api.get_system_av_info(&now_av);
            /* Asking also marks the core's widescreen as told: its shape
             * change is taken from the answer, then. */
            if (now_av.geometry.aspect_ratio > 0.0f && std::fabs(now_av.geometry.aspect_ratio - h.aspect) > 0.01f)
                h.aspect = now_av.geometry.aspect_ratio;
            if (now_av.timing.fps > 10.0 && now_av.timing.fps < 200.0)
                h.fps = now_av.timing.fps;
            if (std::fabs(content_hz() - paced_hz) > 0.5)
            {
                char line[120];
                std::snprintf(line, sizeof line, "core: the game's rate changed: %.3f -> %.3f fps", paced_hz,
                              content_hz());
                ps5::debug::mark(line);
                if (h.log)
                    std::fprintf(h.log, "[porpoise] %s\n", line);
                paced_hz = content_hz();
                pacer.start(paced_hz, "game");
            }
        }

        const long long hook_start = monotonic_ns();
        if (hooks.frame)
            hooks.frame(h.have_frame, measured_fps, hooks.user);
        const long long present_start = monotonic_ns();
        /* 3.1: frame generation (beta) presents this run's frames itself
         * when it takes them; otherwise the game's frame as always. */
        bool presented = false;
        if (h.have_frame)
        {
            porpoise::framegen::Run fg_run;
            fg_run.new_frame = h.new_frame;
            fg_run.width = h.last_width;
            fg_run.height = h.last_height;
            fg_run.aspect = h.aspect;
            fg_run.filter = h.filter;
            fg_run.strength = h.strength;
            fg_run.vsynced = pacer.vsynced();
            fg_run.vblanks_per_tick = pacer.vblanks_per_frame();
            fg_run.vblank_hz = pacer.vblank_hz();
            fg_run.content_hz = paced_hz;
            fg_run.speed = measured_fps > 1.0 ? 100.0 * measured_fps / paced_hz : 100.0;
            fg_run.nominal = runs == 1;
            presented = porpoise::framegen::present(fg_run);
        }
        if (presented)
            ;
        else if (h.have_frame)
            porpoise::vk::present_core_frame(h.last_width, h.last_height, h.aspect, h.filter, h.strength);
        else
            porpoise::vk::present_clear(0, 0, 0);
        window_present_wait_ms += porpoise::vk::last_present_wait_ms();
        const long long pace_start = monotonic_ns();
        window_hook_ns += present_start - hook_start;
        window_present_ns += pace_start - present_start;

        /* The speakers: never run far ahead of what has been heard (pushed
         * sound only: pulled, the mixer keeps its own queue). */
        if (rate_checked && runs == 1 && !pulling)
            window_audio_wait_us +=
                porpoise::audio::wait_below(pacer.display_locked() ? kAudioBackstop : kAudioHighWater, 40);

        pacer.frame_done();
        window_pace_ns += monotonic_ns() - pace_start;
        {
            /* The statistics' second. */
            const long long t = monotonic_ns();
            stat_run_ns += hook_start - run_start;
            stat_run_max_ns = std::max(stat_run_max_ns, hook_start - run_start);
            stat_present_ns += pace_start - present_start;
            stat_pace_ns += t - pace_start;
            ++stat_frames;
            if (t - stat_start >= 1'000'000'000LL)
            {
                const double n = double(stat_frames);
                const long long misses = pacer.misses_total();
                porpoise::framegen::note_loop(stat_run_ns / 1e6 / n, stat_run_max_ns / 1e6, stat_present_ns / 1e6 / n,
                                              stat_pace_ns / 1e6 / n, int(misses - stat_misses_at),
                                              stat_frames * 1e9 / double(t - stat_start));
                stat_misses_at = misses;
                stat_start = t;
                stat_run_ns = stat_run_max_ns = stat_present_ns = stat_pace_ns = 0;
                stat_frames = 0;
            }
        }

        ++frames;
        ++window_frames;
        if (++fps_frames == 30)
        {
            const long long t = monotonic_ns();
            measured_fps = fps_frames * 1e9 / double(std::max(1LL, t - fps_start));
            fps_start = t;
            fps_frames = 0;
        }
        if (frames == 1)
            since_launch("core: first frame run, ms after the launch");
        const long long now = monotonic_ns();
        if (now - window_start >= 10'000'000'000LL && h.log)
        {
            const double secs = (now - window_start) / 1e9;
            const unsigned long long samples = h.samples_in - window_samples;
            /* Emulated speed: the core's sound per wall second over its rate
             * (pushed), or its frames over the game's rate (pulled). */
            const double speed = pulling ? 100.0 * window_frames / secs / (h.fps > 10.0 ? h.fps : 60.0)
                                         : 100.0 * samples / secs / h.sample_rate;
            std::fprintf(h.log,
                         "[porpoise] %.2f frames/s (game %.3f), %.0f%% speed (%.0f audio frames/s at %.0f Hz), "
                         "%s (%d missed), display wait %.1f ms/frame, queue %zu, waited %.1f ms/s for the speakers, "
                         "players %d\n",
                         window_frames / secs, h.fps, speed, samples / secs, h.sample_rate,
                         pacer.display_locked() ? "locked to the vblank" : "own clock", pacer.take_misses(),
                         window_frames ? window_present_wait_ms / window_frames : 0.0, porpoise::audio::queued(),
                         window_audio_wait_us / 1000.0 / secs, porpoise::pad::connected_count());
            /* Where each frame's time went: what holds a game under its rate. */
            if (window_frames)
            {
                const double n = double(window_frames);
                double cpu_ms = 0, cpu_max_ms = 0, gpu_ms = 0;
                const unsigned steps = perf_take ? perf_take(&cpu_ms, &cpu_max_ms, &gpu_ms) : 0;
                std::fprintf(h.log,
                             "[porpoise] frame time: core %.1f ms (max %.1f; Dolphin's CPU thread %.1f ms, max %.1f, "
                             "video loop busy %.1f ms, %u steps), overlay %.1f ms, present %.1f ms, pacing %.1f ms\n",
                             window_run_ns / 1e6 / n, window_run_max_ns / 1e6, cpu_ms, cpu_max_ms, gpu_ms, steps,
                             window_hook_ns / 1e6 / n, window_present_ns / 1e6 / n, window_pace_ns / 1e6 / n);
                if (pulling)
                {
                    unsigned q = 0, t = 0;
                    porpoise::audio::pull_queue(q, t);
                    std::fprintf(h.log, "[porpoise] sound: pulled, mixer queue %u of %u granules, %u gaps filled\n",
                                 q, t, porpoise::audio::take_gaps());
                }
            }
            std::fflush(h.log);
            window_start = now;
            window_frames = 0;
            window_samples = h.samples_in;
            window_audio_wait_us = 0;
            window_present_wait_ms = 0;
            window_run_ns = window_hook_ns = window_present_ns = window_pace_ns = 0;
            window_run_max_ns = 0;
        }
    }

    h.running = false;
    h.fast_forward = 1;
    porpoise::framegen::end_game();
    porpoise::mic::close_all(); /* no game listening any more */
    porpoise::speaker::stop();   /* before the core and its mixer go */
    porpoise::audio::stop_pull(); /* likewise: no call into the core after this */
    porpoise::audio::set_pull_paused(false);
    porpoise::audio::set_fast_forward(false);
    porpoise::audio::flush();
    porpoise::vk::close();
    /* Everything Porpoise made on the core's device goes before the core
     * takes the device down with it. */
    if (hooks.device_closing)
        hooks.device_closing(hooks.user);
    porpoise::vk::close_device();
    if (h.hw.context_destroy)
        h.hw.context_destroy();
    /* An unlock still on its way gets a few seconds before the game goes. */
    porpoise::ra::finish_game(h.library, 5000);
    porpoise::ns2gc::set_rumble(false);
    if (h.ports_real)
        dolphinbar_log(("Porpoise: the game ends; what was seen:\n" + porpoise::usbhid::describe()).c_str());
    h.api.unload_game();
    porpoise::ra::end_game();
    if (h.ports_real)
        dolphinbar_log("Porpoise: the game is unloaded, Dolphin closes (its Wii Remote threads end)");
    h.api.deinit();
    unload_core();
    if (h.ports_real)
        dolphinbar_log("Porpoise: Dolphin closed");
    ps5::debug::mark("core: game closed");
    if (h.log)
        std::fclose(h.log);
    h.log = nullptr;
    h.hold_input = false;
    return exit;
}
} // namespace porpoise::core
