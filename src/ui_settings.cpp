/* Porpoise - settings.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * One table names every setting: its key in settings.ini (and in a game's own
 * file), its field, and its range. Loading, saving, a game's overrides and
 * resetting all go through it. */
#include "ui_settings.hpp"

#include "porpoise_atomic.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace porpoise
{
namespace
{
std::string trim(std::string s)
{
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
        ++i;
    return s.substr(i);
}

bool as_bool(const std::string &v)
{
    return v == "1" || v == "true" || v == "on" || v == "yes" || v == "enabled";
}

struct Field
{
    const char *key;
    int Settings::*i;
    bool Settings::*b;
    int min, max;
};

const Field kFields[] = {
    {"auto_search", nullptr, &Settings::auto_search, 0, 1},
    {"download_covers", nullptr, &Settings::download_covers, 0, 1},
    {"download_info", nullptr, &Settings::download_info, 0, 1},
    {"resolution", &Settings::resolution, nullptr, 1, 6},
    {"widescreen", nullptr, &Settings::widescreen, 0, 1},
    {"wide", &Settings::wide, nullptr, 0, 3},
    {"aspect", &Settings::aspect, nullptr, 0, 3},
    {"anisotropy", &Settings::anisotropy, nullptr, 0, 4},
    {"texture_filter", &Settings::texture_filter, nullptr, 0, 2},
    {"antialiasing", &Settings::antialiasing, nullptr, 0, 6},
    {"resampling", &Settings::resampling, nullptr, 0, 6},
    {"screen_filter", &Settings::screen_filter, nullptr, 0, 16},
    {"filter_strength", &Settings::filter_strength, nullptr, 1, 10},
    {"bloom", &Settings::bloom, nullptr, 0, 3},
    {"color_saturation", &Settings::color_saturation, nullptr, 0, 10},
    {"color_contrast", &Settings::color_contrast, nullptr, 0, 10},
    {"color_warmth", &Settings::color_warmth, nullptr, 0, 10},
    {"enh_advanced", nullptr, &Settings::enh_advanced, 0, 1},
    {"fps_overlay", nullptr, &Settings::fps_overlay, 0, 1},
    {"shader_mode", &Settings::shader_mode, nullptr, 0, 3},
    {"threaded_gpu", nullptr, &Settings::threaded_gpu, 0, 1},
    {"texture_cache", &Settings::texture_cache, nullptr, 0, 2},
    {"pixel_lighting", nullptr, &Settings::pixel_lighting, 0, 1},
    {"disable_fog", nullptr, &Settings::disable_fog, 0, 1},
    {"crop_overscan", nullptr, &Settings::crop_overscan, 0, 1},
    {"custom_textures", nullptr, &Settings::custom_textures, 0, 1},
    {"skip_dupes", nullptr, &Settings::skip_dupes, 0, 1},
    {"gfx_bloom", &Settings::gfx_bloom, nullptr, 0, 3},
    {"gfx_dof", &Settings::gfx_dof, nullptr, 0, 3},
    {"gfx_hud", nullptr, &Settings::gfx_hud, 0, 1},
    {"gfx_extra", nullptr, &Settings::gfx_extra, 0, 1},
    {"vsync", nullptr, &Settings::vsync, 0, 1},
    {"frame_gen", nullptr, &Settings::frame_gen, 0, 1},
    {"fg_steady30", nullptr, &Settings::fg_steady30, 0, 1},
    {"fg_multiplier", &Settings::fg_multiplier, nullptr, 0, 2},
    {"fg_pacing", &Settings::fg_pacing, nullptr, 0, 1},
    {"fg_safety", nullptr, &Settings::fg_safety, 0, 1},
    {"fg_debug_view", nullptr, &Settings::fg_debug_view, 0, 1},
    {"fg_log", nullptr, &Settings::fg_log, 0, 1},
    {"fps_detail", nullptr, &Settings::fps_detail, 0, 1},
    {"output_res", &Settings::output_res, nullptr, 0, 3},
    {"fast_states", nullptr, &Settings::fast_states, 0, 1},
    {"quick_resume", nullptr, &Settings::quick_resume, 0, 1},
    {"volume", &Settings::volume, nullptr, 0, 10},
    {"muted", nullptr, &Settings::muted, 0, 1},
    {"menu_music", nullptr, &Settings::menu_music, 0, 1},
    {"music_volume", &Settings::music_volume, nullptr, 0, 10},
    {"menu_sounds", nullptr, &Settings::menu_sounds, 0, 1},
    {"sound_set", &Settings::sound_set, nullptr, 0, 2},
    {"sounds_volume", &Settings::sounds_volume, nullptr, 0, 10},
    {"button_layout", &Settings::button_layout, nullptr, 0, 5},
    {"invert_main", &Settings::invert_main, nullptr, 0, 3},
    {"p1_port", &Settings::p1_port, nullptr, 0, 3},
    {"invert_c", &Settings::invert_c, nullptr, 0, 3},
    {"stick_sensitivity", &Settings::stick_sensitivity, nullptr, 0, 3},
    {"stick_deadzone", &Settings::stick_deadzone, nullptr, 0, 3},
    {"rumble", nullptr, &Settings::rumble, 0, 1},
    {"ff_buttons", nullptr, &Settings::ff_buttons, 0, 1},
    {"quick_slot", &Settings::quick_slot, nullptr, 0, 3},
    {"shot_buttons", nullptr, &Settings::shot_buttons, 0, 1},
    {"turbo", &Settings::turbo, nullptr, 0, 8},
    {"trigger_feel", &Settings::trigger_feel, nullptr, 0, 3},
    {"light_1", &Settings::light_1, nullptr, 0, 9},
    {"light_2", &Settings::light_2, nullptr, 0, 9},
    {"light_3", &Settings::light_3, nullptr, 0, 9},
    {"light_4", &Settings::light_4, nullptr, 0, 9},
    {"sandbox_notice", nullptr, &Settings::sandbox_notice, 0, 1},
    {"testing_notice", &Settings::testing_notice, nullptr, 0, 2000000000},
    {"console", &Settings::console, nullptr, 0, 2},
    {"dsp_accurate", nullptr, &Settings::dsp_accurate, 0, 1},
    {"wiimote_speaker", &Settings::wiimote_speaker, nullptr, 0, 2},
    {"audio_buffer", &Settings::audio_buffer, nullptr, 0, 2},
    {"audio_stretch", nullptr, &Settings::audio_stretch, 0, 1},
    {"audio_pull", nullptr, &Settings::audio_pull, 0, 1},
    {"audio_fill", nullptr, &Settings::audio_fill, 0, 1},
    {"microphone", nullptr, &Settings::microphone, 0, 1},
    {"wii_widescreen", nullptr, &Settings::wii_widescreen, 0, 1},
    {"pal60", nullptr, &Settings::pal60, 0, 1},
    {"sensor_bar", &Settings::sensor_bar, nullptr, 0, 1},
    {"wii_menu_boot", nullptr, &Settings::wii_menu_boot, 0, 1},
    {"gc_bios", nullptr, &Settings::gc_bios, 0, 1},
    {"wii_online", nullptr, &Settings::wii_online, 0, 1},
    {"online_custom", nullptr, &Settings::online_custom, 0, 1},
    {"online_https", nullptr, &Settings::online_https, 0, 1},
    {"wii_source", &Settings::wii_source, nullptr, 0, 1},
    {"real_wiimote_speaker", nullptr, &Settings::real_wiimote_speaker, 0, 1},
    {"wii_controller", &Settings::wii_controller, nullptr, 0, 5},
    {"wii_pointer", &Settings::wii_pointer, nullptr, 0, 2},
    {"wii_speed", &Settings::wii_speed, nullptr, 0, 10},
    {"wii_grip", &Settings::wii_grip, nullptr, 0, 3},
    {"wii_motion", nullptr, &Settings::wii_motion, 0, 1},
    {"wii_shake", nullptr, &Settings::wii_shake, 0, 1},
    {"wii_motion_plus", &Settings::wii_motion_plus, nullptr, 0, 2},
    {"wii_invert_x", nullptr, &Settings::wii_invert_x, 0, 1},
    {"wii_invert_y", nullptr, &Settings::wii_invert_y, 0, 1},
    {"wii_screen_x", &Settings::wii_screen_x, nullptr, 0, 900},
    {"wii_screen_y", &Settings::wii_screen_y, nullptr, 0, 900},
    {"wii_setup_ask", nullptr, &Settings::wii_setup_ask, 0, 1},
    {"wii_setup_advanced", nullptr, &Settings::wii_setup_advanced, 0, 1},
    {"wii_smooth", &Settings::wii_smooth, nullptr, 0, 3},
    {"wii_reach", &Settings::wii_reach, nullptr, 50, 200},
    {"wii_range", &Settings::wii_range, nullptr, 0, 3},
    {"wii_buttons_use", &Settings::wii_buttons_use, nullptr, 0, 2},
    {"wii_size", &Settings::wii_size, nullptr, 10, 150},
    {"wii_distance", &Settings::wii_distance, nullptr, 5, 250},
    {"wii_preset", &Settings::wii_preset, nullptr, 0, 4},
    {"motion_readout", nullptr, &Settings::motion_readout, 0, 1},
    {"debug_logs", nullptr, &Settings::debug_logs, 0, 1},
    {"developer", nullptr, &Settings::developer, 0, 1},
    {"all_settings", nullptr, &Settings::all_settings, 0, 1},
    {"beta_updates", nullptr, &Settings::beta_updates, 0, 1},
    {"perf_profile", nullptr, &Settings::perf_profile, 0, 1},
    {"setup_checked", nullptr, &Settings::setup_checked, 0, 1},
    {"motion_logs", nullptr, &Settings::motion_logs, 0, 1},
    {"cpu_clock", &Settings::cpu_clock, nullptr, 0, 9},
    {"dual_core", nullptr, &Settings::dual_core, 0, 1},
    {"accurate_fma", nullptr, &Settings::accurate_fma, 0, 1},
    {"own_cores", nullptr, &Settings::own_cores, 0, 1},
    {"fast_disc", nullptr, &Settings::fast_disc, 0, 1},
    {"fast_float", nullptr, &Settings::fast_float, 0, 1},
    {"wait_shaders", nullptr, &Settings::wait_shaders, 0, 1},
    {"cheats", nullptr, &Settings::cheats, 0, 1},
    {"language", &Settings::language, nullptr, 0, 9},
    {"progressive", nullptr, &Settings::progressive, 0, 1},
    {"reduced_motion", nullptr, &Settings::reduced_motion, 0, 1},
    {"large_text", nullptr, &Settings::large_text, 0, 1},
    {"ui_theme", &Settings::ui_theme, nullptr, 0, 18},
    {"ui_palette", &Settings::ui_palette, nullptr, 0, 23},
    {"ui_font", &Settings::ui_font, nullptr, 0, 8},
    {"lib_view", &Settings::lib_view, nullptr, 0, 9},
    {"recent_dock", nullptr, &Settings::recent_dock, 0, 1},
    {"recent_style", &Settings::recent_style, nullptr, 0, 3},
    {"mc_view", &Settings::mc_view, nullptr, 0, 4},
    {"sc_games", &Settings::sc_games, nullptr, 0, 1},
    {"text_size", &Settings::text_size, nullptr, 0, 3},
    {"colour_filter", &Settings::colour_filter, nullptr, 0, 4},
    {"colour_filter_games", nullptr, &Settings::colour_filter_games, 0, 1},
    {"high_contrast", nullptr, &Settings::high_contrast, 0, 1},
    {"bold_focus", nullptr, &Settings::bold_focus, 0, 1},
    {"big_prompts", nullptr, &Settings::big_prompts, 0, 1},
    {"still_background", nullptr, &Settings::still_background, 0, 1},
    {"ui_layout", &Settings::ui_layout, nullptr, 0, 1},
    {"ui_pointer", nullptr, &Settings::ui_pointer, 0, 1},
    {"ui_language", &Settings::ui_language, nullptr, 0, 16},
};

/* Settings kept as text rather than numbers. */
bool is_dolphin_key(const std::string &key)
{
    /* dolphin.<Section>.<Key>: one of Dolphin's own per-game settings. */
    return key.rfind("dolphin.", 0) == 0 && key.find('.', 8) != std::string::npos;
}

bool is_text_key(const std::string &key)
{
    return key == "border" || key == "online_dns" || is_dolphin_key(key);
}

const Field *field(const std::string &key)
{
    for (const Field &f : kFields)
        if (key == f.key)
            return &f;
    return nullptr;
}

void write_field(std::FILE *f, const Settings &s, const Field &fd)
{
    if (fd.i)
        std::fprintf(f, "%s = %d\n", fd.key, s.*fd.i);
    else
        std::fprintf(f, "%s = %d\n", fd.key, (s.*fd.b) ? 1 : 0);
}

/* Where a settings line's key ends (its "="). 4.0: a cheat's name can hold an
 * "=" ("$Ceiling Walk (D-Up & B = ON)"), so for Dolphin's keys the value is
 * after the last " = ". */
std::size_t key_end(const std::string &s)
{
    const std::size_t eq = s.find('=');
    if (eq != std::string::npos && s.rfind("dolphin.", 0) == 0)
        if (const std::size_t last = s.rfind(" = "); last != std::string::npos && last > eq)
            return last + 1;
    return eq;
}

const char *on_off(bool v)
{
    return v ? "enabled" : "disabled";
}

/* Values the Dolphin core expects, in the order Porpoise lists them. */
const char *const kCpuClocks[] = {"0.50", "0.60", "0.70", "0.80", "0.90", "1.00", "1.50", "2.00", "2.50", "3.00"};
const char *const kTextureCache[] = {"128", "512", "0"};
/* English first; the core numbers Japanese 0. */
const int kLanguages[] = {1, 0, 2, 3, 4, 5, 6, 7, 8, 9};

const char *const kManaged[] = {
    "dolphin_efb_scale", "dolphin_widescreen_hack", "dolphin_aspect_ratio", "dolphin_universal_widescreen", "dolphin_libretro_vfs_enabled", "dolphin_max_anisotropy",
    "dolphin_force_texture_filtering_mode", "dolphin_anti_aliasing", "dolphin_enhance_output_resampling",
    "dolphin_shader_compilation_mode", "dolphin_texture_cache_accuracy", "dolphin_pixel_lighting",
    "dolphin_disable_fog", "dolphin_crop_overscan", "dolphin_load_custom_textures",
    "dolphin_cache_custom_textures", "dolphin_skip_dupe_frames", "dolphin_mods_enabled", "dolphin_enable_rumble", "dolphin_cpu_clock_rate",
    "dolphin_main_cpu_thread", "dolphin_accurate_fmadds", "dolphin_osd_enabled", "dolphin_fast_disc_speed", "dolphin_log_level", "dolphin_cheats_enabled", "dolphin_language",
    "dolphin_progressive_scan", "dolphin_dsp_hle", "dolphin_dsp_jit", "dolphin_enable_gamecube_mic",
    "dolphin_hotkey_activate_microphone", "dolphin_wiispeak_enable", "dolphin_wiispeak_muted", "dolphin_widescreen",
    "dolphin_pal60", "dolphin_sensor_bar_position", "dolphin_wait_for_shaders", "dolphin_fast_float_math", "dolphin_disc_based_games_boot_to_wii_menu", "dolphin_skip_gc_bios",
    /* Set per game from its Dolphin settings (porpoise_main.cpp, kGameOptions). */
    "dolphin_efb_to_texture", "dolphin_xfb_to_texture_enable", "dolphin_efb_access_enable",
    "dolphin_efb_access_defer_invalidation", "dolphin_bbox_enabled", "dolphin_efb_to_vram", "dolphin_defer_efb_copies",
    "dolphin_immediate_xfb", "dolphin_efb_scaled_copy", "dolphin_efb_emulate_format_changes",
    "dolphin_vertex_rounding", "dolphin_vi_skip", "dolphin_fast_texture_sampling",
    /* The Wii Remote's pointer: Porpoise's pointer (gyro, touch pad), or the right stick. */
    "dolphin_ir_mode", "dolphin_ir_passthrough",
    /* Off at every launch: Porpoise turns it on itself for two-controller play
     * (porpoise_core.cpp, nunchuk_motion_step). */
    "dolphin_save_load_settings",
};
} // namespace

bool Settings::load(const std::string &path, bool overlay)
{
    std::FILE *f = std::fopen(path.c_str(), "r");
    if (!f)
        return false;
    if (!overlay)
        folders.clear();
    bool versioned = false;
    int version = 0;
    int legacy_sharp = -1;   /* 1.0's Smooth / Sharp switch */
    bool saw_wide = false;   /* 2.1.1's Widescreen choice */
    int legacy_wide = -1;    /* 2.0's widescreen hack switch */
    bool saw_filter = false;
    char line[1024];
    while (std::fgets(line, sizeof line, f))
    {
        std::string s = trim(line);
        if (s.empty() || s[0] == '#')
            continue;
        const auto eq = key_end(s);
        if (eq == std::string::npos)
            continue;
        const std::string k = trim(s.substr(0, eq)), v = trim(s.substr(eq + 1));
        if (k == "settings_version")
        {
            versioned = true;
            version = std::atoi(v.c_str());
            continue;
        }
        if (k == "folder")
        {
            if (!overlay && !v.empty() && std::find(folders.begin(), folders.end(), v) == folders.end())
                folders.push_back(v);
            continue;
        }
        if (k == "border")
        {
            border = v;
            continue;
        }
        if (k == "online_dns")
        {
            if (!overlay)
                online_dns = v; /* the console's setting: never a game's own */
            continue;
        }
        if (is_dolphin_key(k))
        {
            set(k, v);
            continue;
        }
        if (k.size() == 10 && k.rfind("wiipreset", 0) == 0 && k[9] >= '1' && k[9] <= '4')
        {
            /* A Wii preset: name controller grip pointer speed screen_x screen_y smooth reach. */
            if (overlay)
                continue;
            WiiPreset &w = wii_presets[k[9] - '1'];
            int n[9] = {};
            const int got = std::sscanf(v.c_str(), "%d %d %d %d %d %d %d %d %d", &n[0], &n[1], &n[2], &n[3], &n[4],
                                        &n[5], &n[6], &n[7], &n[8]);
            if (got == 9)
            {
                w.used = true;
                w.name = std::clamp(n[0], 0, int(wii_preset_names().size()) - 1);
                w.controller = std::clamp(n[1], 0, int(porpoise::pad::WiiControllerCount) - 1);
                w.grip = std::clamp(n[2], 0, int(porpoise::pad::GripCount) - 1);
                w.pointer = std::clamp(n[3], 0, 2);
                w.speed = std::clamp(n[4], 0, 10);
                w.screen_x = std::clamp(n[5], 0, 900);
                w.screen_y = std::clamp(n[6], 0, 900);
                w.smooth = std::clamp(n[7], 0, 3);
                w.reach = std::clamp(n[8], 50, 200);
            }
            continue;
        }
        if (k.size() == 7 && k.rfind("layout", 0) == 0 && k[6] >= '1' && k[6] <= '4')
        {
            /* One of the player's own layouts: twelve controls. */
            if (overlay)
                continue;
            int *row = presets[k[6] - '1'];
            const char *c = v.c_str();
            for (int i = 0; i < porpoise::pad::GcCount && *c; ++i)
            {
                char *end = nullptr;
                const long n = std::strtol(c, &end, 10);
                if (end == c)
                    break;
                row[i] = std::clamp(int(n), 0, porpoise::pad::CtlCount - 1);
                c = end;
            }
            continue;
        }
        const bool second_set = k.size() == 13 && k.rfind("wiibuttons2_", 0) == 0 && k[12] >= '1' && k[12] <= '4';
        if (second_set || (k.size() == 11 && k.rfind("wiibuttons", 0) == 0 && k[10] >= '1' && k[10] <= '4'))
        {
            /* The player's own Wii buttons: sixteen controls, a permutation. */
            if (overlay)
                continue;
            int row[porpoise::pad::CtlCount];
            bool seen[porpoise::pad::CtlCount] = {};
            bool ok = true;
            const char *c = v.c_str();
            for (int i = 0; i < porpoise::pad::CtlCount && ok; ++i)
            {
                char *end = nullptr;
                const long n = std::strtol(c, &end, 10);
                ok = end != c && n >= 0 && n < porpoise::pad::CtlCount && !seen[n];
                if (ok)
                {
                    row[i] = int(n);
                    seen[n] = true;
                    c = end;
                }
            }
            if (ok) /* anything else (damaged) keeps Porpoise's */
                std::memcpy(second_set ? wii_buttons2[k[12] - '1'] : wii_buttons[k[10] - '1'], row, sizeof row);
            continue;
        }
        if (k.rfind("map_", 0) == 0)
        {
            /* 1.0 kept a single custom layout in map_* keys: it becomes the
             * player's layout 1. */
            static const char *const kNames[porpoise::pad::GcCount] = {
                "map_a", "map_b", "map_x", "map_y", "map_z", "map_l", "map_r", "map_start", "map_up",
                "map_down", "map_left", "map_right"};
            if (!overlay)
                for (int i = 0; i < porpoise::pad::GcCount; ++i)
                    if (k == kNames[i])
                        presets[0][i] = std::clamp(std::atoi(v.c_str()), 0, porpoise::pad::CtlCount - 1);
            continue;
        }
        if (k == "sharp")
        {
            legacy_sharp = as_bool(v) ? 1 : 0;
            continue;
        }
        if (k == "screen_filter")
            saw_filter = true;
        if (k == "wide")
            saw_wide = true;
        if (k == "widescreen")
            legacy_wide = as_bool(v) ? 1 : 0;
        if (const Field *fd = field(k))
        {
            if (fd->i)
                this->*fd->i = std::clamp(std::atoi(v.c_str()), fd->min, fd->max);
            else
                this->*fd->b = as_bool(v);
        }
    }
    std::fclose(f);
    /* Builds before 10 saved 4K as the default; 1080p is the default now, and
     * 4K stays only for those who choose it again. */
    if (!overlay && !versioned && resolution == 6)
        resolution = 3;
    /* Before 11: shaders compiled synchronously by default, which stalls a
     * game each time it draws something new; asynchronous ubershaders are the
     * default now. And the old two-way layout switch is gone: everyone starts
     * on the GameCube layout, PlayStation and Custom are a choice away. */
    /* 13: Output resolution gained "Match the PS5" first (and the default):
     * a chosen 1440p or 4K moves up one; 1080p, the old default, matches. */
    if (!overlay)
        loaded_version = version;
    if (!overlay && version < 13 && output_res > 0)
        output_res = std::min(output_res + 1, 3);
    if (!overlay && version < 11 && shader_mode == 0)
        shader_mode = 2;
    /* 15 (3.0): the trigger click is Firm for everyone, once; Off, Light and
     * Extra Firm stay a choice away. */
    if (!overlay && versioned && version < 15)
        trigger_feel = 2;
    /* 16 (3.0): the menu sounds are Crisp for everyone, once; Soft and
     * Porpoise's original sounds stay a choice away. */
    if (!overlay && versioned && version < 16)
        sound_set = 0;
    /* 17 (3.0 Alpha 5): a DNS server typed in before is a custom server, on. */
    if (!overlay && version < 17 && !online_dns.empty())
        online_custom = true;
    /* 18 (3.0 Alpha 7): Recently Played has looks; off stays off. */
    if (!overlay && version < 18)
        recent_style = recent_dock ? 1 : 0;
    /* 2.1.1: the widescreen hack switch became the Widescreen choice; on, it
     * is On (the hack for games with no code of their own). Per game too. */
    if (!saw_wide && legacy_wide >= 0)
        wide = legacy_wide == 1 ? 1 : 0; /* a game's own "hack off" is Auto: no hack, its code if it has one */
    widescreen = false;
    /* 1.1: Smooth / Sharp became the first two screen filters. */
    if (legacy_sharp >= 0 && !saw_filter)
        screen_filter = legacy_sharp;
    /* 2.1: Larger text became a text size. */
    if (!overlay && large_text)
    {
        if (text_size == 0)
            text_size = 1;
        large_text = false;
    }
    /* 4.0: the release has no developer options, debug logs or performance
     * report; a setting left on by a test build goes off. */
    developer = false;
    debug_logs = false;
    perf_profile = false;
    motion_readout = motion_logs = fps_detail = fg_debug_view = fg_log = false;
    wii_invert_x = wii_invert_y = false;
    return true;
}

bool Settings::save(const std::string &path) const
{
    std::FILE *f = open_atomic(path);
    if (!f)
        return false;
    std::fprintf(f, "# Porpoise settings (written by the Settings screen)\nsettings_version = 18\n");
    for (const Field &fd : kFields)
        write_field(f, *this, fd);
    std::fprintf(f, "border = %s\n", border.c_str());
    std::fprintf(f, "online_dns = %s\n", online_dns.c_str());
    for (int p = 0; p < kPresets; ++p)
    {
        std::fprintf(f, "layout%d =", p + 1);
        for (int i = 0; i < porpoise::pad::GcCount; ++i)
            std::fprintf(f, " %d", presets[p][i]);
        std::fprintf(f, "\n");
    }
    for (int s = 0; s < kWiiButtonSets; ++s)
    {
        std::fprintf(f, "wiibuttons%d =", s + 1);
        for (int i = 0; i < porpoise::pad::CtlCount; ++i)
            std::fprintf(f, " %d", wii_buttons[s][i]);
        std::fprintf(f, "\n");
    }
    for (int s = 0; s < kWiiButtonSets; ++s)
    {
        std::fprintf(f, "wiibuttons2_%d =", s + 1);
        for (int i = 0; i < porpoise::pad::CtlCount; ++i)
            std::fprintf(f, " %d", wii_buttons2[s][i]);
        std::fprintf(f, "\n");
    }
    for (int p = 0; p < kWiiPresets; ++p)
    {
        const WiiPreset &w = wii_presets[p];
        if (w.used)
            std::fprintf(f, "wiipreset%d = %d %d %d %d %d %d %d %d %d\n", p + 1, w.name, w.controller, w.grip,
                         w.pointer, w.speed, w.screen_x, w.screen_y, w.smooth, w.reach);
    }
    for (const std::string &folder : folders)
        std::fprintf(f, "folder = %s\n", folder.c_str());
    return finish_atomic(f, path);
}

bool Settings::save_keys(const std::string &path, const std::vector<std::string> &keys) const
{
    if (keys.empty())
    {
        std::remove(path.c_str());
        return true;
    }
    std::FILE *f = open_atomic(path);
    if (!f)
        return false;
    std::fprintf(f, "# This game's own settings (Porpoise > Details > Game settings)\n");
    std::vector<std::string> all = keys;
    if (std::find(all.begin(), all.end(), "widescreen") != all.end() && std::find(all.begin(), all.end(), "wide") == all.end())
        all.push_back("wide"); /* 2.0's switch, carried into the choice it became */
    for (const std::string &k : all)
    {
        if (const Field *fd = field(k))
            write_field(f, *this, *fd);
        else if (k == "border")
            std::fprintf(f, "border = %s\n", border.c_str());
        else if (k == "online_dns")
            std::fprintf(f, "online_dns = %s\n", online_dns.c_str());
        else if (is_dolphin_key(k) && !get(k).empty())
            std::fprintf(f, "%s = %s\n", k.c_str(), get(k).c_str());
    }
    return finish_atomic(f, path);
}

std::vector<std::string> Settings::keys_in(const std::string &path)
{
    std::vector<std::string> keys;
    std::FILE *f = std::fopen(path.c_str(), "r");
    if (!f)
        return keys;
    char line[512];
    while (std::fgets(line, sizeof line, f))
    {
        const std::string s = trim(line);
        const auto eq = key_end(s);
        if (s.empty() || s[0] == '#' || eq == std::string::npos)
            continue;
        const std::string k = trim(s.substr(0, eq));
        if ((field(k) || is_text_key(k)) && std::find(keys.begin(), keys.end(), k) == keys.end())
            keys.push_back(k);
    }
    std::fclose(f);
    return keys;
}

void Settings::forget(const std::string &key)
{
    dolphin.erase(std::remove_if(dolphin.begin(), dolphin.end(),
                                 [&](const std::pair<std::string, std::string> &kv) { return kv.first == key; }),
                  dolphin.end());
}

bool Settings::write_dolphin_game_ini(const std::string &path) const
{
    /* Dolphin's own per-game file in its user folder (its LocalGame layer),
     * over the shipped Sys/GameSettings: Porpoise writes it before each game
     * from that game's dolphin.* settings, and removes it when there are none,
     * as long as it is Porpoise's. */
    static const char kMark[] = "# Written by Porpoise";
    bool ours = true;
    if (std::FILE *f = std::fopen(path.c_str(), "r"))
    {
        char first[64] = {0};
        ours = std::fgets(first, sizeof first, f) && std::strncmp(first, kMark, sizeof kMark - 1) == 0;
        std::fclose(f);
    }
    else if (dolphin.empty())
        return true;
    if (!ours)
        return false; /* the player's own file: left alone */
    if (dolphin.empty())
        return std::remove(path.c_str()) == 0;
    std::vector<std::string> sections;
    for (const auto &kv : dolphin)
    {
        const std::string sec = kv.first.substr(8, kv.first.find('.', 8) - 8);
        if (std::find(sections.begin(), sections.end(), sec) == sections.end())
            sections.push_back(sec);
    }
    std::FILE *f = open_atomic(path);
    if (!f)
        return false;
    std::fprintf(f, "%s from this game's settings; changes here are replaced.\n", kMark);
    for (const std::string &sec : sections)
    {
        /* A list of codes (OnFrame_Enabled, Gecko_Disabled, ...) names each
         * one on its own line; every other section is key = value. */
        const bool list = (sec.size() > 8 && sec.compare(sec.size() - 8, 8, "_Enabled") == 0) ||
                          (sec.size() > 9 && sec.compare(sec.size() - 9, 9, "_Disabled") == 0);
        std::fprintf(f, "\n[%s]\n", sec.c_str());
        for (const auto &kv : dolphin)
            if (kv.first.compare(8, sec.size() + 1, sec + ".") == 0)
            {
                if (!list)
                    std::fprintf(f, "%s = %s\n", kv.first.substr(9 + sec.size()).c_str(), kv.second.c_str());
                else if (kv.second == "1" || kv.second == "True")
                {
                    /* A Gecko code by the name Dolphin gives it: up to its
                     * author tag (ui_cheats list_name). */
                    std::string name = kv.first.substr(9 + sec.size());
                    if (sec.rfind("Gecko_", 0) == 0)
                    {
                        std::string n = name.substr(0, name.find('['));
                        while (!n.empty() && (n.back() == ' ' || n.back() == '\t'))
                            n.pop_back();
                        std::size_t i = n.empty() || n[0] != '$' ? 0 : 1;
                        while (i < n.size() && (n[i] == ' ' || n[i] == '\t'))
                            n.erase(i, 1);
                        if (n.size() > 1)
                            name = n;
                    }
                    std::fprintf(f, "%s\n", name.c_str());
                }
            }
    }
    return finish_atomic(f, path);
}

bool Settings::migrate_game_file(const std::string &path, Settings &global)
{
    /* 1.0 kept a game's own buttons as map_* lines with button_layout = 2
     * (Custom), and its Smooth / Sharp choice as sharp. 1.1 has the player's
     * four layouts in the global settings, and screen_filter. */
    std::FILE *f = std::fopen(path.c_str(), "r");
    if (!f)
        return false;
    static const char *const kNames[porpoise::pad::GcCount] = {
        "map_a", "map_b", "map_x", "map_y", "map_z", "map_l", "map_r", "map_start", "map_up",
        "map_down", "map_left", "map_right"};
    std::vector<std::string> keep;
    int map[porpoise::pad::GcCount];
    /* 1.0's map_* lines changed 1.0's GameCube layout (A Cross, B Square,
     * X Circle), so that's what they start from. */
    porpoise::pad::Mapping base = porpoise::pad::preset(porpoise::pad::LayoutGameCube);
    base.control[porpoise::pad::GcA] = porpoise::pad::CtlCross;
    base.control[porpoise::pad::GcB] = porpoise::pad::CtlSquare;
    base.control[porpoise::pad::GcX] = porpoise::pad::CtlCircle;
    for (int i = 0; i < porpoise::pad::GcCount; ++i)
        map[i] = base.control[i];
    bool old = false, saw_map = false, custom = false, saw_filter = false;
    int sharp = -1;
    char line[1024];
    while (std::fgets(line, sizeof line, f))
    {
        const std::string t = trim(line);
        const auto eq = t.find('=');
        const std::string k = eq == std::string::npos ? "" : trim(t.substr(0, eq));
        const std::string v = eq == std::string::npos ? "" : trim(t.substr(eq + 1));
        bool drop = false;
        for (int i = 0; i < porpoise::pad::GcCount; ++i)
            if (k == kNames[i])
            {
                map[i] = std::clamp(std::atoi(v.c_str()), 0, porpoise::pad::CtlCount - 1);
                saw_map = drop = old = true;
            }
        if (k == "sharp")
        {
            sharp = as_bool(v) ? 1 : 0;
            drop = old = true;
        }
        if (k == "screen_filter")
            saw_filter = true;
        if (k == "button_layout" && std::atoi(v.c_str()) == 2)
        {
            custom = true;
            drop = true; /* written again below */
        }
        if (!drop)
            keep.push_back(t);
    }
    std::fclose(f);
    if (!old)
        return false;
    bool global_changed = false;
    if (custom && saw_map)
    {
        /* The same layout among the player's, or a free one (still the
         * GameCube layout it started as), else the last. */
        int use = -1;
        for (int p = 0; p < kPresets && use < 0; ++p)
            if (std::equal(map, map + porpoise::pad::GcCount, global.presets[p]))
                use = p;
        for (int p = 1; p < kPresets && use < 0; ++p)
        {
            bool fresh = true;
            for (int i = 0; i < porpoise::pad::GcCount; ++i)
                fresh &= global.presets[p][i] == base.control[i];
            if (fresh)
                use = p;
        }
        if (use < 0)
            use = kPresets - 1;
        if (!std::equal(map, map + porpoise::pad::GcCount, global.presets[use]))
        {
            std::copy(map, map + porpoise::pad::GcCount, global.presets[use]);
            global_changed = true;
        }
        keep.push_back("button_layout = " + std::to_string(2 + use));
    }
    else if (custom)
        keep.push_back("button_layout = 2");
    if (sharp >= 0 && !saw_filter)
        keep.push_back(std::string("screen_filter = ") + (sharp ? "1" : "0"));
    if (std::FILE *o = porpoise::open_atomic(path))
    {
        for (const std::string &l : keep)
            if (!l.empty())
                std::fprintf(o, "%s\n", l.c_str());
        porpoise::finish_atomic(o, path); /* the old file stays if this one didn't make it */
    }
    return global_changed;
}

bool Settings::set(const std::string &key, const std::string &value)
{
    if (key == "border")
    {
        border = value;
        return true;
    }
    if (key == "online_dns")
    {
        online_dns = value;
        return true;
    }
    if (is_dolphin_key(key))
    {
        for (auto &kv : dolphin)
            if (kv.first == key)
            {
                kv.second = value;
                return true;
            }
        dolphin.push_back({key, value});
        return true;
    }
    const Field *fd = field(key);
    if (!fd)
        return false;
    if (fd->i)
        this->*fd->i = std::clamp(std::atoi(value.c_str()), fd->min, fd->max);
    else
        this->*fd->b = as_bool(value);
    return true;
}

std::vector<std::string> Settings::changed_keys(const Settings &before) const
{
    std::vector<std::string> keys;
    for (const Field &fd : kFields)
        if ((fd.i && this->*fd.i != before.*fd.i) || (fd.b && this->*fd.b != before.*fd.b))
            keys.push_back(fd.key);
    if (border != before.border)
        keys.push_back("border");
    if (online_dns != before.online_dns)
        keys.push_back("online_dns");
    if (stay_sandboxed != before.stay_sandboxed)
        keys.push_back("stay_sandboxed");
    auto add_dolphin = [&](const std::vector<std::pair<std::string, std::string>> &from) {
        for (const auto &kv : from)
            if (get(kv.first) != before.get(kv.first) &&
                std::find(keys.begin(), keys.end(), kv.first) == keys.end())
                keys.push_back(kv.first);
    };
    add_dolphin(dolphin);
    add_dolphin(before.dolphin);
    return keys;
}

void Settings::copy_keys(const Settings &from, const std::vector<std::string> &keys)
{
    for (const std::string &k : keys)
    {
        if (k == "stay_sandboxed")
            stay_sandboxed = from.stay_sandboxed;
        else if (is_dolphin_key(k))
        {
            const std::string v = from.get(k);
            if (v.empty())
                forget(k);
            else
                set(k, v);
        }
        else
            set(k, from.get(k));
    }
}

std::string Settings::get(const std::string &key) const
{
    if (key == "border")
        return border;
    if (key == "online_dns")
        return online_dns;
    if (is_dolphin_key(key))
    {
        for (const auto &kv : dolphin)
            if (kv.first == key)
                return kv.second;
        return "";
    }
    const Field *fd = field(key);
    if (!fd)
        return "";
    return fd->i ? std::to_string(this->*fd->i) : (this->*fd->b ? "1" : "0");
}

const std::vector<std::string> &Settings::wii_preset_names()
{
    static const std::vector<std::string> kNames = {"Living room", "Desk", "Bedroom", "Couch",  "Party",
                                                    "Boxing",      "Tennis", "Racing", "Bowling", "Kids",
                                                    "Guest",       "Mine"};
    return kNames;
}

void Settings::save_wii_preset(int slot, int name)
{
    if (slot < 0 || slot >= kWiiPresets)
        return;
    WiiPreset &w = wii_presets[slot];
    w.used = true;
    w.name = std::clamp(name, 0, int(wii_preset_names().size()) - 1);
    w.controller = wii_controller;
    w.grip = wii_grip;
    w.pointer = wii_pointer;
    w.speed = wii_speed;
    w.screen_x = wii_screen_x;
    w.screen_y = wii_screen_y;
    w.smooth = wii_smooth;
    w.reach = wii_reach;
    wii_preset = slot + 1;
}

bool Settings::use_wii_preset(int slot)
{
    if (slot < 0 || slot >= kWiiPresets || !wii_presets[slot].used)
        return false;
    const WiiPreset &w = wii_presets[slot];
    wii_controller = w.controller;
    wii_grip = w.grip;
    wii_pointer = w.pointer;
    wii_speed = w.speed;
    wii_screen_x = w.screen_x;
    wii_screen_y = w.screen_y;
    wii_smooth = w.smooth;
    wii_reach = w.reach;
    wii_preset = slot + 1;
    return true;
}

porpoise::pad::WiiConfig Settings::wii_config(bool active, const std::string &game_id) const
{
    porpoise::pad::WiiConfig c;
    c.active = active;
    c.controller = std::clamp(wii_controller, 0, int(porpoise::pad::WiiControllerCount) - 1);
    c.pointer = std::clamp(wii_pointer, 0, 2);
    c.speed = std::clamp(wii_speed, 1, 10);
    if (wii_speed == 0)
    {
        if (wii_screen_x > 0 && wii_screen_y > 0)
        {
            c.half_x = float(wii_screen_x) * 0.1f * 3.14159265f / 180.0f;
            c.half_y = float(wii_screen_y) * 0.1f * 3.14159265f / 180.0f;
        }
        else
            c.speed = 5; /* "your screen" with nothing measured yet */
    }
    c.grip = std::clamp(wii_grip, 0, int(porpoise::pad::GripCount) - 1);
    c.motion = wii_motion;
    c.shake = wii_shake;
    c.motion_plus = active && porpoise::pad::wants_motion_plus(wii_motion_plus, game_id);
    c.smooth = std::clamp(wii_smooth, 0, 3);
    c.reach = std::clamp(wii_reach, 50, 200);
    /* The pointer's range: Auto is wide for the games known to need it. */
    {
        static const char *const kWide[] = {"RTZ"}; /* Zack & Wiki */
        bool wide = false;
        for (const char *id : kWide)
            wide |= game_id.compare(0, 3, id) == 0;
        const float ranges[4] = {wide ? 1.35f : 1.0f, 1.0f, 1.35f, 1.6f};
        c.range = ranges[std::clamp(wii_range, 0, 3)];
    }
    c.bar_above = sensor_bar == 1; /* where the game is told the bar is: the pointer's lights agree */
    c.invert_x = developer && wii_invert_x; /* developer options only */
    c.invert_y = developer && wii_invert_y;
    /* Real Wii Remotes (a DolphinBar): every port is a real Remote, which
     * brings its own motion, pointer, MotionPlus and attachments; the
     * DualSense's take no part (a plain Remote, so no two-controller play and
     * no Nunchuk motion file). Only in a game: the home screen's pointer
     * stays the DualSense's. */
    if (active && wii_source == 1)
    {
        c.real_remotes = true;
        c.controller = porpoise::pad::WiiRemote;
        c.motion = c.shake = c.motion_plus = false;
    }
    return c;
}

porpoise::pad::Mapping Settings::mapping() const
{
    const int own = preset_in_use();
    if (own < 0)
        return porpoise::pad::preset(button_layout);
    porpoise::pad::Mapping m{};
    for (int gc = 0; gc < porpoise::pad::GcCount; ++gc)
        m.control[gc] = static_cast<std::int8_t>(std::clamp(presets[own][gc], 0, porpoise::pad::CtlCount - 1));
    return m;
}

void Settings::reset()
{
    /* The game folders, the player's own button layouts and a seen notice stay. */
    const std::vector<std::string> keep = folders;
    const int notice = testing_notice; /* a test build's notice, already seen */
    const std::string dns = online_dns; /* the network's, not a look or a feel */
    const bool custom = online_custom, https = online_https;
    int own[kPresets][porpoise::pad::GcCount];
    std::memcpy(own, presets, sizeof own);
    int wii[kWiiButtonSets][porpoise::pad::CtlCount], wii2[kWiiButtonSets][porpoise::pad::CtlCount];
    std::memcpy(wii, wii_buttons, sizeof wii);
    std::memcpy(wii2, wii_buttons2, sizeof wii2);
    *this = Settings{};
    folders = keep;
    testing_notice = notice;
    online_dns = dns;
    online_custom = custom;
    online_https = https;
    std::memcpy(presets, own, sizeof own);
    std::memcpy(wii_buttons, wii, sizeof wii);
    std::memcpy(wii_buttons2, wii2, sizeof wii2);
}

const int (*Settings::wii_buttons_in_use() const)[porpoise::pad::CtlCount]
{
    static const int kPorpoise[kWiiButtonSets][porpoise::pad::CtlCount] = {
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}};
    return wii_buttons_use == 1 ? wii_buttons : wii_buttons_use == 2 ? wii_buttons2 : kPorpoise;
}

int Settings::audio_preset() const
{
    if (!audio_pull)
        return 3; /* Classic */
    if (!audio_fill)
        return kAudioCustom;
    switch (audio_buffer)
    {
    case 1:
        return 0; /* Smooth */
    case 0:
        return 1; /* Responsive */
    case 2:
        return 2; /* Extra smooth */
    default:
        return kAudioCustom;
    }
}

void Settings::use_audio_preset(int preset)
{
    switch (preset)
    {
    case 0: /* Smooth: Dolphin's mixer, as on a PC */
        audio_pull = true;
        audio_buffer = 1;
        audio_fill = true;
        break;
    case 1: /* Responsive: less sound kept ready */
        audio_pull = true;
        audio_buffer = 0;
        audio_fill = true;
        break;
    case 2: /* Extra smooth: more kept ready, for games that slow down often */
        audio_pull = true;
        audio_buffer = 2;
        audio_fill = true;
        break;
    case 3: /* Classic: 2.1's sound */
        audio_pull = false;
        audio_buffer = 1;
        audio_stretch = false;
        break;
    default:
        break;
    }
}

int Settings::audio_buffer_ms() const
{
    return audio_buffer <= 0 ? 40 : audio_buffer >= 2 ? 160 : 80;
}

std::vector<std::pair<std::string, std::string>> Settings::core_options() const
{
    return {
        {"dolphin_efb_scale", std::to_string(resolution)},
        /* Widescreen (ui_widescreen): the hack only when the plan says so; a
         * game's own widescreen code wants the picture at 16:9. */
        {"dolphin_widescreen_hack", on_off(ws_plan == 3 || ws_plan == 4)},
        {"dolphin_aspect_ratio", std::to_string(ws_plan == 1 ? 1 : aspect)},
        /* The universal widescreen (beta): the core sets 16:9 itself when it hooks the game. */
        {"dolphin_universal_widescreen", on_off(ws_plan == 5)},
        /* The core reads games on a network share through Porpoise's VFS
         * (porpoise_netfs), for /net/ paths only. */
        {"dolphin_libretro_vfs_enabled", "enabled"},
        {"dolphin_max_anisotropy", std::to_string(anisotropy)},
        {"dolphin_force_texture_filtering_mode", std::to_string(texture_filter)},
        {"dolphin_anti_aliasing", std::to_string(antialiasing)},
        {"dolphin_enhance_output_resampling", std::to_string(resampling)},
        {"dolphin_shader_compilation_mode", std::to_string(shader_mode)},
        {"dolphin_texture_cache_accuracy", kTextureCache[std::clamp(texture_cache, 0, 2)]},
        {"dolphin_pixel_lighting", on_off(pixel_lighting)},
        {"dolphin_disable_fog", on_off(disable_fog)},
        {"dolphin_crop_overscan", on_off(crop_overscan)},
        {"dolphin_load_custom_textures", on_off(custom_textures)},
        /* Not prefetched: a whole pack read into memory at once can run a PS5 app out. */
        {"dolphin_cache_custom_textures", "disabled"},
        {"dolphin_skip_dupe_frames", on_off(skip_dupes)},
        /* Graphics mods: only for a game that has one on (porpoise_gfxmods). */
        {"dolphin_mods_enabled", on_off(gfx_mods_on)},
        {"dolphin_enable_rumble", on_off(rumble)},
        {"dolphin_cpu_clock_rate", kCpuClocks[std::clamp(cpu_clock, 0, 9)]},
        {"dolphin_main_cpu_thread", on_off(dual_core)},
        {"dolphin_accurate_fmadds", on_off(accurate_fma)},
        {"dolphin_fast_disc_speed", on_off(fast_disc)},
        /* 4.0: Dolphin's own log at Info only with Debug Logs on; otherwise warnings
         * and up, which saves a formatted line per event in busy games. */
        {"dolphin_log_level", debug_logs ? "4" : "3"},
        {"dolphin_fast_float_math", on_off(fast_float)},
        {"dolphin_wait_for_shaders", on_off(wait_shaders)},
        {"dolphin_cheats_enabled", on_off(cheats)},
        {"dolphin_language", std::to_string(kLanguages[std::clamp(language, 0, 9)])},
        {"dolphin_progressive_scan", on_off(progressive)},
        /* Sound: HLE unless the game asks for the exact DSP. */
        {"dolphin_dsp_hle", on_off(!dsp_accurate)},
        {"dolphin_dsp_jit", "enabled"},
        /* The microphone; the GameCube Microphone's button is R3 (no
         * GameCube game uses the stick's click). */
        {"dolphin_enable_gamecube_mic", on_off(microphone)},
        {"dolphin_hotkey_activate_microphone", microphone ? "R3" : "Disabled"},
        {"dolphin_wiispeak_enable", on_off(microphone)},
        {"dolphin_wiispeak_muted", on_off(!microphone)},
        /* The console's own settings. */
        {"dolphin_widescreen", on_off(wii_widescreen)},
        {"dolphin_pal60", on_off(pal60)},
        {"dolphin_sensor_bar_position", sensor_bar == 1 ? "1" : "0"},
        {"dolphin_disc_based_games_boot_to_wii_menu", on_off(wii_menu_boot)},
        {"dolphin_skip_gc_bios", on_off(!gc_bios)},
        {"dolphin_ir_mode", wii_pointer == 2 ? "1" : "2"},
        /* Gyro and touch pad: Porpoise works out what the remote's camera sees
         * (porpoise_core.cpp), so the game's cursor is exactly Porpoise's. */
        {"dolphin_ir_passthrough", on_off(wii_pointer != 2)},
        {"dolphin_save_load_settings", "disabled"},
        /* Dolphin's own yellow notes over the game ("Saved to memory card"
         * and the like): Porpoise shows what matters in its own menus. */
        {"dolphin_osd_enabled", "disabled"},
    };
}

bool Settings::write_core_options(const std::string &options_ini) const
{
    std::vector<std::string> keep;
    std::string old_text;
    if (std::FILE *f = std::fopen(options_ini.c_str(), "r"))
    {
        char line[512];
        while (std::fgets(line, sizeof line, f))
        {
            std::string s = line;
            old_text += s;
            const std::string key = trim(s.substr(0, s.find('=')));
            bool is_managed = false;
            for (const char *m : kManaged)
                is_managed |= key == m;
            if (!is_managed && s.rfind("# Set by Porpoise", 0) != 0)
                keep.push_back(s);
        }
        std::fclose(f);
    }
    std::string text;
    for (const std::string &s : keep)
        text += s;
    text += "# Set by Porpoise's Settings screen:\n";
    for (const auto &[key, value] : core_options())
        text += key + " = " + value + "\n";
    if (text == old_text)
        return true; /* as it is already: no rewrite (this runs several times a launch) */
    std::FILE *f = open_atomic(options_ini);
    if (!f)
        return false;
    std::fputs(text.c_str(), f);
    return finish_atomic(f, options_ini);
}
} // namespace porpoise
