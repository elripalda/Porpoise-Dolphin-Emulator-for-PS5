/* Porpoise - the title's entry point: the launcher, then the game.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Startup keeps what Mihawk's PS5 RetroArch learned the console needs
 * (its src/main.cpp, GPL-3.0-or-later): a trace file, since a title's stderr
 * reaches nothing; the buffered log and its flusher thread; the terminate
 * handler that names an exception before abort; the crash report; core thread
 * tracking; and leaving through the shell rather than exit().
 *
 * Then: the launcher (src/ui_*.cpp) on Porpoise's own Vulkan device; on Play,
 * the launch screen, the hand-over of the display to Dolphin's device, and the
 * game (src/porpoise_core.cpp). */
#include "porpoise_netfs.hpp"
#include "porpoise_dns.hpp"
#include "porpoise_paths.hpp"
#include <algorithm>
#include <tuple>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <cxxabi.h>
#include <dirent.h>
#include <exception>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include <time.h>
#include <typeinfo>
#include <unistd.h>
#include <utility>

#include <ps5platform/libc.h>

#include "../build/title_build_identity.h"
#include "memory_diagnostics.hpp"
#include "porpoise_audio.hpp"
#include "porpoise_gfxmods.hpp"
#include "porpoise_bios.hpp"
#include "porpoise_sfx.hpp"
#include "porpoise_borders.hpp"
#include "porpoise_atomic.hpp"
#include "porpoise_forwarders.hpp"
#include "porpoise_core.hpp"
#include "porpoise_disc.hpp"
#include "porpoise_forward.hpp"
#ifdef PORPOISE_DESKTOP
#include "porpoise_platform.hpp"
#endif
#include "porpoise_speaker.hpp"
#include "porpoise_banner.hpp"
#include "porpoise_covers.hpp"
#include "porpoise_jailbreak.hpp"
#include "porpoise_framegen.hpp"
#include "porpoise_pacer.hpp"
#include "porpoise_aim.hpp"
#include "porpoise_dolphinbar.hpp"
#include "porpoise_ns2gc.hpp"
#include "porpoise_bt.hpp"
#include "porpoise_usbhid.hpp"
#include "porpoise_pad.hpp"
#include "porpoise_ra.hpp"
#include "ui_widescreen.hpp"
#include "porpoise_sound.hpp"
#include "porpoise_shots.hpp"
#include "porpoise_states.hpp"
#include "porpoise_update.hpp"
#include "porpoise_vk.hpp"
#include "title_threads.hpp"
#include "trace.hpp"
#include "ui_app.hpp"
#include "ui_cheats.hpp"
#include "ui_app_common.hpp"
#include "ui_gfx.hpp"
#include "ui_i18n.hpp"
#include "ui_library.hpp"
#include "ui_recommend.hpp"
#include "ui_setups.hpp"
#include "ui_settings.hpp"

extern "C"
{
    int sceSystemServiceHideSplashScreen();
    int sceSystemServiceParamGetInt(int param, int *value);
    int sceSystemServiceLoadExec(const char *, const char *const *);
    void ps5_vulkan_profile_init();
    void ps5_crash_report_install();
    void ps5_core_threads_start();
    void ps5_sampler_start();
    void ps5_open_permissions();
    void ps5_memory_report(const char *, size_t, int);
    int sysctlbyname(const char *, void *, size_t *, const void *, size_t);
}

namespace
{
/* Where the player's things live: /data/porpoise, outside the app folder, so
 * reinstalling Porpoise keeps games, saves, settings and covers. If the
 * sandbox will not let Porpoise write there, the app's own folder is used. */
std::string g_data = PORPOISE_APP "/porpoise";
std::string g_settings_path, g_options_path, g_saves_path, g_options_reference, g_core_log;

bool g_sandboxed = false; /* /data out of reach even after asking the HEN */
int g_freed = -1;         /* the HEN's answer, asked once at the very start (-1: not asked) */
/* Freeing Porpoise closed it last time (how far it got), so it didn't ask this time. */
std::string g_jailbreak_closed;
bool g_stay_sandboxed = false; /* Settings > Games > Stay in the sandbox */
bool g_data_readonly = false;  /* freed, but /data/porpoise can't be written */
/* Porpoise's folder can live on another drive (Settings > Games > Move
 * Porpoise's folder): /data/porpoise/location.txt names it. */
std::string g_location_missing; /* named there but not connected */
/* A first start (nothing in /data/porpoise, no pointer) that finds a Porpoise
 * folder on extended storage or a USB drive: offered, never taken unasked. */
std::string g_found_folder, g_found_place;
bool g_found_handmade = false; /* the found folder has no settings yet */

bool writable_dir(const std::string &dir)
{
    struct stat st;
    if (stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
        return false;
    const std::string probe = dir + "/.write-test";
    std::FILE *f = std::fopen(probe.c_str(), "w");
    if (!f)
        return false;
    std::fclose(f);
    std::remove(probe.c_str());
    return true;
}

/* Copies a file or a whole folder; what's already at `to` is left alone. */
void copy_tree(const std::string &from, const std::string &to, int depth = 0)
{
    struct stat st;
    if (depth > 8 || stat(from.c_str(), &st) != 0)
        return;
    if (S_ISDIR(st.st_mode))
    {
        mkdir(to.c_str(), 0777);
        if (DIR *d = opendir(from.c_str()))
        {
            while (dirent *e = readdir(d))
                if (std::strcmp(e->d_name, ".") != 0 && std::strcmp(e->d_name, "..") != 0)
                    copy_tree(from + "/" + e->d_name, to + "/" + e->d_name, depth + 1);
            closedir(d);
        }
        return;
    }
    if (stat(to.c_str(), &st) == 0)
        return;
    std::FILE *in = std::fopen(from.c_str(), "rb");
    if (!in)
        return;
    const std::string part = to + ".part";
    std::FILE *out = std::fopen(part.c_str(), "wb");
    bool ok = out != nullptr;
    char buf[65536];
    std::size_t n;
    while (ok && (n = std::fread(buf, 1, sizeof buf, in)) > 0)
        ok = std::fwrite(buf, 1, n, out) == n;
    std::fclose(in);
    if (out)
        ok = std::fclose(out) == 0 && ok;
    if (!ok || std::rename(part.c_str(), to.c_str()) != 0)
        std::remove(part.c_str());
}

/* A player whose Porpoise 1.0 couldn't reach /data kept everything in the
 * app's own folder. Now that /data is open, their settings, memory cards,
 * save states and the rest come along once (games stay where they are and are
 * still found there; covers download again). */
void bring_over_app_folder_data()
{
    const std::string old_dir = PORPOISE_APP "/porpoise";
    struct stat st;
    if (stat((old_dir + "/settings.ini").c_str(), &st) != 0 || stat((g_data + "/settings.ini").c_str(), &st) == 0)
        return;
    ps5::debug::mark("main: bringing the player's things over from the app folder");
    for (const char *item : {"settings.ini", "library.txt", "game-settings", "states", "setups", "lang", "saves/User/GC",
                             "saves/User/Wii", "saves/User/Config", "saves/User/GameSettings"})
    {
        const std::string rel = item;
        if (rel.rfind("saves/User/", 0) == 0)
        {
            mkdir((g_data + "/saves").c_str(), 0777);
            mkdir((g_data + "/saves/User").c_str(), 0777);
        }
        copy_tree(old_dir + "/" + rel, g_data + "/" + rel);
    }
}

/* The drives a moved Porpoise folder can be on (Settings > Games > Move
 * Porpoise's folder puts it in <drive>/porpoise). */
std::vector<std::pair<std::string, std::string>> folder_drives()
{
    std::vector<std::pair<std::string, std::string>> drives = {{"/mnt/ext0", "extended storage"},
                                                               {"/mnt/ext1", "extended storage 2"}};
    for (int i = 0; i < 8; ++i)
        drives.emplace_back("/mnt/usb" + std::to_string(i), "a USB drive");
    return drives;
}

bool has_porpoise_folder(const std::string &dir)
{
    struct stat st;
    return stat((dir + "/settings.ini").c_str(), &st) == 0 && writable_dir(dir);
}

/* A Porpoise folder made by hand on a drive, before Porpoise ever ran from it
 * (its games, saves or texture packs copied in): no settings yet. */
bool has_handmade_folder(const std::string &dir)
{
    if (!writable_dir(dir))
        return false;
    struct stat st;
    for (const char *sub : {"games", "saves", "covers", "bios", "cheats"})
        if (stat((dir + "/" + sub).c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            return true;
    return false;
}

/* The pointer names a drive that isn't there. A drive can be slow to appear
 * after rest mode, and a USB drive plugged into another port shows up under
 * another number: both are looked for before giving up on it. Only runs when
 * the folder would otherwise be missing. */
std::string find_moved_folder(const std::string &where)
{
    for (int i = 0; i < 8; ++i)
    {
        const timespec quarter = {0, 250 * 1000 * 1000};
        nanosleep(&quarter, nullptr);
        if (writable_dir(where))
        {
            ps5::debug::mark("main: Porpoise's folder appeared after a moment");
            return where;
        }
    }
    if (where.rfind("/mnt/usb", 0) != 0)
        return "";
    std::string found;
    int count = 0;
    for (int i = 0; i < 8; ++i)
    {
        const std::string dir = "/mnt/usb" + std::to_string(i) + "/porpoise";
        if (has_porpoise_folder(dir))
        {
            found = dir;
            ++count;
        }
    }
    if (count != 1)
        return ""; /* none, or more than one: which is theirs isn't clear */
    ps5::debug::mark(("main: Porpoise's folder is on another USB port now: " + found).c_str());
    const std::string pointer = std::string(PORPOISE_DATA) + "/location.txt";
    if (std::FILE *f = porpoise::open_atomic(pointer))
    {
        std::fprintf(f, "%s\n", found.c_str());
        porpoise::finish_atomic(f, pointer);
    }
    return found;
}

void choose_data_dir()
{
    const bool freed = g_freed >= 0 ? g_freed == 1 : porpoise::jailbreak::ensure();
    if (freed && !porpoise::jailbreak::data_reachable())
    {
        /* Freed, but /data/porpoise can't be written (seen on fresh installs):
         * the console's folders and USB drives are there, and Porpoise's own
         * things stay in its app folder. */
        g_data_readonly = true;
        ps5::debug::mark("main: /data is there but /data/porpoise can't be written; using the app's folder");
    }
    if (freed && !g_data_readonly)
    {
        g_data = PORPOISE_DATA;
        bring_over_app_folder_data();
        struct stat st;
        const bool fresh = stat((g_data + "/settings.ini").c_str(), &st) != 0;
        if (std::FILE *f = std::fopen((g_data + "/location.txt").c_str(), "r"))
        {
            char line[512] = {0};
            std::string where = std::fgets(line, sizeof line, f) ? line : "";
            std::fclose(f);
            while (!where.empty() && (where.back() == '\n' || where.back() == '\r' || where.back() == ' '))
                where.pop_back();
            if (!where.empty() && writable_dir(where))
                g_data = where;
            else if (!where.empty())
            {
                const std::string found = find_moved_folder(where);
                if (!found.empty())
                    g_data = found;
                else
                    g_location_missing = where;
            }
            ps5::debug::mark(("main: Porpoise's folder is set to " + where).c_str());
        }
        else if (fresh)
        {
            /* A folder Porpoise used before first; else one made by hand on
             * extended storage (a fresh install there kept writing to /data
             * and left it empty). */
            for (const auto &drive : folder_drives())
                if (has_porpoise_folder(drive.first + "/porpoise"))
                {
                    g_found_folder = drive.first + "/porpoise";
                    g_found_place = drive.second;
                    ps5::debug::mark(("main: a first start, and a Porpoise folder is on " + g_found_folder).c_str());
                    break;
                }
            if (g_found_folder.empty())
                for (const char *drive : {"/mnt/ext0", "/mnt/ext1"})
                    if (has_handmade_folder(std::string(drive) + "/porpoise"))
                    {
                        g_found_folder = std::string(drive) + "/porpoise";
                        g_found_place = drive[8] == '0' ? "extended storage" : "extended storage 2";
                        g_found_handmade = true;
                        ps5::debug::mark(("main: a first start, and a folder made for Porpoise is on " +
                                          g_found_folder).c_str());
                        break;
                    }
        }
    }
    else if (!freed)
        g_sandboxed = true;
    mkdir(g_data.c_str(), 0777);
    mkdir((g_data + "/covers").c_str(), 0777);
    mkdir((g_data + "/games").c_str(), 0777);
    mkdir((g_data + "/saves").c_str(), 0777);
    mkdir((g_data + "/bios").c_str(), 0777); /* the player's own GameCube BIOS, if they have one */
    /* <data>/cheats (the player's own codes) is never made by Porpoise: the
     * player makes it when they add a code. */
    g_settings_path = g_data + "/settings.ini";
    g_options_path = g_data + "/options.ini";
    g_saves_path = g_data + "/saves";
    g_options_reference = g_data + "/options-reference.txt";
    g_core_log = PORPOISE_APP "/porpoise/core.log";
    porpoise::ui::set_folder(g_data); /* help texts name the folder in use */
    ps5::debug::mark(("main: player data in " + g_data).c_str());
    /* Test files the Dolphin core still reads: worth knowing about when a
     * setting doesn't seem to take. */
    struct stat st;
    if (stat(PORPOISE_APP "/dolphin-options.txt", &st) == 0)
        ps5::debug::mark("main: /app0/dolphin-options.txt is there: its options win over Porpoise's");
    if (stat(PORPOISE_APP "/dolphin-debug.txt", &st) == 0)
        ps5::debug::mark("main: /app0/dolphin-debug.txt is there: the shader cache is off while it is");
}

porpoise::ui::Gfx g_gfx;
porpoise::ui::Library g_library;
porpoise::Settings g_settings;
porpoise::Settings g_play; /* what the game being played uses: g_settings + its own */
/* 3.1: the library keys of the game's other discs, in the core's disc order
 * after the one launched (PORPOISE_EXTRA_DISCS), and their quick resume
 * files: leaving on Disc 2 keeps the resume point for Disc 2's entry. */
std::vector<std::string> g_other_disc_keys;
std::vector<std::string> g_other_disc_resume;
/* 4.0: a file that says a game is starting, gone once it shows a picture. */
std::string g_boot_marker;
std::vector<int> g_disc_numbers; /* 4.0: the launched disc's number, then theirs (Change Disc) */
porpoise::ui::Game *g_playing = nullptr;
bool g_menu_open = false;
/* Black over the screen with Porpoise's mark (App::draw_curtain): leaving a
 * game, coming back to the library, and the start. */
float g_curtain = 0.0f;
std::string g_wii_game_id; /* the game playing, for the Wii MotionPlus's Auto */
bool g_controller_speakers = false; /* this game's Remote sounds go to the controllers */
porpoise::ui::App g_app;
double g_time = 0;

void *log_flusher(void *)
{
    const timespec interval = {0, 250 * 1000 * 1000};
    for (;;)
    {
        nanosleep(&interval, nullptr);
        std::fflush(nullptr);
    }
    return nullptr;
}

void start_log_flusher()
{
    pthread_t thread;
    if (create_title_thread(&thread, log_flusher, nullptr) == 0)
        pthread_detach(thread);
}

void on_terminate()
{
    const std::type_info *type = abi::__cxa_current_exception_type();
    if (!type)
    {
        ps5::debug::mark("terminate: called with no active exception");
        std::fflush(stderr);
        std::abort();
    }
    int status = 0;
    char *pretty = abi::__cxa_demangle(type->name(), nullptr, nullptr, &status);
    char line[256];
    std::snprintf(line, sizeof line, "terminate: exception type=%s",
                  (status == 0 && pretty) ? pretty : type->name());
    std::free(pretty);
    ps5::debug::mark(line);
    std::fflush(stderr);
    std::abort();
}

/* The DolphinBar let go before the shell ends the process: a USB transfer
 * the process still has running when it ends makes the console drain it,
 * and a drain that never ended left Porpoise impossible to close (only
 * holding the power button helped). Waits at most 1.5 s. */
void close_usb()
{
    const bool closed = porpoise::dolphinbar::close_for_exit(1500);
    const bool gc_closed = porpoise::ns2gc::close_for_exit(800) && porpoise::bt::close_for_exit(800);
    ps5::debug::mark_value("main: USB devices closed", closed && gc_closed ? 1 : 0);
}

[[noreturn]] void leave(int status)
{
    ps5::debug::mark_value("main: leaving through the shell, status", status);
    close_usb();
    porpoise::audio::close();
    porpoise::pad::close();
    ps5::memory::finish();
    std::fflush(nullptr);
    const int result = sceSystemServiceLoadExec("exit", nullptr);
    ps5::debug::mark_value("main: LoadExec result", result);
    for (;;)
        usleep(100000); /* the shell ends the process asynchronously */
}

/* Closes Porpoise and opens it again (a setting that takes effect at the
 * start). Where the shell won't load Porpoise's own executable again,
 * Porpoise just closes and the player opens it. */
[[noreturn]] void restart_porpoise()
{
    ps5::debug::mark("main: restarting");
    close_usb();
    porpoise::audio::close();
    porpoise::pad::close();
    ps5::memory::finish();
    std::fflush(nullptr);
    const int result = sceSystemServiceLoadExec(PORPOISE_APP "/eboot.bin", nullptr);
    ps5::debug::mark_value("main: LoadExec (restart) result", result);
    if (result < 0)
        sceSystemServiceLoadExec("exit", nullptr);
    for (;;)
        usleep(100000);
}

long long now_ns()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<long long>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

/* The launcher's frame pacing (porpoise_pacer.hpp): one frame per vblank
 * when the display holds the loop to it, its own clock otherwise. */
porpoise::pacer::Pacer g_pacer;

/* Before each frame's render pass: the pixels of textures that changed
 * (a Wii disc's banner playing) go into them. */
void prepass(VkCommandBuffer cmd, void *)
{
    if (g_gfx.ready())
        g_gfx.record_uploads(cmd);
}

/* The UI is recorded into every presented frame, after the core's picture. */
void overlay(VkCommandBuffer cmd, unsigned slot, void *)
{
    if (g_gfx.ready())
        g_gfx.record(cmd, int(slot));
}

bool start_gfx()
{
    const porpoise::vk::Context c = porpoise::vk::context();
    porpoise::ui::GfxInit init;
    init.instance = c.instance;
    init.gpu = c.gpu;
    init.device = c.device;
    init.queue = c.queue;
    init.queue_family = c.queue_family;
    init.render_pass = c.render_pass;
    init.slots = c.slots;
    init.get_instance_proc = c.get_instance_proc;
    init.get_device_proc = c.get_device_proc;
    init.queue_mutex = c.queue_mutex;
    init.asset_dir = PORPOISE_APP "/assets";
    init.cjk_font = porpoise::ui::cjk_font(); /* the menus' language is set by now */
    init.cjk_also = porpoise::ui::language_names();
    if (!g_data.empty())
    {
        init.cache_dir = g_data + "/cache";
        mkdir(init.cache_dir.c_str(), 0777);
    }
    const long long t0 = now_ns();
    const bool ok = g_gfx.init(init);
    ps5::debug::mark_value(ok ? "ui: renderer ready, ms" : "ui: renderer FAILED, ms", (now_ns() - t0) / 1000000);
    return ok;
}

void begin_ui_frame(float dim)
{
    g_gfx.begin(porpoise::vk::current_slot(), float(porpoise::vk::screen_width()),
                float(porpoise::vk::screen_height()), float(g_time), dim, g_settings.reduced_motion);
}

/* GameTDB's descriptions in the menus' language: Spanish, French, Portuguese
 * and Italian have their own table; English and Japanese use the English one
 * (the Japanese font holds only the menus' own characters). */
const char *info_lang()
{
    switch (porpoise::ui::language())
    {
    case porpoise::ui::Language::Spanish:
    case porpoise::ui::Language::SpanishLatinAmerica: return "ES";
    case porpoise::ui::Language::French: return "FR";
    case porpoise::ui::Language::Portuguese:
    case porpoise::ui::Language::PortugueseBrazil: return "PT";
    case porpoise::ui::Language::Italian: return "IT";
    case porpoise::ui::Language::German: return "DE";
    case porpoise::ui::Language::Dutch: return "NL";
    default: return "EN";
    }
}

std::string info_path();

/* The table the library shows: the language's, or English until it is here. */
std::string shown_info_path()
{
    const std::string own = info_path();
    struct stat st;
    return stat(own.c_str(), &st) == 0 ? own : g_data + "/info.tsv";
}

std::string info_path()
{
    const std::string lang = info_lang();
    if (lang == "EN")
        return g_data + "/info.tsv";
    std::string lower = lang;
    for (char &c : lower)
        c = char(c - 'A' + 'a');
    return g_data + "/info-" + lower + ".tsv";
}

/* Where games are looked for: the usual folders and drives (unless the player
 * turned that off) and every folder the player added in Settings. */
porpoise::ui::LibraryPaths library_paths()
{
    porpoise::ui::LibraryPaths paths;
    paths.roots = {PORPOISE_APP "/content", g_data + "/games"};
    if (g_data != PORPOISE_APP "/porpoise")
        paths.roots.push_back(PORPOISE_APP "/porpoise/games"); /* where a sandboxed 1.0 kept them */
#ifndef PORPOISE_DESKTOP
    if (g_settings.auto_search)
    {
        for (const char *dir : {"/data/games", "/data/GameCube", "/data/gamecube", "/data/Wii", "/data/wii",
                                "/data/roms", "/data/ROMs", "/data/iso", "/data/ISO"})
            paths.roots.push_back(dir);
        for (int i = 0; i < 8; ++i)
            paths.roots.push_back("/mnt/usb" + std::to_string(i));
        paths.roots.push_back("/mnt/ext0");
        paths.roots.push_back("/mnt/ext1");
    }
#endif
    paths.deep = g_settings.folders;
    /* Network shares (Settings > Games): searched like the folders chosen. */
    for (const porpoise::netfs::Share &share : porpoise::netfs::shares())
        paths.deep.push_back(porpoise::netfs::root_of(share));
    paths.covers = g_data + "/covers";
    paths.state = g_data + "/library.txt";
    paths.info = shown_info_path();
    return paths;
}

/* One key = value in an INI file's [section], kept with everything else in it. */
/* The Dolphin graphics settings a game's own files change, and the core
 * options that carry the same thing. The core copies every one of its options
 * straight into the running video config whenever any option changes (as the
 * in-game menu does), which would undo the game's own values for the rest of
 * the session; so for each game these options are given the game's values. */
struct GameOption
{
    const char *section, *key, *option;
};
constexpr GameOption kGameOptions[] = {
    {"Video_Hacks", "EFBToTextureEnable", "dolphin_efb_to_texture"},
    {"Video_Hacks", "XFBToTextureEnable", "dolphin_xfb_to_texture_enable"},
    {"Video_Hacks", "EFBAccessEnable", "dolphin_efb_access_enable"},
    {"Video_Hacks", "EFBAccessDeferInvalidation", "dolphin_efb_access_defer_invalidation"},
    {"Video_Hacks", "BBoxEnable", "dolphin_bbox_enabled"},
    {"Video_Hacks", "DisableCopyToVRAM", "dolphin_efb_to_vram"},
    {"Video_Hacks", "DeferEFBCopies", "dolphin_defer_efb_copies"},
    {"Video_Hacks", "ImmediateXFBEnable", "dolphin_immediate_xfb"},
    {"Video_Hacks", "EFBScaledCopy", "dolphin_efb_scaled_copy"},
    {"Video_Hacks", "EFBEmulateFormatChanges", "dolphin_efb_emulate_format_changes"},
    {"Video_Hacks", "VertexRounding", "dolphin_vertex_rounding"},
    {"Video_Hacks", "VISkip", "dolphin_vi_skip"},
    {"Video_Hacks", "FastTextureSampling", "dolphin_fast_texture_sampling"},
    {"Video_Settings", "SafeTextureCacheColorSamples", "dolphin_texture_cache_accuracy"},
};

/* The game's values for kGameOptions, as Dolphin layers its files: Sys then
 * User, each <first letter>, <first three>, <ID>. */
std::vector<std::pair<std::string, std::string>> game_dolphin_options(const std::string &id)
{
    std::vector<std::pair<std::string, std::string>> out;
    if (id.size() != 6)
        return out;
    std::vector<std::string> files;
    for (const std::string &dir : {std::string(PORPOISE_APP "/system/dolphin-emu/Sys/GameSettings"),
                                   g_saves_path + "/User/GameSettings"})
        for (const std::string &name : {id.substr(0, 1), id.substr(0, 3), id})
            files.push_back(dir + "/" + name + ".ini");
    for (const std::string &path : files)
    {
        std::FILE *f = std::fopen(path.c_str(), "r");
        if (!f)
            continue;
        char raw[512];
        std::string section;
        while (std::fgets(raw, sizeof raw, f))
        {
            std::string line = raw;
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (!line.empty() && line[0] == '[')
            {
                section = line.substr(1, line.find(']') == std::string::npos ? std::string::npos : line.find(']') - 1);
                continue;
            }
            const std::size_t eq = line.find('=');
            if (eq == std::string::npos || line[0] == '#' || line[0] == ';')
                continue;
            std::string key = line.substr(0, eq), value = line.substr(eq + 1);
            while (!key.empty() && key.back() == ' ')
                key.pop_back();
            while (!value.empty() && value.front() == ' ')
                value.erase(0, 1);
            for (const GameOption &g : kGameOptions)
                if (section == g.section && key == g.key)
                {
                    std::string v;
                    if (key == "SafeTextureCacheColorSamples")
                    {
                        const long n = std::strtol(value.c_str(), nullptr, 10);
                        v = n <= 0 ? "0" : n > 128 ? "512" : "128";
                    }
                    else
                        v = (value == "True" || value == "true" || value == "1") ? "enabled" : "disabled";
                    bool replaced = false;
                    for (auto &kv : out)
                        if (kv.first == g.option)
                        {
                            kv.second = v;
                            replaced = true;
                        }
                    if (!replaced)
                        out.emplace_back(g.option, v);
                }
        }
        std::fclose(f);
    }
    return out;
}

void set_ini_value(const std::string &path, const std::string &section, const std::string &key,
                   const std::string &value)
{
    std::vector<std::string> lines;
    if (std::FILE *f = std::fopen(path.c_str(), "r"))
    {
        char buf[1024];
        while (std::fgets(buf, sizeof buf, f))
        {
            std::string l = buf;
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r'))
                l.pop_back();
            lines.push_back(l);
        }
        std::fclose(f);
    }
    auto trim = [](std::string s) {
        const auto a = s.find_first_not_of(" \t");
        const auto b = s.find_last_not_of(" \t");
        return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    };
    const std::string header = "[" + section + "]";
    std::size_t sec = lines.size(), end = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (trim(lines[i]) == header)
        {
            sec = i;
            end = lines.size();
            for (std::size_t j = i + 1; j < lines.size(); ++j)
                if (!trim(lines[j]).empty() && trim(lines[j])[0] == '[')
                {
                    end = j;
                    break;
                }
            break;
        }
    const std::string entry = key + " = " + value;
    bool changed = true;
    if (sec == lines.size())
    {
        lines.push_back(header);
        lines.push_back(entry);
    }
    else
    {
        bool done = false;
        for (std::size_t i = sec + 1; i < end && !done; ++i)
        {
            const auto eq = lines[i].find('=');
            if (eq != std::string::npos && trim(lines[i].substr(0, eq)) == key)
            {
                changed = lines[i] != entry;
                lines[i] = entry;
                done = true;
            }
        }
        if (!done)
            lines.insert(lines.begin() + std::ptrdiff_t(end), entry);
    }
    if (!changed)
        return; /* already so: no rewrite (several of these run at each launch) */
    const std::string dir = path.substr(0, path.rfind('/'));
    mkdir(dir.substr(0, dir.rfind('/')).c_str(), 0777);
    mkdir(dir.c_str(), 0777);
    if (std::FILE *f = porpoise::open_atomic(path))
    {
        for (const std::string &l : lines)
            std::fprintf(f, "%s\n", l.c_str());
        porpoise::finish_atomic(f, path);
    }
}

/* The newest release, as the last check wrote it: GitHub's own answer. */
porpoise::update::Release g_release;
std::vector<porpoise::update::Release> g_releases; /* every one GitHub lists, for Choose a version */
void read_latest_release()
{
    /* A beta of Porpoise always looks for the next beta; a final release
     * only when Beta updates is on. */
    const bool betas = g_settings.beta_updates || porpoise::ui::look::kVersionBeta != 0;
    porpoise::update::Release r;
    if (porpoise::update::read_cached(g_data + "/latest-release.json", r, betas))
    {
        g_release = r;
        g_app.set_latest_release(r.tag, r.page, r.size, r.build);
    }
    if (porpoise::update::read_cached_list(g_data + "/latest-release.json", g_releases))
    {
        std::vector<std::string> tags;
        std::vector<int> builds;
        std::vector<bool> pre;
        std::vector<std::size_t> sizes;
        for (const porpoise::update::Release &each : g_releases)
        {
            tags.push_back(each.tag);
            builds.push_back(each.build);
            pre.push_back(each.prerelease);
            sizes.push_back(each.size);
        }
        g_app.set_versions(tags, builds, pre, sizes);
    }
}

/* Where an update goes: the folder Porpoise was installed to, when it's the
 * usual one and holds this same build (the app folder may be served from a
 * copy of it), else the app folder itself. */
std::string install_dir()
{
    auto read_all = [](const std::string &path) {
        std::string text;
        if (std::FILE *f = std::fopen(path.c_str(), "rb"))
        {
            char buf[16384];
            std::size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
                text.append(buf, n);
            std::fclose(f);
        }
        return text;
    };
    const std::string home = "/data/homebrew/PPSA99764";
    const std::string mine = read_all(PORPOISE_APP "/manifest.sha256");
    bool same = !mine.empty() && mine == read_all(home + "/manifest.sha256");
    if (mine.empty())
    {
        /* No manifest (an FTP copy in text mode leaves it out): the same
         * program in both places, by its size and time, is the same copy. */
        struct stat a, b;
        same = stat(PORPOISE_APP "/eboot.bin", &a) == 0 && stat((home + "/eboot.bin").c_str(), &b) == 0 &&
               a.st_size == b.st_size && a.st_mtime == b.st_mtime;
    }
    ps5::debug::mark(("main: the update goes to " + (same ? home : std::string(PORPOISE_APP))).c_str());
    return same ? home : PORPOISE_APP;
}

bool g_covers_again = false; /* a request came while a run was going: ask again once it ends */
bool g_covers_force = false; /* the player asked for covers and info now (Sort & filter) */
std::vector<std::string> g_covers_again_ids; /* Download covers again: these games' art in place of what's there */

void fetch_covers(bool force = false)
{
    g_covers_force |= force;
    /* Not while the updater is online: one download at a time, and it goes first. */
    const porpoise::update::Phase updating = porpoise::update::progress().phase;
    if (updating == porpoise::update::Phase::Checking || updating == porpoise::update::Phase::Downloading ||
        updating == porpoise::update::Phase::Installing || updating == porpoise::update::Phase::Finishing)
        return;
    porpoise::covers::Request request;
    request.dir = g_data + "/covers";
    /* Asked for, it gets both, whatever the settings say about doing it by itself. */
    const bool covers = g_settings.download_covers || g_covers_force, info = g_settings.download_info || g_covers_force;
    request.force = g_covers_force;
    request.again = g_covers_again_ids;
    request.covers = covers;
    request.discs = info;
    if (info)
    {
        request.info_path = info_path();
        request.info_lang = info_lang();
    }
    /* Recommended settings and the update check ride along, once a day,
     * when the player lets Porpoise go online for covers or game info. */
    if (covers || info)
    {
        request.feed_path = g_data + "/recommended.ini";
        request.release_path = g_data + "/latest-release.json";
    }
    for (const porpoise::ui::Game &game : g_library.games())
        if (!game.id.empty())
            request.ids.push_back(game.id);
    /* Games added while a run is going (the daily checks at start, often)
     * used to wait for the next start of Porpoise: now they follow it. */
    g_covers_again = !porpoise::covers::start(request);
    if (g_covers_again && g_covers_force)
        porpoise::covers::stop(); /* the run going now ends early, and this one follows it */
    if (!g_covers_again)
    {
        g_covers_force = false;
        g_covers_again_ids.clear();
    }
}

/* The player's own GameCube BIOS for a region, found in <data>/bios by what
 * is in it (porpoise_bios: any name, in the folder or one inside it, the old
 * USA / EUR / JAP folders too), copied to where Dolphin looks for it
 * (User/GC/<USA|EUR|JAP>/IPL.bin). An NTSC BIOS serves US and Japanese games,
 * a PAL one European games. Porpoise ships none. */
bool install_ipl_for(const std::string &region)
{
    const std::string to_dir = g_saves_path + "/User/GC/" + region, to = to_dir + "/IPL.bin";
    struct stat src_st, dst_st;
    const auto found = porpoise::bios::find_gamecube(g_data + "/bios");
    const porpoise::bios::GameCubeBios *bios = porpoise::bios::for_region(found, region == "EUR");
    if (!bios || stat(bios->path.c_str(), &src_st) != 0)
        return stat(to.c_str(), &dst_st) == 0; /* one already in place */
    auto head = [](const std::string &path, std::vector<char> &out) {
        out.assign(4096, 0);
        std::FILE *f = std::fopen(path.c_str(), "rb");
        const bool got = f && std::fread(out.data(), 1, out.size(), f) == out.size();
        if (f)
            std::fclose(f);
        return got;
    };
    std::vector<char> a, b;
    if (stat(to.c_str(), &dst_st) == 0 && dst_st.st_size == src_st.st_size && head(bios->path, a) && head(to, b) &&
        a == b)
        return true; /* the same one, already copied (its header and revision say so) */
    mkdir((g_saves_path + "/User").c_str(), 0777);
    mkdir((g_saves_path + "/User/GC").c_str(), 0777);
    mkdir(to_dir.c_str(), 0777);
    /* Copied whole beside it, then put in place: a short copy never stays. */
    const std::string part = to + ".part";
    std::FILE *in = std::fopen(bios->path.c_str(), "rb");
    std::FILE *out = in ? std::fopen(part.c_str(), "wb") : nullptr;
    bool ok = in && out;
    char buf[65536];
    std::size_t n;
    while (ok && (n = std::fread(buf, 1, sizeof buf, in)) > 0)
        ok = std::fwrite(buf, 1, n, out) == n;
    if (in)
        std::fclose(in);
    if (out)
        ok = std::fclose(out) == 0 && ok;
    ok = ok && std::rename(part.c_str(), to.c_str()) == 0;
    if (!ok)
        std::remove(part.c_str());
    ps5::debug::mark(ok ? ("main: GameCube BIOS in place for " + region).c_str() : "main: couldn't copy the BIOS");
    return ok;
}

/* A game's region folder: USA, JAP (Japan, Korea) or EUR. */
std::string ipl_region(const porpoise::ui::Game &game)
{
    return game.region == "USA"                               ? "USA"
           : game.region == "Japan" || game.region == "Korea" ? "JAP"
                                                              : "EUR";
}

bool install_ipl(const porpoise::ui::Game &game)
{
    return install_ipl_for(ipl_region(game));
}

/* The search for games runs on its own thread: a big or slow drive (a
 * large exFAT USB drive) once held Porpoise at its start for minutes. The
 * console's own folders are read before the menus (start_library); the
 * drives' games join the library when the search ends, and each place
 * searched gives up after kSearchLimitMs. */
constexpr int kSearchLimitMs = 15000;
struct Search
{
    std::atomic<int> state{0}; /* 0 idle, 1 searching, 2 found (for the menu loop to take) */
    bool again = false;        /* asked again while searching */
    porpoise::ui::LibraryPaths paths;
    std::vector<std::string> files, cut;
    std::vector<porpoise::ui::Game> games; /* the files' headers, read by the worker too */
    long long started_ns = 0;
    long long took_ms = 0;              /* set by the worker as it ends */
    long long last_ms = -1;             /* the last search's time, for the diagnostic test */
    std::vector<std::string> last_cut;  /* and the places it gave up on */
    std::vector<std::string> roots;     /* where this search looks */
    /* At a start: the drives first and network shares after, in a second
     * search, so a share that's off doesn't hold the USB drive's games back. */
    bool shares_next = false;
} g_search;

/* The host's part of Settings > About > Diagnostic test. */
std::vector<std::pair<bool, std::string>> diagnostics()
{
    using porpoise::ui::tr;
    using porpoise::ui::trf;
    std::vector<std::pair<bool, std::string>> out;
#ifndef PORPOISE_DESKTOP
    if (!g_jailbreak_closed.empty())
    {
        std::string stage = g_jailbreak_closed; /* "start: display open, ms" -> "display open" */
        if (stage.rfind("start: ", 0) == 0)
            stage = stage.substr(7);
        if (stage.size() > 4 && stage.compare(stage.size() - 4, 4, ", ms") == 0)
            stage.resize(stage.size() - 4);
        out.emplace_back(false, trf("Last time, Porpoise closed while your jailbreak freed it (it got to: {stage}).",
                                    {{"stage", stage}}));
    }
    if (g_sandboxed && g_stay_sandboxed)
        out.emplace_back(true, tr("Stay in the Sandbox is on: Porpoise doesn't ask your jailbreak to free it."));
    else if (g_sandboxed && porpoise::jailbreak::root_put_back())
        out.emplace_back(false, tr("Your jailbreak freed Porpoise in a way that hid its own files, so it stayed in "
                                   "the sandbox."));
    if (g_data_readonly)
        out.emplace_back(false, tr("Your jailbreak freed Porpoise, but it can't write /data/porpoise, so its "
                                   "settings and saves are in its app folder."));
#endif
    out.emplace_back(writable_dir(g_data), writable_dir(g_data)
                                               ? trf("Porpoise's folder: {path}.", {{"path", g_data}})
                                               : trf("Porpoise can't write to its folder ({path}).", {{"path", g_data}}));
    if (g_search.last_ms >= 0)
        out.emplace_back(g_search.last_ms < 10000,
                         trf("The search for games took {s} s.",
                             {{"s", std::to_string(g_search.last_ms / 1000) + "." +
                                        std::to_string(g_search.last_ms / 100 % 10)}}));
    for (const std::string &root : g_search.last_cut)
        out.emplace_back(false, trf("The search gave up on {place} (too slow). Turn off Find Games Automatically "
                                    "and add your game folder instead.",
                                    {{"place", root}}));
    /* The last game's speed, from its core.log: the middle of its speed lines. */
    if (std::FILE *f = std::fopen(g_core_log.c_str(), "r"))
    {
        std::vector<int> speeds;
        char line[512];
        while (std::fgets(line, sizeof line, f))
            if (const char *at = std::strstr(line, "% speed"))
            {
                const char *p = at;
                while (p > line && p[-1] >= '0' && p[-1] <= '9')
                    --p;
                if (p < at)
                    speeds.push_back(std::atoi(p));
            }
        std::fclose(f);
        if (speeds.size() > 3)
        {
            speeds.erase(speeds.begin(), speeds.begin() + 2); /* its loading */
            std::sort(speeds.begin(), speeds.end());
            const int mid = speeds[speeds.size() / 2];
            out.emplace_back(mid >= 95, trf("The last game ran at about {n}% speed.", {{"n", std::to_string(mid)}}));
        }
    }
    return out;
}

std::vector<std::string> search_roots(const porpoise::ui::LibraryPaths &paths)
{
    std::vector<std::string> roots = paths.roots;
    roots.insert(roots.end(), paths.deep.begin(), paths.deep.end());
    return roots;
}

void *search_worker(void *)
{
    g_search.files = porpoise::ui::find_game_files(g_search.roots, kSearchLimitMs, &g_search.cut);
    /* The headers here too, off the menus' thread: a network share's take a
     * round trip or two each. */
    g_search.games = porpoise::ui::read_games(g_search.files);
    g_search.took_ms = (now_ns() - g_search.started_ns) / 1000000;
    g_search.state.store(2, std::memory_order_release);
    return nullptr;
}

void rescan_library(bool shares_after = false)
{
    if (g_search.state.load(std::memory_order_acquire) != 0)
    {
        g_search.again = true;
        return;
    }
    g_search.paths = library_paths();
    g_search.roots = search_roots(g_search.paths);
    g_search.shares_next = false;
    if (shares_after)
    {
        std::vector<std::string> near;
        for (const std::string &root : g_search.roots)
            if (!porpoise::netfs::is_net(root))
                near.push_back(root);
        if (near.size() != g_search.roots.size())
        {
            g_search.roots = near;
            g_search.shares_next = true;
        }
    }
    g_search.files.clear();
    g_search.games.clear();
    g_search.cut.clear();
    g_search.started_ns = now_ns();
    g_search.state.store(1, std::memory_order_release);
    pthread_t thread;
    if (create_title_thread(&thread, search_worker, nullptr) == 0)
        pthread_detach(thread);
    else
        search_worker(nullptr); /* no thread: search here, as before */
}

/* From the menu loop: a search that has ended joins the library. */
void take_search()
{
    if (g_search.state.load(std::memory_order_acquire) != 2 || !g_app.library_free())
        return;
    /* The same games as the library has (the usual case at a start): nothing
     * to redo. Otherwise every cover was decoded and faded in again, and
     * the cursor went back to the game saved last. */
    auto key_list = [](const std::vector<porpoise::ui::Game> &games) {
        std::vector<std::tuple<std::string, std::string, std::uint64_t, int>> keys;
        keys.reserve(games.size());
        for (const porpoise::ui::Game &g : games)
            keys.emplace_back(g.path, g.id, g.bytes, g.disc_number);
        std::sort(keys.begin(), keys.end());
        return keys;
    };
    const bool same = g_library.games().size() == g_search.games.size() &&
                      key_list(g_library.games()) == key_list(g_search.games);
    if (!same)
    {
        const std::string selected = g_library.selected_id(); /* where the player is now */
        g_app.release_covers();
        g_library.scan_games(g_search.paths, std::move(g_search.games));
        if (!selected.empty())
            g_library.set_selected(selected);
        g_app.library_changed();
    }
    g_search.games.clear();
    g_search.files.clear();
    ps5::debug::mark_value("main: games after the search", static_cast<long long>(g_library.games().size()));
    ps5::debug::mark_value("main: the search took, ms", g_search.took_ms);
    for (const std::string &root : g_search.cut)
        ps5::debug::mark(("main: the search gave up on " + root + " (too slow)").c_str());
    g_search.last_ms = g_search.took_ms; /* the search itself, not how long the menus kept it waiting */
    g_search.last_cut = g_search.cut;
    g_search.state.store(0, std::memory_order_release);
    if (!same)
        fetch_covers();
    if (g_search.shares_next)
    {
        /* Now everything, the shares too (the drives' headers again, from
         * the header cache). */
        g_search.shares_next = false;
        rescan_library();
    }
    else if (g_search.again)
    {
        g_search.again = false;
        rescan_library();
    }
}

/* At the start: the console's own folders now, for the first menus; the
 * drives (and anything else slow) in the background. */
void start_library()
{
    const porpoise::ui::LibraryPaths paths = library_paths();
    std::vector<std::string> local;
    for (const std::string &root : search_roots(paths))
        if (root.rfind("/mnt/", 0) != 0 && !porpoise::netfs::is_net(root))
            local.push_back(root);
    std::vector<std::string> cut;
    g_library.scan_files(paths, porpoise::ui::find_game_files(local, kSearchLimitMs, &cut));
    for (const std::string &root : cut)
        ps5::debug::mark(("main: the search gave up on " + root + " (too slow)").c_str());
    /* The drives and shares in the background; also the console's own
     * folders again when this quick look gave up on one. */
    if (local.size() != search_roots(paths).size() || !cut.empty())
        rescan_library(true);
}

/* 3.1: frame generation (beta) for the game being played: the switches are
 * the game's; how it is tuned only with developer options on. */
void apply_frame_gen()
{
    porpoise::framegen::Config c;
    c.on = g_play.frame_gen;
    c.steady30 = g_play.fg_steady30;
    if (g_settings.developer)
    {
        c.multiplier = 2 + std::clamp(g_settings.fg_multiplier, 0, 2);
        c.pacing = g_settings.fg_pacing;
        c.safety_net = g_settings.fg_safety;
        c.debug_view = g_settings.fg_debug_view;
        c.log = g_settings.fg_log && g_settings.debug_logs;
    }
    porpoise::framegen::set_config(c);
}

/* The game's own controller extras: fast forward and quick save buttons,
 * turbo, and the trigger click where L2 / R2 are the GameCube's L and R. In
 * Porpoise's menus they are all off (apply_settings). */
void apply_game_controls(bool wii_game)
{
    porpoise::core::set_stick_invert(g_play.invert_main, g_play.invert_c);
    porpoise::core::set_player1_port(wii_game ? 0 : g_play.p1_port);
    porpoise::core::set_stick_shape(g_play.stick_sensitivity, g_play.stick_deadzone);
    porpoise::pad::set_fast_forward_buttons(g_play.ff_buttons);
    porpoise::pad::set_quick_buttons(g_play.quick_slot > 0);
    porpoise::pad::set_shot_buttons(g_play.shot_buttons);
    porpoise::pad::set_turbo(g_play.turbo_control());
    const bool gamecube_triggers = !wii_game || g_play.wii_controller == porpoise::pad::WiiGameCube;
    porpoise::pad::set_trigger_feel(gamecube_triggers ? g_play.trigger_feel : 0);
}

/* Settings > Audio > Sound Set: the menus' effects, made in code
 * (porpoise_sfx) or Porpoise's own WAVs. A set that changes plays a step of
 * itself, so it can be heard as it's chosen. */
bool g_sounds_ready = false; /* porpoise::sound::load has run */

void apply_sound_set(int set)
{
    static int current = -1;
    if (!g_sounds_ready || set == current)
        return;
    const bool first = current < 0;
    current = set;
    if (set == porpoise::sfx::Crisp || set == porpoise::sfx::Soft)
    {
        std::vector<std::int16_t> clips[5];
        porpoise::sfx::make(porpoise::sfx::Set(set), clips);
        for (int i = 0; i < 5; ++i)
            porpoise::sound::set_effect(porpoise::sound::Effect(i), clips[i]);
    }
    else
        porpoise::sound::use_own_effects();
    if (!first)
        porpoise::sound::play(porpoise::sound::Effect::GameRow);
}

void apply_settings()
{
    porpoise::sound::set_music(g_settings.menu_music, g_settings.music_volume / 10.0f);
    porpoise::sound::set_effects(g_settings.menu_sounds, g_settings.sounds_volume / 10.0f);
    apply_sound_set(g_settings.sound_set);
    porpoise::pad::set_mapping(g_settings.mapping());
    porpoise::pad::set_rumble_enabled(g_settings.rumble);
    porpoise::pad::set_fast_forward_buttons(g_settings.ff_buttons);
    porpoise::pad::set_wii_buttons(g_settings.wii_buttons_in_use());
    {
        const int lights[4] = {g_settings.light_1, g_settings.light_2, g_settings.light_3, g_settings.light_4};
        porpoise::pad::set_light_colours(lights);
    }
    /* The game's extras are a game's only. */
    porpoise::pad::set_quick_buttons(false);
    porpoise::pad::set_shot_buttons(false);
    porpoise::pad::set_turbo(-1);
    porpoise::pad::set_trigger_feel(0);
    porpoise::pacer::set_vsync(g_settings.vsync);
    {
        /* Settings > Video > Output resolution: porpoise_vk reads it when the
         * display opens, before the settings are loaded. */
        static const char *const kHeights[] = {"0", "1080", "1440", "2160"}; /* 0: match the console */
        const char *want = kHeights[std::clamp(g_settings.output_res, 0, 3)];
        char have[8] = {0};
        if (std::FILE *f = std::fopen(PORPOISE_APP "/porpoise/output.txt", "r"))
        {
            if (!std::fgets(have, sizeof have, f))
                have[0] = 0;
            std::fclose(f);
        }
        if (std::strcmp(have, want) != 0)
            if (std::FILE *f = std::fopen(PORPOISE_APP "/porpoise/output.txt", "w"))
            {
                std::fputs(want, f);
                std::fclose(f);
            }
    }
    /* Settings > About > Performance report: the sampling profiler
     * (sampler_ps5.cpp) reads this file when Porpoise starts. */
    const char *const profile = PORPOISE_APP "/ps5-sampler.txt";
    struct stat st;
    const bool present = stat(profile, &st) == 0;
    if (g_settings.perf_profile && !present)
    {
        if (std::FILE *f = std::fopen(profile, "w"))
        {
            std::fputs("stall-ms 1\nall-threads\nleaves\n", f);
            std::fclose(f);
        }
    }
    else if (!g_settings.perf_profile && present)
        std::remove(profile);
}

/* ---- the launch, as seen by the core host -------------------------------------------- */

/* A long step with a measure while the core loads (Wii System Update). */
void launch_progress(const char *text, unsigned done, unsigned total, void *)
{
    /* "Updating the Wii's system software (12 of 48)": the count as it goes.
     * No total: a step without a measure, the launch screen kept moving. */
    if (total == 0)
    {
        g_app.set_launch_status(porpoise::ui::tr(text), -1.0f);
        /* The screen's own motion goes on with the clock (no frames count it here). */
        static long long last = 0;
        const long long now = now_ns();
        if (last && now > last)
            g_time += std::min(0.1, double(now - last) / 1e9);
        last = now;
    }
    else
        g_app.set_launch_status(porpoise::ui::tr(text) + " (" + std::to_string(std::min(done + 1, total)) + "/" +
                                    std::to_string(total) + ")",
                                float(done) / float(total));
    if (!g_gfx.ready() || porpoise::vk::core_presenting())
        return;
    begin_ui_frame(0.6f);
    g_app.draw_launch(g_time);
    porpoise::vk::present_clear(0, 0, 0);
}

void launch_status(const char *text, void *)
{
    ps5::debug::mark(text);
    g_app.set_launch_status(text, -1);
    if (!g_gfx.ready() || porpoise::vk::core_presenting())
        return;
    begin_ui_frame(0.6f);
    g_app.draw_launch(g_time);
    porpoise::vk::present_clear(0, 0, 0);
}

/* The border beside a 4:3 picture, on whichever device is drawing. */
porpoise::ui::Texture *g_border = nullptr;
std::string g_border_loaded; /* its name; "" none */
bool g_border_tried = false;

void forget_border()
{
    g_border = nullptr; /* its device is going, and the texture with it */
    g_border_tried = false;
}

void draw_border()
{
    if (g_play.border.empty() || !g_gfx.ready())
        return;
    if (!g_border_tried || g_border_loaded != g_play.border)
    {
        if (g_border && g_border_tried)
            g_gfx.free_texture(g_border);
        g_border = nullptr;
        g_border_tried = true;
        g_border_loaded = g_play.border;
        const std::string path = porpoise::borders::path_of(g_play.border);
        if (!path.empty())
            g_border = g_gfx.texture_file(path, 2048); /* full screen: full size */
        if (!g_border)
            ps5::debug::mark(("main: border not found: " + g_play.border).c_str());
    }
    if (!g_border)
        return;
    /* Only for a picture about 4:3 that leaves bars at the sides. */
    float rect[4];
    porpoise::vk::picture_rect(rect);
    const float h = rect[3] > 1 ? rect[3] : 1;
    const float aspect = rect[2] / h;
    if (aspect < 1.15f || aspect > 1.55f || rect[3] < 1000.0f)
        return;
    g_gfx.image(g_border, 0, 0, 1920, 1080, porpoise::ui::rgba(0xFFFFFF));
}

/* RetroAchievements' popups drawn over the game, when the console's own don't
 * reach the screen (porpoise_ra popup): one at a time, top right. */
porpoise::ra::GameToast g_toast;
double g_toast_from = -1;
porpoise::ui::Texture *g_toast_tex = nullptr;

void launch_device_closing(void *)
{
    forget_border();
    g_toast_tex = nullptr; /* the device takes it */
    g_app.forget_textures();
    g_gfx.shutdown();
}

void launch_device_ready(void *)
{
    start_gfx();
    g_app.launch_cover_now();
}

double g_game_rate = 48000.0; /* the game's audio rate, kept while the menu's sounds play */

void play_sound(porpoise::ui::Sound s)
{
    porpoise::sound::play(static_cast<porpoise::sound::Effect>(int(s)));
}

/* 4.0: other games' discs Change Disc can put in (a game asking for its
 * expansion's disc: Samurai Warriors 3 and Xtreme Legends): the library's
 * disc games of the same console, the most alike in name first. */
std::vector<const porpoise::ui::Game *> g_other_discs;

void list_other_discs()
{
    g_other_discs.clear();
    const porpoise::ui::Game *now = g_playing;
    if (!now || now->boot_menu || !now->kind.empty() || porpoise::core::own_disc_count() == 0)
        return;
    for (const porpoise::ui::Game &g : g_library.games())
        if (&g != now && g.kind.empty() && !g.boot_menu && g.platform == now->platform && g.id.size() == 6 &&
            g.id != now->id && !g.path.empty())
            g_other_discs.push_back(&g);
    auto alike = [&](const porpoise::ui::Game *g) {
        std::size_t n = 0;
        while (n < g->title.size() && n < now->title.size() &&
               std::tolower(static_cast<unsigned char>(g->title[n])) ==
                   std::tolower(static_cast<unsigned char>(now->title[n])))
            ++n;
        return n;
    };
    std::stable_sort(g_other_discs.begin(), g_other_discs.end(),
                     [&](const porpoise::ui::Game *a, const porpoise::ui::Game *b) {
                         const std::size_t la = alike(a), lb = alike(b);
                         if (la != lb)
                             return la > lb;
                         return a->title < b->title;
                     });
}

void menu_opened(void *)
{
    g_menu_open = true;
    g_game_rate = porpoise::audio::source_rate();
    list_other_discs();
    const int own = int(porpoise::core::own_disc_count());
    int current = int(porpoise::core::disc_index());
    std::vector<std::pair<std::string, int>> titles;
    for (const porpoise::ui::Game *g : g_other_discs)
        titles.push_back({g->title.empty() ? g->file : g->title, g->disc_number});
    /* The game's own discs, by number: the core's list is the launched one,
     * then the others the launch found (a list of its own, as an .m3u: 1, 2...). */
    std::vector<int> numbers(static_cast<std::size_t>(own));
    for (int i = 0; i < own; ++i)
        numbers[std::size_t(i)] = own == int(g_disc_numbers.size()) ? g_disc_numbers[std::size_t(i)] : i;
    if (current >= own)
    {
        /* Another game's disc is in: which. */
        const std::string in = porpoise::core::disc_path_in();
        current = -1;
        for (std::size_t i = 0; i < g_other_discs.size(); ++i)
            if (g_other_discs[i]->path == in)
                current = own + int(i);
    }
    g_app.set_menu_discs(std::move(numbers), current, std::move(titles));
    g_app.open_game_menu(g_playing, &g_play);
}

void poll_shots(const porpoise::pad::State *pad); /* below */

int menu_paused(void *)
{
    const porpoise::pad::State &pad = porpoise::pad::state();
    porpoise::ui::Input in;
    in.held = pad.buttons;
    /* A real Wii Remote works the in-game menu too (the game has the
     * DolphinBar: its buttons from the reports as they come). */
    in.wii = porpoise::ui::wii_menu_bits(porpoise::usbhid::remote_buttons());
    in.stick_x = pad.left_x / 32768.0f;
    in.stick_y = pad.left_y / 32768.0f;
    g_app.set_sound_pulled(porpoise::audio::pulling());
    const int answer = g_app.update_game_menu(in, 1.0 / 60.0);
    /* Applied settings take effect right away, each in turn. */
    for (std::string key = g_app.take_menu_change(); !key.empty(); key = g_app.take_menu_change())
    {
    if (key == "volume")
        porpoise::audio::set_volume(g_play.volume / 10.0f);
    else if (key == "muted")
        porpoise::audio::set_muted(g_play.muted);
    else if (key == "audio_preset" || key == "audio_buffer" || key == "audio_fill" || key == "audio_stretch" ||
             key == "audio_pull")
    {
        /* The sound: the mixer's buffer and gap filling at once (Classic and
         * pulled sound swap at the next start). */
        if (porpoise::audio::pulling())
            porpoise::audio::set_pull_config(g_play.audio_buffer_ms(), g_play.audio_fill);
        else
        {
            porpoise::audio::set_buffer(g_play.audio_buffer);
            porpoise::audio::set_stretching(g_play.audio_stretch);
        }
    }
    else if (key == "screen_filter" || key == "filter_strength")
        porpoise::core::set_picture(g_play.screen_filter, g_play.filter_strength / 10.0f);
    else if (key == "bloom" || key.rfind("color_", 0) == 0)
    {
        float adjust[4];
        g_play.picture_adjust(adjust);
        porpoise::vk::set_picture_adjust(adjust[0], adjust[1], adjust[2], adjust[3]);
    }
    else if (key == "setup")
    {
        /* A whole setup: the picture and every Dolphin option. */
        porpoise::core::set_picture(g_play.screen_filter, g_play.filter_strength / 10.0f);
        for (const auto &[k, v] : g_play.core_options())
            porpoise::core::set_option(k.c_str(), v.c_str());
    }
    else if (key == "button_layout")
        porpoise::pad::set_mapping(g_play.mapping());
    else if (key == "invert_main" || key == "invert_c")
        porpoise::core::set_stick_invert(g_play.invert_main, g_play.invert_c);
    else if (key == "p1_port")
        porpoise::core::set_player1_port(g_play.p1_port);
    else if (key == "stick_sensitivity" || key == "stick_deadzone")
        porpoise::core::set_stick_shape(g_play.stick_sensitivity, g_play.stick_deadzone);
    else if (key == "real_wiimote_speaker")
        porpoise::usbhid::set_speaker_enabled(g_play.real_wiimote_speaker);
    else if (key == "fast_forward")
        porpoise::core::set_fast_forward(g_app.menu_fast_forward());
    else if (key == "border" || key == "fps_overlay" || key == "motion_readout")
        ; /* drawn by launch_frame */
    else if (key == "frame_gen" || key == "fg_steady30")
        apply_frame_gen();
    else if (key.rfind("wii_", 0) == 0)
    {
        /* The Wii Remote: the pad and the core's ports; the pointer's source
         * is also a Dolphin option. */
        porpoise::core::set_wii(g_play.wii_config(true, g_wii_game_id));
        porpoise::pad::set_wii_buttons(g_play.wii_buttons_in_use());
        apply_game_controls(porpoise::pad::wii().active); /* the GameCube controller gets the trigger click */
        if (key == "wii_pointer" || key == "wii_setup")
            for (const auto &[k, v] : g_play.core_options())
                porpoise::core::set_option(k.c_str(), v.c_str());
    }
    else if (!key.empty())
    {
        /* A Dolphin option: hand the core every value (it applies what it can
         * while running; the rest at the next start). */
        for (const auto &[k, v] : g_play.core_options())
            porpoise::core::set_option(k.c_str(), v.c_str());
        if (key == "rumble")
            porpoise::pad::set_rumble_enabled(g_play.rumble);
        apply_game_controls(porpoise::pad::wii().active);
    }
    }
    /* Save states, asked for in the menu, done here on the core's thread. */
    const porpoise::ui::App::MenuRequest request = g_app.take_menu_request();
    if (request.kind != porpoise::ui::App::MenuRequest::None && g_playing)
    {
        const std::string game = porpoise::ui::Library::key_of(*g_playing);
        if (request.kind == porpoise::ui::App::MenuRequest::Save)
        {
            /* Taken now; written in the background (reported below). */
            if (!porpoise::states::save(game, request.slot))
                g_app.menu_state_done(request.kind, request.slot, false);
        }
        else
        {
            const bool ok = porpoise::states::load(game, request.slot);
            ps5::debug::mark_value("main: state loaded from slot", ok ? request.slot + 1 : -(request.slot + 1));
            g_app.menu_state_done(request.kind, request.slot, ok);
        }
    }
    {
        int slot = 0;
        bool ok = false;
        if (porpoise::states::take_finished(slot, ok))
        {
            ps5::debug::mark_value("main: state saved to slot", ok ? slot + 1 : -(slot + 1));
            g_app.menu_state_done(porpoise::ui::App::MenuRequest::Save, slot, ok);
        }
    }
    poll_shots(nullptr);
    porpoise::sound::pump(); /* the menu's own sounds, while the game is still */
    if (answer != porpoise::core::kMenuStay)
    {
        g_menu_open = false;
        porpoise::audio::flush();
        porpoise::audio::set_source_rate(g_game_rate);
    }
    if (answer == 6) /* porpoise::core::kMenuChangeDisc: the disc chosen */
    {
        const int pick = g_app.take_menu_disc();
        const int own = int(porpoise::core::own_disc_count());
        if (pick >= 0 && pick < own)
            porpoise::core::choose_disc(pick, "");
        else if (pick >= own && pick - own < int(g_other_discs.size()))
        {
            porpoise::core::choose_disc(-1, g_other_discs[std::size_t(pick - own)]->path);
            ps5::debug::mark(("main: another game's disc in: " + g_other_discs[std::size_t(pick - own)]->title).c_str());
        }
    }
    return answer;
}

/* Testing the Wii Remote: what player 1's controller feels, and where the
 * pointer is, over the game. */
/* A Wii game with the gyro pointer: how to centre it, for the first seconds,
 * and a short "centered" each time it is. */
double g_wii_hint_from = -1;      /* g_time the game started, or -1 */
unsigned g_wii_centrings = 0;
double g_wii_centred_at = -100;

void draw_wii_hint()
{
    using namespace porpoise::ui;
    const porpoise::pad::WiiConfig wii = porpoise::pad::wii();
    const bool held = wii.controller != porpoise::pad::WiiSideways && wii.controller != porpoise::pad::WiiClassic &&
                      wii.controller != porpoise::pad::WiiGameCube;
    if (!wii.active || !held || wii.pointer != porpoise::pad::PointerGyro)
        return;
    const porpoise::pad::Motion m = porpoise::pad::snapshot(0).motion;
    if (m.centrings != g_wii_centrings)
    {
        g_wii_centrings = m.centrings;
        g_wii_centred_at = g_time;
    }
    const bool left = porpoise::pad::pose_left_hand(m.pose);
    std::string text;
    float a = 0;
    if (g_time - g_wii_centred_at < 1.4)
    {
        text = tr("Centered");
        a = float(std::min(1.0, (1.4 - (g_time - g_wii_centred_at)) * 3.0));
    }
    else if (g_wii_hint_from >= 0 && g_time >= g_wii_hint_from && g_time - g_wii_hint_from < 10.0 && m.centrings == 0)
    {
        text = tr(left ? "Hold the controller as you'll play, point at the middle of the screen, and hold L1"
                       : "Hold the controller as you'll play, point at the middle of the screen, and hold R1");
        a = float(std::min(1.0, (10.0 - (g_time - g_wii_hint_from)) * 2.0));
    }
    if (text.empty() || a <= 0)
        return;
    const float w = g_gfx.measure(Font::SemiBold, 26, text) + 70, h = 58, x = 960 - w * 0.5f, y = 1080 - 120;
    g_gfx.panel(x, y, w, h, rgba(0x0A1236, 0.82f * a), 0.9f, h * 0.5f, rgba(0x6BE3A8, 0.9f * a), 1.8f);
    g_gfx.text_mid(Font::SemiBold, 26, 960, y + h * 0.5f, rgba(0xF4F7FF, a), Align::Center, text);
}

/* Developer options: the FPS overlay with the frame's times (Detailed Frame
 * Stats), for tuning frame generation. English only, like the motion readout. */
bool frame_stats_detailed()
{
    return g_settings.developer && g_settings.fps_detail;
}

void draw_frame_stats(double fps)
{
    using namespace porpoise::ui;
    const porpoise::framegen::Stats st = porpoise::framegen::stats();
    const porpoise::framegen::Config cfg = porpoise::framegen::config();
    const float x = 36, y = 30, w = 760, h = 290;
    g_gfx.panel(x, y, w, h, rgba(0x0A1236, 0.8f), 0.9f, 14, rgba(0x5CD3FF, 0.9f), 1.6f);
    char line[200];
    float ty = y + 32;
    auto row = [&](const char *text, std::uint32_t colour = 0xF4F7FF) {
        g_gfx.text_mid(Font::SemiBold, 22, x + 22, ty, rgba(colour), Align::Left, text);
        ty += 32;
    };
    std::snprintf(line, sizeof line, "%.1f FPS   game %.1f, shown %.1f, made %.1f", fps, st.game_fps, st.shown_fps,
                  st.generated_fps);
    row(line);
    const bool generating = st.state == porpoise::framegen::State::Generating;
    std::snprintf(line, sizeof line, "Frame Gen  %s%s%s   %dx, %s", cfg.on ? porpoise::framegen::state_name(st.state)
                                                                    : "Off",
                  st.error.empty() ? "" : ": ", st.error.c_str(), cfg.multiplier,
                  cfg.pacing == 1 ? "always" : "steady");
    row(line, generating ? 0x8CFFB0 : cfg.on ? 0xFFD38C : 0xF4F7FF);
    std::snprintf(line, sizeof line, "  steady %u/8, %.2f refreshes a frame, lag +%.1f ms, %u pauses", st.steady,
                  st.refreshes, st.added_lag_ms, st.pauses);
    row(line);
    std::snprintf(line, sizeof line, "  GPU %.2f ms (max %.2f; flow %.2f, in-between %.2f)", st.gpu_ms, st.gpu_max_ms,
                  st.flow_ms, std::max(0.0, st.gpu_ms - st.flow_ms));
    row(line);
    if (st.flips_per_s >= 0)
        std::snprintf(line, sizeof line, "Display  %.2f Hz, %d vblank%s a run, flips %.1f/s (%d queued)", st.vblank_hz,
                      st.vblanks_per_tick, st.vblanks_per_tick == 1 ? "" : "s", st.flips_per_s, st.flips_pending);
    else
        std::snprintf(line, sizeof line, "Display  %.2f Hz, %d vblank%s a run", st.vblank_hz, st.vblanks_per_tick,
                      st.vblanks_per_tick == 1 ? "" : "s");
    row(line);
    std::snprintf(line, sizeof line, "Run  %.2f ms (max %.2f)   present %.2f   pace %.2f   frame gen %.2f", st.run_ms,
                  st.run_max_ms, st.present_ms, st.pace_ms, st.fg_ms);
    row(line);
    std::snprintf(line, sizeof line, "Loop  %.1f runs/s, %d late%s", st.loop_fps, st.misses,
                  cfg.log ? "   logging to debug/framegen-*.csv" : "");
    row(line, st.misses > 0 ? 0xFFD38C : 0xF4F7FF);
    std::snprintf(line, sizeof line, "Picture  %ux%u output", porpoise::vk::screen_width(), porpoise::vk::screen_height());
    row(line);
}

void draw_motion_readout()
{
    using namespace porpoise::ui;
    const porpoise::pad::State p = porpoise::pad::snapshot(0);
    const porpoise::pad::Motion &m = p.motion;
    const float x = 36, y = 100, w = 560, h = 398;
    g_gfx.panel(x, y, w, h, rgba(0x0A1236, 0.8f), 0.9f, 14, rgba(0x5CD3FF, 0.9f), 1.6f);
    char line[160];
    float ty = y + 34;
    auto row = [&](const char *text) {
        g_gfx.text_mid(Font::SemiBold, 22, x + 22, ty, rgba(0xF4F7FF), Align::Left, text);
        ty += 34;
    };
    std::snprintf(line, sizeof line, "Motion %s, %u readings", m.valid ? "on" : "none", m.samples);
    row(line);
    std::snprintf(line, sizeof line, "accel g   %+.2f %+.2f %+.2f", m.raw_accel[0], m.raw_accel[1], m.raw_accel[2]);
    row(line);
    std::snprintf(line, sizeof line, "gyro r/s  %+.2f %+.2f %+.2f", m.raw_gyro[0], m.raw_gyro[1], m.raw_gyro[2]);
    row(line);
    std::snprintf(line, sizeof line, "remote a  %+.2f %+.2f %+.2f", m.accel[0], m.accel[1], m.accel[2]);
    row(line);
    std::snprintf(line, sizeof line, "remote g  %+.2f %+.2f %+.2f", m.gyro[0], m.gyro[1], m.gyro[2]);
    row(line);
    static const char *const kPose[] = {"flat", "upright R (trigger)", "upright L (trigger)", "upright R (facing)",
                                        "upright L (facing)"};
    const porpoise::pad::Motion m2 = porpoise::pad::snapshot(1).motion;
    std::snprintf(line, sizeof line, "hold      %s%s%s", kPose[std::clamp(m.pose, 0, 4)], m2.valid ? "  / 2: " : "",
                  m2.valid ? kPose[std::clamp(m2.pose, 0, 4)] : "");
    row(line);
    std::snprintf(line, sizeof line, "pointer   %+.2f %+.2f  roll %+.0f", m.aim_x, m.aim_y, m.roll * 57.29578f);
    row(line);
    std::snprintf(line, sizeof line, "touch     %s %d, %d", m.touching ? "yes" : "no ", m.touch_x, m.touch_y);
    row(line);
    std::snprintf(line, sizeof line, "shake %s   buttons %04x", m.shaking ? "YES" : "no ", unsigned(p.joypad));
    row(line);
    /* What the game is told the remote's camera sees: the sensor bar's lights. */
    porpoise::aim::Dot dots[2];
    porpoise::aim::sensor_bar(m.aim_x, m.aim_y, m.roll, dots);
    if (g_play.wii_pointer != 2)
        std::snprintf(line, sizeof line, "camera    %4d,%3d  %4d,%3d", dots[0].visible ? int(dots[0].x * 1023) : -1,
                      dots[0].visible ? int(dots[0].y * 767) : -1, dots[1].visible ? int(dots[1].x * 1023) : -1,
                      dots[1].visible ? int(dots[1].y * 767) : -1);
    else
        std::snprintf(line, sizeof line, "camera    (right stick aims)");
    row(line);
    /* Where Porpoise points: a ring over the picture. */
    const float px = 960 + m.pointer_x * 940, py = 540 + m.pointer_y * 520;
    g_gfx.panel(px - 16, py - 16, 32, 32, rgba(0xFFFFFF, 0.0f), 0.0f, 16, rgba(0xFFC85C, 0.95f), 3.0f);
    g_gfx.panel(px - 3, py - 3, 6, 6, rgba(0xFFC85C), 1, 3);
}

/* ---- moving Porpoise's folder to another drive ------------------------------------------ */

struct Mover
{
    std::string from, to;
    std::atomic<long long> done{0}, total{0};
    std::atomic<int> state{0}; /* 1 copying, 2 tidying the old folder, 3 done, 4 failed, 5 stopped */
    std::atomic<bool> stop{false};
    std::string error;
    /* What the copy made (files, then the folders it had to make), so a
     * stop takes back only that: the target may hold things of its own. */
    std::vector<std::string> made_files, made_dirs;
};
Mover g_mover;

long long tree_bytes(const std::string &dir)
{
    long long n = 0;
    if (DIR *d = opendir(dir.c_str()))
    {
        while (dirent *e = readdir(d))
        {
            const std::string name = e->d_name;
            if (name == "." || name == "..")
                continue;
            struct stat st;
            const std::string full = dir + "/" + name;
            if (stat(full.c_str(), &st) != 0)
                continue;
            n += S_ISDIR(st.st_mode) ? tree_bytes(full) : (long long)st.st_size;
        }
        closedir(d);
    }
    return n;
}

/* Copies dir's contents into to; false on a failed write or a stop. The
 * pointer file itself (location.txt) stays behind. */
bool copy_into(const std::string &dir, const std::string &to, bool top, std::vector<char> &buf)
{
    if (mkdir(to.c_str(), 0777) == 0)
        g_mover.made_dirs.push_back(to);
    DIR *d = opendir(dir.c_str());
    if (!d)
        return false;
    bool ok = true;
    while (dirent *e = readdir(d))
    {
        const std::string name = e->d_name;
        if (name == "." || name == ".." || (top && (name == "location.txt" || name == ".write-test")))
            continue;
        if (g_mover.stop.load())
        {
            ok = false;
            break;
        }
        const std::string src = dir + "/" + name, dst = to + "/" + name;
        struct stat st;
        if (stat(src.c_str(), &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
        {
            if (!copy_into(src, dst, false, buf))
            {
                ok = false;
                break;
            }
            continue;
        }
        std::FILE *in = std::fopen(src.c_str(), "rb");
        struct stat before;
        const bool existed = stat(dst.c_str(), &before) == 0;
        std::FILE *out = in ? std::fopen(dst.c_str(), "wb") : nullptr;
        if (out && !existed)
            g_mover.made_files.push_back(dst);
        bool file_ok = in && out;
        while (file_ok && !g_mover.stop.load())
        {
            const std::size_t n = std::fread(buf.data(), 1, buf.size(), in);
            if (n == 0)
                break;
            file_ok = std::fwrite(buf.data(), 1, n, out) == n;
            g_mover.done += (long long)n;
        }
        if (in)
            std::fclose(in);
        if (out)
            file_ok = std::fclose(out) == 0 && file_ok;
        if (!file_ok || g_mover.stop.load())
        {
            ok = false;
            break;
        }
    }
    closedir(d);
    return ok;
}

/* Removes dir's contents (and dir, unless keep_top): location.txt at the top stays. */
void remove_tree(const std::string &dir, bool top)
{
    if (DIR *d = opendir(dir.c_str()))
    {
        while (dirent *e = readdir(d))
        {
            const std::string name = e->d_name;
            if (name == "." || name == ".." || (top && name == "location.txt"))
                continue;
            const std::string full = dir + "/" + name;
            struct stat st;
            if (stat(full.c_str(), &st) != 0)
                continue;
            if (S_ISDIR(st.st_mode))
                remove_tree(full, false);
            else
                std::remove(full.c_str());
        }
        closedir(d);
    }
    if (!top)
        rmdir(dir.c_str());
}

void *mover_worker(void *)
{
    /* A target that already holds a Porpoise folder (settings.ini) is someone's
     * data: the copy would write over it. */
    struct stat st;
    if (g_mover.to != PORPOISE_DATA && stat((g_mover.to + "/settings.ini").c_str(), &st) == 0)
    {
        g_mover.error = "That place already has a Porpoise folder. Move or rename it first, then try again.";
        g_mover.state = 4;
        return nullptr;
    }
    std::vector<char> buf(std::size_t(4) << 20);
    const bool ok = copy_into(g_mover.from, g_mover.to, true, buf);
    if (!ok)
    {
        /* Stopped or failed: what the copy made goes (only that: the target
         * may be a folder of its own, such as /data/porpoise with its
         * location.txt), and the folder stays where it was. */
        for (auto it = g_mover.made_files.rbegin(); it != g_mover.made_files.rend(); ++it)
            std::remove(it->c_str());
        for (auto it = g_mover.made_dirs.rbegin(); it != g_mover.made_dirs.rend(); ++it)
            rmdir(it->c_str()); /* only when empty */
        if (g_mover.stop.load())
            g_mover.state = 5;
        else
        {
            g_mover.error = "The move didn't finish (the drive may be full or not writable). Porpoise's folder "
                            "stays where it was.";
            g_mover.state = 4;
        }
        return nullptr;
    }
    /* Everything is there: the pointer, then the old copy goes; only once the
     * pointer is on the disk and reads back right (else Porpoise would start
     * from an emptied folder, the data orphaned on the drive). */
    g_mover.state = 2;
    const std::string pointer = std::string(PORPOISE_DATA) + "/location.txt";
    bool pointed = false;
    if (g_mover.to == PORPOISE_DATA)
        pointed = std::remove(pointer.c_str()) == 0 || errno == ENOENT;
    else if (std::FILE *f = porpoise::open_atomic(pointer))
    {
        std::fprintf(f, "%s\n", g_mover.to.c_str());
        if (porpoise::finish_atomic(f, pointer))
            if (std::FILE *r = std::fopen(pointer.c_str(), "r"))
            {
                char line[512] = {0};
                std::string back = std::fgets(line, sizeof line, r) ? line : "";
                std::fclose(r);
                while (!back.empty() && (back.back() == '\n' || back.back() == '\r'))
                    back.pop_back();
                pointed = back == g_mover.to;
            }
    }
    if (!pointed)
    {
        /* The folder in use is still the old one: the copy goes again. */
        for (auto it = g_mover.made_files.rbegin(); it != g_mover.made_files.rend(); ++it)
            std::remove(it->c_str());
        for (auto it = g_mover.made_dirs.rbegin(); it != g_mover.made_dirs.rend(); ++it)
            rmdir(it->c_str());
        g_mover.error = "The move didn't finish (the drive may be full or not writable). Porpoise's folder "
                        "stays where it was.";
        g_mover.state = 4;
        return nullptr;
    }
    remove_tree(g_mover.from, g_mover.from == PORPOISE_DATA);
    g_mover.state = 3;
    return nullptr;
}

void draw_mover(double hz)
{
    g_time += 1.0 / hz;
    begin_ui_frame(0.6f);
    g_app.draw(g_time);
    using namespace porpoise::ui;
    g_gfx.panel(0, 0, 1920, 1080, rgba(0x02040C, 0.6f), 1, 0);
    g_gfx.panel(560, 380, 800, 320, rgba(0x0F1F63, 0.92f), 0.9f, 28, rgba(0x8FB4FF, 0.9f), 2.0f, 12, 0.15f);
    const int state = g_mover.state.load();
    g_gfx.text_mid(Font::Bold, 40, 960, 450, rgba(0xFFFFFF), Align::Center,
                   tr(state == 2 ? "Tidying up the old folder\xE2\x80\xA6" : "Moving Porpoise's folder\xE2\x80\xA6"));
    const double total = double(std::max(1LL, g_mover.total.load()));
    const float part = float(std::min(1.0, double(g_mover.done.load()) / total));
    g_gfx.panel(620, 520, 680, 26, rgba(0x07102E, 0.9f), 1, 13, rgba(0x3D4F9E, 0.9f), 1.4f);
    g_gfx.panel(620, 520, std::max(26.0f, 680 * part), 26, rgba(0x5CD3FF, 0.95f), 1, 13);
    char amount[96];
    std::snprintf(amount, sizeof amount, "%.1f / %.1f GB", double(g_mover.done.load()) / 1e9, total / 1e9);
    g_gfx.text_mid(Font::SemiBold, 28, 960, 590, rgba(0xC9D6FF), Align::Center, amount);
    if (state == 1)
        g_gfx.text_mid(Font::Regular, 24, 960, 645, rgba(0x9FB0E8), Align::Center, tr("Circle: stop (nothing changes)"));
    porpoise::vk::present_clear(0, 0, 0);
    porpoise::sound::pump();
    g_pacer.frame_done();
}

/* Porpoise's folder changed: a note, then Porpoise closes so it opens from there. */
[[noreturn]] void close_for_folder(const std::string &title, double hz)
{
    for (int frame = 0; frame < int(hz * 2.5); ++frame)
    {
        begin_ui_frame(0.6f);
        g_app.draw(g_time);
        using namespace porpoise::ui;
        g_gfx.panel(560, 400, 800, 260, rgba(0x0F1F63, 0.92f), 0.9f, 28, rgba(0x6BE3A8, 0.9f), 2.0f, 12, 0.15f);
        g_gfx.text_mid(Font::Bold, 38, 960, 480, rgba(0xFFFFFF), Align::Center, title);
        g_gfx.text_mid(Font::Regular, 26, 960, 560, rgba(0xC9D6FF), Align::Center,
                       tr("Porpoise closes now. Open it again."));
        porpoise::vk::present_clear(0, 0, 0);
        g_pacer.frame_done();
    }
    leave(0);
}

/* Settings > Games > Move Porpoise's folder: everything copied to the drive,
 * the pointer written, the old copy removed; then Porpoise closes so it opens
 * from there. */
void move_data(const std::string &to, double hz)
{
    ps5::debug::mark(("main: moving Porpoise's folder from " + g_data + " to " + to).c_str());
    g_mover.from = g_data;
    g_mover.to = to;
    g_mover.done = 0;
    g_mover.total = tree_bytes(g_data);
    g_mover.stop = false;
    g_mover.error.clear();
    g_mover.made_files.clear();
    g_mover.made_dirs.clear();
    g_mover.state = 1;
    porpoise::covers::stop();
    pthread_t thread;
    const bool threaded = create_title_thread(&thread, mover_worker, nullptr) == 0;
    if (!threaded)
        mover_worker(nullptr);
    while (g_mover.state.load() == 1 || g_mover.state.load() == 2)
    {
        const porpoise::pad::State &pad = porpoise::pad::poll();
        if ((pad.buttons & porpoise::pad::BtnCircle) && g_mover.state.load() == 1)
            g_mover.stop = true;
        draw_mover(hz);
    }
    if (threaded)
        pthread_join(thread, nullptr);
    const int state = g_mover.state.load();
    ps5::debug::mark_value("main: the move ended, state", state);
    if (state == 3)
        close_for_folder(porpoise::ui::tr("Porpoise's folder moved"), hz);
    if (state == 4)
        g_app.show_message(porpoise::ui::tr("The move didn't finish"), porpoise::ui::tr(g_mover.error));
}

/* A Porpoise folder that's already on a drive becomes the one in use
 * (Settings > Games, or a first start that found it): only the pointer
 * changes. Nothing is copied or removed, so both folders stay as they are. */
void use_data(const std::string &to, double hz)
{
    ps5::debug::mark(("main: using the Porpoise folder at " + to).c_str());
    const std::string pointer = std::string(PORPOISE_DATA) + "/location.txt";
    mkdir(PORPOISE_DATA, 0777);
    bool pointed = false;
    if (to == PORPOISE_DATA)
        pointed = std::remove(pointer.c_str()) == 0 || errno == ENOENT;
    else if (std::FILE *f = porpoise::open_atomic(pointer))
    {
        std::fprintf(f, "%s\n", to.c_str());
        pointed = porpoise::finish_atomic(f, pointer);
    }
    if (!pointed)
    {
        g_app.show_message(porpoise::ui::tr("Porpoise couldn't switch folders"),
                           porpoise::ui::tr("Writing the note that says where Porpoise's folder is didn't work, so "
                                            "nothing changed."));
        return;
    }
    close_for_folder(porpoise::ui::tr("Porpoise will use that folder"), hz);
}

/* The game's texture pack, counted as it starts (in the background: a pack
 * can be tens of thousands of files) and shown for a few seconds over the
 * game, as Dolphin's own note did before Porpoise hid Dolphin's messages. */
std::atomic<int> g_texture_count{-1};
std::string g_texture_pack;
double g_texture_note_from = 0;

int count_textures(const std::string &dir, int depth)
{
    int n = 0;
    if (DIR *d = opendir(dir.c_str()))
    {
        while (dirent *e = readdir(d))
        {
            const std::string name = e->d_name;
            if (name.empty() || name[0] == '.')
                continue;
            const std::string full = dir + "/" + name;
            /* The listing says what each entry is; asked only when it doesn't
             * (a pack is tens of thousands of files, on USB a while each). */
            bool is_dir = e->d_type == DT_DIR;
            if (e->d_type == DT_UNKNOWN || e->d_type == DT_LNK)
            {
                struct stat st;
                if (stat(full.c_str(), &st) != 0)
                    continue;
                is_dir = S_ISDIR(st.st_mode);
            }
            else if (e->d_type != DT_REG && !is_dir)
                continue;
            if (is_dir)
            {
                if (depth < 8)
                    n += count_textures(full, depth + 1);
            }
            else if (name.size() > 4)
            {
                std::string ext = name.substr(name.size() - 4);
                for (char &c : ext)
                    c = char(std::tolower(static_cast<unsigned char>(c)));
                if (ext == ".png" || ext == ".dds")
                    ++n;
            }
        }
        closedir(d);
    }
    return n;
}

void *texture_counter(void *arg)
{
    std::string *dir = static_cast<std::string *>(arg);
    g_texture_count.store(count_textures(*dir, 0));
    delete dir;
    return nullptr;
}

/* Looks for the game's pack (Load/Textures/<ID>, or its first three letters)
 * and starts counting it. */
void start_texture_note(const porpoise::ui::Game &game)
{
    g_texture_count.store(-1);
    g_texture_pack.clear();
    if (!g_play.custom_textures || game.id.empty())
        return;
    const std::string root = g_saves_path + "/User/Load/Textures/";
    for (const std::string &name : {game.id, game.id.substr(0, std::min<std::size_t>(3, game.id.size()))})
    {
        struct stat st;
        if (stat((root + name).c_str(), &st) == 0 && S_ISDIR(st.st_mode))
        {
            g_texture_pack = name;
            break;
        }
    }
    if (g_texture_pack.empty())
        return;
    g_texture_note_from = 0;
    pthread_t thread;
    if (create_title_thread(&thread, texture_counter, new std::string(root + g_texture_pack)) == 0)
        pthread_detach(thread);
}

void draw_texture_note()
{
    const int n = g_texture_count.load();
    if (n < 0 || g_texture_pack.empty())
        return;
    if (g_texture_note_from == 0)
        g_texture_note_from = g_time;
    const double age = g_time - g_texture_note_from;
    if (age > 8.0)
        return;
    const float a = float(age < 0.4 ? age / 0.4 : age > 7.2 ? (8.0 - age) / 0.8 : 1.0);
    const std::string text =
        n > 0 ? porpoise::ui::trf(n == 1 ? "Custom Textures: 1 found ({pack})" : "Custom Textures: {n} found ({pack})",
                                  {{"n", std::to_string(n)}, {"pack", g_texture_pack}})
              : porpoise::ui::trf("Custom Textures: none found in {pack}", {{"pack", g_texture_pack}});
    const float y = g_play.fps_overlay ? (frame_stats_detailed() ? 334 : 96) : 30;
    const float w = g_gfx.measure(porpoise::ui::Font::SemiBold, 26, text) + 48;
    g_gfx.panel(36, y, w, 50, porpoise::ui::rgba(0x0A1236, 0.72f * a), 0.9f * a, 14,
                porpoise::ui::rgba(0x5CD3FF, 0.9f * a), 1.6f);
    g_gfx.text_mid(porpoise::ui::Font::SemiBold, 26, 60, y + 25, porpoise::ui::rgba(0xF4F7FF, a),
                   porpoise::ui::Align::Left, text);
}

/* Quick save buttons (touch pad + L1 / L2): a short note over the game. */
std::string g_quick_note;
double g_quick_note_from = -100;

void quick_note(const std::string &text)
{
    g_quick_note = text;
    g_quick_note_from = g_time;
}

/* Touch pad + L1: the game's state into its quick slot (written in the
 * background, as the menu's saves are); touch pad + L2: that slot back. Here,
 * on the game's own thread between two of its frames. */
void quick_states(const porpoise::pad::State &pad)
{
    using porpoise::ui::trf;
    if (!g_playing || g_play.quick_slot <= 0)
        return;
    const std::string game = porpoise::ui::Library::key_of(*g_playing);
    const int slot = std::clamp(g_play.quick_slot, 1, porpoise::states::kSlots) - 1;
    const std::string n = std::to_string(slot + 1);
    if (pad.quick_save)
    {
        if (porpoise::states::busy())
            quick_note(porpoise::ui::tr("Still saving\xE2\x80\xA6"));
        else if (porpoise::states::save(game, slot))
            quick_note(trf("Saving to slot {n}\xE2\x80\xA6", {{"n", n}}));
        else
            quick_note(trf("Couldn't save to slot {n}", {{"n", n}}));
    }
    else if (pad.quick_load)
    {
        porpoise::states::wait(); /* a save still being written goes first */
        if (!porpoise::states::slot(game, slot).exists)
            quick_note(trf("Slot {n} is empty", {{"n", n}}));
        else
        {
            const bool ok = porpoise::states::load(game, slot);
            ps5::debug::mark_value("main: quick load from slot", ok ? slot + 1 : -(slot + 1));
            quick_note(ok ? trf("Loaded slot {n}", {{"n", n}}) : trf("Couldn't load slot {n}", {{"n", n}}));
        }
    }
    int done = 0;
    bool ok = false;
    if (porpoise::states::take_finished(done, ok))
    {
        ps5::debug::mark_value("main: quick save to slot", ok ? done + 1 : -(done + 1));
        quick_note(ok ? trf("Saved to slot {n}", {{"n", std::to_string(done + 1)}})
                      : trf("Couldn't save to slot {n}", {{"n", std::to_string(done + 1)}}));
    }
}

/* Screenshots: touch pad + Square in a game, Take Screenshot in its menu;
 * each written in the background, and said when it's done. */
void poll_shots(const porpoise::pad::State *pad)
{
    using porpoise::ui::tr;
    if (g_playing)
    {
        const bool asked = pad ? pad->screenshot : g_app.take_menu_shot();
        if (asked)
        {
            const bool busy = porpoise::shots::busy();
            const bool started = !busy && porpoise::shots::take(porpoise::ui::Library::key_of(*g_playing));
            const std::string note = busy      ? tr("Still saving\xE2\x80\xA6")
                                     : started ? tr("Saving the screenshot\xE2\x80\xA6")
                                               : tr("No picture to save yet.");
            if (pad)
                quick_note(note);
            else
                g_app.menu_note(note);
        }
    }
    bool ok = false;
    if (porpoise::shots::take_finished(ok))
    {
        const std::string note = ok ? tr("Screenshot saved.") : tr("The screenshot couldn't be saved.");
        if (g_menu_open)
            g_app.menu_note(note);
        else
            quick_note(note);
    }
}

void draw_quick_note()
{
    using namespace porpoise::ui;
    const double age = g_time - g_quick_note_from;
    if (age > 2.5 || g_quick_note.empty())
        return;
    const float a = float(std::min(1.0, std::min(age / 0.2, (2.5 - age) / 0.4)));
    const float w = g_gfx.measure(Font::Bold, 28, g_quick_note) + 60;
    g_gfx.panel(40, 1080 - 40 - 56, w, 56, rgba(0x0A1236, 0.82f * a), 0.9f * a, 16, rgba(0x6BE3A8, 0.9f * a), 1.6f);
    g_gfx.text_mid(Font::Bold, 28, 70, 1080 - 40 - 28, rgba(0xF4F7FF, a), Align::Left, g_quick_note);
}

void draw_game_toast()
{
    using namespace porpoise::ui;
    if (g_toast_from >= 0 && g_time - g_toast_from > 6.0)
    {
        if (g_toast_tex)
            g_gfx.free_texture(g_toast_tex);
        g_toast_tex = nullptr;
        g_toast_from = -1;
    }
    if (g_toast_from < 0)
    {
        if (!porpoise::ra::take_game_toast(g_toast))
            return;
        g_toast_from = g_time;
    }
    const double age = g_time - g_toast_from;
    const float in = float(std::min(1.0, age / 0.35)), out = float(std::min(1.0, std::max(0.0, (6.0 - age) / 0.5)));
    const float a = std::min(in, out), slide = (1.0f - in) * 60.0f;
    if (!g_toast_tex && !g_toast.badge_path.empty())
    {
        struct stat st;
        if (stat(g_toast.badge_path.c_str(), &st) == 0 && st.st_size > 0)
        {
            bool pending = false;
            g_toast_tex = g_gfx.texture_file_async(g_toast.badge_path, &pending, 128);
        }
    }
    const float w = 600, h = 116, x = 1920 - 40 - w + slide, y = 40;
    const Color edge = g_toast.trophy ? rgba(0xFFD45C, 0.95f * a) : rgba(0x5CD3FF, 0.9f * a);
    g_gfx.panel(x, y, w, h, rgba(0x0A1236, 0.86f * a), 0.95f * a, 18, edge, 2.0f, 10, 0.2f * a);
    const float bs = 84, bx = x + 16, by = y + (h - bs) * 0.5f;
    if (g_toast_tex)
        g_gfx.image(g_toast_tex, bx, by, bs, bs, rgba(0xFFFFFF, a), 12);
    else
        g_gfx.panel(bx, by, bs, bs, rgba(0x13308A, 0.8f * a), a, 12, rgba(0x8BD9FF, 0.6f * a), 1.4f);
    const float tx = bx + bs + 18, tw = x + w - 20 - tx;
    g_gfx.text_mid(Font::SemiBold, 19, tx, y + 26, g_toast.trophy ? rgba(0xFFD45C, a) : rgba(0x5CD3FF, a), Align::Left,
                   look::fit(g_gfx, Font::SemiBold, 19, g_toast.caption, tw));
    g_gfx.text_mid(Font::Bold, 26, tx, y + 58, rgba(0xF4F7FF, a), Align::Left,
                   look::fit(g_gfx, Font::Bold, 26, g_toast.title, tw));
    g_gfx.text_mid(Font::Regular, 20, tx, y + 90, rgba(0xC9D3FF, a), Align::Left,
                   look::fit(g_gfx, Font::Regular, 20, g_toast.text, tw));
}

void launch_frame(bool core_frame, double fps, void *)
{
    if (core_frame && !g_boot_marker.empty())
    {
        std::remove(g_boot_marker.c_str()); /* the game showed its first picture */
        g_boot_marker.clear();
    }
    porpoise::ra::pump(); /* the game's achievements list and badges, after an unlock */
    g_time += 1.0 / 60.0;
    if (!g_gfx.ready())
        return;
    begin_ui_frame(core_frame ? 0.0f : 0.6f);
    {
        /* Over the game: plain panels (liquid glass bends the menus' room). */
        porpoise::ui::Look over_game = g_gfx.look();
        over_game.panel_style = 0;
        g_gfx.set_look(over_game);
    }
    if (!core_frame)
    {
        g_app.draw_launch(g_time);
        return;
    }
    if (!g_menu_open)
    {
        /* Touch pad + R1: the next speed (off, 2x, 4x); touch pad + R2:
         * 4x while held, back to the chosen speed after. */
        const porpoise::pad::State &pad = porpoise::pad::state();
        static bool holding = false;
        if (pad.ff_step)
        {
            g_app.step_fast_forward();
            porpoise::core::set_fast_forward(g_app.menu_fast_forward());
        }
        if (pad.ff_hold != holding)
        {
            holding = pad.ff_hold;
            porpoise::core::set_fast_forward(holding ? 4 : g_app.menu_fast_forward());
        }
        quick_states(pad);
        poll_shots(&pad);
    }
    draw_border();
    if ((g_app.menu_fast_forward() > 1 || porpoise::core::fast_forward() > 1) && !g_menu_open)
    {
        /* Fast forward is on: say so, top right. */
        const char *text = porpoise::core::fast_forward() >= 4 ? "4x" : "2x";
        g_gfx.panel(1884 - 150, 30, 150, 50, porpoise::ui::rgba(0x0A1236, 0.72f), 0.9f, 14,
                    porpoise::ui::rgba(0xFFC85C, 0.9f), 1.6f);
        const float kHalfPi = 1.5707963f;
        g_gfx.glyph(porpoise::ui::Glyph::Arrow, 1884 - 116, 55, 20, porpoise::ui::rgba(0xFFE7B0), kHalfPi);
        g_gfx.glyph(porpoise::ui::Glyph::Arrow, 1884 - 100, 55, 20, porpoise::ui::rgba(0xFFE7B0), kHalfPi);
        g_gfx.text_mid(porpoise::ui::Font::Bold, 28, 1884 - 52, 55, porpoise::ui::rgba(0xFFE7B0),
                       porpoise::ui::Align::Center, text);
    }
    if (g_play.fps_overlay && !g_menu_open && frame_stats_detailed())
        draw_frame_stats(fps);
    else if (g_play.fps_overlay && !g_menu_open)
    {
        /* 3.1: with frame generation making frames, what is shown too. */
        const porpoise::framegen::Stats fg = porpoise::framegen::stats();
        char text[48];
        /* (the game's own frames a second, then what is shown) */
        if (fg.state == porpoise::framegen::State::Generating && fg.game_fps > 1.0 && fg.shown_fps > fg.game_fps + 1.0)
            std::snprintf(text, sizeof text, "%.0f / %.0f FPS", fg.game_fps, fg.shown_fps);
        else
            std::snprintf(text, sizeof text, "%.0f FPS", fps);
        const float w = std::max(150.0f, g_gfx.measure(porpoise::ui::Font::Bold, 28, text) + 40);
        g_gfx.panel(36, 30, w, 50, porpoise::ui::rgba(0x0A1236, 0.72f), 0.9f, 14,
                    porpoise::ui::rgba(0x5CD3FF, 0.9f), 1.6f);
        g_gfx.text_mid(porpoise::ui::Font::Bold, 28, 36 + w * 0.5f, 55, porpoise::ui::rgba(0xF4F7FF),
                   porpoise::ui::Align::Center, text);
    }
    if (!g_menu_open)
    {
        draw_texture_note();
        draw_game_toast();
        draw_quick_note();
    }
    if (g_play.motion_readout && g_play.developer && !g_menu_open)
        draw_motion_readout();
    if (!g_menu_open)
        draw_wii_hint();
    if (g_menu_open)
        g_app.draw_game_menu(g_time);
    porpoise::ui::draw_wiimote_notices(g_gfx); /* real Wii Remotes coming and going, over everything */
    g_app.draw_curtain(g_curtain);
}

/* The start: Porpoise's mark fades in on black, and stays while the library
 * is read; the launcher's first frames lift the curtain off it. */
void show_boot_mark()
{
    porpoise::ui::Texture *mark = g_gfx.texture_file(PORPOISE_APP "/assets/brand/logo.png");
    g_pacer.start(porpoise::vk::refresh_hz(), "boot");
    const float hz = float(porpoise::vk::refresh_hz() > 10 ? porpoise::vk::refresh_hz() : 60.0);
    /* The black, then the dolphin rising out of it with a soft glow. */
    const int frames = int(hz * 0.55f); /* 2.7: 0.9 s before */
    for (int frame = 0; frame <= frames; ++frame)
    {
        const float t = float(frame) / float(frames);
        const float a = t * t * (3.0f - 2.0f * t);
        begin_ui_frame(0.0f);
        g_gfx.panel(-20, -20, 1960, 1120, porpoise::ui::rgba(0x02040C), 1.0f, 0);
        g_gfx.blob(960, 540, 900 * (0.6f + 0.4f * a), 900 * (0.6f + 0.4f * a), porpoise::ui::rgba(0x2F6BFF, 0.16f * a));
        if (mark && mark->width > 0)
        {
            const float w = 220 * (0.92f + 0.08f * a), h = w * float(mark->height) / float(mark->width);
            g_gfx.image(mark, 960 - w * 0.5f, 540 - h * 0.5f + 10.0f * (1.0f - a), w, h,
                        porpoise::ui::rgba(0xFFFFFF, a));
        }
        porpoise::vk::present_clear(0.008f, 0.016f, 0.047f);
        if (frame == 0)
            ps5::debug::mark_value("main: splash hidden", sceSystemServiceHideSplashScreen());
        g_pacer.frame_done();
    }
    if (mark)
        g_gfx.free_texture(mark);
    g_curtain = 1.0f; /* lifted by the launcher's first frames */
}

void leaving_game(float amount, void *)
{
    /* The menu stays under the curtain as it comes down, not gone at once. */
    g_curtain = amount;
    if (amount >= 1.0f)
        g_menu_open = false;
}

/* The menus' entrance, at the start and back from a game. The curtain (black,
 * the dolphin in the middle) stays while the library's pictures come in, so
 * nothing hitches on screen; then the dolphin and the black fade off the
 * background, and the menus fade and rise in as the music comes up. */
struct Entrance
{
    bool on = false, lifting = false;
    double held = 0, lifted = 0;
    int quiet = 0;  /* frames in a row with no picture loading */
    int steady = 0; /* frames in a row drawn in time */
};
Entrance g_entrance;
constexpr double kEntranceMinHold = 0.2, kEntranceMaxHold = 3.0;
constexpr double kEntranceLift = 0.5, kEntranceUiFrom = 0.2, kEntranceUi = 0.6; /* 2.7: quicker into the menus */
constexpr float kEntranceRise = 28.0f;

void start_entrance()
{
    g_entrance = Entrance{};
    g_entrance.on = true;
    g_curtain = 1.0f;
    g_app.set_intro(0.0f, kEntranceRise);
    porpoise::sound::fade_music(0.0f, 0.0f);
}

/* Once a launcher frame, before the menus update: true while the entrance
 * keeps the player's buttons from the menus. */
bool entrance_frame(double dt, double frame_ms)
{
    Entrance &e = g_entrance;
    if (!e.on)
        return false;
    if (!e.lifting)
    {
        e.held += dt;
        e.quiet = g_gfx.loading() ? 0 : e.quiet + 1;
        /* Only once frames come on time: a fade drawn through hitches looks
         * like a cut. */
        e.steady = frame_ms < 1500.0 * dt ? e.steady + 1 : 0;
        if ((e.quiet >= 8 && e.steady >= 12 && e.held >= kEntranceMinHold) || e.held >= kEntranceMaxHold)
        {
            e.lifting = true;
            porpoise::sound::fade_music(1.0f, 2.0f);
            char line[80];
            std::snprintf(line, sizeof line, "main: menus in after %.0f ms behind the curtain", e.held * 1000.0);
            ps5::debug::mark(line);
        }
        g_curtain = 1.0f;
        return true;
    }
    e.lifted += dt;
    auto smooth = [](double x) {
        x = std::clamp(x, 0.0, 1.0);
        return float(x * x * (3.0 - 2.0 * x));
    };
    g_curtain = 1.0f - smooth(e.lifted / kEntranceLift);
    const float ui = smooth((e.lifted - kEntranceUiFrom) / kEntranceUi);
    g_app.set_intro(ui, kEntranceRise * (1.0f - ui));
    if (e.lifted >= kEntranceUiFrom + kEntranceUi)
    {
        e.on = false;
        g_curtain = 0.0f;
        g_app.set_intro(1.0f, 0.0f);
        g_app.swallow_held(); /* a button held through it doesn't press anything */
        return false;
    }
    return true;
}
} // namespace

extern "C" int sceKernelGetCurrentCpu(void);
extern "C" void ps5_core_threads_set_affinity(unsigned long long mask);
long long g_main_ns = 0;

/* Settings > System > Emulator on its own cores. A title's threads run on
 * processors 0-12 (PS5_PayloadSDK PROBE.md: mask 0x1fff), the two halves of
 * each core numbered side by side. While a game runs, Dolphin's CPU thread
 * keeps to processor 10 and its video loop (this thread) to processor 8, and
 * every other thread to the rest, so neither shares its core with busy work.
 * Only on the mask that was measured; anything else is left alone. */
constexpr unsigned long long kTitleCpus = 0x1fff;
constexpr unsigned long long kEmulatorCores = 0xf00; /* processors 8-11: two cores */
unsigned long long g_start_cpus = 0;

void keep_cores(bool on)
{
    if (g_start_cpus != kTitleCpus)
        return;
    const unsigned long long rest = on ? kTitleCpus & ~kEmulatorCores : kTitleCpus;
    const int self = ps5_pthread_setaffinity_np(pthread_self(), sizeof rest, &rest);
    ps5_core_threads_set_affinity(on ? rest : 0);
    /* The title's own threads started later go there too (title_threads.hpp). */
    g_title_thread_pin = ps5_pthread_setaffinity_np;
    g_title_thread_cpus.store(on ? rest : 0);
    if (on)
    {
        setenv("PORPOISE_CPU_THREAD_CPUS", "400", 1);   /* processor 10 */
        setenv("PORPOISE_VIDEO_THREAD_CPUS", "100", 1); /* processor 8, once the game runs */
    }
    else
    {
        unsetenv("PORPOISE_CPU_THREAD_CPUS");
        unsetenv("PORPOISE_VIDEO_THREAD_CPUS");
    }
    char line[96];
    std::snprintf(line, sizeof line, "main: emulator on its own cores %s (%d)", on ? "on" : "off", self);
    ps5::debug::mark(line);
}
/* For the trace: how long Porpoise took to reach each step of its start. */
void mark_start(const char *what)
{
    ps5::debug::mark_value(what, (now_ns() - g_main_ns) / 1000000);
    porpoise::jailbreak::stage(what); /* how far a start after being freed got */
}

/* argc and argv: a forwarder's --rom and --exit-after-game (porpoise_forward.hpp;
 * on the PS5, app_crt passes the launch arguments). */
int main(int argc, char **argv)
{
#ifdef PORPOISE_DESKTOP
    /* The window first: it also makes the program's folder the working one. */
    if (!porpoise::platform::init(argc, argv))
        return 1;
#endif
    g_main_ns = now_ns();
    ps5::debug::mark("Porpoise: main() entered");
    /* A home screen forwarder's game (--rom; porpoise_forward.hpp): started
     * straight away once the library is read, with no logo or menus first. */
    const porpoise::forward::Args forwarded = porpoise::forward::parse(argc, argv);
    /* Before any thread is started: clone this process's credential, so a Lapy
     * owned-root daemon will free it later (choose_data_dir -> jailbreak). */
    porpoise::jailbreak::prepare();
#ifndef PORPOISE_DESKTOP
    /* Ask to be freed now, while Porpoise is still one thread: the Lapy
     * owned-root daemon frees only a single-threaded process, and one freed
     * after Porpoise's threads had started could close. */
    g_stay_sandboxed = porpoise::jailbreak::stay_wanted();
    g_jailbreak_closed = porpoise::jailbreak::closed_last();
    if (g_stay_sandboxed || !g_jailbreak_closed.empty())
        g_freed = porpoise::jailbreak::data_reachable() || porpoise::jailbreak::data_visible()
                      ? 1
                      : 0; /* the console's own way only */
    else
        g_freed = porpoise::jailbreak::ensure() ? 1 : 0;
#endif
    ps5_open_permissions();
    if (std::freopen(PORPOISE_APP "/trace.txt", "a", stderr))
    {
        static char stderr_buffer[64 * 1024];
        std::setvbuf(stderr, stderr_buffer, _IOFBF, sizeof stderr_buffer);
        start_log_flusher();
    }
    std::set_terminate(on_terminate);
    ps5::debug::mark(PS5_RETROARCH_BUILD_ID);
    if (!g_jailbreak_closed.empty())
        ps5::debug::mark(("jailbreak: Porpoise closed after being freed last time (it got to: " + g_jailbreak_closed +
                          "), so it didn't ask this time").c_str());
    if (g_stay_sandboxed)
        ps5::debug::mark("jailbreak: Stay in the sandbox is on: not asking");
#ifndef PORPOISE_DESKTOP
    {
        /* The console's firmware, for reports (0x04500000 is 4.50). */
        unsigned int firmware = 0;
        std::size_t bytes = sizeof firmware;
        if (sysctlbyname("kern.sdk_version", &firmware, &bytes, nullptr, 0) == 0)
        {
            char line[64];
            std::snprintf(line, sizeof line, "main: firmware %x.%02x (0x%08x)", firmware >> 24,
                          (firmware >> 16) & 0xFF, firmware);
            ps5::debug::mark(line);
        }
    }
#endif
    ps5::memory::init(PORPOISE_APP "/memory-diagnostics.log", PS5_RETROARCH_BUILD_ID);
    ps5_vulkan_profile_init();
    ps5_crash_report_install();
    ps5_memory_report("startup", 0, 0);
    ps5_core_threads_start();
    ps5_sampler_start();
    {
        /* Which processors Porpoise may run on (the sampler says which each
         * thread did), for the trace. */
        unsigned long long mask = 0;
        const int got = ps5_pthread_getaffinity_np(pthread_self(), sizeof mask, &mask);
        g_start_cpus = got == 0 ? mask : 0;
        char line[96];
        std::snprintf(line, sizeof line, "main: may run on processors %#llx (%d), now on %d", mask, got,
                      sceKernelGetCurrentCpu());
        ps5::debug::mark(line);
    }

    mkdir(PORPOISE_APP "/content", 0777);
    mkdir(PORPOISE_APP "/savefiles", 0777);
    mkdir(PORPOISE_APP "/system", 0777);
    mkdir(PORPOISE_APP "/porpoise", 0777);
    mkdir(PORPOISE_APP "/porpoise/covers", 0777);

    if (!porpoise::vk::open_display())
    {
        ps5::debug::mark("main: no display; nothing can be shown");
        leave(1);
    }
    mark_start("start: display open, ms");
    /* The console's splash stays up while Porpoise gets ready; it goes
     * with Porpoise's first frame (show_boot_mark), so there is no black gap. */
    porpoise::pad::open();
    /* The Switch 2's GameCube controller, whenever it's plugged in. */
    porpoise::ns2gc::start();
    porpoise::bt::start(); /* and without its cable, through a USB Bluetooth adapter */
    porpoise::audio::open();

    choose_data_dir();
    mark_start("start: player data found, ms");
    porpoise::ui::set_header_cache(g_data + "/disc-cache.tsv");
    g_settings.load(g_settings_path);
    g_settings.stay_sandboxed = g_stay_sandboxed; /* kept in /app0/porpoise, not settings.ini */
    {
        /* Network shares: connected only when something on them is read. */
        std::vector<porpoise::netfs::Share> shares;
        if (porpoise::netfs::load(g_data + "/network-shares.txt", shares))
            porpoise::netfs::set_shares(shares);
        ps5::debug::mark_value("main: network shares", static_cast<long long>(shares.size()));
    }
    {
        /* Games' own settings from 1.0, brought up to date once (settings
         * saved by this version have been: no need to read every file). */
        bool changed = false;
        if (DIR *d = g_settings.loaded_version >= 17 ? nullptr : opendir((g_data + "/game-settings").c_str()))
        {
            while (dirent *e = readdir(d))
            {
                const std::string n = e->d_name;
                if (n.size() > 4 && n.compare(n.size() - 4, 4, ".ini") == 0)
                    changed |= porpoise::Settings::migrate_game_file(g_data + "/game-settings/" + n, g_settings);
            }
            closedir(d);
        }
        if (changed)
            g_settings.save(g_settings_path);
    }
    g_settings.write_core_options(g_options_path);
    apply_settings();
    /* Performance report on, but its file was only made now (a fresh copy of
     * Porpoise): the sampler starts here instead of at the next start. */
    ps5_sampler_start();
    {
        /* The menus speak the PS5's language unless Settings says otherwise. */
        int language = 1;
        const int rc = sceSystemServiceParamGetInt(1 /* language */, &language);
        ps5::debug::mark_value("main: system language", rc == 0 ? language : -1);
        porpoise::ui::set_system_language(rc == 0 ? language : 1);
        mkdir((g_data + "/lang").c_str(), 0777);
        porpoise::ui::apply_language(g_settings.ui_language, g_data + "/lang");
    }

    /* The launcher runs on Porpoise's own device. */
    if (!porpoise::vk::open_device(nullptr) || !start_gfx())
    {
        sceSystemServiceHideSplashScreen();
        leave(1);
    }
    porpoise::vk::set_overlay(overlay, nullptr);
    porpoise::vk::set_prepass(prepass, nullptr);
    if (forwarded.rom.empty())
        show_boot_mark();
    else
    {
        /* Straight to the game: the launch screen is the first thing seen. */
        ps5::debug::mark_value("main: splash hidden", sceSystemServiceHideSplashScreen());
        g_pacer.start(porpoise::vk::refresh_hz(), "boot");
    }
    mark_start("start: Porpoise's mark on screen, ms");

    start_library();
    porpoise::forwarders::repair(g_data);
    ps5::debug::mark_value("main: games in the library", static_cast<long long>(g_library.games().size()));
    mark_start("start: library read, ms");
    /* 2.7: Revolution shows GameCube games too; its old Wii-only filter goes. */
    if (g_settings.loaded_version > 0 && g_settings.loaded_version < 14 && g_settings.ui_theme == 1 &&
        g_library.show() == porpoise::ui::Library::Show::Wii)
    {
        g_library.set_show(porpoise::ui::Library::Show::All);
        g_library.save();
        g_settings.save(g_settings_path); /* settings_version 14: once */
    }
    /* A home screen forwarder's game (--rom): found now that Porpoise's folder
     * and the drives can be read, and started once instead of the library.
     * Before g_app.init: open_file() may add it to the library's games. */
    porpoise::ui::Game *forwarded_game = nullptr;
    bool forwarded_session = false;
    std::string forwarded_missing, forwarded_foreign;
    if (!forwarded.rom.empty())
    {
        const std::string path = porpoise::forward::resolve(forwarded.rom, g_data + "/games");
        bool foreign = false;
        if (!path.empty())
            forwarded_game = g_library.open_file(path, &foreign);
        if (foreign)
            forwarded_foreign = path.substr(path.rfind('/') + 1);
        else if (!forwarded_game)
            forwarded_missing = path.empty() ? forwarded.rom : path;
        ps5::debug::mark(("main: forwarded game " + (path.empty() ? forwarded.rom : path) +
                          (forwarded_game ? ": found" : ": not found"))
                             .c_str());
    }
    g_app.init(&g_gfx, &g_library, &g_settings, g_settings_path, g_options_path, g_saves_path);
    g_app.set_sys_dir(PORPOISE_APP "/system/dolphin-emu/Sys");
    porpoise::ui::widescreen::set_dir(PORPOISE_APP "/assets/widescreen");
    g_app.set_sound_hook(play_sound);
    g_app.set_jingle_hook([](const std::int16_t *frames, std::size_t count) {
        porpoise::sound::play_jingle(frames, count);
    });
#ifndef PORPOISE_DESKTOP /* RetroAchievements is the PS5's for now */
    porpoise::ra::init(g_data);
    g_app.set_ra(
        [] {
            const porpoise::ra::Account a = porpoise::ra::account();
            porpoise::ui::App::RaState s;
            s.signed_in = a.signed_in;
            s.busy = a.busy;
            s.user = a.user;
            s.points = a.points;
            s.message = a.message;
            return s;
        },
        [](const std::string &user, const std::string &password) {
            porpoise::covers::stop(); /* one connection at a time: the sign-in goes first */
            return porpoise::ra::begin_login(user, password);
        },
        [] { porpoise::ra::logout(); });
    g_app.set_achievement_source([](const std::string &disc_id, bool live) {
        porpoise::ui::AchievementSet set;
        if (live)
            porpoise::ra::live_list(set);
        else
            porpoise::ra::kept_list(disc_id, set);
        return set;
    });
#endif
    porpoise::banner::set_cache_dir(g_data + "/banners");
    porpoise::sound::load(PORPOISE_APP "/assets");
    g_sounds_ready = true;
    apply_sound_set(g_settings.sound_set);
    porpoise::states::set_data_dir(g_data);
    porpoise::shots::set_data_dir(g_data);
    porpoise::borders::set_dirs(PORPOISE_APP "/assets", g_data);
    porpoise::ui::setups::set_dir(g_data);
    porpoise::ui::recommend::set_paths(g_data + "/recommended.ini", PORPOISE_APP "/system/dolphin-emu/Sys/GameSettings",
                                       PORPOISE_APP "/assets/recommended.ini");
    read_latest_release();
    apply_settings();
    g_app.set_sandboxed(g_sandboxed);
    g_app.set_diagnostics(diagnostics);
    if (!g_sandboxed && !g_settings.setup_checked)
    {
        /* The first start: a look to begin with, then the welcome and what
         * Porpoise can see (setup_checked is saved once a look is chosen).
         * A Porpoise folder found on a drive is offered first. */
        if (!g_found_folder.empty())
            g_app.offer_found_folder(g_found_folder, g_found_place, g_found_handmade);
        else
            g_app.start_welcome();
    }
    if (porpoise::ui::look::kChannel != porpoise::ui::look::Channel::Release &&
        g_settings.testing_notice != porpoise::ui::look::kNoticeId)
        g_app.show_testing_notice(); /* after anything below */
    if (!g_jailbreak_closed.empty() && !g_sandboxed)
        porpoise::jailbreak::forget_closed(); /* /data is there without asking: nothing to choose */
    if (!g_location_missing.empty())
        g_app.show_message(porpoise::ui::tr("Porpoise's folder isn't connected"),
                           porpoise::ui::trf("Porpoise's folder is on a drive that isn't connected now ({path}). "
                                             "Connect it and open Porpoise again. Until then, Porpoise uses the "
                                             "console's storage.",
                                             {{"path", g_location_missing}}));
    else if (g_sandboxed && !g_jailbreak_closed.empty())
        g_app.offer_jailbreak_retry();
    else if (g_sandboxed && porpoise::jailbreak::root_put_back())
        g_app.show_message(porpoise::ui::tr("Your jailbreak hid Porpoise's files"),
                           porpoise::ui::tr("The jailbreak daemon freed Porpoise in a way that hides Porpoise's own "
                                            "folder, so Porpoise stayed in the app sandbox to keep running: it can't "
                                            "see /data or USB drives this time. Updating ShadowMountPlus to 1.7 beta "
                                            "4 or newer fixes this for most players."));
    else if (g_sandboxed && g_settings.sandbox_notice && !g_stay_sandboxed)
        g_app.show_sandbox_notice(porpoise::ui::tr("Porpoise can't reach /data"),
                           porpoise::ui::tr("The console started Porpoise inside the app sandbox, so it can't see "
                                            "/data or USB drives, and no jailbreak daemon freed it. A daemon that "
                                            "frees Porpoise must be running before you open it:\n"
                                            "\xE2\x80\xA2 etaHEN: turn on Legacy Command Server in etaHEN's Toolbox "
                                            "settings (etaHEN's built-in app list can't be edited to add Porpoise).\n"
                                            "\xE2\x80\xA2 OnionHEN: add PPSA99764 to exact_title_ids in "
                                            "/data/OnionHEN/config.ini.\n"
                                            "\xE2\x80\xA2 Or run a standalone daemon such as Lapy.\n"
                                            "Then open Porpoise again; if a launch still lands here, try once more. "
                                            "Until then, games go in /app0/porpoise/games."));
    else if (!forwarded_foreign.empty())
        g_app.show_message(porpoise::ui::tr("This isn't a GameCube or Wii game"),
                           porpoise::ui::trf("{file} is a disc image from another console, so Porpoise can't play it.",
                                             {{"file", forwarded_foreign}}));
    else if (!forwarded_missing.empty())
        g_app.show_message(porpoise::ui::tr("Forwarded game not found"),
                           porpoise::ui::trf("Porpoise was asked to start {path}, but can't find that file.",
                                             {{"path", forwarded_missing}}));
    start_entrance(); /* the music comes up with the menus */
    fetch_covers();

    const double hz = porpoise::vk::refresh_hz();
    g_pacer.start(hz, "launcher");
    porpoise::ui::Game *relaunch = nullptr; /* left to start afresh (in-game Apply) */
    for (;;)
    {
        porpoise::ui::Game *launch = relaunch;
        relaunch = nullptr;
        while (!launch)
        {
            if (forwarded_game)
            {
                /* The forwarder's game, straight away (no Resume or Wii
                 * setup question: its own settings and quick resume apply). */
                launch = std::exchange(forwarded_game, nullptr);
                forwarded_session = true;
                ps5::debug::mark("main: starting the forwarded game");
                break;
            }
            const porpoise::pad::State &pad = porpoise::pad::poll();
#ifdef PORPOISE_DESKTOP
            if (porpoise::platform::quit_requested())
                leave(0); /* the window was closed */
#endif
            porpoise::ui::Input in;
            in.held = pad.buttons;
            in.stick_x = pad.left_x / 32768.0f;
            in.stick_y = pad.left_y / 32768.0f;
            in.right_x = pad.right_x / 32768.0f;
            in.right_y = pad.right_y / 32768.0f;
            const double dt = 1.0 / hz;
            g_time += dt;
            std::string cover_id;
            while (porpoise::covers::take_ready(cover_id))
                g_app.cover_arrived(cover_id);
            if (porpoise::covers::take_info_ready())
            {
                g_library.set_info_path(shown_info_path());
                g_library.load_info();
            }
            if (porpoise::covers::take_feed_ready())
                porpoise::ui::recommend::feed_changed();
            if (porpoise::covers::take_release_ready())
                read_latest_release();
            {
                /* The updater: checked, downloading, installing, done. */
                const porpoise::update::Progress up = porpoise::update::progress();
                if (up.phase == porpoise::update::Phase::Checked)
                    read_latest_release();
                g_app.set_update_progress(int(up.phase), up.done, up.total, up.error);
                if (up.phase == porpoise::update::Phase::Checked || up.phase == porpoise::update::Phase::Failed)
                {
                    porpoise::update::acknowledge();
                    fetch_covers(); /* the covers paused for it */
                }
            }
            {
                int phase = 0, done = 0, total = 0;
                std::string note;
                if (porpoise::covers::progress(phase, done, total) && phase < 4)
                    note = phase == 1 ? porpoise::ui::tr("Getting game info")
                                      : porpoise::ui::trf(phase == 2 ? "Getting disc art {done} of {total}"
                                                                     : "Getting covers {done} of {total}",
                                                          {{"done", std::to_string(done)}, {"total", std::to_string(total)}});
                g_app.set_note(note);
            }
            if (g_covers_again && !porpoise::covers::busy())
                fetch_covers();
            if (g_app.take_covers_resume())
                fetch_covers(); /* the covers paused for Wii System Update */
            if (porpoise::ra::take_login_done())
            {
                g_app.account_changed();
                fetch_covers(); /* the covers paused for the sign-in */
            }
            static long long last_frame_ns = now_ns();
            const long long frame_now = now_ns();
            const double frame_ms = (frame_now - last_frame_ns) / 1e6;
            last_frame_ns = frame_now;
            if (entrance_frame(dt, frame_ms))
                in = porpoise::ui::Input{}; /* the menus aren't on screen yet */
            take_search();
            g_app.set_searching(g_search.state.load(std::memory_order_relaxed) != 0);
            const auto action = g_app.update(in, dt);
            if (action == porpoise::ui::App::Action::SettingsChanged)
            {
#ifndef PORPOISE_DESKTOP
                porpoise::jailbreak::set_stay(g_settings.stay_sandboxed);
#endif
                if (g_app.take_rescan())
                    rescan_library(); /* Find games automatically changed */
                apply_settings();
                read_latest_release(); /* Beta updates may have changed what counts as newer */
                if (g_library.paths().info != shown_info_path())
                {
                    /* The menus changed language: descriptions in it too. */
                    g_library.set_info_path(shown_info_path());
                    g_library.load_info();
                }
                fetch_covers(); /* in case covers were just turned on */
            }
            if (action == porpoise::ui::App::Action::LiveSettings)
                apply_settings(); /* a look or sound setting, saved as it was chosen */
            if (action == porpoise::ui::App::Action::Rescan)
                rescan_library();
#ifndef PORPOISE_DESKTOP
            if (action == porpoise::ui::App::Action::StayInSandbox)
            {
                porpoise::jailbreak::set_stay(true);
                porpoise::jailbreak::forget_closed();
            }
            if (action == porpoise::ui::App::Action::Restart)
                restart_porpoise(); /* settings that take effect at the start */
            if (action == porpoise::ui::App::Action::RetryJailbreak)
            {
                porpoise::jailbreak::forget_closed();
                restart_porpoise();
            }
#endif
            if (action == porpoise::ui::App::Action::Reinitialize)
            {
                /* A fresh start: the screen goes to Porpoise's mark, everything
                 * is read again behind it, and the first start's welcome comes. */
                ps5::debug::mark("main: reinitializing");
                porpoise::sound::fade_music(0.0f, 0.4f);
                const int frames = int(hz * 0.5);
                for (int frame = 1; frame <= frames; ++frame)
                {
                    g_time += 1.0 / hz;
                    begin_ui_frame(0.0f);
                    g_app.draw(g_time);
                    g_app.draw_curtain(float(frame) / float(frames));
                    porpoise::vk::present_clear(0, 0, 0);
                    porpoise::sound::pump();
                    g_pacer.frame_done();
                }
                apply_settings();
                {
                    int language = 1;
                    if (sceSystemServiceParamGetInt(1 /* language */, &language) == 0)
                        porpoise::ui::set_system_language(language);
                    porpoise::ui::apply_language(g_settings.ui_language, g_data + "/lang");
                }
                rescan_library();
                g_app.restart_fresh();
                if (!g_sandboxed)
                    g_app.start_welcome();
                start_entrance();
                continue;
            }
            if (action == porpoise::ui::App::Action::FetchCovers)
                fetch_covers(true);
            if (action == porpoise::ui::App::Action::CoversAgain)
            {
                for (const std::string &id : g_app.take_covers_again())
                    g_covers_again_ids.push_back(id);
                ps5::debug::mark_value("main: art downloaded again for games",
                                       static_cast<long long>(g_covers_again_ids.size()));
                fetch_covers(true);
            }
            if (action == porpoise::ui::App::Action::MoveData && !g_app.move_target().empty())
                move_data(g_app.move_target(), hz);
            if (action == porpoise::ui::App::Action::UseFolder && !g_app.move_target().empty())
                use_data(g_app.move_target(), hz);
            if (action == porpoise::ui::App::Action::CheckUpdate)
            {
                porpoise::covers::stop(); /* one download at a time: let the check go first */
                porpoise::update::start_check(g_data + "/latest-release.json");
            }
            if (action == porpoise::ui::App::Action::InstallUpdate)
            {
                porpoise::covers::stop();
                ps5::debug::mark(("main: updating to " + g_release.tag).c_str());
                porpoise::update::start_install(g_release, install_dir());
            }
            if (action == porpoise::ui::App::Action::InstallVersion)
            {
                const int pick = g_app.picked_version();
                if (pick >= 0 && pick < int(g_releases.size()))
                {
                    porpoise::covers::stop();
                    ps5::debug::mark(("main: installing chosen version " + g_releases[std::size_t(pick)].tag).c_str());
                    porpoise::update::start_install(g_releases[std::size_t(pick)], install_dir());
                }
            }
            if (action == porpoise::ui::App::Action::Quit)
            {
                ps5::debug::mark("main: closing after the update");
                leave(0);
            }
            if (action == porpoise::ui::App::Action::Launch && g_app.launch_game())
            {
                launch = g_app.launch_game();
                break;
            }
            begin_ui_frame(0.0f);
            g_app.draw(g_time);
            porpoise::ui::draw_wiimote_notices(g_gfx);
            if (g_curtain > 0.0f)
                g_app.draw_curtain(g_curtain); /* the entrance (entrance_frame) */
            porpoise::vk::present_clear(0, 0, 0);
            static bool first_menu_frame = true;
            if (first_menu_frame)
            {
                first_menu_frame = false;
                mark_start("start: first menu frame, ms");
            }
            /* Up a few seconds in the menus: whatever freed Porpoise didn't
             * close it, so the next start asks again. */
            static int menu_frames = 0;
            if (++menu_frames == int(hz * 5))
                porpoise::jailbreak::survived();
            porpoise::sound::pump();
            g_pacer.frame_done();
        }

        /* The launch: the screen dims into the launch tile for a moment, then
         * the game starts behind it. */
        ps5::debug::mark(("main: launching " + launch->path).c_str());
        porpoise::covers::stop();
        porpoise::banner::pause(true); /* the disc is the game's now */
        porpoise::sound::play_jingle(nullptr, 0);
        /* The game's own settings, on top of the global ones. */
        g_play = g_settings;
        if (g_play.load(g_app.game_settings_path(*launch), true))
            ps5::debug::mark("main: the game has its own settings");
        /* Widescreen: the game's own code or 16:9 option where it has one; the
         * emulated hack only when asked for (ui_widescreen.hpp). */
        const bool ws_wii = g_play.console == 2 || (g_play.console == 0 && launch->platform == "Wii");
        /* A game whose widescreen code Dolphin keeps per disc revision: the
         * revision from the disc's header (byte 7). */
        int ws_revision = -1;
        if (!ws_wii && porpoise::ui::widescreen::has_revisions(PORPOISE_APP "/system/dolphin-emu/Sys", launch->id))
        {
            std::uint8_t header[0x100];
            std::string error;
            if (porpoise::disc::read_header(launch->path, header, error))
                ws_revision = header[7];
            ps5::debug::mark_value("main: disc revision (its widescreen code is per revision)", ws_revision);
        }
        const porpoise::ui::widescreen::Plan ws_plan = porpoise::ui::widescreen::plan_for(
            g_play.wide_mode(),
            porpoise::ui::widescreen::kind_of(launch->id, PORPOISE_APP "/system/dolphin-emu/Sys", ws_wii, ws_revision),
            porpoise::ui::widescreen::code_needs_hack(launch->id));
        static_assert(int(porpoise::ui::widescreen::Plan::Patch) == 1 && int(porpoise::ui::widescreen::Plan::Hack) == 3 &&
                          int(porpoise::ui::widescreen::Plan::PatchHack) == 4 &&
                          int(porpoise::ui::widescreen::Plan::Universal) == 5,
                      "Settings::core_options reads the plan as these numbers");
        g_play.ws_plan = int(ws_plan);
        ps5::debug::mark_value("main: widescreen plan (0 4:3, 1 code, 2 the game's own, 3 hack, 4 code with hack, "
                               "5 universal)",
                               g_play.ws_plan);
        porpoise::pacer::set_vsync(g_play.vsync);
        apply_frame_gen();
        if (g_play.frame_gen)
            ps5::debug::mark(g_play.fg_steady30 ? "main: frame generation (beta) on, steady 30" : "main: frame generation (beta) on");
        /* The driver reads this when Dolphin makes the game's device
         * (PS5_Mesa's threaded layer); the launcher's own device never has it. */
        setenv("RADV_THREADED_RECORDING", g_play.threaded_gpu ? "1" : "0", 1);
        keep_cores(g_play.own_cores);
        ps5::debug::mark(g_play.threaded_gpu ? "main: threaded GPU recording on" : "main: threaded GPU recording off");
        {
            /* Dolphin's built-in graphics mods: its list of the game's mods,
             * with the chosen ones on, and its mods switch only then. */
            const porpoise::gfxmods::Choice mods{g_play.gfx_bloom, g_play.gfx_dof, g_play.gfx_hud, g_play.gfx_extra};
            g_play.gfx_mods_on = porpoise::gfxmods::wanted(launch->id, mods);
            if (g_play.gfx_mods_on)
            {
                porpoise::gfxmods::write_profile(g_saves_path + "/User/Config", launch->id, mods);
                ps5::debug::mark("main: graphics mods on for this game");
            }
        }
        /* 4.0: Build Shaders Before Starting builds every shader the game's
         * list (User/Cache/<ID>.uidcache) names before its first picture. If
         * the last start never got that far, the list is set aside (renamed,
         * not deleted): the game starts, and the list grows again as it plays. */
        g_boot_marker.clear();
        if (launch->id.size() == 6 && g_play.wait_shaders && !launch->boot_menu)
        {
            const std::string cache = g_saves_path + "/User/Cache/" + launch->id;
            const std::string marker = cache + ".porpoise-starting";
            struct stat st;
            if (stat(marker.c_str(), &st) == 0 && stat((cache + ".uidcache").c_str(), &st) == 0)
            {
                std::rename((cache + ".uidcache").c_str(), (cache + ".uidcache.set-aside").c_str());
                ps5::debug::mark("main: the last start never showed a picture: the game's shader list set aside");
            }
            if (std::FILE *f = std::fopen(marker.c_str(), "w"))
            {
                std::fclose(f);
                g_boot_marker = marker;
            }
        }
        g_play.write_core_options(g_options_path);
        /* Fast save states: Dolphin leaves its GPU texture cache out of them
         * (Dolphin.ini's base layer, read when the core starts). */
        set_ini_value(g_saves_path + "/User/Config/GFX.ini", "Settings", "SaveTextureCacheToState",
                      g_play.fast_states ? "False" : "True");
        /* Shaders compile on four background threads, not Dolphin's one: the
         * game spends less time on the slow ubershaders and stutters less the
         * first time it shows something. */
        set_ini_value(g_saves_path + "/User/Config/GFX.ini", "Settings", "ShaderCompilerThreads", "4");
        /* The Wii Remote's own speaker (Dolphin leaves it off by default): in
         * the TV's sound, or on each player's controller when its speaker
         * opens (routing keeps it out of the TV's sound then). */
        const std::string dolphin_ini = g_saves_path + "/User/Config/Dolphin.ini";
        const bool wii_game = g_play.console == 2 || (g_play.console == 0 && launch->platform == "Wii");
        bool controller_speakers = false;
        if (wii_game && g_play.wiimote_speaker == 2)
            controller_speakers = porpoise::speaker::open_ports() > 0;
        set_ini_value(dolphin_ini, "Core", "WiimoteEnableSpeaker", g_play.wiimote_speaker > 0 ? "True" : "False");
        /* Online (beta): WiiConnect24 through WiiLink. */
        set_ini_value(dolphin_ini, "Core", "EnableWiiLink", g_play.wii_online ? "True" : "False");
        set_ini_value(dolphin_ini, "Core", "WiimoteAudioRoutingEnabled", controller_speakers ? "True" : "False");
        for (int p = 0; p < 4; ++p)
            set_ini_value(dolphin_ini, "Core", "Wiimote" + std::to_string(p + 1) + "AudioOutputEnabled",
                          controller_speakers && porpoise::speaker::open(p) ? "True" : "False");
        g_controller_speakers = controller_speakers;
        porpoise::audio::set_buffer(g_play.audio_buffer);
        porpoise::audio::set_stretching(g_play.audio_stretch);
        /* The GameCube's start-up: only from the player's own BIOS, put where
         * Dolphin looks for the game's region; without one, straight in. */
        if (g_play.gc_bios && launch->platform != "Wii" && !install_ipl(*launch))
        {
            g_play.gc_bios = false;
            g_play.write_core_options(g_options_path); /* straight into the game after all */
            ps5::debug::mark("main: GameCube boot animation on, but no IPL.bin for this region in bios/");
        }
        /* Settings > Console's menus: the GameCube Menu or the Wii Menu, with
         * the chosen game in the drive or none; and channels the core puts in
         * the Wii first. The core reads these when it boots (Boot.cpp). */
        if (launch->boot_menu)
        {
            setenv("PORPOISE_BOOT_MENU", launch->boot_menu == 1 ? "gc" : "wii", 1);
            setenv("PORPOISE_BOOT_DISC", launch->boot_disc ? "1" : "0", 1);
            if (launch->boot_menu == 1)
            {
                const std::string region = ipl_region(*launch);
                setenv("PORPOISE_BOOT_REGION", region.c_str(), 1);
                if (!install_ipl_for(region))
                    ps5::debug::mark("main: the GameCube Menu, but no IPL.bin for its region in bios/");
            }
            ps5::debug::mark(launch->boot_menu == 1 ? "main: starting the GameCube Menu" : "main: starting the Wii Menu");
        }
        else
        {
            unsetenv("PORPOISE_BOOT_MENU");
            unsetenv("PORPOISE_BOOT_DISC");
            unsetenv("PORPOISE_BOOT_REGION");
        }
        {
            /* 3.1: a game on two discs: the other disc (same ID, the other
             * number) goes to the core too, and the game changes to it itself
             * when it asks for it, as Dolphin does with a disc list. */
            std::string extra;
            g_other_disc_keys.clear();
            g_disc_numbers = {launch->disc_number};
            if (!launch->boot_menu && launch->kind.empty() && launch->id.size() == 6)
            {
                std::vector<const porpoise::ui::Game *> others;
                for (const porpoise::ui::Game &g : g_library.games())
                    if (&g != launch && g.id == launch->id && g.disc_number != launch->disc_number && g.kind.empty() &&
                        g.path != launch->path &&
                        std::none_of(others.begin(), others.end(),
                                     [&](const porpoise::ui::Game *o) { return o->disc_number == g.disc_number; }))
                        others.push_back(&g);
                std::sort(others.begin(), others.end(), [](const porpoise::ui::Game *a, const porpoise::ui::Game *b) {
                    return a->disc_number < b->disc_number;
                });
                for (const porpoise::ui::Game *g : others)
                {
                    extra += g->path + "\n";
                    g_other_disc_keys.push_back(porpoise::ui::Library::key_of(*g));
                    g_disc_numbers.push_back(g->disc_number);
                }
            }
            if (!extra.empty())
            {
                setenv("PORPOISE_EXTRA_DISCS", extra.c_str(), 1);
                ps5::debug::mark(("main: the game's other disc too: " + extra.substr(0, extra.size() - 1)).c_str());
            }
            else
                unsetenv("PORPOISE_EXTRA_DISCS");
        }
        {
            const std::string wads = g_app.take_wad_installs();
            if (!wads.empty())
            {
                setenv("PORPOISE_INSTALL_WADS", wads.c_str(), 1);
                ps5::debug::mark("main: channels to install in the Wii first");
            }
            else
                unsetenv("PORPOISE_INSTALL_WADS");
            /* Wii System Update: the core installs the titles in the list
             * Porpoise asked for, before the Wii Menu starts. */
            const std::string update = g_app.take_sys_update();
            if (!update.empty())
            {
                setenv("PORPOISE_SYSTEM_UPDATE", update.c_str(), 1);
                setenv("PORPOISE_NUS_RESPONSE", g_app.update_list().c_str(), 1);
                ps5::debug::mark(("main: Wii System Update for " + update).c_str());
            }
            else
            {
                unsetenv("PORPOISE_SYSTEM_UPDATE");
                unsetenv("PORPOISE_NUS_RESPONSE");
            }
        }
        if (launch->id.size() == 6)
        {
            /* The game's Dolphin settings (recommended ones turned on or off),
             * where Dolphin reads them: over its own per-game fixes. */
            const std::string dir = g_saves_path + "/User/GameSettings";
            mkdir((g_saves_path + "/User").c_str(), 0777);
            mkdir(dir.c_str(), 0777);
            if (!g_play.write_dolphin_game_ini(dir + "/" + launch->id + ".ini"))
            {
                ps5::debug::mark("main: the game's Dolphin settings file is the player's own; left as it is");
                if (g_play.ws_plan == int(porpoise::ui::widescreen::Plan::Patch) ||
                    g_play.ws_plan == int(porpoise::ui::widescreen::Plan::PatchHack))
                {
                    /* No code can be added there: the game stays 4:3. */
                    g_play.ws_plan = int(porpoise::ui::widescreen::Plan::Standard);
                    g_play.write_core_options(g_options_path);
                }
            }
            else
            {
                /* The player's own codes (<data>/cheats/<ID>.ini): on unless
                 * turned off in the game's Cheats; a cheat among them needs
                 * Dolphin's cheats on for this game. */
                auto off = [](const porpoise::ui::Cheat &c, const void *user) {
                    const auto *play = static_cast<const porpoise::Settings *>(user);
                    return play->get(porpoise::ui::cheat_key(c, false)) == "1";
                };
                if (porpoise::ui::add_own_cheats(g_data + "/cheats", launch->id, dir + "/" + launch->id + ".ini", off,
                                                 &g_play))
                {
                    ps5::debug::mark("main: the player's own cheats are on for this game");
                    if (!g_play.cheats)
                    {
                        g_play.cheats = true;
                        g_play.write_core_options(g_options_path);
                    }
                }
                /* A custom server: the https-to-http code, for a Wii game. */
                const bool wii_disc = g_play.console == 2 || (g_play.console == 0 && launch->platform == "Wii");
                if (wii_disc && g_play.online_custom && g_play.online_https && !g_settings.online_dns.empty() &&
                    porpoise::ui::add_online_code(dir + "/" + launch->id + ".ini"))
                {
                    ps5::debug::mark("main: the custom server's secure connection fix is on");
                    if (!g_play.cheats)
                    {
                        g_play.cheats = true;
                        g_play.write_core_options(g_options_path);
                    }
                }
                /* The game's widescreen code (Warped Polygon's collection, or
                 * Dolphin's own), on when the plan says so. */
                bool ws_on = false;
                const bool ws_cheats = porpoise::ui::widescreen::add_codes(
                    launch->id, PORPOISE_APP "/system/dolphin-emu/Sys", dir + "/" + launch->id + ".ini", ws_plan, off,
                    &g_play, ws_on, ws_revision);
                if (ws_on)
                    ps5::debug::mark("main: the game's widescreen code is on");
                if (ws_cheats && !g_play.cheats)
                {
                    g_play.cheats = true;
                    g_play.write_core_options(g_options_path);
                }
                if ((ws_plan == porpoise::ui::widescreen::Plan::Patch ||
                     ws_plan == porpoise::ui::widescreen::Plan::PatchHack) &&
                    !ws_on)
                {
                    /* Its code turned off: 4:3 rather than a stretched picture. */
                    g_play.ws_plan = int(porpoise::ui::widescreen::Plan::Standard);
                    g_play.write_core_options(g_options_path);
                }
            }
            for (const auto &[k, v] : g_play.dolphin)
                ps5::debug::mark(("main: Dolphin setting for this game: " + k + " = " + v).c_str());
            /* The same values as core options, so a change in the in-game menu
             * keeps them (see kGameOptions). */
            const auto pinned = game_dolphin_options(launch->id);
            if (!pinned.empty())
                if (std::FILE *f = std::fopen(g_options_path.c_str(), "a"))
                {
                    std::fprintf(f, "# Set by Porpoise for this game only (its Dolphin settings):\n");
                    for (const auto &[k, v] : pinned)
                    {
                        std::fprintf(f, "%s = %s\n", k.c_str(), v.c_str());
                        ps5::debug::mark(("main: core option for this game: " + k + " = " + v).c_str());
                    }
                    std::fclose(f);
                }
        }
        porpoise::pad::set_mapping(g_play.mapping());
        porpoise::pad::set_wii_buttons(g_play.wii_buttons_in_use());
        porpoise::pad::set_rumble_enabled(g_play.rumble);
        apply_game_controls(g_play.console == 2 || (g_play.console == 0 && launch->platform == "Wii"));
        g_app.begin_launch(launch);
        /* The music fades as the screen dims; the Play sound finishes. */
        porpoise::sound::fade_music(0.0f, 0.5f);
        /* From a home screen tile there is no library to fade from: the game
         * starts loading at once, under the launch screen. */
        const int launch_frames = forwarded_session ? 2 : std::max(36, int(hz * 0.6));
        for (int frame = 0; frame < launch_frames; ++frame)
        {
            g_time += 1.0 / hz;
            const float t = std::min(1.0f, float(frame + 1) / float(launch_frames * 2 / 3));
            begin_ui_frame(0.6f * t);
            if (g_app.launch_intro(g_time) < 1.0f && !forwarded_session)
                g_app.draw(g_time); /* the library, fading under the launch screen */
            g_app.draw_launch(g_time);
            porpoise::vk::present_clear(0, 0, 0);
            porpoise::sound::pump();
            g_pacer.frame_done();
        }

        g_playing = launch;
        g_menu_open = false;
        porpoise::core::Hooks hooks;
        hooks.status = launch_status;
        hooks.progress = launch_progress;
        hooks.device_closing = launch_device_closing;
        hooks.device_ready = launch_device_ready;
        hooks.frame = launch_frame;
        hooks.leaving = leaving_game;
        g_curtain = 0.0f;
        hooks.opened = menu_opened;
        hooks.paused = menu_paused;
        porpoise::core::Playback playback;
        playback.volume = g_play.volume / 10.0f;
        playback.muted = g_play.muted;
        playback.filter = g_play.screen_filter;
        playback.strength = g_play.filter_strength / 10.0f;
        /* A Wii game: the DualSense as a Wii Remote; a test build logs it. */
        /* The game's console: as detected, or as its own settings say. */
        const bool is_wii = g_play.console == 2 || (g_play.console == 0 && launch->platform == "Wii");
        g_wii_game_id = launch->id;
#ifndef PORPOISE_DESKTOP
        /* A Wii game going online: through the custom server's DNS when
         * Custom Server is on (this game's choice, or everyone's). */
        porpoise::dns::set_server(g_play.online_custom ? g_settings.online_dns : std::string());
        ps5::debug::mark(g_play.online_custom && !g_settings.online_dns.empty()
                             ? ("main: online through the custom server, DNS " + g_settings.online_dns).c_str()
                             : "main: online the game's own way (no custom server)");
#endif
        playback.wii = g_play.wii_config(is_wii, g_wii_game_id);
        porpoise::usbhid::set_speaker_enabled(g_play.real_wiimote_speaker); /* this game's own choice, if it has one */
        playback.controller_speakers = g_controller_speakers;
        playback.audio_pull = g_play.audio_pull;
        playback.audio_buffer_ms = g_play.audio_buffer_ms();
        playback.audio_fill = g_play.audio_fill;
        g_wii_hint_from = g_time + 2.0; /* once the game is up */
        g_wii_centrings = 0;
        const std::string debug_dir = g_data + "/debug";
        /* A Wii game's every socket call in core.log: for reports only. */
        if (g_settings.debug_logs)
            setenv("PORPOISE_NET_LOG", "1", 1);
        else
            unsetenv("PORPOISE_NET_LOG");
        if (g_settings.debug_logs)
        {
            playback.debug_dir = debug_dir.c_str();
            playback.motion_log = g_settings.developer && g_settings.motion_logs;
            mkdir(debug_dir.c_str(), 0777);
            if (std::FILE *f = std::fopen((debug_dir + "/README.txt").c_str(), "w"))
            {
                std::fputs("Porpoise's debug folder (turn it off in Settings > Console > Debug logs).\n\n"
                           "WiimoteNew.ini  The Wii Remote set-up Dolphin was given for the last Wii game.\n\n"
                           "Send it with /data/homebrew/PPSA99764/trace.txt and "
                           "/data/homebrew/PPSA99764/porpoise/core.log when you report a problem.\n",
                           f);
                std::fclose(f);
            }
        }
        if (playback.wii.active)
            ps5::debug::mark_value("main: Wii game; Wii controller", playback.wii.controller);
        std::string start_state = g_app.take_launch_state();
        /* Quick resume (beta): where the game was left, unless a save state
         * was chosen in Details. */
        /* Not for Settings > Console's menus: the chosen game's own resume
         * point is left as it is. */
        const std::string resume = g_play.quick_resume && !launch->boot_menu
                                       ? porpoise::states::resume_path(porpoise::ui::Library::key_of(*launch))
                                       : std::string();
        if (!resume.empty())
        {
            struct stat st;
            if (start_state.empty() && stat(resume.c_str(), &st) == 0 && st.st_size > 0)
            {
                start_state = resume;
                ps5::debug::mark("main: quick resume: picking up where the game was left");
            }
            playback.resume_path = resume.c_str();
            g_other_disc_resume.clear();
            for (const std::string &key : g_other_disc_keys)
                g_other_disc_resume.push_back(porpoise::states::resume_path(key));
            playback.disc_resume_paths = &g_other_disc_resume;
        }
        playback.load_state = start_state.empty() ? nullptr : start_state.c_str();
        start_texture_note(*launch);
        porpoise::ra::prepare_game(launch->id);
        porpoise::core::Paths core_paths;
        core_paths.saves = g_saves_path.c_str();
        core_paths.options = g_options_path.c_str();
        core_paths.options_reference = g_options_reference.c_str();
        core_paths.log = g_core_log.c_str();
        porpoise::core::set_fast_forward(1);
        porpoise::vk::set_game_colour_filter(g_settings.colour_filter_games ? g_settings.colour_filter : 0);
        {
            float adjust[4];
            g_play.picture_adjust(adjust);
            porpoise::vk::set_picture_adjust(adjust[0], adjust[1], adjust[2], adjust[3]);
        }
        const long long played_from = now_ns();
#ifdef PORPOISE_DESKTOP
        porpoise::platform::set_in_game(true);
#endif
        const porpoise::core::Exit exit = porpoise::core::run_game(launch->path.c_str(), core_paths, hooks, playback);
        if (!g_boot_marker.empty())
        {
            /* Left (or failed) before the first picture: not a stall, so the
             * shader list stays as it is next time. */
            std::remove(g_boot_marker.c_str());
            g_boot_marker.clear();
        }
#ifdef PORPOISE_DESKTOP
        porpoise::platform::set_in_game(false);
#endif
        setenv("RADV_THREADED_RECORDING", "0", 1); /* the launcher's device, made next, records directly */
        porpoise::speaker::close_ports();
        keep_cores(false);
        /* Play time: the whole visit, loading included, as consoles count it. */
        if (exit != porpoise::core::Exit::Failed)
            g_library.add_play_time(*launch, (now_ns() - played_from) / 1000000000LL);
        porpoise::states::wait(); /* a save still being written */
        porpoise::shots::forget(); /* and a screenshot */
        g_playing = nullptr;
        g_menu_open = false;
        if (exit == porpoise::core::Exit::Home)
            leave(0);
        /* --exit-after-game: the forwarded game ended normally, so back to the
         * home screen; one that didn't start says why in the library. */
        if (std::exchange(forwarded_session, false) && forwarded.exit_after_game &&
            exit == porpoise::core::Exit::Library)
        {
            ps5::debug::mark("main: the forwarded game ended; closing (--exit-after-game)");
            leave(0);
        }

        /* Back to the library: Porpoise's own device again (a game that
         * failed early never took it away). */
        if (!g_gfx.ready())
        {
            if (!porpoise::vk::open_device(nullptr) || !start_gfx())
                leave(exit == porpoise::core::Exit::Failed ? 2 : 1);
        }
        porpoise::vk::set_overlay(overlay, nullptr);
        porpoise::audio::flush();
        porpoise::audio::set_volume(1.0f);
        porpoise::audio::set_buffer(1); /* the menus: the usual depth, no stretching */
        porpoise::audio::set_stretching(false);
        porpoise::audio::set_muted(false);
        g_settings.write_core_options(g_options_path);
        apply_settings();
        porpoise::banner::pause(false);
        g_app.return_from_game();
        if (g_app.take_relaunch() && exit == porpoise::core::Exit::Library)
        {
            ps5::debug::mark("main: starting the game again for its new settings");
            relaunch = launch;
        }
        if (exit == porpoise::core::Exit::Failed)
        {
            /* Which part didn't start: a file, or Dolphin itself (then no game
             * would), with the reason in its own words. */
            using porpoise::core::Failure;
            const Failure why = porpoise::core::last_failure();
            std::string text =
                why == Failure::Core
                    ? porpoise::ui::tr("Dolphin itself didn't load, so no game can start, whatever the file. Share "
                                       "trace.txt and porpoise/core.log from Porpoise's app folder on the Discord, "
                                       "with your firmware and HEN.")
                : why == Failure::Graphics
                    ? porpoise::ui::tr("Dolphin couldn't start its graphics, so no game can start, whatever the file. "
                                       "Share trace.txt and porpoise/core.log from Porpoise's app folder on the "
                                       "Discord, with your firmware and HEN.")
                : why == Failure::File
                    ? porpoise::ui::tr("Porpoise couldn't read this game's file. If it's on a USB drive, check that "
                                       "the drive is connected and that the file copied over in full.")
                    : porpoise::ui::tr("Dolphin could not start it. The file may be damaged or in a format "
                                       "Porpoise can't read yet. Details are in porpoise/core.log.");
            const std::string reason = porpoise::core::last_failure_reason();
            if (!reason.empty())
                text += "\n\n" + porpoise::ui::trf("Reason: {reason}", {{"reason", reason}});
            g_app.show_message(porpoise::ui::tr("This game didn't start"), text);
        }
        if (porpoise::core::take_update_failed())
            g_app.show_message(porpoise::ui::tr("The Wii System Update didn't finish"),
                               porpoise::ui::tr("Some of the Wii's system software couldn't be downloaded or "
                                                "installed. Check that the console is online and try again; what "
                                                "was installed stays. Details are in porpoise/core.log."));
        ps5::debug::mark("main: back in the library");
        start_entrance();
        fetch_covers();
        porpoise::pacer::set_vsync(g_settings.vsync);
        g_pacer.start(hz, "launcher");
    }
}

/* The SDK's _start calls this when main returns. Leaving through the shell is
 * what returns to the home screen; kernel exit(0) raises SIGSYS for an app. */
extern "C" void catchReturnFromMain(int status)
{
    leave(status);
}
