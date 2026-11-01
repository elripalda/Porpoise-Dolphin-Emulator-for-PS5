/* Porpoise UI - Settings, a game's own settings, and the game folder browser.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Settings is a rail of sections on the left and the rows of one section on
 * the right. Focus starts on the rail: up and down pick a section, Right or
 * Cross go into it, Circle comes back out. A game's own settings use the same
 * screen with the global values underneath and its changes on top. */
#ifdef PORPOISE_DESKTOP
#include "porpoise_platform.hpp"
#endif
#include "porpoise_gfxmods.hpp"
#include "porpoise_forwarders.hpp"
#include "porpoise_paths.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>

#include "porpoise_pad.hpp"
#include "porpoise_update.hpp"
#include "ui_widescreen.hpp"
#include "ui_app.hpp"

#include "porpoise_nand.hpp"
#include "porpoise_bios.hpp"
#include "ui_app_common.hpp"
#include "ui_i18n.hpp"
#include "ui_recommend.hpp"
#include "ui_setups.hpp"
#include "title_threads.hpp"

#if defined(__has_include)
#if __has_include("title_build_identity.h")
#include "title_build_identity.h"
#endif
#endif

namespace porpoise::ui
{
using namespace look;
using namespace porpoise::pad;

/* "2.0 beta 1", "1.5.1", "1.1 (build 15)": how a release is named to the player. */
std::string App::version_label(const std::string &tag, int build)
{
    int major = 0, minor = 0, patch = 0, beta = 0;
    if (!porpoise::update::tag_version(tag, major, minor, patch, beta))
        return tag;
    std::string shown = std::to_string(major) + "." + std::to_string(minor);
    if (patch)
        shown += "." + std::to_string(patch);
    if (beta < 0)
        shown += " Alpha " + std::to_string(beta + 1000);
    else if (beta)
        shown += " Beta " + std::to_string(beta);
    if (build > 0)
        shown += " (build " + std::to_string(build) + ")";
    return shown;
}

namespace
{
/* "Quick Resume": the label without its marker, which is drawn as a
 * BETA badge beside it instead. */
std::string beta_label(const char *label, bool &beta, bool *alpha = nullptr)
{
    std::string text = label;
    auto ends = [&](const std::string &mark) {
        return text.size() > mark.size() && text.compare(text.size() - mark.size(), mark.size(), mark) == 0;
    };
    const std::string mark = " (beta)", alpha_mark = " (alpha)";
    beta = ends(mark);
    if (beta)
        text.resize(text.size() - mark.size());
    else if (alpha && ends(alpha_mark))
    {
        /* 3.1: "... (alpha)", an ALPHA badge (the BETA badge's place). */
        beta = *alpha = true;
        text.resize(text.size() - alpha_mark.size());
    }
    return tr(text);
}

bool copy_file(const std::string &from, const std::string &to, std::size_t keep_tail = 0)
{
    std::FILE *in = std::fopen(from.c_str(), "rb");
    if (!in)
        return false;
    /* A long log keeps its end, where the problem is. */
    if (keep_tail && std::fseek(in, 0, SEEK_END) == 0)
    {
        const long size = std::ftell(in);
        std::fseek(in, size > long(keep_tail) ? size - long(keep_tail) : 0, SEEK_SET);
    }
    std::FILE *out = std::fopen(to.c_str(), "wb");
    bool ok = out != nullptr;
    char buf[65536];
    std::size_t n;
    while (ok && (n = std::fread(buf, 1, sizeof buf, in)) > 0)
        ok = std::fwrite(buf, 1, n, out) == n;
    std::fclose(in);
    if (out)
        ok = std::fclose(out) == 0 && ok;
    return ok;
}
} // namespace

void App::show_setup_check(bool first_start)
{
    std::vector<std::pair<bool, std::string>> checks;
    auto line = [&](bool ok, const std::string &what) { checks.emplace_back(ok, what); };
#ifdef PORPOISE_DESKTOP
    /* A computer: no sandbox and no console drives, just the games. */
    {
        const int games = int(lib_->games().size());
        if (games > 0)
            line(true, plural(games, "1 game found.", "{n} games found."));
        else
            line(false, tr("No games yet. Put them in the data/games folder beside Porpoise.exe, or add a folder "
                           "in Settings > Games."));
        line(settings_->download_covers, settings_->download_covers
                                             ? tr("Covers download while the computer is online.")
                                             : tr("Cover downloads are off (Settings > Downloads)."));
        open_dialog(DialogKind::Info, first_start ? tr("Welcome to Porpoise") : tr("Your Setup"), "", "");
        dialog_.checks = std::move(checks);
        return;
    }
#endif
    if (sandboxed_)
        line(false, tr("Porpoise is inside the app sandbox: it can't see /data or USB drives. Turn on Legacy Command "
                       "Server in etaHEN, add PPSA99764 to OnionHEN's exact_title_ids, or run a Lapy daemon, then "
                       "open Porpoise again."));
    else
        line(true, tr("Porpoise can see /data and USB drives."));
    std::vector<std::string> drives;
    for (int i = 0; i < 8; ++i)
    {
        struct stat st;
        const std::string root = "/mnt/usb" + std::to_string(i);
        if (stat(root.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            drives.push_back("USB " + std::to_string(i + 1));
    }
    for (int i = 0; i < 2; ++i)
    {
        struct stat st;
        if (stat(("/mnt/ext" + std::to_string(i)).c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            drives.push_back(tr("extended storage"));
    }
    if (!sandboxed_)
    {
        std::string list;
        for (const std::string &d : drives)
            list += (list.empty() ? "" : ", ") + d;
        line(true, drives.empty() ? tr("No USB drive found (games on one: format it exFAT).")
                                  : trf("Drives: {list}.", {{"list", list}}));
    }
    const int games = int(lib_->games().size());
    if (games > 0)
        line(true, plural(games, "1 game found.", "{n} games found."));
    else
        line(false, tr("No games yet. Put them in /data/porpoise/games or on a USB drive, or add a folder in "
                       "Settings > Games."));
    line(settings_->download_covers, settings_->download_covers ? tr("Covers download while the console is online.")
                                                                 : tr("Cover downloads are off (Settings > Downloads)."));
    open_dialog(DialogKind::Info, first_start ? tr("Welcome to Porpoise") : tr("Your Setup"), "", "");
    dialog_.checks = std::move(checks);
}

void App::make_forwarder()
{
    if (!game_for_)
        return;
    porpoise::forwarders::Options o;
    o.title = game_for_->title;
    o.game_path = game_for_->path;
    o.cover_path = lib_->cover_path(*game_for_);
    o.game_id = tile_key();
    o.art = porpoise::tileart::load(data_dir_, o.game_id);
    o.exit_after_game = fwd_exit_;
    const porpoise::forwarders::Result r = porpoise::forwarders::make(data_dir_, o);
    if (r.ok)
        open_dialog(DialogKind::Info, tr("Home screen tile made"),
                    trf("It's in {folder}. Install it with ShadowMountPlus (or the way you install homebrew) to see "
                        "it on the home screen; it opens Porpoise straight into {game}. Close Porpoise before you open "
                        "the tile. Made it again later? Install it again to see the new art.",
                        {{"folder", r.folder}, {"game", game_for_->title}}),
                    "");
    else
        open_dialog(DialogKind::Info, tr("The tile couldn't be made"), trf("Reason: {reason}", {{"reason", r.error}}),
                    "");
    const int row = settings_row_;
    build_game_settings();
    settings_row_ = std::clamp(row, 0, int(rows_.size()) - 1);
}

void App::run_diagnostic()
{
    /* What Porpoise can see (as Check my setup), the host's own checks, and
     * a few of Porpoise's: then all of it saved with the logs. */
    show_setup_check(false);
    std::vector<std::pair<bool, std::string>> checks = std::move(dialog_.checks);
    close_dialog();
    if (diagnostics_)
        for (auto &c : diagnostics_())
            checks.push_back(std::move(c));
    {
        struct stat st;
        const bool core = stat(PORPOISE_APP "/cores/dolphin_libretro.so", &st) == 0 && st.st_size > 0;
        checks.emplace_back(core, core ? tr("Dolphin is in place.")
                                       : tr("Dolphin's file is missing from Porpoise's folder: install Porpoise again."));
    }
    std::string usb;
    const std::string where = save_report(usb);
    if (!where.empty())
        if (std::FILE *f = std::fopen((where + "/diagnostic.txt").c_str(), "w"))
        {
            std::fprintf(f, "Porpoise %s diagnostic test\n\n", build_label().c_str());
            for (const auto &c : checks)
                std::fprintf(f, "[%s] %s\n", c.first ? "ok" : "!!", c.second.c_str());
            std::fclose(f);
            if (!usb.empty())
                for (int i = 0; i < 8; ++i)
                {
                    const std::string to = "/mnt/usb" + std::to_string(i) + "/" + usb;
                    struct stat st;
                    if (stat(to.c_str(), &st) == 0)
                    {
                        copy_file(where + "/diagnostic.txt", to + "/diagnostic.txt");
                        break;
                    }
                }
        }
    open_dialog(DialogKind::Info, tr("Diagnostic Test"),
                where.empty() ? tr("The results couldn't be saved.")
                : usb.empty() ? trf("Saved with the logs in {path}. Share them on the Discord.", {{"path", where}})
                              : trf("Saved with the logs in {path}, and on your USB drive as {usb}. Share them on "
                                    "the Discord.",
                                    {{"path", where}, {"usb", usb}}),
                "");
    dialog_.checks = std::move(checks);
}

std::string App::save_report(std::string &usb)
{
    usb.clear();
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
    const std::string name = std::string("report-") + stamp;
    const std::string dir = data_dir_ + "/reports/" + name;
    mkdir((data_dir_ + "/reports").c_str(), 0777);
    if (mkdir(dir.c_str(), 0777) != 0)
        return "";
    /* What happened (the newest 2 MB of each log), the settings, and a note
     * of what this is. */
    copy_file(PORPOISE_APP "/trace.txt", dir + "/trace.txt", 2u << 20);
    copy_file(PORPOISE_APP "/porpoise/core.log", dir + "/core.log", 2u << 20);
    copy_file(settings_path_, dir + "/settings.ini");
    if (std::FILE *f = std::fopen((dir + "/about.txt").c_str(), "w"))
    {
        std::fprintf(f, "Porpoise %s\n", build_label().c_str());
        std::fprintf(f, "Saved %s\n", stamp);
        std::fprintf(f, "Games in the library: %d\n", int(lib_->games().size()));
        if (selected_ >= 0 && selected_ < int(lib_->games().size()))
        {
            const Game &g = lib_->games()[std::size_t(selected_)];
            std::fprintf(f, "Selected game: %s (%s, %s)\n", g.title.c_str(), g.id.c_str(), g.platform.c_str());
        }
        std::fprintf(f, "Player data: %s\n", data_dir_.c_str());
        std::fclose(f);
    }
    /* A copy on a USB drive, for a computer. */
    for (int i = 0; i < 8; ++i)
    {
        const std::string root = "/mnt/usb" + std::to_string(i);
        struct stat st;
        if (stat(root.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
            continue;
        const std::string to = root + "/porpoise-" + name;
        if (mkdir(to.c_str(), 0777) != 0)
            continue;
        bool any = false;
        for (const char *file : {"trace.txt", "core.log", "settings.ini", "about.txt"})
            any |= copy_file(dir + "/" + file, to + "/" + file);
        if (any)
        {
            usb = "porpoise-" + name;
            break;
        }
    }
    return dir;
}

void App::set_versions(const std::vector<std::string> &tags, const std::vector<int> &builds,
                       const std::vector<bool> &betas, const std::vector<std::size_t> &sizes)
{
    const std::string was = version_pick_ >= 0 && version_pick_ < int(version_tags_.size())
                                ? version_tags_[std::size_t(version_pick_)]
                                : "";
    version_tags_ = tags;
    version_builds_ = builds;
    version_sizes_ = sizes;
    version_labels_.clear();
    version_pick_ = 0;
    bool latest_marked = false;
    for (std::size_t i = 0; i < tags.size(); ++i)
    {
        std::string label = version_label(tags[i], builds[i]);
        int major = 0, minor = 0, patch = 0, beta = 0;
        porpoise::update::tag_version(tags[i], major, minor, patch, beta);
        if (porpoise::update::compare_versions(major, minor, patch, beta, builds[i], kVersionMajor, kVersionMinor,
                                               kVersionPatch, kVersionBeta, kBuild) == 0)
            label += "  \xE2\x80\xA2  " + tr("installed");
        else if (betas[i])
            label += "  \xE2\x80\xA2  " + tr("beta");
        else if (!latest_marked)
            label += "  \xE2\x80\xA2  " + tr("latest");
        if (!betas[i])
            latest_marked = true;
        version_labels_.push_back(label);
        if (tags[i] == was)
            version_pick_ = int(i);
    }
    if (screen_ != Screen::GameSettings && screen_ != Screen::Mapping && !map_in_game_)
        build_settings();
}

void App::set_latest_release(const std::string &tag, const std::string &url, std::size_t zip_size, int build)
{
    latest_size_ = zip_size;
    /* "v1.2", "1.2.1" or "v2.0-beta.2": newer than this build? */
    int major = 0, minor = 0, patch = 0, beta = 0;
    if (!porpoise::update::tag_version(tag, major, minor, patch, beta))
        return;
    const bool newer = porpoise::update::compare_versions(major, minor, patch, beta, build, kVersionMajor,
                                                          kVersionMinor, kVersionPatch, kVersionBeta, kBuild) > 0;
    const std::string shown = version_label(tag, build);
    latest_version_ = newer ? shown : "";
    latest_url_ = newer ? url : "";
    /* Only while the global settings are what rows_ holds: a game's settings
     * or the mapping screen keep theirs (About is built afresh later). */
    if (screen_ != Screen::GameSettings && screen_ != Screen::Mapping && !map_in_game_)
        build_settings();
}

std::string App::update_row_value() const
{
    switch (update_phase_)
    {
    case 1: return tr("Checking\xE2\x80\xA6");
    case 2:
    case 3:
    case 4:
    case 7: return tr("Updating\xE2\x80\xA6");
    default: break;
    }
    if (update_available())
        return trf("Install {version}", {{"version", latest_version_}});
    return tr("Check Now");
}

void App::set_update_progress(int phase, std::size_t done, std::size_t total, const std::string &error)
{
    if (phase == update_phase_ && done == update_done_ && total == update_total_)
        return;
    const int was = update_phase_;
    update_phase_ = phase;
    update_done_ = done;
    update_total_ = total;
    update_error_ = error;
    if ((was == 2 || was == 3) && phase == 5)
        update_failed_ = true;
    if (phase == 4 && was != 4)
        update_done_time_ = time_;
    if (was == 1 && phase == 5)
    {
        update_note_ = tr(error);
        update_note_time_ = time_;
    }
    if (was == 1 && phase == 6)
    {
        update_note_ = update_available() ? trf("Porpoise {version} is out.", {{"version", latest_version_}})
                                          : tr("This is the newest Porpoise.");
        update_note_time_ = time_;
    }
}

std::string App::build_label() const
{
    std::string label = kVersion;
    if (kBuild > 0)
        label += " (build " + std::to_string(kBuild) + ")";
#ifdef PS5_RETROARCH_BUILD_ID
    const std::string id = PS5_RETROARCH_BUILD_ID;
    const auto colon = id.rfind(": ");
    if (colon != std::string::npos && id.size() >= colon + 10)
        label += "  (" + id.substr(colon + 2, 8) + ")";
#endif
    return label;
}

/* A game's own settings, counted as the player sees them: its custom buttons
 * are one change, not one per button. */
long long App::change_count() const
{
    long long n = 0;
    bool buttons = false;
    for (const std::string &k : game_keys_)
    {
        if (k.rfind("map_", 0) == 0 || k == "button_layout")
            buttons = true;
        else
            ++n;
    }
    return n + (buttons ? 1 : 0);
}

std::string App::game_settings_path(const Game &g) const
{
    return data_dir_ + "/game-settings/" + Library::settings_key_of(g) + ".ini";
}

/* ---- the rows ------------------------------------------------------------------------------ */

/* Video, Graphics, Audio, Controls and System: the rows a game can change too. */
void App::add_game_rows(Settings &t, bool per_game)
{
    std::string section;
    auto header = [&](const char *name) {
        SettingRow r;
        r.section = section = name;
        r.header = true;
        rows_.push_back(r);
    };
    auto choice = [&](const char *key, const char *label, const char *help, int *value, int min,
                      std::vector<std::string> values) {
        SettingRow r;
        r.section = section;
        r.key = key;
        r.label = beta_label(label, r.beta, &r.alpha); /* "... (beta)": a BETA badge */
        r.help = tr(help);
        r.int_value = value;
        r.min = min;
        for (const std::string &v : values)
            r.values.push_back(tr(v));
        rows_.push_back(r);
    };
    auto toggle = [&](const char *key, const char *label, const char *help, bool *value, const char *off = "Off",
                      const char *on = "On") {
        SettingRow r;
        r.section = section;
        r.key = key;
        r.label = beta_label(label, r.beta, &r.alpha);
        r.help = tr(help);
        r.bool_value = value;
        r.values = {tr(off), tr(on)};
        rows_.push_back(r);
    };

    header("Video");
#ifndef PORPOISE_DESKTOP /* the PS5's own */
    if (!per_game)
        choice("output_res", "Output Resolution",
               "The picture Porpoise sends to the TV, menus and games alike. Match the PS5 keeps the console's own "
               "output, so the TV doesn't switch modes when Porpoise opens. 1080p is the quickest; 1440p and 4K are "
               "sharper on a 4K TV with a high internal resolution. Takes effect the next time Porpoise starts.",
               &t.output_res, 0, {"Match the PS5", "1080p", "1440p", "4K"});
#endif
#ifndef PORPOISE_DESKTOP
    toggle("frame_gen", "Frame Generation (beta)",
           "Makes a frame between each two of the game's: a 30 fps game is shown at 60, and with the PS5 set to "
           "120 Hz a 60 fps game at 120. Adds a little lag and can smear fast motion. Needs V-Sync.",
           &t.frame_gen);
    toggle("fg_steady30", "Steady 30 FPS",
           "With Frame Generation on, the game's own frames are shown at an even 30 a second and frame generation "
           "fills in the rest, for a steady 60. For games that swing between 30 and 60. The game still runs at full "
           "speed, but this doesn't make a slow game faster. Leave it off for games that hold 60.",
           &t.fg_steady30);
#endif
    choice("wide", "Widescreen",
           "Auto: 16:9 for a game with a widescreen code or a 16:9 option of its own, 4:3 for the rest. On: the same, "
           "and Dolphin's emulated widescreen hack for a game with neither, which can glitch at the screen edges. "
           "Off: always 4:3. Universal (beta): Porpoise widens the game's own 3D view to 16:9 where it can, and uses "
           "the widescreen hack where it can't.",
           &t.wide, 0, {"Auto", "On", "Off", "Universal (Beta)"});
    if (per_game && game_for_)
    {
        const widescreen::Kind kind = widescreen::kind_of(game_for_->id, sys_dir_, game_for_->platform == "Wii");
        rows_.back().help += " " + widescreen::about(kind);
    }
    choice("aspect", "Aspect Ratio", "The picture's shape. Auto follows the game; Stretch fills the screen.",
           &t.aspect, 0, {"Auto", "Force 16:9", "Force 4:3", "Stretch to Fill"});
    {
        /* Borders: the built-in ones and the player's own PNGs. */
        SettingRow r;
        r.section = section;
        r.key = "border";
        r.label = tr("Border");
        r.help = tr("Fills the bars beside a 4:3 picture (widescreen off). Add your own 1920x1080 PNGs to "
                    "/data/porpoise/borders.");
        border_names_.clear();
        border_choice_ = 0;
        for (const porpoise::borders::Border &b : porpoise::borders::list())
        {
            if (b.name == t.border)
                border_choice_ = int(border_names_.size());
            border_names_.push_back(b.name);
            r.values.push_back(b.built_in ? tr(b.label) : b.label);
        }
        r.int_value = &border_choice_;
        r.text_value = &t.border;
        rows_.push_back(r);
    }
    toggle("fps_overlay", "FPS Overlay", "Shows the frame rate in the corner while you play.", &t.fps_overlay);
    toggle("vsync", "V-Sync",
           "Shows every frame on the TV's own refresh, for the smoothest motion. Off times frames with Porpoise's "
           "own clock instead.",
           &t.vsync);

    if (!per_game)
        add_setup_rows(false);

    /* 3.0: everything that makes a game look better, together: the everyday
     * options first; Advanced Options shows the rest (a view, the same for
     * every game). */
    header("Enhancements");
    const bool advanced = settings_->enh_advanced;
    choice("resolution", "Internal Resolution", "How sharp games render. Higher is sharper and can slow some games.",
           &t.resolution, 1, {"1x (480p)", "2x (720p)", "3x (1080p)", "4x (1440p)",
            "5x (1800p)", "6x (4K)"});
    choice("antialiasing", "Anti-Aliasing", "Smooths jagged edges. SSAA is the sharpest and the heaviest.",
           &t.antialiasing, 0, {"Off", "2x MSAA", "4x MSAA", "8x MSAA", "2x SSAA", "4x SSAA", "8x SSAA"});
    choice("anisotropy", "Anisotropic Filtering", "Sharper textures on floors and walls seen at an angle.",
           &t.anisotropy, 0, {"1x", "2x", "4x", "8x", "16x"});
    choice("screen_filter", "Screen Filter",
           "Porpoise's own filter on the way to the TV: smooth or sharp scaling, sharpening, CRTs and an arcade "
           "monitor, VHS tapes, 8-bit and 16-bit pixels, a green handheld screen, scanlines, a composite cable, a "
           "TV's shadow mask, an aperture grille, or a sharp LCD.",
           &t.screen_filter, 0,
           {"Smooth", "Sharp", "Sharpen (CAS)", "CRT", "Arcade CRT", "VHS", "Soft VHS", "8-Bit", "Pocket", "Scanlines",
            "Shadow Mask", "LCD", "FSR 1", "16-Bit", "NTSC Composite", "Aperture Grille", "Smooth Edges (FXAA)"});
    rows_.back().help += " " + tr("FSR 1: AMD's upscaler brings the game's picture up to the TV's resolution with "
                                  "clean edges; Filter Strength sets its sharpening. Best with an internal resolution "
                                  "below the output's.") +
                            " " + tr("Smooth Edges (FXAA): softens jagged edges at almost no cost, for when Anti-Aliasing "
                                     "slows a game down.");
    choice("filter_strength", "Filter Strength", "How strong the screen filter is.", &t.filter_strength, 1,
           {"10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"});
    choice("bloom", "Extra Bloom", "A soft glow around the brightest parts of the picture, on top of the game's own.",
           &t.bloom, 0, {"Off", "Low", "Medium", "High"});
    {
        SettingRow r;
        r.section = section;
        r.key = "enh_advanced";
        r.label = tr("Advanced Options");
        r.help = tr("Shows every enhancement: color, texture filtering, resampling, lighting, fog, texture packs "
                    "and the game's own graphics mods.");
        r.bool_value = &settings_->enh_advanced;
        r.values = {tr("Off"), tr("On")};
        rows_.push_back(r);
    }
    if (advanced)
    {
        static const std::vector<std::string> kAdjust = {"-5", "-4", "-3", "-2", "-1", "0",
                                                         "+1", "+2", "+3", "+4", "+5"};
        choice("color_saturation", "Saturation", "How rich the colors are. 0 is as the game has them.",
               &t.color_saturation, 0, kAdjust);
        choice("color_contrast", "Contrast", "How far apart the darks and the lights are. 0 is as the game has them.",
               &t.color_contrast, 0, kAdjust);
        choice("color_warmth", "Warmth", "Warmer (more red) or cooler (more blue). 0 is as the game has it.",
               &t.color_warmth, 0, kAdjust);
        choice("texture_filter", "Texture Filtering", "Force sharp or smooth textures, or leave it to the game.",
               &t.texture_filter, 0, {"Game's Own", "Nearest (Sharp)", "Linear (Smooth)"});
        choice("resampling", "Output Resampling", "How Dolphin scales its picture. Sharp Bilinear keeps pixels crisp.",
               &t.resampling, 0,
               {"Default", "Bilinear", "B-Spline", "Mitchell-Netravali", "Catmull-Rom", "Sharp Bilinear",
                "Area Sampling"});
        toggle("pixel_lighting", "Per-Pixel Lighting", "Smoother lighting on surfaces. A little heavier.",
               &t.pixel_lighting);
        toggle("disable_fog", "Disable Fog", "Removes distance fog. Some games use fog for their look.",
               &t.disable_fog);
        if (per_game && game_for_)
        {
            /* Dolphin's built-in graphics mods, for the games that have them. */
            const porpoise::gfxmods::Offer mods = porpoise::gfxmods::offer(game_for_->id);
            if (mods.bloom)
                choice("gfx_bloom", "Bloom", kGfxBloomHelp, &t.gfx_bloom, 0,
                       mods.own_bloom ? std::vector<std::string>{"Game's Own", "Off", "Blurred"}
                                      : std::vector<std::string>{"Game's Own", "Off", "Blurred", "Native Resolution"});
            if (mods.dof)
                choice("gfx_dof", "Depth of Field", kGfxDofHelp, &t.gfx_dof, 0,
                       {"Game's Own", "Off", "Blurred", "Native Resolution"});
            if (mods.hud)
                toggle("gfx_hud", "Hide the HUD", kGfxHudHelp, &t.gfx_hud);
            if (!mods.extra_title.empty())
                toggle("gfx_extra", "Native Resolution Goop", kGfxGoopHelp, &t.gfx_extra);
        }
    }

    header("Graphics");
    choice("shader_mode", "Shader Compilation",
           "Ubershaders hide the stutter when a game draws something new, at a GPU cost.", &t.shader_mode, 0,
           {"Synchronous", "Ubershaders", "Async Ubershaders", "Async, Skip Drawing"});
    toggle("wait_shaders", "Build Shaders Before Starting",
           "The shaders the game used last time are built while it loads, so effects you've seen don't stutter "
           "the first time. The game takes a moment longer to start.",
           &t.wait_shaders);
#ifndef PORPOISE_DESKTOP /* the PS5's own */
    toggle("threaded_gpu", "Threaded GPU Recording",
           "The graphics driver records Dolphin's drawing on a thread of its own, so Dolphin's video thread spends "
           "less time in the driver. Can speed up demanding games. Turn it off if a game crashes or looks wrong. "
           "Applies the next time a game starts.",
           &t.threaded_gpu);
#endif
    choice("texture_cache", "Texture Cache Accuracy", "Safe fixes some games' text and effects; Fast is quickest.",
           &t.texture_cache, 0, {"Fast", "Middle", "Safe"});
    toggle("crop_overscan", "Crop Overscan", "Hides the black borders some games draw at the edges.",
           &t.crop_overscan);
    toggle("skip_dupes", "Skip Duplicate Frames", "Saves work when a game shows the same frame twice.",
           &t.skip_dupes);

    /* HD texture packs: the switch, and (in Settings) every pack found. */
    header("Textures");
    toggle("custom_textures", "Custom Textures",
           "Loads HD texture packs. Put each pack's folder, named with the game's ID (like GALE01), in "
           "/data/porpoise/saves/User/Load/Textures. The packs Porpoise finds are listed below.",
           &t.custom_textures);
    add_texture_rows(per_game);

    header("Save States");
    toggle("quick_resume", "Quick Resume",
           "Leaving a game from the in-game menu keeps where you were, and the game picks up right there the next "
           "time you start it. Start Over (in the in-game menu) boots it fresh.",
           &t.quick_resume);
    toggle("fast_states", "Fast Save States",
           "Leaves the GPU's texture cache out of save states: much quicker to save, and smaller. Turn it off if a "
           "game looks wrong for a moment after loading a state.",
           &t.fast_states);

    header("Audio");
    choice("volume", "Game Volume", "Volume of the game's sound.", &t.volume, 0,
           {"0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"});
    toggle("muted", "Mute Game", "Silences the game.", &t.muted);
    toggle("dsp_accurate", "Accurate Audio",
           "Dolphin's exact sound chip (LLE) instead of its fast one: fixes missing or wrong sound in a few games, but "
           "needs much more of the processor. Best turned on for one game.",
           &t.dsp_accurate);
    choice("wiimote_speaker", "Wii Remote Speaker",
           "Sounds Wii games play from the Remote's own speaker (a bow, an item box): in the TV's sound, or from "
           "each player's controller, as on a Wii.",
           &t.wiimote_speaker, 0, {"Off", "TV", "Controller"});
    {
        /* A preset sets the rows below at once (Settings::use_audio_preset). */
        SettingRow r;
        r.section = section;
        r.key = "audio_preset";
        r.label = tr("Sound Preset");
        r.help = tr("Smooth: Dolphin's own mixer covers the gaps when a game runs slow, as on a PC, instead of "
                    "crackling. Responsive keeps less sound ready, for a little less delay. Extra Smooth keeps more, "
                    "for games that slow down often. Classic is the sound of Porpoise 2.1. A change to or from "
                    "Classic applies the next time a game starts.");
        audio_preset_ = t.audio_preset();
        r.int_value = &audio_preset_;
        r.values = {tr("Smooth"), tr("Responsive"), tr("Extra Smooth"), tr("Classic (2.1)")};
        if (audio_preset_ == Settings::kAudioCustom)
            r.values.push_back(tr("Custom"));
        r.order = {0, 1, 2, 3};
        rows_.push_back(r);
    }
    if (t.audio_pull)
    {
        choice("audio_buffer", "Audio Buffer",
               "How much sound is kept ready. More holds off crackling when a game slows down, for a little delay.",
               &t.audio_buffer, 0, {"40 ms", "80 ms", "160 ms"});
        toggle("audio_fill", "Fill Audio Gaps",
               "When a game runs slow, the sound it just played covers the gap, faded, instead of a crackle. Off "
               "leaves the gap silent.",
               &t.audio_fill);
    }
    else
    {
        choice("audio_buffer", "Audio Buffer",
               "How much sound is kept ready. Safe holds more, against crackling in demanding games, for a little "
               "delay.",
               &t.audio_buffer, 0, {"Low", "Normal", "Safe"});
        toggle("audio_stretch", "Audio Stretching",
               "When a game slows down, its sound slows with it, slightly lower, instead of crackling.",
               &t.audio_stretch);
    }
    toggle("microphone", "Microphone",
           "The DualSense's microphone as the GameCube Microphone (Mario Party 6 and 7; R3 is its button) and the "
           "Wii Speak.",
           &t.microphone);

    if (!per_game)
    {
        /* The controllers and what each is doing now (ui_app_controllers.cpp):
         * the rows' values follow the devices while the section is open. */
        header("Controllers & Accessories");
        {
            SettingRow r;
            r.section = section;
            r.label = tr("Controller Test & Diagnostics");
            r.help = tr("Every controller and accessory, what it's doing now, a live test of its buttons, sticks, "
                        "motion and IR pointer, and what to do when something doesn't work. No game needed.");
            r.values = {tr("Open\xE2\x80\xA6")};
            r.action = kRowControllers;
            r.folder = 1;
            rows_.push_back(r);
        }
        static const char *const kDevices[8] = {"DualSense",    "Mayflash DolphinBar W010", "Wii Remote 1",
                                                "Wii Remote 2", "Wii Remote 3",             "Wii Remote 4",
                                                "Switch 2 GameCube Controller", "USB Bluetooth Adapter"};
        const std::string present = controllers_present();
        ctl_rows_present_ = present;
        for (int i = 0; i < 8; ++i)
        {
            /* the DolphinBar and its Remotes, the GameCube controller, a Bluetooth adapter: only while
             * plugged in */
            const int item = i >= 6 ? i + 1 : i;
            if (present.find(char('a' + item)) == std::string::npos)
                continue;
            SettingRow r;
            r.section = section;
            r.label = tr(kDevices[i]);
            r.help = i == 0   ? tr("The DualSense controllers on the console. Cross opens their live test.")
                     : i == 1 ? tr("The Mayflash DolphinBar (USB 057E:0306, mode 4). Cross opens its details and "
                                   "the diagnosis.")
                     : i == 6 ? tr("Nintendo's GameCube controller for the Switch 2, by USB cable: player 1's "
                                   "GameCube controller in games, beside the DualSense. Cross opens its live test.")
                     : i == 7 ? tr("A USB Bluetooth adapter for the Switch 2's GameCube controller without its "
                                   "cable: press the SYNC button on top of the controller. Cross opens its details.")
                              : tr("A Wii Remote through the DolphinBar, with its attachment (Nunchuk, Classic "
                                   "Controller, MotionPlus). Cross opens its live test.");
            r.live = i;
            r.values = {controller_row_value(i, nullptr)};
            r.action = kRowControllers;
            r.folder = item; /* the test screen's list: the GameCube controller and the adapter after the speaker */
            rows_.push_back(r);
        }
        /* with a DolphinBar plugged in (or real Remotes chosen, so it can be undone) */
        if (present.find('b') != std::string::npos || t.wii_source)
        choice("wii_source", "Wii Remotes",
               "DualSense: your controller plays the Wii Remote. Real Wii Remotes: original Wii Remotes and Wii Remote "
               "Plus through a Mayflash DolphinBar (mode 4) in a USB port, up to four, with their own Nunchuk, Classic "
               "Controller, MotionPlus, pointer and motion; the DualSense still opens Porpoise's menu. A Remote is "
               "found when it's connected to the DolphinBar. Applies when the game starts or this changes.",
               &t.wii_source, 0, {"DualSense", "Real Wii Remotes (DolphinBar)"});
        if (present.find('b') != std::string::npos)
        toggle("real_wiimote_speaker", "Real Wii Remote Speakers",
               "The speakers of real Wii Remotes (DolphinBar), in games and the speaker test. Off: no sound data is "
               "sent to them at all. The TV's sound and the DualSense's speaker stay as they are.",
               &t.real_wiimote_speaker);
    }

    header("Controls");    {
        std::vector<std::string> layouts = {"GameCube", "PlayStation"};
        for (int i = 1; i <= Settings::kPresets; ++i)
            layouts.push_back(trf("My Layout {n}", {{"n", std::to_string(i)}}));
        choice("button_layout", "Button Layout",
               "PlayStation: Cross is A, Circle is B. GameCube: Circle is A, Cross is B. My layouts: your own, made "
               "in Customize Buttons. For GameCube games, and Wii games played with the GameCube Controller; other "
               "Wii controllers have Wii Buttons (Wii Remote).",
               &t.button_layout, 0, layouts);
        choice("p1_port", "Player 1's Port",
               "The GameCube controller port your controller is in. For the few games that want it in another "
               "port for a moment (Metal Gear Solid: The Twin Snakes); change it from the in-game menu while "
               "playing. GameCube games only.",
               &t.p1_port, 0, {"Port 1", "Port 2", "Port 3", "Port 4"});
        choice("invert_main", "Invert the Control Stick",
               "Turns the left stick around, up-down, left-right or both: for a game that moves the other way. "
               "Best set for one game (its own settings).",
               &t.invert_main, 0, {"Off", "Up and Down", "Left and Right", "Both"});
        choice("invert_c", "Invert the C-Stick",
               "Turns the right stick (the C-stick) around: for a game whose camera goes the other way. Best set for "
               "one game (its own settings).",
               &t.invert_c, 0, {"Off", "Up and Down", "Left and Right", "Both"});
        choice("stick_sensitivity", "Stick Sensitivity",
               "How much a small push of the control stick moves: lower for steering that's too twitchy (F-Zero "
               "GX). A full push is still a full turn. Best set for one game (its own settings).",
               &t.stick_sensitivity, 0, {"Normal", "Lower", "Low", "Lowest"});
        choice("stick_deadzone", "Stick Dead Zone",
               "How far the control stick moves before the game sees it: for a stick that drifts on its own.",
               &t.stick_deadzone, 0, {"Off", "Small", "Medium", "Large"});
    }
    {
        SettingRow r;
        r.section = section;
        r.label = tr("Customize Buttons");
        r.help = tr("Make up to four layouts of your own, on a picture of the controller. Any game can use any of "
                    "them. L1 / R1 there switch to the Wii controllers, with two sets of Wii buttons of your own.");
        r.values = {tr("Edit\xE2\x80\xA6")};
        r.action = kRowMapping;
        rows_.push_back(r);
    }
    toggle("rumble", "Vibration", "Controller rumble.", &t.rumble);
    if (per_game)
        choice("console", "Console",
               "Which console's controls the game gets: as Porpoise detected it, or GameCube or Wii, for a game it "
               "got wrong.",
               &t.console, 0, {"Auto", "GameCube", "Wii"});
    toggle("ff_buttons", "Fast Forward Buttons",
           "In a game, touch pad + R1 steps fast forward (off, 2x, 4x) and touch pad + R2 fast-forwards while held. "
           "On a Wii Remote, the touch pad's Minus then goes when you let go of it.",
           &t.ff_buttons);
    choice("quick_slot", "Quick Save Buttons", kQuickSlotHelp, &t.quick_slot, 0, kQuickSlotValues);
    choice("turbo", "Turbo Button", kTurboHelp, &t.turbo, 0, kTurboValues);
    choice("trigger_feel", "Trigger Click", kTriggerFeelHelp, &t.trigger_feel, 0, kTriggerFeelValues);
    if (!per_game)
    {
        static const char *const kLightRows[4] = {"Light Bar, Player 1", "Light Bar, Player 2", "Light Bar, Player 3",
                                                  "Light Bar, Player 4"};
        int *const lights[4] = {&t.light_1, &t.light_2, &t.light_3, &t.light_4};
        for (int i = 0; i < 4; ++i)
            choice(i == 0 ? "light_1" : i == 1 ? "light_2" : i == 2 ? "light_3" : "light_4", kLightRows[i],
                   kLightHelp, lights[i], 0, kLightValues);
    }
    if (!per_game)
    {
        SettingRow r;
        r.section = section;
        r.key = "players"; /* its value is counted when it is drawn */
        r.label = tr("Controllers");
        r.help = tr("Up to four players. To join, turn on another controller and choose a user for it.");
        r.values = {""};
        rows_.push_back(r);
    }

    header("Wii Remote");
    if (per_game) /* everywhere else it's in Controllers & Accessories */
    choice("wii_source", "Wii Remotes",
           "DualSense: your controller plays the Wii Remote. Real Wii Remotes: original Wii Remotes and Wii Remote "
           "Plus through a Mayflash DolphinBar (mode 4) in a USB port, up to four, with their own Nunchuk, Classic "
           "Controller, MotionPlus, pointer and motion; the DualSense still opens Porpoise's menu. A Remote is "
           "found when it's connected to the DolphinBar. Applies when the game starts or this changes.",
           &t.wii_source, 0, {"DualSense", "Real Wii Remotes (DolphinBar)"});
    {
        SettingRow r;
        r.section = section;
        r.label = tr("Wii Remote Setup");
        r.help = tr("Beta. Pick the Wii controller, then point at the middle and two corners of your screen: the "
                    "pointer then matches your screen and how far you sit from it.");
        r.values = {t.wii_screen_x > 0 ? tr("Measured") : tr("Start\xE2\x80\xA6")};
        r.action = kRowWiiSetup;
        rows_.push_back(r);
    }
    if (!per_game)
    {
        std::vector<std::string> presets = {"None"};
        const auto &names = Settings::wii_preset_names();
        for (int i = 0; i < Settings::kWiiPresets; ++i)
        {
            const Settings::WiiPreset &w = t.wii_presets[i];
            presets.push_back(std::to_string(i + 1) + ": " + (w.used ? tr(names[std::size_t(w.name)]) : tr("empty")));
        }
        choice("wii_preset", "Wii Preset",
               "A whole Wii Remote set-up kept under a name: made in the setup's Fine-tune page (Advanced).",
               &t.wii_preset, 0, presets);
    }
    if (per_game)
        toggle("wii_setup_ask", "Setup Before This Game",
               "Shows the Wii Remote Setup when this game starts; Triangle there plays straight away.",
               &t.wii_setup_ask, "Don't Show", "Show");
    else
        toggle("wii_setup_ask", "Setup Before Each Wii Game",
               "Shows the Wii Remote Setup when a Wii game starts; Triangle there plays straight away. A game "
               "can have its own choice in its settings.",
               &t.wii_setup_ask, "Don't Show", "Show");
    {
        SettingRow r;
        r.section = section;
        r.label = tr("How to Hold It");
        r.help = tr("Beta: a picture of the DualSense as each Wii controller, how to hold it and what every button "
                    "does. Wii motion controls are still being tuned.");
        r.values = {tr("Show\xE2\x80\xA6")};
        r.action = kRowWiiGuide;
        rows_.push_back(r);
    }
    choice("wii_controller", "Wii Controller",
           "How a Wii game sees your DualSense. Remote + Nunchuk: the Nunchuk on the left stick and L1 / L2. Remote: "
           "held pointing at the TV, with its motion. Sideways: held like an NES pad, tilt to steer. Two "
           "controllers (beta): the second DualSense is the Nunchuk. Otherwise every other controller is another "
           "player's own Wii Remote, with its own pointer and motion.",
           &t.wii_controller, 0, {"Remote + Nunchuk", "Remote", "Remote Sideways", "Classic Controller",
                                  "Two Controllers (Alpha)", "GameCube Controller"});
    rows_.back().help += " " + tr(kGameCubeOnWiiHelp);
    choice("wii_buttons_use", "Wii Buttons",
           "Which buttons the Wii controllers use: Porpoise's, or one of your own two sets, made in Controls > "
           "Customize Buttons (L1 / R1 to each Wii controller). A game can have its own.",
           &t.wii_buttons_use, 0, {"Porpoise's Set", "Mine 1", "Mine 2"});
    choice("wii_pointer", "Pointer", "What moves the Remote's pointer. Gyro: point the controller at the screen; hold R1 a moment to center it.",
           &t.wii_pointer, 0, {"Gyro", "Touch Pad", "Right Stick"});
    choice("wii_speed", "Pointer Speed",
           "How far you turn the controller to reach the screen's edge. Your screen: as measured by the Wii Remote "
           "Setup, so the pointer is where you point.",
           &t.wii_speed, 0, {"Your screen", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"});
    choice("wii_range", "Pointer Range",
           "How far the game's pointer can go. Wide helps in a game whose pointer can't reach an edge of the "
           "screen. Auto: Wide for the games known to need it, like Zack & Wiki.",
           &t.wii_range, 0, {"Auto", "Standard", "Wide", "Extra Wide"});
    choice("wii_grip", "Grip", "How you hold the DualSense. Auto reads it: flat in both hands, or stood on end in either hand.", &t.wii_grip, 0,
           {"Auto", "Both Hands", "Upright, trigger to the TV", "Upright, facing you"});
    toggle("wii_motion", "Motion", "The DualSense's motion is the Remote's: tilt, swing and point.", &t.wii_motion);
    toggle("wii_shake", "Flick to Shake", "A quick flick of the controller shakes the Remote.", &t.wii_shake);
    choice("wii_motion_plus", "Wii MotionPlus",
           "A Wii MotionPlus on the Remote, played by the DualSense's gyroscope (Motion on). Auto: for the games "
           "that need one (Wii Sports Resort, Skyward Sword, Red Steel 2, Wii Play: Motion). Applies when the game "
           "starts or the controller changes.",
           &t.wii_motion_plus, 0, {"Auto", "On", "Off"});

    header("Emulation");
    choice("cpu_clock", "CPU Clock", "Overclocking can smooth a game that slows down. 100% is the real console.",
           &t.cpu_clock, 0, {"50%", "60%", "70%", "80%", "90%", "100%", "150%", "200%", "250%", "300%"});
    toggle("fast_float", "Riptide Boost (beta)",
           "Speeds up the emulated processor's 3D math. Up to twice as fast in CPU-heavy games. Turn it off if a "
           "game misbehaves. Applies when the game starts.",
           &t.fast_float);
    toggle("dual_core", "Dual Core", "Faster. Turn it off for a game that freezes or glitches.", &t.dual_core);
#ifndef PORPOISE_DESKTOP /* the PS5's own */
    toggle("own_cores", "Emulator on Its Own Cores",
           "Gives the emulated console's processor and its graphics a core of the PS5 each, away from "
           "Porpoise's other work. Usually faster; turn it off if a game runs worse.",
           &t.own_cores);
#endif
    toggle("accurate_fma", "Exact Multiply-Add",
           "Rounds the console's floating-point multiply-adds exactly, as Dolphin does on a PC. Much slower in "
           "games heavy on 3D math, and very few games need it.",
           &t.accurate_fma);
    toggle("fast_disc", "Fast Disc Loading", "Shorter loading screens. A few games need real disc speed.",
           &t.fast_disc);
    toggle("cheats", "Cheats", "Dolphin's cheat codes for games that have them.", &t.cheats);

    header("Console");
    choice("language", "System Language", "The console's language. European games show their text in it.",
           &t.language, 0,
           {"English", "Japanese", "German", "French", "Spanish", "Italian", "Dutch", "Chinese (Simplified)",
            "Chinese (Traditional)", "Korean"});
    toggle("progressive", "Progressive Scan", "480p output, as on a component cable.", &t.progressive);
    toggle("wii_widescreen", "Wii Widescreen",
           "The Wii's own 16:9 setting, which Wii games follow. Off: 4:3.", &t.wii_widescreen);
    toggle("pal60", "PAL Games at 60 Hz",
           "European Wii games run at 60 Hz, as a Wii set to EURGB60 does. Off: 50 Hz.", &t.pal60);
    choice("sensor_bar", "Sensor Bar", "Where the Wii is told its sensor bar sits.", &t.sensor_bar, 0,
           {"Below the TV", "Above the TV"});
    if (!per_game)
    {
        /* The consoles' own menus, started with a game in the drive or none. */
        const auto bioses = porpoise::bios::find_gamecube(data_dir_ + "/bios");
        SettingRow gc;
        gc.section = section;
        gc.label = tr("GameCube Menu");
        gc.help = tr("Starts your own GameCube's menu, from your own BIOS (its IPL.bin in /data/porpoise/bios): "
                     "with a game in the drive, or none for Memory Cards, the calendar and its settings.");
        gc.values = {bioses.empty() ? tr("No BIOS") : tr("Start\xE2\x80\xA6")};
        gc.action = kRowGcMenu;
        rows_.push_back(gc);
        SettingRow wm;
        wm.section = section;
        wm.label = tr("Wii Menu");
        wm.help = tr("Starts the Wii Menu, with a Wii game in the drive or none. It comes from Wii System Update, "
                     "your Wii System Data, or the Wii Menu's WAD from your own console in your games.");
        wm.values = {wii_menu_installed() || wii_menu_wad() ? tr("Start\xE2\x80\xA6") : tr("Not Installed")};
        wm.action = kRowWiiMenu;
        rows_.push_back(wm);
    }
    toggle("wii_menu_boot", "Start Wii Discs in the Wii Menu",
           "Wii discs start from the Wii Menu, as on a Wii. Needs your own Wii Menu (Wii System Data, or its WAD "
           "in your games). A disc of another region than your Wii's, and GameCube games, start directly.",
           &t.wii_menu_boot);
#ifndef PORPOISE_DESKTOP
    if (!per_game)
    {
        /* The player's own Wii's NAND (a BootMii backup), for Wiimmfi and
         * the Wii Menu's own settings. */
        SettingRow r;
        r.section = section;
        r.label = beta_label("Wii System Data (beta)", r.beta);
        r.help = tr("Your own Wii's NAND backup, made with BootMii: its settings, Miis, channels, saves and the "
                    "certificates online services like Wiimmfi check. Put nand.bin (and keys.bin) in "
                    "/data/porpoise/nand or at a USB drive's top, then import it.");
        if (porpoise::nand::imported(saves_dir_ + "/User/Wii"))
            r.help = tr("Your own Wii's system data is in Porpoise: its settings, Miis, channels and saves. Press to "
                        "remove it and start with a fresh Wii (what's in it is kept in saves/User/Wii-removed).");
        r.values = {porpoise::nand::imported(saves_dir_ + "/User/Wii") ? tr("Imported") : tr("Import\xE2\x80\xA6")};
        r.action = kRowNandImport;
        rows_.push_back(r);
    }
#endif
    if (!per_game)
    {
        /* The player's channels (WADs) into the Wii Menu. */
        int wads = 0, in = 0;
        for (const Game &g : lib_->games())
            if (g.format == "WAD" && g.id.size() >= 4 && g.id.find('_') > 3)
            {
                ++wads;
                in += channel_installed(g) ? 1 : 0;
            }
        SettingRow ch;
        ch.section = section;
        ch.label = tr("Wii Channels");
        ch.help = tr("Puts your channels (WAD files in your games) in the Wii Menu, so they're there with your "
                     "Miis and settings. Choose them, and the Wii Menu opens with them in it.");
        ch.values = {wads == 0 ? tr("None Found") : trf("{a} of {n} in the Wii Menu", {{"a", std::to_string(in)}, {"n", std::to_string(wads)}})};
        ch.action = kRowChannels;
        rows_.push_back(ch);
#ifndef PORPOISE_DESKTOP /* the desktop build's HTTPS can't make the update's request */
        SettingRow up;
        up.section = section;
        up.label = tr("Wii System Update");
        up.help = tr("Downloads your Wii's system software (the Wii Menu, its IOS and channels) from Nintendo's "
                     "servers, as Dolphin's Online System Update does, so you have a Wii Menu without a backup. "
                     "Needs the console online.");
        up.values = {tr("Update\xE2\x80\xA6")};
        up.action = kRowSysUpdate;
        rows_.push_back(up);
#endif
    }
    toggle("gc_bios", "GameCube Boot Animation",
           "GameCube games start with the console's own start-up, from your own console's BIOS: put its IPL.bin in "
           "/data/porpoise/bios. Porpoise includes none.",
           &t.gc_bios);

    /* Wii games online: the server, and the WiiConnect24 channels. */
    header("Online");
    add_online_rows(t, per_game);
    toggle("wii_online", "WiiConnect24 Channels",
           "WiiConnect24 channels through WiiLink: Forecast, News, Check Mii Out and more. Needs the console "
           "online.",
           &t.wii_online);
}

void App::build_settings()
{
    /* The rows show the settings with the changes not yet applied on top:
     * what is in effect now (other screens may have changed it), plus those. */
    {
        const std::vector<std::string> pending = draft_.changed_keys(base_);
        Settings fresh = *settings_;
        fresh.copy_keys(draft_, pending);
        base_ = *settings_;
        draft_ = fresh;
    }
    rows_.clear();
    std::string section;
    auto header = [&](const char *name) {
        SettingRow r;
        r.section = section = name;
        r.header = true;
        rows_.push_back(r);
    };
    auto toggle = [&](const char *key, const char *label, const char *help, bool *value) {
        SettingRow r;
        r.section = section;
        r.key = key;
        r.label = beta_label(label, r.beta);
        r.help = tr(help);
        r.bool_value = value;
        r.values = {tr("Off"), tr("On")};
        rows_.push_back(r);
    };
    auto action = [&](const char *label, const char *help, const std::string &value, int act, int folder = -1) {
        SettingRow r;
        r.section = section;
        r.label = beta_label(label, r.beta);
        r.help = tr(help);
        r.values = {tr(value)};
        r.action = act;
        r.folder = folder;
        rows_.push_back(r);
    };
    auto info = [&](const char *label, const std::string &value, const char *help) {
        SettingRow r;
        r.section = section;
        r.label = tr(label);
        r.help = tr(help);
        r.values = {value};
        rows_.push_back(r);
    };
    auto choice = [&](const char *key, const char *label, const char *help, int *value,
                      std::initializer_list<const char *> values) {
        SettingRow r;
        r.section = section;
        r.key = key;
        r.label = tr(label);
        r.help = tr(help);
        r.int_value = value;
        for (const char *v : values)
            r.values.push_back(tr(v));
        rows_.push_back(r);
    };

    /* 3.0 Alpha 5: what was one long Games section, in three: where games
     * are found, what is downloaded for them, and where Porpoise keeps its
     * things. */
    header("Games");
#ifndef PORPOISE_DESKTOP /* the PS5's own */
    toggle("auto_search", "Find Games Automatically",
           "Looks in /data/porpoise/games, /data/games, /data/roms, /data/iso and on USB drives.",
           &draft_.auto_search);
    rows_.back().rescan = true;
#endif
    for (std::size_t i = 0; i < draft_.folders.size(); ++i)
    {
        SettingRow r;
        r.section = section;
        r.label = draft_.folders[i];
        r.help = tr("Porpoise looks in this folder and four levels below it. Cross stops looking here; files stay.");
        r.values = {tr("Remove")};
        r.action = kRowRemoveFolder;
        r.folder = int(i);
        rows_.push_back(r);
    }
    action("Add a Game Folder", "Pick any folder on the console or a USB drive to search for games.",
           "Choose\xE2\x80\xA6", kRowAddFolder);
#ifndef PORPOISE_DESKTOP /* Windows opens a share's folder by its own path */
    {
        /* Network shares (porpoise_netfs): searched like a folder. */
        const std::vector<netfs::Share> shares = netfs::shares();
        for (std::size_t i = 0; i < shares.size(); ++i)
        {
            SettingRow r;
            r.section = section;
            /* Shown as Windows writes a share (\\computer\share\folder), or
             * an NFS export as NFS does (computer:/export/folder). */
            const bool nfs = shares[i].protocol == "nfs";
            const char sep = nfs ? '/' : '\\';
            std::string folder = shares[i].folder;
            std::replace(folder.begin(), folder.end(), nfs ? '\\' : '/', sep);
            while (!folder.empty() && folder.front() == sep)
                folder.erase(folder.begin());
            if (nfs)
            {
                std::string exp = shares[i].share;
                if (exp.empty() || exp.front() != '/')
                    exp.insert(exp.begin(), '/');
                while (exp.size() > 1 && exp.back() == '/')
                    exp.pop_back();
                r.label = shares[i].host + ":" + exp + (folder.empty() ? "" : (exp == "/" ? "" : "/") + folder);
            }
            else
                r.label = "\\\\" + shares[i].host + "\\" + shares[i].share + (folder.empty() ? "" : "\\" + folder);
            r.help = trf("A shared folder on {computer}, on your network. Porpoise looks in it and four levels below "
                         "it. Cross changes or removes it.",
                         {{"computer", shares[i].host}});
            r.values = {tr("Change\xE2\x80\xA6")};
            r.action = kRowEditShare;
            r.folder = int(i);
            rows_.push_back(r);
        }
    }
    action("Add a Network Share",
           "Games from a shared folder on a computer or NAS on your home network (SMB or NFS). A wired "
           "connection works best for big games.",
           "Add\xE2\x80\xA6", kRowAddShare);
#endif
    const std::size_t n = lib_ ? lib_->games().size() : 0;
    action("Search for Games Now", "Looks through every folder again, for games you have just copied over.",
           searching_ ? std::string("Searching\xE2\x80\xA6") : plural((long long)n, "1 game", "{n} games"), kRowRescan);
#ifndef PORPOISE_DESKTOP /* the PS5's own */
    action("Check My Setup", "What Porpoise can see on this console - /data, USB drives, games - and what to do "
           "about anything missing.",
           "Check\xE2\x80\xA6", kRowSetupCheck);
    if (ra_state_)
    {
        const RaState ra = ra_state_();
        SettingRow r;
        r.section = section;
        r.label = "RetroAchievements";
        r.help = tr("Earn achievements as you play (softcore). Unlocks pop up like PS5 trophies. In your library, "
                    "L1 + Square opens this too.");
        r.values = {ra.signed_in ? ra.user : tr("Sign in\xE2\x80\xA6")};
        r.action = kRowAccount;
        rows_.push_back(r);
    }
#endif

    header("Downloads");
    toggle("download_covers", "Download Covers",
           "Box art from GameTDB.com, saved in /data/porpoise/covers. Needs the console online.",
           &draft_.download_covers);
    toggle("download_info", "Download Game Info",
           "Descriptions, developers, release dates and disc art from GameTDB.com, for Details.",
           &draft_.download_info);
    action("Download Covers Again",
           "Gets every game's cover, box and disc art from GameTDB.com again, in place of what Porpoise has (your "
           "own art too). For art that downloaded wrong or cut short. Needs the console online.",
           "Download\xE2\x80\xA6", kRowCoversAgain);

#ifndef PORPOISE_DESKTOP /* the PS5's own: its drives and sandbox */
    header("Storage");
#endif
#ifndef PORPOISE_DESKTOP
    {
        /* Porpoise's folder on another drive: extended storage or a USB drive,
         * for big texture packs and saves (the console's storage stays free). */
        move_places_.clear();
        std::vector<std::string> move_names; /* each place as the row lists it */
        auto place = [&](const std::string &label, const std::string &name, const std::string &drive) {
            struct stat st;
            if (drive != "/data" && (stat(drive.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)))
                return;
            const std::string path = drive + "/porpoise";
            if (path != data_dir_)
            {
                move_places_.emplace_back(label, path);
                move_names.push_back(name);
            }
        };
        place(tr("the console's storage"), tr("Console Storage"), "/data");
        place(tr("extended storage"), tr("Extended Storage"), "/mnt/ext0");
        place(tr("extended storage 2"), tr("Extended Storage 2"), "/mnt/ext1");
        for (int i = 0; i < 8; ++i)
        {
            const std::string usb = trf("USB Drive {n}", {{"n", std::to_string(i + 1)}});
            place(usb, usb, "/mnt/usb" + std::to_string(i));
        }
        std::string here = tr("the console's storage");
        if (data_dir_.rfind("/mnt/ext0", 0) == 0)
            here = tr("extended storage");
        else if (data_dir_.rfind("/mnt/ext1", 0) == 0)
            here = tr("extended storage 2");
        else if (data_dir_.rfind("/mnt/usb", 0) == 0)
            here = tr("a USB drive");
        if (!sandboxed_ && !move_places_.empty())
        {
            /* One row: left and right pick the drive, Cross moves it there. */
            SettingRow r;
            r.section = section;
            r.key = "move_pick";
            r.label = tr("Move Porpoise's Folder");
            r.help = trf("Porpoise's folder is on {here} now. Left and right pick a drive; Cross moves everything "
                         "in the folder there: settings, saves, save states, covers and texture packs. Then "
                         "Porpoise closes; open it again. Your game files elsewhere stay where they are.",
                         {{"here", here}});
            for (const std::string &name : move_names)
                r.values.push_back(name);
            move_choice_ = std::clamp(move_choice_, 0, int(move_places_.size()) - 1);
            r.int_value = &move_choice_;
            r.action = kRowMoveData;
            rows_.push_back(r);
        }
    }
    toggle("stay_sandboxed", "Stay in the Sandbox",
           "On: Porpoise doesn't ask your jailbreak to free it from the app sandbox. For jailbreaks that close "
           "Porpoise when they free it. Porpoise then can't see /data or USB drives; games in "
           "/app0/porpoise/games work. Takes effect the next time Porpoise starts.",
           &draft_.stay_sandboxed);
    toggle("sandbox_notice", "Sandbox Message at Start",
           "When the console starts Porpoise inside the app sandbox, says so and how to free it. Off: Porpoise "
           "just uses its own folder (games in /app0/porpoise/games).",
           &draft_.sandbox_notice);
#endif
#ifndef PORPOISE_DESKTOP
    action("Saves from a USB Drive",
           "Copies saves from the USB drive's Porpoise Saves folder in: GameCube saves onto Slot A, Wii saves to "
           "their games. A save that's already here is left as it is. Options in Memory Cards copies a save out.",
           "Copy In\xE2\x80\xA6", kRowImportSaves);
#endif

    add_game_rows(draft_, false);

    header("Interface");
    {
        SettingRow r;
        r.section = section;
        r.key = "ui_language";
        r.label = tr("Language");
        r.help = tr("The language of Porpoise's menus. System follows your PS5.");
        r.int_value = &draft_.ui_language;
        r.order = language_order();
        for (int i : r.order)
            r.values.push_back(language_choice(i));
        rows_.push_back(r);
    }
    {
        SettingRow r;
        r.section = section;
        r.key = "ui_theme";
        r.label = tr("Theme");
        r.help = tr(th().about);
        r.int_value = &draft_.ui_theme;
        for (int i = 0; i < kThemes; ++i)
            r.values.push_back(tr(theme(i).name));
        rows_.push_back(r);
    }
    if (th().palettes)
    {
        SettingRow r;
        r.section = section;
        r.key = "ui_palette";
        r.label = tr("Colors");
        r.help = tr("The theme's colors: its glass, its light and what is chosen.");
        r.int_value = &draft_.ui_palette;
        r.values.push_back(tr("Theme Default"));
        for (int i = 0; i < kPalettes; ++i)
            r.values.push_back(tr(palette(i).name));
        rows_.push_back(r);
    }
    {
        SettingRow r;
        r.section = section;
        r.key = "ui_font";
        r.label = tr("Font");
        r.help = tr("The letters across Porpoise. Each theme starts with its own; any font works with any theme.");
        r.int_value = &draft_.ui_font;
        r.values.push_back(tr("Theme Default"));
        for (int i = 0; i < kFontSets; ++i)
            r.values.push_back(font_set(i).name);
        rows_.push_back(r);
    }
    if (draft_.ui_theme == 1)
    {
        SettingRow r;
        r.section = section;
        r.key = "ui_layout";
        r.label = tr("Home Screen");
        r.help = tr("Tiles: a grid you point at, twelve to a page. Library View: your games in the Library View below.");
        r.int_value = &draft_.ui_layout;
        r.values = {tr("Tiles"), tr("Library View")};
        rows_.push_back(r);
        toggle("ui_pointer", "Point with the Controller",
               "Move the controller to point at tiles and buttons. The touch pad turns it on and off too.",
               &draft_.ui_pointer);
    }
    if (draft_.ui_theme == int(ThemeId::StarCube))
        choice("sc_games", "Games Page", "Star Cube's Games Page: your games as spinning discs, or as covers.",
               &draft_.sc_games, {"Discs", "Covers"});
    else
    {
        static const char *const kHelp[10] = {
            "Cover Flow: the boxes in a row, the chosen one in front.",
            "Wheel: the boxes around a turning wheel; the one at the front is chosen.",
            "Disc Flow: your discs, spinning into place as you go.",
            "Shelf: rows of boxes, many at once. Up and down change row.",
            "Box: one box at a time, its whole cover wrapped round it. The right stick turns it.",
            "List: your games by name, the chosen one's box beside them. Up and down go through them.",
            "Stack: a deck of boxes; the front one flips away as you go.",
            "Helix: the boxes climbing around a turning column.",
            "Spines: your cases side by side on a shelf, spine out; the chosen one comes out to show its cover.",
            "Spotlight: one game at a time over its own art, with what's known about it; the rest in a strip below."};
        choice("lib_view", "Library View", kHelp[std::clamp(draft_.lib_view, 0, 9)], &draft_.lib_view,
               {"Cover Flow", "Wheel", "Disc Flow", "Shelf", "Box", "List", "Stack", "Helix", "Spines", "Spotlight"});
        choice("recent_style", "Recently Played",
               "The games you played last, along the bottom of the library; Down goes to them. Dock: a glass bar "
               "with the game's name. Floating: the boxes on their own, the chosen one growing like a dock's "
               "icons. Discs: their discs, the chosen one spinning.",
               &draft_.recent_style, {"Off", "Dock", "Floating", "Discs"});
    }
    if (draft_.ui_theme != int(ThemeId::StarCube))
    {
        static const char *const kHelp[5] = {
            "Cards: both memory cards side by side, their saves as icons.",
            "Blocks: one card at a time, drawn as the card itself with each save's blocks.",
            "By Game: every save on both cards and the Wii, grouped by game.",
            "Cubes: both cards side by side, each save a little glass cube on a grid.",
            "Retro TV: the card in your hand, its saves on an old TV, the chosen one hopping."};
        choice("mc_view", "Memory Cards View", kHelp[std::clamp(draft_.mc_view, 0, 4)], &draft_.mc_view,
               {"Cards", "Blocks", "By Game", "Cubes", "Retro TV"});
    }
    {
        /* Porpoise's own sound, the menus' (not the games'): each off or how
         * loud, in one row (menu_music and music_volume, menu_sounds and
         * sounds_volume, set together in the row's change). */
        music_level_ = draft_.menu_music ? std::clamp(draft_.music_volume, 0, 10) : 0;
        sounds_level_ = draft_.menu_sounds ? std::clamp(draft_.sounds_volume, 0, 10) : 0;
        choice("menu_music_level", "Menu Music", "The music in Porpoise's menus, and how loud it plays.",
               &music_level_, {"Off", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"});
        choice("menu_sounds_level", "Menu Sounds", "The sounds of moving through the menus, and how loud they play.",
               &sounds_level_, {"Off", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"});
        choice("sound_set", "Sound Set", "How the menus sound: Crisp, Soft or Porpoise's original sounds.",
               &draft_.sound_set, {"Crisp", "Soft", "Porpoise"});
    }
    action("Reset All Settings", "Every setting back to how Porpoise ships. Games, folders and saves stay.",
           "Reset\xE2\x80\xA6", kRowResetAll);
    action("Reinitialize Porpoise",
           "A fresh start: Porpoise opens as it did the first time and you choose a theme again. Games, folders, "
           "memory cards, saves and save states stay.",
           "Reinitialize\xE2\x80\xA6", kRowReinitialize);

    header("Screenshots");
    {
        if (shots_total_ < 0)
            shots_total_ = int(find_shots("").size());
        const int count = shots_total_;
        action("Screenshots", "Every game's screenshots, newest first. Cross shows one over the whole screen; Triangle deletes it.",
               plural(count, "1 screenshot", "{n} screenshots"), kRowShots);
    }
    toggle("shot_buttons", "Screenshot Buttons",
           "In a game, touch pad + Square takes a screenshot. The game's menu has Take Screenshot too.",
           &draft_.shot_buttons);
    info("Screenshot Folder", data_dir_ + "/screenshots",
         "Where screenshots are kept, a folder for each game. PS5 Upload or FTP can copy them to a computer.");

    header("Accessibility");
    {
        SettingRow r;
        r.section = section;
        r.key = "text_size";
        r.label = tr("Text Size");
        r.help = tr("Smaller or bigger labels across Porpoise.");
        r.int_value = &draft_.text_size;
        r.values = {tr("Smaller"), tr("Normal"), tr("Large"), tr("Larger")};
        r.order = {3, 0, 1, 2};
        rows_.push_back(r);
    }
    choice("colour_filter", "Color Filter",
           "For color blindness: moves the colors you may not tell apart to ones you can. Red-weak and "
           "green-weak help with reds and greens, blue-weak with blues and yellows.",
           &draft_.colour_filter, {"Off", "Red-Weak", "Green-Weak", "Blue-Weak", "Grayscale"});
    toggle("colour_filter_games", "Color Filter in Games", "The same color filter on the game's picture too.",
           &draft_.colour_filter_games);
    toggle("high_contrast", "High Contrast", "Solid panels, clearer edges and brighter text.",
           &draft_.high_contrast);
    toggle("bold_focus", "Bolder Focus", "What is chosen gets a thick, bright edge, easier to follow.",
           &draft_.bold_focus);
    toggle("reduced_motion", "Reduced Motion", "Stops the moving lights and shortens animations.",
           &draft_.reduced_motion);
    toggle("still_background", "Still Background", "The background holds still; everything else moves as usual.",
           &draft_.still_background);
    toggle("big_prompts", "Larger Button Hints", "The button hints along the bottom of the screen, larger.",
           &draft_.big_prompts);

    header("About");
    info("Porpoise (DolphinPS5)", build_label(), "A GameCube and Wii emulator for PS5, powered by Dolphin.");
    {
        SettingRow r;
        r.section = section;
        r.label = update_available() ? trf("Porpoise {version} is out", {{"version", latest_version_}})
                                     : tr("Updates");
        r.help = update_available()
                     ? trf("Downloads Porpoise {version} from GitHub, checks it, and puts it in place of this one. "
                           "Your games, saves and settings stay. Porpoise closes when it's done.",
                           {{"version", latest_version_}})
                     : tr("Looks on GitHub for a newer Porpoise. Porpoise also looks once a day by itself.");
        r.values = {""};
        r.action = kRowUpdate;
        rows_.push_back(r);
    }
    toggle("beta_updates", "Beta Updates",
           "Offer test versions (pre-releases) too when they come out: newer, but less tested. A beta of Porpoise "
           "always offers the next beta.",
           &draft_.beta_updates);
    if (!version_labels_.empty())
    {
        SettingRow r;
        r.section = section;
        r.key = "version_pick";
        r.label = tr("Choose a Version");
        r.help = tr("Any Porpoise release, newer or older, betas included: left and right to pick, Cross to install. "
                    "Going back keeps your games, saves and settings; save states made by a newer Porpoise may not "
                    "load in an older one.");
        r.values = version_labels_;
        r.int_value = &version_pick_;
        r.action = kRowPickVersion;
        rows_.push_back(r);
    }
    {
        /* The player's own GameCube BIOS, found by what is in it. */
        if (!bios_looked_)
        {
            bios_looked_ = true;
            bios_found_.clear();
            for (const porpoise::bios::GameCubeBios &b : porpoise::bios::find_gamecube(data_dir_ + "/bios"))
            {
                const std::string one = (b.pal ? "PAL " : "NTSC ") + b.revision;
                if (bios_found_.find(one) == std::string::npos)
                    bios_found_ += (bios_found_.empty() ? "" : ", ") + one;
            }
        }
        info("GameCube BIOS", bios_found_.empty() ? tr("None") : bios_found_,
             "Your console's own BIOS (IPL.bin), for the boot animation: put it in "
             "/data/porpoise/bios, under any name. NTSC plays US and Japanese games, PAL European ones. Porpoise "
             "includes none.");
    }
    /* Help and the community: the RIPALDA Discord (bug reports go there now),
     * with a code to scan beside it. */
    info("Discord", "discord.gg/GgDE5Vynyu",
         "Help, bug reports, news and the community, on the RIPALDA Discord. Scan the code with your phone to join.");
    rows_.back().key = "discord";
    action("Diagnostic Test",
           "Cross checks what Porpoise can see and do on this console - the jailbreak, its folder, drives, games, "
           "the last game's speed - and saves the results with the logs, to share on the Discord.",
           "Run\xE2\x80\xA6", kRowDiagnostic);
    {
        SettingRow r;
        r.section = section;
        r.label = tr("Report a Bug");
        r.help = tr("Cross gathers Porpoise's logs into /data/porpoise/reports (and onto a USB drive, when one is "
                    "in). Share them in the Discord's bug reports with the game and what happened.");
        r.values = {tr("Save a Report\xE2\x80\xA6")};
        r.action = kRowSendReport;
        rows_.push_back(r);
    }

    /* Credits: Ruben first, his mark beside his name. */
    info("Created By", "Ruben (@elripalda)", "Porpoise, its menus, music and sounds: Ruben. @elripalda - ripalda.dev");
    info("Website", "ripalda.dev", "Ruben's projects, news and downloads, with links to the Discord and more.");
    info("Music and Sounds", "@elripalda", "The menu music and sound effects, made for Porpoise by Ruben.");
    info("Dolphin on PS5", "Mihawk (mihawk-99)",
         "Mihawk (mihawk-99) brought the Dolphin core to the PS5. Porpoise is built on his port of Dolphin and "
         "RetroArch.");
    info("Frame Generation", "swordpdf",
         "ps5-framegen by swordpdf, from PS5SX2 and RPCS3-PS5, with AMD FidelityFX FSR 3's optical flow (GPL v3; "
         "shaders MIT).");
    info("Controller Art", "Zacksly",
         "PS5 Button Icons and Controls by Zacksly - zacksly.itch.io, @_Zacksly on Twitter. CC BY 3.0, adapted for "
         "Porpoise.");
    info("Emulation", "Dolphin", "Dolphin, by the Dolphin Team - dolphin-emu.org. Free software, GPL v2 or later.");
    info("Dolphin Core", "libretro", "Dolphin's libretro core, maintained by the libretro team (GPL v2 or later).");
    info("Core API", "libretro / RetroArch", "Porpoise hosts the core through the libretro API that RetroArch made.");
    info("PS5 Graphics", "Mesa RADV", "Vulkan on PS5 through Mesa's RADV driver (MIT), PS5_Mesa and PS5_Vulkan ports.");
    info("PS5 Toolchain", "ps5-payload-sdk", "John T\xC3\xB6rnblom's ps5-payload-sdk (GPL v3) and its PS5 ports.");
    info("Inspired By", "PS5SX2, ProsperoEden", "PS5 homebrew front ends that showed the way.");
    info("Box Art and Info", "GameTDB.com", "Covers, disc art and game details from GameTDB.com and its contributors.");
    info("Fonts", "Nunito, Noto Sans",
         "Nunito; Noto Sans JP, SC, TC and KR; M PLUS 1; JetBrains Mono; VT323; Doto; Exo 2; and Lora (as Porpoise "
         "Serif). All under the SIL Open Font License.");
    info("Images and Audio", "stb", "stb_image, stb_truetype and stb_vorbis by Sean Barrett (public domain / MIT).");
    info("Wii Banners", "Wii Banner Player",
         "How a Wii disc's own tile and banner play: after the Wii Banner Player Project (zlib license), rewritten "
         "for Porpoise. RVZ discs are read with Zstandard's decoder (BSD).");
    info("Thanks", "PS5 Scene", "etaHEN, kstuff and ShadowMountPlus make homebrew like this possible.");
    info("Trademarks", "Nintendo", "GameCube and Wii are trademarks of Nintendo. Porpoise is not affiliated with Nintendo.");
    info("Trademarks", "Sony",
         "PlayStation, PS5 and DualSense are trademarks of Sony Interactive Entertainment. Porpoise is not affiliated "
         "with Sony.");

    simplify_rows(false);
    if (settings_row_ < 0 || settings_row_ >= int(rows_.size()) || rows_[std::size_t(settings_row_)].header)
        settings_row_ = 1;
    rail_ = std::clamp(rail_, 0, std::max(0, section_count() - 1));
}

void App::build_game_settings()
{
    rows_.clear();
    SettingRow h;
    h.section = "This Game";
    h.header = true;
    rows_.push_back(h);
    SettingRow r;
    r.section = "This Game";
    r.label = tr("Own Settings");
    r.key = "own_count"; /* (found by it: the Show All Settings switch comes before it) */
    r.help = tr("Changes here apply to this game only. Everything else follows your settings.");
    r.values = {game_keys_.empty() ? tr("None yet") : plural(change_count(), "1 change", "{n} changes")};
    rows_.push_back(r);
    SettingRow reset;
    reset.section = "This Game";
    reset.label = tr("Reset to Default");
    reset.help = tr("Forgets this game's own settings; it follows your settings again.");
    reset.values = {tr("Reset\xE2\x80\xA6")};
    reset.action = kRowResetGame;
    rows_.push_back(reset);
    if (game_for_ && !game_for_->id.empty())
    {
        SettingRow art;
        art.section = "This Game";
        art.label = tr("Download Cover Again");
        art.help = tr("Gets this game's cover, box and disc art from GameTDB.com again, in place of what Porpoise has. "
                      "Needs the console online.");
        art.values = {tr("Download")};
        art.action = kRowCoversAgain;
        rows_.push_back(art);
    }
    /* The saved setups belong to This Game: before the Home Screen section,
     * or moving down stopped at that header and never reached them. */
    add_setup_rows(true);
#ifndef PORPOISE_DESKTOP
    if (game_for_ && !sandboxed_)
    {
        /* Its own home screen tile (porpoise_forwarders): no settings of the
         * game's, so no key (they wait for nothing). */
        SettingRow hh;
        hh.section = "Home Screen";
        hh.header = true;
        rows_.push_back(hh);
        SettingRow artrow;
        artrow.section = "Home Screen";
        artrow.label = tr("Tile Art");
        artrow.help = tr("The tile's icon and the background behind it, seen as the home screen shows them: the "
                         "cover, a screenshot, a title screen or your own picture, cropped and zoomed, or whole "
                         "over a blur, white, black or Porpoise's pattern.");
        artrow.values = {tr("Edit\xE2\x80\xA6")};
        artrow.action = kRowTileArt;
        rows_.push_back(artrow);
        SettingRow ex;
        ex.section = "Home Screen";
        ex.label = tr("Close Porpoise After the Game");
        ex.help = tr("On: leaving the game from its tile goes back to the home screen. Off: to Porpoise's library.");
        ex.values = {tr("Off"), tr("On")};
        ex.bool_value = &fwd_exit_;
        rows_.push_back(ex);
        const bool made = !porpoise::forwarders::existing(data_dir_, game_for_->path).empty();
        SettingRow add;
        add.section = "Home Screen";
        add.label = tr(made ? "Update Its Home Screen Tile" : "Add to Home Screen");
        add.help = tr("Makes an app with this game's own tile in /data/homebrew. Install it with ShadowMountPlus (or "
                      "the way you install homebrew) to see it on the home screen; it opens Porpoise straight into "
                      "the game. Close Porpoise before you open the tile.");
        add.values = {tr(made ? "Update\xE2\x80\xA6" : "Add\xE2\x80\xA6")};
        add.action = kRowForwarder;
        rows_.push_back(add);
    }
#endif
    add_game_rows(game_, true);
    add_recommended_rows();
    add_cheat_rows();
    simplify_rows(true);
    if (settings_row_ < 0 || settings_row_ >= int(rows_.size()) || rows_[std::size_t(settings_row_)].header)
        settings_row_ = 1;
    rail_ = std::clamp(rail_, 0, std::max(0, section_count() - 1));
}

/* One row for each saved setup: Cross puts its Video and Graphics settings on
 * this game (per_game) or on every game. */
void App::add_setup_rows(bool per_game)
{
    const std::string section = per_game ? "This Game" : "Video";
    for (int i = 0; i < setups::kCount; ++i)
    {
        const setups::Setup su = setups::get(i);
        if (!su.exists)
            continue;
        SettingRow r;
        r.section = section;
        r.label = trf("Use Setup {n}", {{"n", std::to_string(i + 1)}});
        r.help = per_game ? trf("Puts setup {n}'s Video and Graphics settings on this game. It was saved from {game}.",
                                {{"n", std::to_string(i + 1)}, {"game", su.from}})
                          : trf("Puts setup {n}'s Video and Graphics settings on every game. It was saved from {game}.",
                                {{"n", std::to_string(i + 1)}, {"game", su.from}});
        r.values = {su.from.empty() ? tr("Use") : su.from};
        r.action = kRowUseSetup;
        r.setup = i;
        rows_.push_back(r);
    }
}

/* Every game's cheats and patches: how it plays in widescreen first, then
 * the codes Dolphin, the widescreen collection and the player's own folder
 * have for it (ui_cheats.hpp), one switch each; or that it has none, so
 * nothing on screen is a mystery. */
void App::add_cheat_rows()
{
    cheats_.clear();
    cheat_on_.clear();
    if (!game_for_)
        return;
    const std::string section = "Cheats and Patches";
    SettingRow h;
    h.section = section;
    h.header = true;
    rows_.push_back(h);
    {
        const bool wii = game_for_->platform == "Wii";
        const widescreen::Kind kind =
            wii ? widescreen::Kind::Native : widescreen::kind_of(game_for_->id, sys_dir_, false);
        SettingRow r;
        r.section = section;
        r.label = tr("Widescreen");
        r.help = wii ? tr("Wii games are 16:9 when Wii Widescreen is on in Porpoise's settings, as on a Wii.")
                     : widescreen::about(kind);
        r.values = {wii                                 ? tr("The Wii's Setting")
                    : kind == widescreen::Kind::Patch  ? tr("16:9 Code")
                    : kind == widescreen::Kind::Native ? tr("In the game's options")
                                                       : tr("4:3 Only")};
        rows_.push_back(r);
    }
    if (game_for_->id.size() == 6)
    {
        /* A code of the player's own, typed here (as Dolphin's Add New Code). */
        SettingRow add;
        add.section = section;
        add.label = tr("Your Own Code");
        add.help = tr("Type in a Gecko code, like the WiiLink WFC code for a game, with a name. It's saved in "
                      "/data/porpoise/cheats and is on unless you turn it off.");
        add.values = {tr("Add a Code\xE2\x80\xA6")};
        add.action = kRowAddCode;
        rows_.push_back(add);
    }
    if (game_for_->id.size() == 6 && !sys_dir_.empty())
    {
        /* <data>/cheats, where the player's own codes go, is theirs to make: an
         * empty one made here was taken for a broken online patch. */
        cheats_ = cheats_for(sys_dir_, game_for_->id, data_dir_ + "/cheats", game_for_->platform == "Wii");
        /* What Porpoise or the player turns on first (the widescreen code, the
         * player's own), so the 80 shown always hold them. */
        std::stable_sort(cheats_.begin(), cheats_.end(), [](const Cheat &a, const Cheat &b) {
            return (a.default_on || a.own) > (b.default_on || b.own);
        });
    }
    if (cheats_.empty())
    {
        SettingRow r;
        r.section = section;
        r.label = tr("None for this game");
        r.help = trf("Dolphin lists none for this game. Your own codes go in /data/porpoise/cheats/{id}.ini.",
                     {{"id", game_for_->id.empty() ? std::string("<ID>") : game_for_->id}});
        r.values = {""};
        rows_.push_back(r);
        return;
    }
    for (std::size_t i = 0; i < cheats_.size() && i < 80; ++i)
    {
        const Cheat &c = cheats_[i];
        const bool on = c.default_on ? game_.get(cheat_key(c, false)) != "1" : game_.get(cheat_key(c, true)) == "1";
        cheat_on_.push_back(on);
    }
    for (std::size_t i = 0; i < cheat_on_.size(); ++i)
    {
        const Cheat &c = cheats_[i];
        SettingRow r;
        r.section = section;
        r.key = "cheat";
        r.folder = int(i);
        r.label = c.name.substr(1);
        r.help = cheat_help(c);
        r.bool_value = &cheat_on_[i];
        r.values = {tr("Off"), tr("On")};
        rows_.push_back(r);
    }
}

/* What's recommended for this game, as switches: Porpoise's picks (all at
 * once, or one by one) and Dolphin's own fixes, which are on unless turned
 * off here. Built after the game's other rows, whose names it borrows. */
void App::add_recommended_rows()
{
    rec_rows_.clear();
    if (!game_for_)
        return;
    std::vector<SettingRow> out;
    SettingRow h;
    h.section = "Auto Boost";
    h.header = true;
    out.push_back(h);
    /* A Dolphin settings file of the player's own for this game: Porpoise
     * leaves it as it is, so its switches would change nothing. */
    if (game_for_->id.size() == 6)
    {
        const std::string own = "User/GameSettings/" + game_for_->id + ".ini";
        if (std::FILE *f = std::fopen((saves_dir_ + "/" + own).c_str(), "r"))
        {
            char first[64] = {0};
            const bool ours = std::fgets(first, sizeof first, f) && std::strncmp(first, "# Written by Porpoise", 21) == 0;
            std::fclose(f);
            if (!ours)
            {
                SettingRow r;
                r.section = "Auto Boost";
                r.label = tr("Your own Dolphin file");
                r.help = trf("saves/{file} is yours, so Porpoise leaves it as it is and the switches below change "
                             "nothing. Remove it to use them.",
                             {{"file", own}});
                r.values = {""};
                out.push_back(r);
            }
        }
    }
    auto add = [&](const std::string &label, const std::string &help, RecRow rec) {
        SettingRow r;
        r.section = "Auto Boost";
        r.label = label;
        r.tag = rec.kind == RecRow::DolphinFix ? tr("Dolphin's Fix")
                : rec.kind == RecRow::Pick     ? tr("Porpoise's Pick")
                                               : "";
        r.help = help;
        r.values = {""};
        r.rec = int(rec_rows_.size());
        r.toggle = rec_on(rec) ? 1 : 0;
        rec_rows_.push_back(rec);
        out.push_back(r);
    };
    /* How a setting and its value read, from the game's own rows. */
    auto describe = [&](const std::string &key, const std::string &value, std::string &label, std::string &text) {
        if (key.rfind("dolphin.", 0) == 0)
        {
            recommend::describe_dolphin(key, value, label, text);
            label = tr(label);
            text = tr(text);
            return;
        }
        label = key;
        text = value;
        for (const SettingRow &r : rows_)
            if (r.key == key && (r.int_value || r.bool_value))
            {
                label = r.label;
                const int v = std::atoi(value.c_str());
                const int i = r.bool_value ? (v ? 1 : 0) : v - r.min;
                if (i >= 0 && i < int(r.values.size()))
                    text = r.values[std::size_t(i)];
            }
    };

    recommend::Pick pick;
    if (recommend::pick_for(game_for_->id, pick))
    {
        RecRow all;
        all.kind = RecRow::AllPicks;
        add(tr("Auto Boost"),
            (pick.note.empty() ? tr("Settings that run this game best on PS5, as tested.") : pick.note) + "  " +
                tr("Cross turns them all on or off."),
            all);
        for (const auto &[k, v] : pick.values)
        {
            std::string label, text;
            describe(k, v, label, text);
            RecRow one;
            one.kind = RecRow::Pick;
            one.key = k;
            one.value = v;
            add(label + ": " + text, trf("One of Porpoise's picks for this game: {setting}.", {{"setting", label + " " + text}}),
                one);
        }
    }
    for (const recommend::Fix &fix : recommend::dolphin_fixes(game_for_->id))
    {
        const std::string label = tr(fix.label) + ": " + tr(fix.value);
        const std::string why = fix.why.empty() ? "" : trf("Dolphin's note: {why}", {{"why", fix.why}}) + "  ";
        if (fix.override_key.empty())
        {
            /* A value, not a switch: shown as it is. */
            SettingRow r;
            r.section = "Auto Boost";
            r.label = label;
            r.help = why + tr("One of Dolphin's own fixes for this game. Dolphin applies it by itself.");
            r.values = {tr("Applied")};
            out.push_back(r);
            continue;
        }
        RecRow rec;
        rec.kind = RecRow::DolphinFix;
        rec.key = fix.override_key;
        rec.value = fix.raw;
        rec.off_value = fix.off_value;
        /* Dual core turned back off by the player's own choice, not forced on. */
        if (fix.key == "CPUThread")
            rec.off_value = game_.dual_core ? "True" : "False";
        add(label, why + tr("Dolphin's own fix for this game, on by itself. Turning it off may help speed but can bring "
                            "back the problem it fixes."),
            rec);
    }
    if (out.size() == 1)
    {
        SettingRow r;
        r.section = "Auto Boost";
        r.label = tr("Runs as it is");
        r.help = tr("This game needs no Auto Boost: Dolphin lists no fixes for it, and Porpoise's own settings "
                    "already run it as tested. The list grows as games are tested.");
        r.values = {""};
        out.push_back(r);
    }
    /* Right after This game. */
    std::size_t at = 0;
    while (at < rows_.size() && (rows_[at].header ? rows_[at].section == "This Game" : rows_[at].section == "This Game"))
        ++at;
    rows_.insert(rows_.begin() + std::ptrdiff_t(at), out.begin(), out.end());
}

/* 4.0: Settings for someone who just wants to play. The Show All Settings
 * switch comes first; with it off, only the settings most players look for
 * stay: the rest keep their values, they just aren't shown. Developer
 * options show everything. */
/* A game's Own Settings row: how many of its settings are its own. */
void App::set_own_count()
{
    for (SettingRow &r : rows_)
        if (r.key == "own_count")
            r.values = {plural(change_count(), "1 change", "{n} changes")};
}

void App::simplify_rows(bool per_game)
{
    if (rows_.empty())
        return;
    {
        SettingRow sw;
        sw.section = rows_.front().section;
        sw.label = tr("Show All Settings");
        sw.help = tr("Off: just the settings most players need. On: every setting Porpoise has, for fine-tuning. "
                     "Hidden settings keep their values either way.");
        sw.values = {""};
        sw.action = kRowAllSettings;
        sw.toggle = settings_->all_settings ? 1 : 0;
        rows_.insert(rows_.begin() + 1, sw);
    }
    if (settings_->all_settings || settings_->developer)
        return;
    struct Keep
    {
        const char *section;
        bool all;                 /* the whole section */
        bool actions;             /* its rows without a setting (buttons, lists) */
        std::vector<const char *> keys;
    };
    static const Keep kKeep[] = {
        {"This Game", true, true, {}},
        {"Auto Boost", true, true, {}},
        {"Home Screen", true, true, {}},
        {"Cheats and Patches", true, true, {}},
        {"Games", true, true, {}},
        {"Video", false, false, {"output_res", "frame_gen", "fg_steady30", "wide", "fps_overlay"}},
        {"Enhancements", false, false, {"resolution", "antialiasing", "screen_filter", "filter_strength"}},
        {"Save States", false, false, {"quick_resume"}},
        {"Audio", false, false, {"volume", "audio_preset"}},
        {"Controllers & Accessories", true, true, {}},
        {"Controls", false, true, {"button_layout", "rumble"}},
        {"Wii Remote", false, true, {"wii_preset", "wii_controller", "wii_pointer"}},
        {"Console", false, true, {"language"}},
        {"Interface", false, false, {"ui_language", "ui_theme", "ui_palette", "menu_music_level", "menu_sounds_level"}},
        {"Screenshots", true, true, {}},
        {"Accessibility", true, true, {}},
        {"About", true, true, {}},
    };
    auto kept = [&](const SettingRow &r) {
        if (r.action == kRowAllSettings)
            return true;
        for (const Keep &k : kKeep)
        {
            if (r.section != k.section)
                continue;
            if (k.all)
                return true;
            if (r.key.empty())
                return k.actions;
            for (const char *key : k.keys)
                if (r.key == key)
                    return true;
            return false;
        }
        return false;
    };
    (void)per_game;
    std::vector<SettingRow> out;
    out.reserve(rows_.size());
    for (std::size_t i = 0; i < rows_.size(); ++i)
    {
        const SettingRow &r = rows_[i];
        if (r.header)
        {
            /* A section stays when something in it does. */
            bool any = false;
            for (std::size_t j = i + 1; j < rows_.size() && !rows_[j].header; ++j)
                any = any || kept(rows_[j]);
            if (any)
                out.push_back(r);
        }
        else if (kept(r))
            out.push_back(r);
    }
    rows_.swap(out);
}

bool App::rec_on(const RecRow &rec) const
{
    auto has = [&](const std::string &k, const std::string &v) {
        if (std::find(game_keys_.begin(), game_keys_.end(), k) == game_keys_.end())
            return false;
        porpoise::Settings probe = game_;
        probe.set(k, v);
        return probe.get(k) == game_.get(k);
    };
    switch (rec.kind)
    {
    case RecRow::DolphinFix:
        /* On unless this game turns it off. */
        return game_.get(rec.key).empty() || game_.get(rec.key) == rec.value;
    case RecRow::Pick:
        return has(rec.key, rec.value);
    case RecRow::AllPicks:
    {
        recommend::Pick pick;
        if (!recommend::pick_for(game_for_->id, pick))
            return false;
        for (const auto &[k, v] : pick.values)
            if (!has(k, v))
                return false;
        return true;
    }
    }
    return false;
}

void App::set_game_key(const std::string &key, const std::string *value)
{
    const auto it = std::find(game_keys_.begin(), game_keys_.end(), key);
    if (value)
    {
        game_.set(key, *value);
        if (it == game_keys_.end())
            game_keys_.push_back(key);
        return;
    }
    /* Back to what every game uses. */
    if (key.rfind("dolphin.", 0) == 0)
        game_.forget(key);
    else
        game_.set(key, settings_->get(key));
    if (it != game_keys_.end())
        game_keys_.erase(it);
}

void App::toggle_recommended(int index)
{
    if (!game_for_ || index < 0 || index >= int(rec_rows_.size()))
        return;
    const RecRow rec = rec_rows_[std::size_t(index)];
    const bool on = rec_on(rec);
    switch (rec.kind)
    {
    case RecRow::DolphinFix:
        if (on)
            set_game_key(rec.key, &rec.off_value);
        else
            set_game_key(rec.key, nullptr);
        break;
    case RecRow::Pick:
        set_game_key(rec.key, on ? nullptr : &rec.value);
        break;
    case RecRow::AllPicks:
    {
        recommend::Pick pick;
        if (recommend::pick_for(game_for_->id, pick))
            for (const auto &[k, v] : pick.values)
                set_game_key(k, on ? nullptr : &v);
        break;
    }
    }
    const int row = settings_row_; /* waits for Apply, like any change */
    build_game_settings();
    settings_row_ = std::clamp(row, 0, int(rows_.size()) - 1);
    sfx(on ? Sound::MovingTab : Sound::LaunchGame);
}

int App::section_count() const
{
    int n = 0;
    for (const SettingRow &r : rows_)
        n += r.header ? 1 : 0;
    return n;
}

std::string App::section_name(int index) const
{
    int n = 0;
    for (const SettingRow &r : rows_)
        if (r.header && n++ == index)
            return r.section;
    return "";
}

int App::first_row_of(const std::string &section) const
{
    for (int i = 0; i < int(rows_.size()); ++i)
        if (!rows_[std::size_t(i)].header && rows_[std::size_t(i)].section == section)
            return i;
    return -1;
}

void App::open_game_settings(Game &g)
{
    game_for_ = &g;
    game_ = *settings_;
    const std::string path = game_settings_path(g);
    game_.load(path, true);
    game_keys_ = Settings::keys_in(path);
    game_base_ = game_;
    game_keys_base_ = game_keys_;
    on_rail_ = true;
    rail_ = 0;
    settings_row_ = 1;
    build_game_settings();
    open_screen(Screen::GameSettings);
}

void App::close_game_settings()
{
    details_custom_ = !game_keys_.empty();
    game_for_ = nullptr;
    build_settings();
    on_rail_ = true;
    rail_ = 0;
    open_screen(Screen::Details);
}

/* ---- changing ------------------------------------------------------------------------------ */

void App::change_setting(int dir)
{
    SettingRow &r = rows_[std::size_t(settings_row_)];
    if (r.header || r.action || (!r.bool_value && !r.int_value))
        return;
    if (r.bool_value)
        *r.bool_value = !*r.bool_value;
    else
    {
        const int n = int(r.values.size());
        if (!r.order.empty())
        {
            const auto at = std::find(r.order.begin(), r.order.end(), *r.int_value);
            const int i = at == r.order.end() ? 0 : int(at - r.order.begin());
            *r.int_value = r.order[std::size_t(std::clamp(i + dir, 0, int(r.order.size()) - 1))];
        }
        else
            *r.int_value = std::clamp(*r.int_value + dir, r.min, r.min + n - 1);
        if (r.text_value && r.int_value == &border_choice_)
            *r.text_value = border_names_[std::size_t(std::clamp(border_choice_, 0, int(border_names_.size()) - 1))];
    }
    if (r.key == "enh_advanced")
    {
        /* A view, the same everywhere: kept at once, and the rows follow. */
        draft_.enh_advanced = base_.enh_advanced = settings_->enh_advanced;
        settings_->save(settings_path_);
        const int row = settings_row_;
        if (screen_ == Screen::GameSettings && game_for_)
            build_game_settings();
        else
            build_settings();
        settings_row_ = std::clamp(row, 0, int(rows_.size()) - 1);
        return;
    }
    if (r.key == "cheat" && screen_ == Screen::GameSettings && game_for_ && r.folder >= 0 &&
        r.folder < int(cheats_.size()))
    {
        /* A code's switch: named in [<kind>_Enabled] when it is on and
         * Dolphin doesn't turn it on by itself, in [<kind>_Disabled] when it
         * is off and Dolphin would. Cheats (not patches) need Dolphin's
         * cheats on for the game. */
        const Cheat &c = cheats_[std::size_t(r.folder)];
        const bool on = cheat_on_[std::size_t(r.folder)];
        for (const std::string &k : {cheat_key(c, true), cheat_key(c, false)})
        {
            game_.forget(k);
            game_keys_.erase(std::remove(game_keys_.begin(), game_keys_.end(), k), game_keys_.end());
        }
        const std::string k = on && !c.default_on ? cheat_key(c, true) : !on && c.default_on ? cheat_key(c, false) : "";
        if (!k.empty())
        {
            game_.set(k, "1");
            game_keys_.push_back(k);
        }
        if (on && c.kind != "OnFrame" && !game_.cheats)
        {
            game_.cheats = true;
            if (std::find(game_keys_.begin(), game_keys_.end(), "cheats") == game_keys_.end())
                game_keys_.push_back("cheats");
        }
        set_own_count();
        return;
    }
    if (r.key == "audio_preset" || r.key == "audio_buffer" || r.key == "audio_fill" || r.key == "audio_stretch")
    {
        /* The sound rows follow each other: a preset sets several, and the
         * preset shown follows the rows. */
        const bool per_game = screen_ == Screen::GameSettings && game_for_;
        Settings &t = per_game ? game_ : draft_;
        const std::string key = r.key; /* r goes with the rebuild */
        if (key == "audio_preset")
            t.use_audio_preset(audio_preset_);
        if (per_game)
        {
            const std::vector<std::string> keys =
                key == "audio_preset" ? std::vector<std::string>{"audio_pull", "audio_buffer", "audio_fill", "audio_stretch"}
                                      : std::vector<std::string>{key};
            for (const std::string &k : keys)
                if (std::find(game_keys_.begin(), game_keys_.end(), k) == game_keys_.end())
                    game_keys_.push_back(k);
            build_game_settings();
            return;
        }
        build_settings();
        return;
    }
    if (r.key == "wide")
    {
        /* 2.0's hack switch gives way to the Widescreen choice. */
        Settings &t = screen_ == Screen::GameSettings && game_for_ ? game_ : draft_;
        if (t.widescreen)
        {
            t.widescreen = false;
            if (screen_ == Screen::GameSettings && std::find(game_keys_.begin(), game_keys_.end(), "widescreen") ==
                                                       game_keys_.end())
                game_keys_.push_back("widescreen");
        }
        /* On for a game with no widescreen code: say what the hack does. */
        const bool forced = t.wide == widescreen::ModeOn &&
                            (screen_ != Screen::GameSettings || !game_for_ ||
                             widescreen::kind_of(game_for_->id, sys_dir_, game_for_->platform == "Wii") ==
                                 widescreen::Kind::None);
        if (forced)
            open_dialog(DialogKind::Info, tr("Emulated Widescreen"),
                        tr(screen_ == Screen::GameSettings
                               ? "This game has no widescreen code, so Porpoise uses Dolphin's emulated widescreen "
                                 "hack. You may see graphical glitches: things at the edges of the screen can pop in "
                                 "and out or disappear. Auto keeps it in 4:3."
                               : "Games with a widescreen code or a 16:9 option of their own play in 16:9 either "
                                 "way. For the rest, On uses Dolphin's emulated widescreen hack, and you may see "
                                 "graphical glitches: things at the edges of the screen can pop in and out or "
                                 "disappear. Auto keeps those games in 4:3."),
                        "");
    }
    if (screen_ == Screen::GameSettings && game_for_)
    {
        if (!r.key.empty() && std::find(game_keys_.begin(), game_keys_.end(), r.key) == game_keys_.end())
            game_keys_.push_back(r.key);
        if (applies_at_once(r.key))
            apply_at_once({r.key});
        set_own_count();
        return;
    }
    if (r.key == "menu_music_level" || r.key == "menu_sounds_level")
    {
        /* The menus' music or sounds: off, or on at a volume. */
        const bool music = r.key == "menu_music_level";
        const int level = music ? music_level_ : sounds_level_;
        (music ? draft_.menu_music : draft_.menu_sounds) = level > 0;
        if (level > 0)
            (music ? draft_.music_volume : draft_.sounds_volume) = level;
        apply_at_once(music ? std::vector<std::string>{"menu_music", "music_volume"}
                            : std::vector<std::string>{"menu_sounds", "sounds_volume"});
        return;
    }
    /* Everything else waits for Apply (apply_pending), but what changes as
     * you see it, which is saved at once. */
    const std::string key = r.key; /* r goes with a rebuild */
    if (key == "ui_theme")
    {
        /* A new theme brings its own colours and font; both can be changed after. */
        draft_.ui_palette = 0;
        draft_.ui_font = 0;
    }
    if (key == "wii_preset" && draft_.wii_preset > 0)
        draft_.use_wii_preset(draft_.wii_preset - 1); /* a preset brings its whole set-up */
    if (applies_at_once(key))
        apply_at_once(key == "ui_theme" ? std::vector<std::string>{"ui_theme", "ui_palette", "ui_font"}
                                        : std::vector<std::string>{key});
    if (key == "ui_theme" || key == "ui_layout" || key == "lib_view" || key == "mc_view" || key == "sc_games" ||
        key == "ui_palette" || key == "ui_font" || key == "wii_preset" || key == "text_size")
    {
        const int row = settings_row_;
        build_settings(); /* rows and help that follow the look */
        settings_row_ = std::clamp(row, 0, int(rows_.size()) - 1);
    }
}

bool App::applies_at_once(const std::string &key)
{
    static const char *const kKeys[] = {
        "ui_theme",      "ui_palette",   "ui_font",         "ui_layout",      "lib_view",     "mc_view",  "recent_dock", "recent_style",
        "sc_games",      "ui_pointer",   "text_size",       "high_contrast",  "bold_focus",  "reduced_motion", "still_background",
        "big_prompts",   "colour_filter", "colour_filter_games", "border",    "screen_filter", "filter_strength",
        "menu_music",    "menu_sounds",  "music_volume",    "sounds_volume", "sound_set",
        "bloom",         "color_saturation", "color_contrast", "color_warmth"};
    for (const char *k : kKeys)
        if (key == k)
            return true;
    return false;
}

void App::apply_at_once(const std::vector<std::string> &keys)
{
    if (screen_ == Screen::GameSettings && game_for_)
    {
        /* Into the game's own file with what it already had saved; the other
         * changes still wait for Apply. */
        Settings saved = game_base_;
        saved.copy_keys(game_, keys);
        std::vector<std::string> saved_keys = game_keys_base_;
        for (const std::string &k : keys)
            if (std::find(saved_keys.begin(), saved_keys.end(), k) == saved_keys.end())
                saved_keys.push_back(k);
        mkdir((data_dir_ + "/game-settings").c_str(), 0777);
        saved.save_keys(game_settings_path(*game_for_), saved_keys);
        game_base_ = saved;
        game_keys_base_ = saved_keys;
        return;
    }
    const int was_theme = settings_->ui_theme;
    settings_->copy_keys(draft_, keys);
    base_.copy_keys(draft_, keys);
    if (settings_->ui_theme != was_theme)
    {
        /* The theme's own view (look_changed), keeping the colours and font
         * chosen with it. */
        const int palette = settings_->ui_palette, font = settings_->ui_font;
        look_changed(was_theme);
        settings_->ui_palette = palette;
        settings_->ui_font = font;
    }
    apply_look();
    settings_->save(settings_path_);
    settings_->write_core_options(options_path_);
    live_changed_ = true;
}

App::Action App::activate_row(const SettingRow &row)
{
    switch (row.action)
    {
    case kRowAccount:
        open_account();
        return Action::None;
    case kRowAddShare:
        open_share(-1);
        return Action::None;
    case kRowEditShare:
        open_share(row.folder);
        return Action::None;
    case kRowOnlineDns:
        open_dns();
        return Action::None;
    case kRowOnlineHub:
        open_hub();
        return Action::None;
    case kRowMoveData:
        if (move_choice_ >= 0 && move_choice_ < int(move_places_.size()))
        {
            move_pick_ = move_choice_;
            const auto &place = move_places_[std::size_t(move_choice_)];
            struct stat st;
            if (place.second != PORPOISE_DATA && stat((place.second + "/settings.ini").c_str(), &st) == 0)
            {
                /* A Porpoise folder is there already (from before a reset, or
                 * copied over): moving would write over it, using it doesn't. */
                move_target_ = place.second;
                open_dialog(DialogKind::UseFolder,
                            trf("There's a Porpoise folder on {place}", {{"place", place.first}}),
                            tr("Porpoise can use that folder, with its own settings and saves, instead of moving "
                               "this one there. The folder in use now stays as it is. Porpoise closes; open it "
                               "again."),
                            tr("Use It"));
                return Action::None;
            }
            open_dialog(DialogKind::MoveData,
                        trf("Move Porpoise's folder to {place}?", {{"place", place.first}}),
                        tr("Everything in Porpoise's folder moves there: settings, saves, save states, covers and "
                           "texture packs. A big folder takes a while. Porpoise closes when it's done; open it "
                           "again."),
                        tr("Move"));
        }
        return Action::None;
    case kRowAddFolder:
#ifdef PORPOISE_DESKTOP
        open_browser(""); /* the shortcuts: the games folder, Downloads, the drives */
        return Action::None;
#endif
        open_browser("/");
        /* Start on /data, where most players keep their games. */
        for (int i = 0; i < int(browse_entries_.size()); ++i)
            if (browse_entries_[std::size_t(i)].path == "/data")
                browse_row_ = i;
        return Action::None;
    case kRowUpdate:
        if (updating() || update_phase_ == 1)
            return Action::None;
        if (update_available())
        {
            open_dialog(DialogKind::InstallUpdate, trf("Install Porpoise {version}?", {{"version", latest_version_}}),
                        latest_size_ ? trf("{mb} MB from GitHub. Porpoise checks the download, puts the new files in "
                                           "place, and then closes so you can open the new one. Your games, saves "
                                           "and settings stay.",
                                           {{"mb", std::to_string((latest_size_ + (1 << 20) - 1) >> 20)}})
                                     : tr("Porpoise checks the download, puts the new files in place, and then closes "
                                          "so you can open the new one. Your games, saves and settings stay."),
                        tr("Install"));
            return Action::None;
        }
        sfx(Sound::MenuScroll);
        update_phase_ = 1; /* until the updater says otherwise: a quick failure still shows */
        return Action::CheckUpdate;
    case kRowSetupCheck:
        sfx(Sound::MenuScroll);
        show_setup_check(false);
        return Action::None;
    case kRowImportSaves:
    {
        sfx(Sound::MenuScroll);
        if (usb_root().empty())
        {
            open_dialog(DialogKind::Info, tr("No USB drive"),
                        tr("Plug in the USB drive with a Porpoise Saves folder (made by Options in Memory Cards)."),
                        "");
            return Action::None;
        }
        Card a, b;
        load_cards(saves_dir_, a, b);
        int gc = 0, wii = 0, skipped = 0;
        import_usb_saves(saves_dir_, a, gc, wii, skipped);
        cards_scanned_ = wii_scanned_ = false;
        save_scan_.reset(); /* a scan already running read the old state */
        std::string text = trf("GameCube saves copied onto Slot A: {gc}. Wii saves copied: {wii}.",
                               {{"gc", std::to_string(gc)}, {"wii", std::to_string(wii)}});
        if (skipped)
            text += " " + plural(skipped, "1 was already here and was left as it is.",
                                 "{n} were already here and were left as they are.");
        if (!a.folder && gc == 0)
            text += " " + tr("Slot A is one memory card file, so GameCube saves can't be added to it one by one.");
        open_dialog(DialogKind::Info, tr("Saves from the USB Drive"), text, "");
        return Action::None;
    }
    case kRowDiagnostic:
        sfx(Sound::MenuScroll);
        run_diagnostic();
        return Action::None;
    case kRowSendReport:
    {
        sfx(Sound::MenuScroll);
        std::string usb;
        const std::string where = save_report(usb);
        if (where.empty())
            open_dialog(DialogKind::Info, tr("No report saved"),
                        tr("Porpoise couldn't write the report folder. Copy trace.txt and porpoise/core.log from "
                           "Porpoise's app folder by FTP instead."),
                        "");
        else
            open_dialog(DialogKind::Info, tr("Report saved"),
                        usb.empty() ? trf("It's in {path}. Scan the code beside Report a Bug to join the Discord, "
                                          "and share its files in the bug reports.",
                                          {{"path", where}})
                                    : trf("It's in {path}, and on your USB drive as {usb}. Scan the code beside "
                                          "Report a Bug to join the Discord, and share its files in the bug reports.",
                                          {{"path", where}, {"usb", usb}}),
                        "");
        return Action::None;
    }
    case kRowPickVersion:
    {
        if (updating() || update_phase_ == 1 || version_pick_ < 0 || version_pick_ >= int(version_tags_.size()))
            return Action::None;
        const std::string shown = version_label(version_tags_[std::size_t(version_pick_)],
                                                version_builds_[std::size_t(version_pick_)]);
        int major = 0, minor = 0, patch = 0, beta = 0;
        porpoise::update::tag_version(version_tags_[std::size_t(version_pick_)], major, minor, patch, beta);
        const int order = porpoise::update::compare_versions(major, minor, patch, beta,
                                                             version_builds_[std::size_t(version_pick_)], kVersionMajor,
                                                             kVersionMinor, kVersionPatch, kVersionBeta, kBuild);
        const std::string mb = std::to_string((version_sizes_[std::size_t(version_pick_)] + (1 << 20) - 1) >> 20);
        open_dialog(DialogKind::InstallVersion, trf("Install Porpoise {version}?", {{"version", shown}}),
                    order < 0 ? trf("{mb} MB from GitHub. This goes back to an older Porpoise: your games, saves and "
                                    "settings stay, but save states made by this version may not load in it. "
                                    "Porpoise closes when it's done.",
                                    {{"mb", mb}})
                    : order == 0
                        ? trf("{mb} MB from GitHub. This is the version you have: it is put in place again, which "
                              "repairs a damaged install. Porpoise closes when it's done.",
                              {{"mb", mb}})
                        : trf("{mb} MB from GitHub. Porpoise checks the download, puts the new files in place, and "
                              "then closes so you can open the new one. Your games, saves and settings stay.",
                              {{"mb", mb}}),
                    tr("Install"));
        return Action::None;
    }
    case kRowUseSetup:
        if (screen_ == Screen::GameSettings && game_for_)
        {
            if (setups::apply(row.setup, game_, &game_keys_))
            {
                build_game_settings(); /* waits for Apply, like any change */
                sfx(Sound::LaunchGame);
            }
            return Action::None;
        }
        if (setups::apply(row.setup, draft_))
        {
            build_settings();
            sfx(Sound::LaunchGame);
        }
        return Action::None;

    case kRowRemoveFolder:
        if (row.folder >= 0 && row.folder < int(settings_->folders.size()))
        {
            settings_->folders.erase(settings_->folders.begin() + row.folder);
            settings_->save(settings_path_);
            build_settings();
            return Action::Rescan;
        }
        return Action::None;
    case kRowRescan:
        return Action::Rescan;
    case kRowResetAll:
        open_dialog(DialogKind::ResetAll, tr("Reset all settings?"),
                    tr("Every setting goes back to how Porpoise ships. Your games, folders, covers and saves stay."),
                    tr("Reset"), true);
        return Action::None;
    case kRowReinitialize:
        open_dialog(DialogKind::Reinitialize, tr("Reinitialize Porpoise?"),
                    tr("Every setting, and each game's own settings, go back to how Porpoise ships, and it starts again "
                       "as it did the first time. Your games, folders, memory cards, saves and save states stay."),
                    tr("Reinitialize"), true);
        return Action::None;
    case kRowMapping:
        open_mapping();
        return Action::None;
    case kRowShots:
        open_shots("", Screen::Main);
        return Action::None;
    case kRowWiiGuide:
        open_wii_guide();
        return Action::None;
    case kRowControllers:
        open_controllers(row.folder < 0 ? 1 : row.folder);
        return Action::None;
    case kRowWiiSetup:
        open_wii_setup(screen_ == Screen::GameSettings ? game_for_ : nullptr, false, "");
        return Action::None;
    case kRowDeveloperOff:
        settings_->developer = false;
        settings_->motion_readout = settings_->wii_invert_x = settings_->wii_invert_y = false;
        settings_->save(settings_path_);
        build_settings();
        on_rail_ = true;
        rail_ = std::min(rail_, section_count() - 1);
        sfx(Sound::MovingTab);
        return Action::SettingsChanged;
    case kRowForwarder:
        sfx(Sound::LaunchGame);
        make_forwarder();
        return Action::None;
    case kRowTileArt:
        open_tile_art();
        return Action::None;
    case kRowGcMenu:
        if (porpoise::bios::find_gamecube(data_dir_ + "/bios").empty())
        {
            open_dialog(DialogKind::Info, tr("No GameCube BIOS found"),
                        tr("The GameCube Menu comes from your own console's BIOS: put its IPL.bin in "
                           "/data/porpoise/bios (any name works). Porpoise includes none."),
                        "");
            return Action::None;
        }
        open_system(1);
        return Action::None;
    case kRowWiiMenu:
        if (!wii_menu_installed() && !wii_menu_wad())
        {
            open_dialog(DialogKind::Info, tr("No Wii Menu yet"),
                        tr("Get one with Wii System Update or Wii System Data on this page, or put the Wii Menu's "
                           "WAD from your own console in your games."),
                        "");
            return Action::None;
        }
        open_system(2);
        return Action::None;
    case kRowAddCode:
        open_code();
        return Action::None;
    case kRowSysUpdate:
    {
        /* The installed Wii Menu's region, else the player chooses one. */
        const char menu = wii_menu_installed() ? wii_menu_region() : 0;
        if (menu)
        {
            update_region_ = menu == 'E' ? "EUR" : menu == 'J' ? "JPN" : menu == 'K' ? "KOR" : "USA";
            ask_sys_update();
        }
        else
            open_system(4);
        return Action::None;
    }
    case kRowChannels:
        if (!wii_menu_installed() && !wii_menu_wad())
        {
            open_dialog(DialogKind::Info, tr("No Wii Menu yet"),
                        tr("Channels go in the Wii Menu: get one first with Wii System Update or Wii System Data on "
                           "this page, or put the Wii Menu's WAD from your own console in your games."),
                        "");
            return Action::None;
        }
        open_system(3);
        return Action::None;
    case kRowNandImport:
        if (porpoise::nand::imported(saves_dir_ + "/User/Wii"))
        {
            open_dialog(DialogKind::NandRemove, tr("Remove your Wii System Data?"),
                        tr("Porpoise starts with a fresh Wii again. What's in it now (your Wii's settings, Miis, "
                           "channels and Wii game saves) is moved to saves/User/Wii-removed, not deleted. You can "
                           "import a backup again after."),
                        tr("Remove"), true);
            return Action::None;
        }
        if (!porpoise::nand::find_backup(data_dir_, nand_bin_, nand_keys_))
        {
            open_dialog(DialogKind::Info, tr("No Wii NAND backup found"),
                        tr("Make a NAND backup on your own Wii with BootMii, then copy its nand.bin (and keys.bin, "
                           "if BootMii made one) to /data/porpoise/nand, or to a USB drive's top folder."),
                        "");
            return Action::None;
        }
        open_dialog(DialogKind::NandImport, tr("Import your Wii's system data?"),
                    trf("From {file}: your Wii's settings, Miis, channels, saves and the certificates online "
                        "services check. Saves Porpoise already has for the same games are moved to "
                        "saves/User/Wii-before-NAND, not lost. It takes a minute or two.",
                        {{"file", nand_bin_}}),
                    tr("Import"));
        return Action::None;
    case kRowCoversAgain:
        if (screen_ == Screen::GameSettings && game_for_)
        {
            covers_again_ = {game_for_->id};
            applied_note_ = tr("Downloading this game's art again\xE2\x80\xA6");
            applied_note_time_ = time_;
            sfx(Sound::LaunchGame);
            return Action::CoversAgain;
        }
        open_dialog(DialogKind::CoversAgain, tr("Download every cover again?"),
                    tr("Porpoise gets each game's cover, box and disc art from GameTDB.com again, in place of what "
                       "it has, your own art too. It runs in the background."),
                    tr("Download"));
        return Action::None;
    case kRowResetGame:
        open_dialog(DialogKind::ResetGame, tr("Reset this game's settings?"),
                    tr("It forgets its own settings and follows your settings again."), tr("Reset"), true);
        return Action::None;
    default:
        return Action::None;
    }
}

App::Action App::update_settings(bool up, bool down, bool left, bool right)
{
    const bool game = screen_ == Screen::GameSettings;
    const int sections = section_count();
    if (on_rail_)
    {
        if (up && rail_ > 0)
        {
            --rail_;
            sfx(Sound::MenuScroll);
        }
        if (down && rail_ + 1 < sections)
        {
            ++rail_;
            sfx(Sound::MenuScroll);
        }
        if (right || pressed(BtnCross))
        {
            const int first = first_row_of(section_name(rail_));
            if (first >= 0)
            {
                settings_row_ = first;
                on_rail_ = false;
                sfx(Sound::MenuScroll);
            }
        }
        if (pressed(BtnSquare) && !pending_keys().empty())
            return apply_pending();
        if (pressed(BtnCircle))
        {
            if (game)
            {
                leave_game_settings_ = true;
                if (!ask_before_leaving())
                    finish_leaving();
            }
            else
                set_tab(int(Tab::Library), -1); /* asks first when changes wait (set_tab) */
        }
        return Action::None;
    }
    /* Square: Apply, from anywhere in Settings. */
    if (pressed(BtnSquare) && !pending_keys().empty())
        return apply_pending();

    /* Inside a section: up and down stay in it. */
    const std::string section = rows_[std::size_t(settings_row_)].section;
    auto step = [&](int dir) {
        int r = settings_row_ + dir;
        if (r >= 0 && r < int(rows_.size()) && !rows_[std::size_t(r)].header && rows_[std::size_t(r)].section == section)
        {
            settings_row_ = r;
            sfx(Sound::MenuScroll);
        }
    };
    if (up)
        step(-1);
    if (down)
        step(+1);
    if (pressed(BtnCircle))
    {
        on_rail_ = true;
        sfx(Sound::MenuScroll);
        return Action::None;
    }
    const SettingRow &row = rows_[std::size_t(settings_row_)];
    if (row.action == kRowAllSettings)
    {
        /* Show All Settings: at once, and kept (no Apply). */
        if (pressed(BtnCross) || (left && row.toggle) || (right && !row.toggle))
        {
            settings_->all_settings = !settings_->all_settings;
            draft_.all_settings = base_.all_settings = settings_->all_settings;
            settings_->save(settings_path_);
            if (game)
                build_game_settings();
            else
                build_settings();
            settings_row_ = 1;
            rail_ = 0;
            sfx(Sound::MenuScroll);
        }
        return Action::None;
    }
    if (row.toggle >= 0)
    {
        /* A recommendation's switch: Cross, or left / right toward off / on. */
        if (pressed(BtnCross) || (left && row.toggle) || (right && !row.toggle))
            toggle_recommended(row.rec);
        return Action::None;
    }
    if (row.action == kRowPickVersion && (left || right) && !row.values.empty())
    {
        version_pick_ = std::clamp(version_pick_ + (right ? 1 : -1), 0, int(row.values.size()) - 1);
        sfx(Sound::MenuScroll);
        return Action::None;
    }
    if (row.action == kRowMoveData && (left || right) && !row.values.empty())
    {
        move_choice_ = std::clamp(move_choice_ + (right ? 1 : -1), 0, int(row.values.size()) - 1);
        sfx(Sound::MenuScroll);
        return Action::None;
    }
    if (row.action)
        return pressed(BtnCross) ? activate_row(row) : Action::None;
    if (!row.bool_value && !row.int_value)
        return Action::None;
    /* A change waits for Apply (Square, or when leaving Settings), unless it
     * is one you see (applies_at_once). */
    live_changed_ = false;
    if (left)
    {
        change_setting(-1);
        sfx(Sound::MenuScroll);
    }
    if (right || pressed(BtnCross))
    {
        change_setting(+1);
        sfx(Sound::MenuScroll);
    }
    return live_changed_ ? Action::LiveSettings : Action::None;
}

/* ---- Apply ------------------------------------------------------------------------------- */

namespace
{
/* Settings that take effect only when Porpoise starts again. */
bool needs_restart(const std::string &key)
{
    return key == "output_res" || key == "stay_sandboxed" || key == "perf_profile";
}
} // namespace

std::vector<std::string> App::pending_keys() const
{
    if (screen_ == Screen::GameSettings && game_for_)
    {
        std::vector<std::string> keys = game_.changed_keys(game_base_);
        auto add = [&](const std::vector<std::string> &a, const std::vector<std::string> &b) {
            for (const std::string &k : a)
                if (std::find(b.begin(), b.end(), k) == b.end() && std::find(keys.begin(), keys.end(), k) == keys.end())
                    keys.push_back(k);
        };
        add(game_keys_, game_keys_base_);
        add(game_keys_base_, game_keys_);
        return keys;
    }
    return draft_.changed_keys(base_);
}

bool App::row_pending(const SettingRow &row, const std::vector<std::string> &pending) const
{
    if (row.header || row.action || pending.empty())
        return false;
    auto has = [&](const std::string &k) { return std::find(pending.begin(), pending.end(), k) != pending.end(); };
    if (row.key == "cheat")
        return row.folder >= 0 && row.folder < int(cheats_.size()) &&
               (has(cheat_key(cheats_[std::size_t(row.folder)], true)) ||
                has(cheat_key(cheats_[std::size_t(row.folder)], false)));
    if (row.key == "audio_preset")
        return has("audio_pull") || has("audio_buffer") || has("audio_fill") || has("audio_stretch");
    return !row.key.empty() && has(row.key);
}

App::Action App::apply_pending()
{
    if (screen_ == Screen::GameSettings && game_for_)
    {
        if (pending_keys().empty())
            return Action::None;
        mkdir((data_dir_ + "/game-settings").c_str(), 0777);
        game_.save_keys(game_settings_path(*game_for_), game_keys_);
        game_base_ = game_;
        game_keys_base_ = game_keys_;
        const int row = settings_row_;
        build_game_settings();
        settings_row_ = std::clamp(row, 0, int(rows_.size()) - 1);
        applied_note_ = tr("Applied: from the next time this game starts.");
        applied_note_time_ = time_;
        sfx(Sound::LaunchGame);
        return Action::None;
    }
    const std::vector<std::string> pending = draft_.changed_keys(base_);
    if (pending.empty())
        return Action::None;
    auto has = [&](const char *k) { return std::find(pending.begin(), pending.end(), k) != pending.end(); };
    const int was_theme = settings_->ui_theme;
    settings_->copy_keys(draft_, pending);
    if (has("ui_theme"))
    {
        /* The theme's own view (look_changed), keeping the colours and font
         * chosen with it. */
        const int palette = settings_->ui_palette, font = settings_->ui_font;
        look_changed(was_theme);
        settings_->ui_palette = palette;
        settings_->ui_font = font;
        apply_look();
    }
    if (has("ui_language"))
        apply_language(settings_->ui_language, data_dir_ + "/lang");
    settings_->save(settings_path_);
    settings_->write_core_options(options_path_);
    std::string restart;
    for (const SettingRow &r : rows_)
    {
        if (r.header || r.key.empty() || std::find(pending.begin(), pending.end(), r.key) == pending.end())
            continue;
        if (r.rescan)
            rescan_after_apply_ = true;
        if (needs_restart(r.key) && restart.find(r.label) == std::string::npos)
            restart += "\n\xE2\x80\xA2 " + r.label;
    }
    /* 4.0: a change made with every setting shown, applied with some hidden. */
    for (const std::string &k : pending)
    {
        if (k == "auto_search")
            rescan_after_apply_ = true;
        const bool shown = std::any_of(rows_.begin(), rows_.end(), [&](const SettingRow &r) { return r.key == k; });
        if (!shown && needs_restart(k))
            restart += "\n\xE2\x80\xA2 " + tr(k == "output_res" ? "Output Resolution"
                                                 : k == "stay_sandboxed" ? "Stay in the Sandbox"
                                                                         : "Performance Report");
    }
    base_ = draft_ = *settings_;
    const int row = settings_row_;
    build_settings();
    settings_row_ = std::clamp(row, 0, int(rows_.size()) - 1);
    sfx(Sound::LaunchGame);
    if (!restart.empty())
    {
        open_dialog(DialogKind::RestartPorpoise, tr("Restart Porpoise to finish?"),
                    tr("These take effect when Porpoise starts again:") + restart, tr("Restart Now"));
        dialog_.no = tr("Later");
        dialog_.choice = 1;
    }
    else
    {
        applied_note_ = tr("Settings applied.");
        applied_note_time_ = time_;
    }
    return Action::SettingsChanged;
}

void App::discard_pending()
{
    if (screen_ == Screen::GameSettings && game_for_)
    {
        game_ = game_base_;
        game_keys_ = game_keys_base_;
        build_game_settings();
        return;
    }
    draft_ = base_;
    build_settings();
}

bool App::ask_before_leaving()
{
    const std::size_t n = pending_keys().size();
    if (n == 0)
        return false;
    open_dialog(DialogKind::ApplyChanges, tr("Apply your changes?"),
                plural((long long)n, "You changed 1 setting. Apply it, or leave it as it was? Circle keeps editing.",
                       "You changed {n} settings. Apply them, or leave them as they were? Circle keeps editing."),
                tr("Apply"));
    dialog_.no = tr("Discard");
    dialog_.choice = 1;
    return true;
}

void App::finish_leaving()
{
    if (leave_game_settings_)
    {
        leave_game_settings_ = false;
        close_game_settings();
    }
    else if (leave_tab_ >= 0)
    {
        const int tab = leave_tab_;
        leave_tab_ = -1;
        set_tab(tab, leave_dir_);
    }
}

/* ---- drawing ------------------------------------------------------------------------------- */

void App::draw_settings()
{
    Gfx &g = *g_;
    const bool game = screen_ == Screen::GameSettings;
    const std::string current = on_rail_ ? section_name(rail_) : rows_[std::size_t(settings_row_)].section;

    /* Section rail. */
    const float rx = 90, ry = 136, rw = 380, rh = 800;
    g.panel(rx, ry, rw, rh, rgba(0x0A1236, 0.62f), 0.85f, kR, rgba(0x3D4F9E, 0.9f), 1.6f, 0, 0.10f);
    float sy = ry + 18;
    if (game && game_for_)
    {
        g.text_mid(Font::SemiBold, ts(22), rx + 30, sy + 18, kLavender, Align::Left, tr("GAME SETTINGS"), 2.0f);
        g.text_mid(Font::Bold, ts(28), rx + 30, sy + 56, kWhite, Align::Left,
                   fit(g, Font::Bold, ts(28), game_for_->title, rw - 60));
        sy += 92;
    }

    /* The sections, a row each at an even pitch. More than the rail holds
     * (3.0 has more sections, and a game's own settings put its name on top):
     * the list scrolls to keep the chosen one in view, with a fade and an
     * arrow at an edge that has more past it. */
    int sections = 0, here_index = 0;
    for (const SettingRow &r : rows_)
        if (r.header)
        {
            if (r.section == current)
                here_index = sections;
            ++sections;
        }
    const float top = sy, room = ry + rh - 14 - top;
    const float pitch = 64.0f, item_h = pitch - 8;
    const int fits = std::max(1, int(room / pitch));
    /* The highlights glide from where they were to where they go. */
    const float glide_dt = float(std::clamp(time_ - glide_time_, 0.0, 0.1));
    glide_time_ = time_;
    auto glide = [&](float &at, float target, float jump) {
        if (at < 0 || settings_->reduced_motion || std::fabs(target - at) > jump)
            at = target;
        else
            at += (target - at) * std::min(1.0f, glide_dt * 16.0f);
        return at;
    };
    /* The first row shown: the chosen one kept a row from either edge. */
    float first_target = 0;
    if (sections > fits)
    {
        const float shown_first = rail_scroll_ < 0 ? 0.0f : std::round(rail_scroll_);
        float f = shown_first;
        if (here_index < f + 1)
            f = float(here_index - 1);
        else if (here_index > f + float(fits) - 2)
            f = float(here_index - fits + 2);
        first_target = std::clamp(f, 0.0f, float(sections - fits));
    }
    const float rail_first = glide(rail_scroll_, first_target, 1000);
    const float bottom = top + float(fits) * pitch;
    auto fade_at = [&](float y) {
        /* Rows leaving the rail fade out over half a row. */
        const float a = std::min((y + item_h - top) / (pitch * 0.5f), (bottom - y) / (pitch * 0.5f));
        return std::clamp(a, 0.0f, 1.0f);
    };
    {
        const float hy = top + (float(here_index) - rail_first) * pitch;
        const float y = glide(rail_glide_, hy, 2000);
        const float a = fade_at(y);
        if (a > 0.01f)
        {
            if (on_rail_)
                g.panel(rx + 14, y, rw - 28, item_h, rgba(0x1F63F0, a), 0.62f, kR, rgba(0x7FD9FF, a), 1.6f, 6, 0.35f);
            else
                g.panel(rx + 14, y, rw - 28, item_h, rgba(0x1F63F0, 0.25f * a), 0.8f, kR, rgba(0x7FD9FF, 0.55f * a),
                        1.4f);
        }
    }
    {
        int k = 0;
        for (const SettingRow &r : rows_)
        {
            if (!r.header)
                continue;
            const float y = top + (float(k++) - rail_first) * pitch;
            const float a = fade_at(y);
            if (a <= 0.01f)
                continue;
            const bool here = r.section == current;
            const Font f = here ? Font::Bold : Font::SemiBold;
            const float size = ts(29);
            g.text_mid(f, size, rx + 46, y + item_h * 0.5f, with_alpha(here ? kWhite : kSoft, a), Align::Left,
                       fit(g, f, size, tr(r.section), rw - 46 - (here && on_rail_ ? 70 : 30)));
            if (here && on_rail_)
                g.glyph(Glyph::Arrow, rx + rw - 46, y + item_h * 0.5f, 20, with_alpha(kWhite, a), kPi * 0.5f);
        }
        /* More above or below. */
        if (rail_first > 0.05f)
            g.glyph(Glyph::Arrow, rx + rw * 0.5f, top - 4, 16, with_alpha(kLavender, std::min(1.0f, rail_first)), 0);
        if (rail_first < float(sections - fits) - 0.05f)
            g.glyph(Glyph::Arrow, rx + rw * 0.5f, bottom + 2, 16,
                    with_alpha(kLavender, std::min(1.0f, float(sections - fits) - rail_first)), kPi);
    }

    /* The section's rows. */
    const float px = 500, py = 136, pw = 1330, ph = 800;
    g.panel(px, py, pw, ph, rgba(0x0F1F63, 0.62f), 0.75f, kR, rgba(0x4C6FD8, 0.9f), 1.8f, 0, 0.12f);
    const float title_w = g.text_mid(starcube() ? Font::ExtraBold : Font::Bold, ts(46), px + 50, py + 64, kWhite,
                                     Align::Left, tr(current));
    if (current == "Auto Boost")
    {
        /* A beta: the list grows as games are tested. */
        const std::string beta = tr("Beta");
        const float bw = g.measure(Font::Bold, ts(20), beta) + 28;
        g.panel(px + 50 + title_w + 18, py + 64 - 17, bw, 34, rgba(0x6A3FD8, 0.35f), 1, 17, rgba(0xB89BFF), 1.6f);
        g.text_mid(Font::Bold, ts(20), px + 50 + title_w + 18 + bw * 0.5f, py + 64, rgba(0xE4DAFF), Align::Center, beta);
    }
    std::string subtitle = tr("Changes apply the next time a game starts");
    if (current == "About")
        subtitle = tr("Porpoise for PS5") + " \xE2\x80\xA2 " + tr("created by @elripalda") + " \xE2\x80\xA2 ripalda.dev";
    else if (current == "Games")
        subtitle = tr("Where Porpoise looks for your games");
    else if (current == "Downloads")
        subtitle = tr("Covers and game info from GameTDB.com");
    else if (current == "Storage")
        subtitle = tr("Where Porpoise keeps its things, and the app sandbox");
    else if (current == "Textures" && !game)
        subtitle = tr("HD texture packs, and the games they're for");
    else if (current == "Online" && !game)
        subtitle = tr("Wii games online \xE2\x80\xA2 the Online Hub has everything");
    else if (current == "This Game")
        subtitle = tr("Values in blue are this game's own");
    else if (current == "Interface")
        subtitle = tr("How Porpoise looks and reads");
    else if (current == "Accessibility")
        subtitle = tr("Easier to see, read and follow");
    else if (current == "Screenshots")
        subtitle = tr("Pictures of your games, taken while you play");
    else if (current == "Developer")
        subtitle = tr("For tuning the Wii Remote; nothing here is needed to play");
    else if (current == "Home Screen")
        subtitle = tr("This game's own tile on the PS5's home screen");
    else if (current == "Auto Boost")
        subtitle = tr("The best settings for this game on PS5, tested \xE2\x80\xA2 from the next time it starts");
    else if (game)
        subtitle = tr("For this game only \xE2\x80\xA2 values in blue are its own");
    const std::vector<std::string> pending = pending_keys();
    const Color kPending = rgba(0xFFC857);
    if (!pending.empty())
        subtitle = plural((long long)pending.size(), "1 change waiting \xE2\x80\xA2 Square applies it",
                          "{n} changes waiting \xE2\x80\xA2 Square applies them");
    g.text_mid(Font::Regular, ts(26), px + 50, py + 112, pending.empty() ? kLavender : kPending, Align::Left,
               subtitle);
    if (!pending.empty())
    {
        /* The Apply button, top right of the panel (left of a code shown there). */
        const std::string label = tr("Apply");
        const float lw = g.measure(Font::Bold, ts(26), label);
        const float bw = lw + 84, bh = 52;
        const bool code_shown = !on_rail_ && settings_row_ >= 0 && settings_row_ < int(rows_.size()) &&
                                (rows_[std::size_t(settings_row_)].label == tr("Report a Bug") ||
                                 rows_[std::size_t(settings_row_)].key == "discord" ||
                                 rows_[std::size_t(settings_row_)].key == "creator");
        const float bx = px + pw - bw - (code_shown ? 230 : 50), by = py + 64 - bh * 0.5f;
        const float pulse = settings_->reduced_motion ? 0.0f : 0.5f + 0.5f * std::sin(float(time_) * 3.0f);
        g.panel(bx, by, bw, bh, rgba(0x5A3A00, 0.55f + 0.2f * pulse), 0.9f, bh * 0.5f, kPending, 2.0f, 6, 0.2f);
        g.glyph(Glyph::Square, bx + 32, by + bh * 0.5f, 30, kWhite);
        g.text_mid(Font::Bold, ts(26), bx + 58, by + bh * 0.5f, kWhite, Align::Left, label);
    }

    const float row_h = 80, row_x = px + 26, row_w = pw - 52;
    float y = py + 158;
    std::vector<int> section_rows;
    for (int i = 0; i < int(rows_.size()); ++i)
        if (!rows_[std::size_t(i)].header && rows_[std::size_t(i)].section == current)
            section_rows.push_back(i);
    /* A game's cheats and patches: cards of their own (draw_cheat_cards). */
    if (current == "Cheats and Patches" && !cheats_.empty())
        draw_cheat_cards(section_rows, px, py, pw, ph);
    else
    {
        /* Six rows fit; a longer section scrolls with the focus. */
        constexpr std::size_t kVisible = 6;
        std::size_t first = 0;
        for (std::size_t k = 0; k < section_rows.size(); ++k)
            if (!on_rail_ && section_rows[k] == settings_row_ && k >= kVisible)
                first = k - kVisible + 1;
        if (on_rail_)
            row_glide_ = -1;
        else
            for (std::size_t k = first; k < section_rows.size() && k < first + kVisible; ++k)
                if (section_rows[k] == settings_row_)
                {
                    const float hy = glide(row_glide_, y + float(k - first) * row_h, row_h * 3.5f);
                    g.panel(row_x, hy + 4, row_w, row_h - 8, rgba(0x1D45B8, 0.88f), 0.7f, kR, kIcy, 2.4f, 10, 0.18f);
                }
        for (std::size_t k = first; k < section_rows.size() && k < first + kVisible; ++k)
        {
            const int i = section_rows[k];
            const SettingRow &r = rows_[std::size_t(i)];
            const bool on = !on_rail_ && i == settings_row_;
            const bool own = game && !r.key.empty() &&
                             std::find(game_keys_.begin(), game_keys_.end(), r.key) != game_keys_.end();
            const bool waiting = row_pending(r, pending);
            const float cy = y + row_h * 0.5f;
            if (waiting)
                g.panel(row_x + 8, cy - 7, 14, 14, kPending, 1, 7); /* changed, not applied yet */
            if (on)
                ; /* the gliding highlight, above */
            else if (k + 1 < section_rows.size() && k + 1 < first + kVisible && (on_rail_ || section_rows[k + 1] != settings_row_))
                g.panel(row_x + 24, y + row_h - 1, row_w - 48, 1.5f, rgba(0x3D4F9E, 0.55f), 1, 0);
            {
                const std::string shown = fit(g, Font::SemiBold, ts(30), r.label, row_w - (r.beta ? 540 : 420));
                g.text_mid(Font::SemiBold, ts(30), row_x + 28, cy, on ? kWhite : (on_rail_ ? with_alpha(kSoft, 0.8f) : kSoft),
                           Align::Left, shown);
                if (r.beta)
                {
                    /* The BETA badge: new, and still being tuned. */
                    const float bx = row_x + 28 + g.measure(Font::SemiBold, ts(30), shown) + 14;
                    const char *badge = r.alpha ? "ALPHA" : "BETA";
                    const float bw = g.measure(Font::Bold, ts(18), badge) + 22, bh = 30;
                    g.panel(bx, cy - bh * 0.5f, bw, bh, rgba(r.alpha ? 0xFF7A8A : 0xFFB347, on ? 0.95f : 0.8f), 1.0f,
                            bh * 0.5f);
                    g.text_mid(Font::Bold, ts(18), bx + bw * 0.5f, cy, rgba(0x2A1600), Align::Center, badge);
                }
            }

            std::string value;
            int vi = 0;
            const int count = int(r.values.size());
            if (r.bool_value)
                vi = *r.bool_value ? 1 : 0;
            else if (r.int_value && !r.order.empty())
                vi = int(std::find(r.order.begin(), r.order.end(), *r.int_value) - r.order.begin());
            else if (r.int_value)
                vi = *r.int_value - r.min;
            if (vi >= 0 && vi < count)
                value = r.values[std::size_t(vi)];
            if (r.key == "players")
            {
                const int n = porpoise::pad::connected_count();
                value = n <= 1 ? tr("1 player") : trf("{n} players", {{"n", std::to_string(n)}});
            }
            const float right = row_x + row_w - 24;
            const Color value_c = waiting ? kPending : own ? kCyan : (on ? kWhite : kSoft);
            if (r.action == kRowUpdate)
                value = update_row_value();
            if (r.action && r.toggle < 0) /* (a switch with an action, Show All Settings: drawn as a switch) */
            {
                const bool danger = r.action == kRowResetAll || r.action == kRowResetGame || r.action == kRowReinitialize;
                const float vw = g.measure(Font::Bold, ts(28), value);
                const float cw = std::max(250.0f, vw + 110), ch = 50, cx = right - cw;
                g.panel(cx, cy - ch * 0.5f, cw, ch, on ? (danger ? rgba(0xB0305A, 0.85f) : rgba(0x1F63F0, 0.85f))
                                                      : rgba(0x07102E, 0.40f),
                        0.7f, kR, on ? (danger ? kDanger : rgba(0x8BD9FF)) : rgba(0x3D5AB0, 0.75f), on ? 1.8f : 1.4f, 0,
                        on ? 0.25f : 0.0f);
                if (on)
                {
                    const float group = 30 + 10 + vw;
                    g.glyph(Glyph::Cross, cx + cw * 0.5f - group * 0.5f + 15, cy, 30, kWhite);
                    g.text_mid(Font::Bold, ts(28), cx + cw * 0.5f - group * 0.5f + 40, cy, kWhite, Align::Left, value);
                }
                else
                    g.text_mid(Font::Bold, ts(28), cx + cw * 0.5f, cy, kSoft, Align::Center, value);
            }
            else if (r.toggle >= 0)
            {
                /* A switch: a green track with the knob right when on. */
                const bool lit = r.toggle == 1;
                const float tw = 92, th = 44, tx = right - tw;
                g.panel(tx, cy - th * 0.5f, tw, th, lit ? rgba(0x2FB574, 0.95f) : rgba(0x07102E, 0.6f), 0.85f, th * 0.5f,
                        lit ? rgba(0xBDF5D8) : (on ? rgba(0x8BD9FF) : rgba(0x3D5AB0, 0.85f)), on ? 2.0f : 1.4f,
                        lit ? 8 : 0);
                const float kx = lit ? tx + tw - th * 0.5f : tx + th * 0.5f;
                g.blob(kx, cy, th * 0.9f, th * 0.9f, rgba(0xFFFFFF, lit ? 0.35f : 0.15f));
                g.panel(kx - th * 0.38f, cy - th * 0.38f, th * 0.76f, th * 0.76f, lit ? kWhite : rgba(0xA9B8E8), 1,
                        th * 0.38f);
                const float ow = g.text_mid(Font::Bold, ts(26), tx - 18, cy, lit ? rgba(0x7CF0B4) : kLavender,
                                            Align::Right, lit ? tr("On") : tr("Off"));
                if (!r.tag.empty())
                {
                    /* Where it comes from, in a small pill. */
                    const bool dolphin = r.rec >= 0 && rec_rows_[std::size_t(r.rec)].kind == RecRow::DolphinFix;
                    const float pw2 = g.measure(Font::SemiBold, ts(19), r.tag) + 26, px2 = tx - 18 - ow - 22 - pw2;
                    g.panel(px2, cy - 15, pw2, 30, dolphin ? rgba(0x2A2F6E, 0.8f) : rgba(0x0E3A6E, 0.8f), 1, 15,
                            dolphin ? rgba(0xA9A0FF, 0.8f) : with_alpha(kCyan, 0.8f), 1.2f);
                    g.text_mid(Font::SemiBold, ts(19), px2 + pw2 * 0.5f, cy, dolphin ? rgba(0xCFC8FF) : kCyan,
                               Align::Center, r.tag);
                }
            }
            else if (!r.bool_value && !r.int_value)
            {
                const float vw = g.text_mid(Font::SemiBold, ts(28), right, cy, on ? kWhite : kSoft, Align::Right, value);
                if (r.key == "creator")
                {
                    /* Ruben's mark beside his name. */
                    if (!mark_tried_)
                    {
                        mark_tried_ = true;
                        ripalda_ = g.texture_file(g.asset_dir() + "/brand/ripalda.png");
                    }
                    if (ripalda_)
                        g.image(ripalda_, right - vw - 58, cy - 22, 44, 44, on ? kWhite : kSoft);
                }
            }
            else
            {
                const float vw = g.measure(Font::Bold, ts(28), value);
                /* Room for the arrows, and for the flag beside a language's name. */
                const float flag_w = r.key == "ui_language" ? 54.0f : 0.0f;
                const float cw = std::max(270.0f, vw + 110 + flag_w), ch = 50, cx = right - cw;
                g.panel(cx, cy - ch * 0.5f, cw, ch, rgba(0x07102E, on ? 0.55f : 0.40f), 1, kR,
                        own ? with_alpha(kCyan, 0.9f) : (on ? rgba(0x8BD9FF) : rgba(0x3D5AB0, 0.75f)), on ? 1.8f : 1.4f);
                if (r.key == "ui_language" && r.int_value)
                {
                    /* The language's flag beside its name; System shows the one it follows. */
                    if (!flags_tried_)
                    {
                        flags_tried_ = true;
                        flags_ = g.texture_file(g.asset_dir() + "/ui/flags.png", 2048);
                    }
                    const int lang = *r.int_value > 0 ? *r.int_value : int(language()) + 1;
                    const float vw = g.measure(Font::Bold, ts(28), value), fw = 42, fh = 28, gap = 12;
                    const float x0 = cx + (cw - (fw + gap + vw)) * 0.5f;
                    if (flags_ && lang >= 1 && lang <= kLanguages)
                    {
                        const float uv[4] = {float(lang - 1) / float(kLanguages), 0, float(lang) / float(kLanguages), 1};
                        g.image_part(flags_, x0, cy - fh * 0.5f, fw, fh, uv);
                    }
                    g.text_mid(Font::Bold, ts(28), x0 + fw + gap, cy, value_c, Align::Left, value);
                }
                else
                    g.text_mid(Font::Bold, ts(28), cx + cw * 0.5f, cy, value_c, Align::Center, value);
                if (on)
                {
                    const bool at_min = r.int_value && vi <= 0;
                    const bool at_max = r.int_value && vi >= count - 1;
                    g.glyph(Glyph::Arrow, cx + 26, cy, 18, with_alpha(kCyan, at_min ? 0.3f : 1.0f), -kPi * 0.5f);
                    g.glyph(Glyph::Arrow, cx + cw - 26, cy, 18, with_alpha(kCyan, at_max ? 0.3f : 1.0f), kPi * 0.5f);
                }
            }
            y += row_h;
        }
        if (first > 0)
            g.glyph(Glyph::Arrow, px + pw - 30, py + 170, 18, rgba(0x58B8FF), 0);
        if (first + kVisible < section_rows.size())
            g.glyph(Glyph::Arrow, px + pw - 30, py + ph - 120, 18, rgba(0x58B8FF), kPi);
    }

    /* What the focused row does. */
    std::string help;
    if (on_rail_)
        help = tr("Choose a section with up and down, then press Right or Cross to go into it.");
    else
        help = rows_[std::size_t(settings_row_)].help;
    if (!on_rail_ &&
        (rows_[std::size_t(settings_row_)].action == kRowUpdate || rows_[std::size_t(settings_row_)].key == "creator") &&
        !update_note_.empty() &&
        time_ - update_note_time_ < 8.0)
        help = update_note_;
    if (!applied_note_.empty() && time_ - applied_note_time_ < 4.0)
        help = applied_note_;
    g.panel(px + 50, py + ph - 100, pw - 100, 1.5f, rgba(0x3D4F9E, 0.7f), 1, 0);
    {
        /* One line, or two smaller ones when it is long. */
        const auto one = wrap(g, Font::Regular, ts(26), help, pw - 100, 2);
        if (one.size() <= 1)
            g.text_mid(Font::Regular, ts(26), px + 50, py + ph - 50, kLavender, Align::Left, help);
        else
        {
            const auto two = wrap(g, Font::Regular, ts(23), help, pw - 100, 2);
            for (std::size_t i = 0; i < two.size(); ++i)
                g.text_mid(Font::Regular, ts(23), px + 50, py + ph - 66 + float(i) * 32, kLavender, Align::Left,
                           two[i]);
        }
    }
    /* About > Discord and Report a bug: a code to the Discord, for a phone. */
    if (!on_rail_ && settings_row_ >= 0 && settings_row_ < int(rows_.size()) &&
        (rows_[std::size_t(settings_row_)].label == tr("Report a Bug") ||
         rows_[std::size_t(settings_row_)].key == "discord"))
    {
        if (!qr_tried_)
        {
            qr_tried_ = true;
            qr_ = g.texture_file(g.asset_dir() + "/ui/report-qr.png");
        }
        if (qr_)
        {
            const float qs = 118, qx = px + pw - qs - 70, qy = py + 22; /* above the first row */
            g.panel(qx - 8, qy - 8, qs + 16, qs + 16, rgba(0x07102E, 0.5f), 1, kR, with_alpha(kCyan, 0.9f), 1.6f, 8);
            g.image(qr_, qx, qy, qs, qs, kWhite, 10);
        }
    }
    /* About > Created by: Ruben's mark, large, where the code shows. */
    if (!on_rail_ && settings_row_ >= 0 && settings_row_ < int(rows_.size()) &&
        rows_[std::size_t(settings_row_)].key == "creator" && ripalda_)
    {
        const float qs = 118, qx = px + pw - qs - 70, qy = py + 22;
        g.panel(qx - 8, qy - 8, qs + 16, qs + 16, rgba(0x07102E, 0.7f), 1, kR, with_alpha(kCyan, 0.9f), 1.6f, 8);
        g.image(ripalda_, qx + 12, qy + 12, qs - 24, qs - 24, kWhite);
    }

    if (on_rail_)
    {
        draw_prompts({{Glyph::DPad, "Sections"}, {Glyph::Cross, "Open"}, {Glyph::Circle, game ? "Details" : "Back"}},
                     {}, "");
        return;
    }
    const SettingRow &focus = rows_[std::size_t(settings_row_)];
    const bool info = !focus.bool_value && !focus.int_value;
    if (focus.action == kRowAddFolder)
        draw_prompts({{Glyph::Cross, "Choose a Folder"}, {Glyph::Circle, "Sections"}}, {}, "");
    else if (focus.action == kRowRemoveFolder)
        draw_prompts({{Glyph::Cross, "Remove"}, {Glyph::Circle, "Sections"}}, {}, "");
    else if (focus.action == kRowAddShare)
        draw_prompts({{Glyph::Cross, "Add"}, {Glyph::Circle, "Sections"}}, {}, "");
    else if (focus.action == kRowEditShare)
        draw_prompts({{Glyph::Cross, "Change"}, {Glyph::Circle, "Sections"}}, {}, "");
    else if (focus.action == kRowRescan)
        draw_prompts({{Glyph::Cross, "Search"}, {Glyph::Circle, "Sections"}}, {}, "");
    else if (focus.action == kRowMapping)
        draw_prompts({{Glyph::Cross, "Customize"}, {Glyph::Circle, "Sections"}}, {}, "");
    else if (focus.action == kRowShots)
        draw_prompts({{Glyph::Cross, "Open"}, {Glyph::Circle, "Sections"}}, {}, "");
    else if (focus.action == kRowControllers)
        draw_prompts({{Glyph::Cross, "Open"}, {Glyph::Circle, "Sections"}}, {}, "");
    else if (focus.action == kRowWiiGuide)
        draw_prompts({{Glyph::Cross, "Show"}, {Glyph::Circle, "Sections"}}, {}, "");
    else if (focus.action == kRowWiiSetup)
        draw_prompts({{Glyph::Cross, "Start"}, {Glyph::Circle, "Sections"}}, {}, "");
    else if (focus.action == kRowOnlineDns)
        draw_prompts({{Glyph::Cross, "Change"}, {Glyph::Circle, "Sections"}}, {}, "");
    else if (focus.action == kRowMoveData)
        draw_prompts({{Glyph::DPad, "Choose"}, {Glyph::Cross, "Move"}, {Glyph::Circle, "Sections"}}, {}, "");
    else if (focus.action == kRowUpdate)
        draw_prompts({{Glyph::Cross, update_available() ? "Install" : "Check Now"}, {Glyph::Circle, "Sections"}}, {},
                     "");
    else if (focus.action == kRowUseSetup)
        draw_prompts({{Glyph::Cross, "Use"}, {Glyph::Circle, "Sections"}}, {}, "");
    else if (focus.toggle >= 0)
        draw_prompts({{Glyph::Cross, focus.toggle ? "Turn Off" : "Turn On"}, {Glyph::Circle, "Sections"}}, {}, "");
    else if (focus.action)
    {
        /* The button says what the row's own button says ("Check…" → Check). */
        std::string verb = focus.values.empty() ? std::string("Select") : focus.values.front();
        const std::string dots = "\xE2\x80\xA6";
        if (verb.size() >= dots.size() && verb.compare(verb.size() - dots.size(), dots.size(), dots) == 0)
            verb.resize(verb.size() - dots.size());
        if (verb.empty())
            verb = "Select";
        draw_prompts({{Glyph::Cross, verb}, {Glyph::Circle, "Sections"}}, {}, "");
    }
    else if (info)
        draw_prompts({{Glyph::DPad, "Browse"}, {Glyph::Circle, "Sections"}}, {}, "");
    else
        draw_prompts({{Glyph::DPad, "Change"}, {Glyph::Circle, "Sections"}}, {}, "");
}

/* ---- game folders ------------------------------------------------------------------------ */

namespace
{
bool is_dir(const std::string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string lower_copy(std::string s)
{
    for (char &c : s)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return s;
}

/* Whether a folder has anything in it (an empty USB port's mount point
 * doesn't). */
bool has_entries(const std::string &path)
{
    bool any = false;
    if (DIR *d = opendir(path.c_str()))
    {
        while (dirent *e = readdir(d))
            if (e->d_name[0] != '.')
            {
                any = true;
                break;
            }
        closedir(d);
    }
    return any;
}

std::string size_text(long long bytes)
{
    char buf[32];
    if (bytes >= (1LL << 30))
        std::snprintf(buf, sizeof buf, "%.1f GB", double(bytes) / double(1LL << 30));
    else
        std::snprintf(buf, sizeof buf, "%lld MB", (bytes + (1LL << 20) - 1) >> 20);
    return buf;
}

/* ---- copying a game to the console ---- */

struct GameCopy
{
    std::string from, to;
    std::atomic<long long> done{0}, total{0};
    std::atomic<int> state{0}; /* 0 idle, 1 copying, 2 done, 3 failed, 4 stopped */
    std::atomic<bool> stop{false};
    std::string error;
    pthread_t thread{};
    bool joinable = false;
};
GameCopy g_copy;

void *copy_worker(void *)
{
    const std::string part = g_copy.to + ".part";
    std::FILE *in = std::fopen(g_copy.from.c_str(), "rb");
    std::FILE *out = in ? std::fopen(part.c_str(), "wb") : nullptr;
    bool ok = in && out;
    std::vector<char> buf(std::size_t(4) << 20);
    while (ok && !g_copy.stop.load())
    {
        const std::size_t n = std::fread(buf.data(), 1, buf.size(), in);
        if (n == 0)
            break;
        ok = std::fwrite(buf.data(), 1, n, out) == n;
        g_copy.done += (long long)n;
    }
    if (in)
    {
        ok = ok && !std::ferror(in);
        std::fclose(in);
    }
    if (out)
        ok = std::fclose(out) == 0 && ok;
    if (g_copy.stop.load())
    {
        std::remove(part.c_str());
        g_copy.state = 4;
        return nullptr;
    }
    if (!ok || std::rename(part.c_str(), g_copy.to.c_str()) != 0)
    {
        std::remove(part.c_str());
        g_copy.error = "The copy didn't finish (the console may be full). Nothing was left behind.";
        g_copy.state = 3;
        return nullptr;
    }
    g_copy.state = 2;
    return nullptr;
}

void finish_copy_thread()
{
    if (g_copy.joinable)
    {
        pthread_join(g_copy.thread, nullptr);
        g_copy.joinable = false;
    }
}
} // namespace

/* Where to start, and the shortcuts Triangle shows: the console's storage,
 * Porpoise's own games folder, and the drives that have something on them. */
std::vector<App::BrowseEntry> App::browse_places() const
{
    std::vector<BrowseEntry> out;
    auto add = [&](const std::string &label, const std::string &path) {
        BrowseEntry e;
        e.kind = BrowseEntry::Place;
        e.label = label;
        e.path = path;
        out.push_back(e);
    };
    if (browse_images_)
        add(tr("Porpoise's Tile Art Folder"), data_dir_ + "/home-art");
#ifdef PORPOISE_DESKTOP
    /* A computer: Porpoise's own games folder, the player's folders, and
     * every drive. */
    add(tr("Porpoise's Games Folder"), porpoise::platform::absolute(data_dir_ + "/games"));
    for (const auto &f : porpoise::platform::user_folders())
        if (is_dir(f.second))
            add(tr(f.first.c_str()), f.second);
    add(tr("Whole System"), "/");
    return out;
#endif
    if (is_dir(data_dir_ + "/games") && data_dir_ != PORPOISE_APP "/porpoise")
        add(tr("Porpoise's Games Folder"), data_dir_ + "/games");
    if (is_dir("/data"))
        add(tr("Console Storage"), "/data");
    for (int i = 0; i < 8; ++i)
    {
        const std::string p = "/mnt/usb" + std::to_string(i);
        if (is_dir(p) && has_entries(p))
            add(trf("USB Drive {n}", {{"n", std::to_string(i + 1)}}), p);
    }
    for (int i = 0; i < 2; ++i)
    {
        const std::string p = "/mnt/ext" + std::to_string(i);
        if (is_dir(p) && has_entries(p))
            add(tr("Extended Storage") + std::string(i ? " 2" : ""), p);
    }
    add(tr("Whole System"), "/");
    return out;
}

void App::open_browser(std::string path)
{
    if (screen_ != Screen::Browse)
        open_screen(Screen::Browse);
    browse_path_ = path;
    browse_entries_.clear();
    browse_row_ = 0;
    browse_first_ = 0;
    browse_games_ = 0;
    browse_unreadable_ = false;
    if (path.empty())
    {
        browse_entries_ = browse_places();
        return;
    }
    std::vector<BrowseEntry> folders, games;
#ifdef PORPOISE_DESKTOP
    if (path == "/")
    {
        /* The whole computer: its drives. */
        for (const std::string &drive : porpoise::platform::drives())
        {
            BrowseEntry b;
            b.kind = BrowseEntry::Folder;
            b.label = drive.substr(0, 2);
            b.path = drive;
            browse_entries_.push_back(b);
        }
        return;
    }
#endif
    if (DIR *d = opendir(path.c_str()))
    {
        while (dirent *e = readdir(d))
        {
            const std::string name = e->d_name;
            if (name.empty() || name[0] == '.')
                continue;
            const std::string full = !path.empty() && path.back() == '/' ? path + name : path + "/" + name;
            /* The entry's own type when the file system gives it; stat
             * otherwise (links, and file systems that don't say). A folder
             * stat can't look into still shows. */
            struct stat st;
            const bool stat_ok = stat(full.c_str(), &st) == 0;
#ifdef PORPOISE_DESKTOP
            const bool folder = stat_ok && S_ISDIR(st.st_mode); /* Windows' entries carry no type */
#else
            const bool folder = e->d_type == DT_DIR || (stat_ok && S_ISDIR(st.st_mode));
#endif
            BrowseEntry b;
            b.label = name;
            b.path = full;
            if (folder)
            {
                b.kind = BrowseEntry::Folder;
                folders.push_back(b);
            }
            else if (browse_images_)
            {
                std::string ext = name.substr(name.find_last_of('.') == std::string::npos ? name.size()
                                                                                         : name.find_last_of('.'));
                ext = lower_copy(ext);
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
                {
                    b.kind = BrowseEntry::Picture;
                    b.size = stat_ok ? (long long)st.st_size : 0;
                    games.push_back(b);
                }
            }
            else if (is_game_name(name))
            {
                b.kind = BrowseEntry::Game;
                b.size = stat_ok ? (long long)st.st_size : 0;
                games.push_back(b);
            }
        }
        closedir(d);
    }
    else
        browse_unreadable_ = true;
    auto by_name = [](const BrowseEntry &a, const BrowseEntry &b) { return lower_copy(a.label) < lower_copy(b.label); };
    std::sort(folders.begin(), folders.end(), by_name);
    std::sort(games.begin(), games.end(), by_name);
    /* How many games each folder holds right inside it (not on "/", where
     * the folders are the system's own). */
    if (path != "/" && !browse_images_)
        for (BrowseEntry &f : folders)
            f.games = count_games(f.path, 0);
    browse_games_ = int(games.size());
    browse_entries_ = std::move(folders);
    browse_entries_.insert(browse_entries_.end(), games.begin(), games.end());
}

void App::start_game_copy(const std::string &from)
{
    finish_copy_thread();
    const std::string dir = data_dir_ + "/games";
    mkdir(dir.c_str(), 0777);
    g_copy.from = from;
    g_copy.to = dir + from.substr(from.rfind('/'));
    g_copy.done = 0;
    struct stat st;
    g_copy.total = stat(from.c_str(), &st) == 0 ? (long long)st.st_size : 0;
    g_copy.stop = false;
    g_copy.error.clear();
    g_copy.state = 1;
    browse_copy_name_ = from.substr(from.rfind('/') + 1);
    if (create_title_thread(&g_copy.thread, copy_worker, nullptr) == 0)
        g_copy.joinable = true;
    else
        copy_worker(nullptr);
}

App::Action App::update_browser(bool up, bool down)
{
    /* A game being copied holds the screen; Circle stops it. */
    const int copy_state = g_copy.state.load();
    if (copy_state == 1)
    {
        if (pressed(BtnCircle))
            g_copy.stop = true;
        return Action::None;
    }
    if (copy_state >= 2)
    {
        finish_copy_thread();
        g_copy.state = 0;
        if (copy_state == 2)
        {
            open_dialog(DialogKind::Info, tr("Copied to the console"),
                        trf("{game} is in Porpoise's games folder now, and in your library.",
                            {{"game", browse_copy_name_}}),
                        "");
            open_browser(browse_path_);
            return Action::Rescan;
        }
        if (copy_state == 3)
            open_dialog(DialogKind::Info, tr("The copy didn't finish"), tr(g_copy.error), "");
        return Action::None;
    }

    const int n = int(browse_entries_.size());
    if (up && browse_row_ > 0)
    {
        --browse_row_;
        sfx(Sound::MenuScroll);
    }
    if (down && browse_row_ + 1 < n)
    {
        ++browse_row_;
        sfx(Sound::MenuScroll);
    }
    if (pressed(BtnCross) && browse_row_ < n)
    {
        const BrowseEntry &e = browse_entries_[std::size_t(browse_row_)];
        if (e.kind == BrowseEntry::Picture)
        {
            /* The tile art editor's picture: this one. */
            porpoise::tileart::Layer &l = art_.part == 0 ? art_.spec.icon : art_.spec.bg;
            l.source = porpoise::tileart::File;
            l.file = e.path;
            if (l.fit == porpoise::tileart::Whole && art_.part == 1)
                l.fit = porpoise::tileart::Fill; /* a picture of their own fills the background */
            porpoise::tileart::reset_position(l, art_.part == 1);
            art_.dirty[art_.part] = true;
            art_.note.clear();
            browse_images_ = false;
            open_screen(Screen::TileArt);
            sfx(Sound::LaunchGame);
            return Action::None;
        }
        if (e.kind != BrowseEntry::Game)
        {
            open_browser(e.path);
            return Action::None;
        }
        /* A game: offer to copy it to the console's own storage, unless it's
         * there already. */
        const std::string home = data_dir_ + "/games";
        if (data_dir_ == PORPOISE_APP "/porpoise" || browse_path_ == home)
        {
            sfx(Sound::MovingTab);
            return Action::None;
        }
        browse_copy_name_ = e.path;
        open_dialog(DialogKind::CopyGame, tr("Copy this game to the console?"),
                    trf("{game} ({size}) is copied to Porpoise's games folder on the console, where Porpoise always "
                        "finds it. The original stays where it is.",
                        {{"game", e.label}, {"size", size_text(e.size)}}),
                    tr("Copy"));
        return Action::None;
    }
    if (pressed(BtnTriangle))
    {
        sfx(Sound::MovingTab);
        open_browser(browse_path_.empty() ? "/" : "");
        return Action::None;
    }
    if (pressed(BtnSquare) && !browse_images_ && !browse_path_.empty() && browse_path_ != "/")
    {
        std::string folder = browse_path_;
        while (folder.size() > 1 && folder.back() == '/')
            folder.pop_back();
        if (std::find(settings_->folders.begin(), settings_->folders.end(), folder) == settings_->folders.end())
            settings_->folders.push_back(folder);
        settings_->save(settings_path_);
        build_settings();
        for (int i = 0; i < int(rows_.size()); ++i)
            if (rows_[std::size_t(i)].action == kRowRemoveFolder && rows_[std::size_t(i)].label == folder)
                settings_row_ = i;
        open_screen(Screen::Main);
        tab_ = Tab::Settings;
        on_rail_ = false;
        return Action::Rescan;
    }
    if (pressed(BtnCircle))
    {
        /* Up a level; from "/" or the shortcuts, out. */
        if (browse_images_ && (browse_path_.empty() || browse_path_ == "/" ||
                               browse_path_ == data_dir_ + "/home-art"))
        {
            browse_images_ = false;
            open_screen(Screen::TileArt);
            return Action::None;
        }
        if (browse_path_.empty() || browse_path_ == "/")
        {
            open_screen(Screen::Main);
            return Action::None;
        }
        const std::string from = browse_path_;
        std::string parent = from.substr(0, from.rfind('/'));
        if (parent.empty())
            parent = "/";
#ifdef PORPOISE_DESKTOP
        /* C:/Games -> C:/ -> the drives. */
        if (from.size() <= 3 && from.size() >= 2 && from[1] == ':')
            parent = "/";
        else if (parent.size() == 2 && parent[1] == ':')
            parent += "/";
#endif
        open_browser(parent);
        for (int i = 0; i < int(browse_entries_.size()); ++i)
            if (browse_entries_[std::size_t(i)].path == from)
                browse_row_ = i;
    }
    return Action::None;
}

void App::draw_browser()
{
    Gfx &g = *g_;
    const float x = 190, y = 136, w = 1540, h = 800;
    g.panel(x, y, w, h, rgba(0x0F1F63, 0.66f), 0.75f, kR, rgba(0x4C6FD8, 0.9f), 1.8f, 0, 0.12f);
    g.text_mid(Font::Bold, ts(44), x + 50, y + 62, kWhite, Align::Left,
               tr(browse_images_ ? "Choose a Picture" : "Choose a Game Folder"));
    const std::string where = browse_path_.empty() ? tr("Drives and Shortcuts") : browse_path_;
    g.text_mid(Font::SemiBold, ts(26), x + 50, y + 110, kIcy, Align::Left, fit(g, Font::SemiBold, ts(26), where, w - 520));
    if (!browse_path_.empty() && browse_path_ != "/")
    {
        const std::string count =
            browse_images_ ? (browse_games_ == 0 ? tr("No pictures right here")
                                                 : plural(browse_games_, "1 picture right here",
                                                          "{n} pictures right here"))
            : browse_games_ == 0 ? tr("No games right here")
                                 : plural(browse_games_, "1 game right here", "{n} games right here");
        g.text_mid(Font::SemiBold, ts(26), x + w - 50, y + 110, browse_games_ ? kCyan : kLavender, Align::Right, count);
    }
    g.panel(x + 50, y + 146, w - 100, 1.5f, rgba(0x3D4F9E, 0.7f), 1, 0);

    const int n = int(browse_entries_.size());
    constexpr int kVisible = 8;
    if (browse_row_ < browse_first_)
        browse_first_ = browse_row_;
    if (browse_row_ >= browse_first_ + kVisible)
        browse_first_ = browse_row_ - kVisible + 1;
    const float row_h = 72, rx = x + 26, rw = w - 52;
    float ry = y + 166;
    if (n == 0)
    {
        g.text_mid(Font::SemiBold, ts(30), x + w * 0.5f, y + 380, kSoft, Align::Center,
                   browse_unreadable_ ? tr("Porpoise can't open this folder") : tr("This folder is empty"));
        if (browse_images_ && !browse_unreadable_)
            g.text_mid(Font::Regular, ts(24), x + w * 0.5f, y + 440, kLavender, Align::Center,
                       fit(g, Font::Regular, ts(24),
                           tr("Put .png or .jpg pictures here (over FTP, for example), or press Triangle for a "
                              "USB drive."),
                           w - 160));
    }
    for (int i = browse_first_; i < n && i < browse_first_ + kVisible; ++i)
    {
        const BrowseEntry &e = browse_entries_[std::size_t(i)];
        const bool on = i == browse_row_;
        const bool game = e.kind == BrowseEntry::Game || e.kind == BrowseEntry::Picture;
        const float cy = ry + row_h * 0.5f;
        if (on)
            g.panel(rx, ry + 4, rw, row_h - 8, rgba(game ? 0x0E5A8A : 0x1D45B8, 0.88f), 0.7f, kR, kIcy, 2.4f, 10, 0.18f);
        if (e.kind == BrowseEntry::Picture)
        {
            /* A little picture: a frame and a hill. */
            const Color pc = on ? kWhite : kCyan;
            g.panel(rx + 30, cy - 16, 40, 32, with_alpha(pc, 0.9f), 1, 5);
            g.panel(rx + 34, cy - 12, 32, 24, rgba(0x0F1F63), 1, 3);
            g.panel(rx + 38, cy + 1, 24, 8, with_alpha(pc, 0.9f), 1, 4);
        }
        else if (game)
        {
            /* A little disc. */
            const Color dc = on ? kWhite : kCyan;
            g.panel(rx + 34, cy - 16, 32, 32, with_alpha(dc, 0.9f), 1, 16);
            g.panel(rx + 45, cy - 5, 10, 10, rgba(0x0F1F63), 1, 5);
        }
        else
        {
            /* A little folder (a drive is a folder too). */
            const Color fc = on ? kIcy : with_alpha(kCyan, 0.75f);
            g.panel(rx + 30, cy - 15, 18, 8, fc, 1, 3);
            g.panel(rx + 30, cy - 10, 40, 26, fc, 0.8f, 4);
        }
        const float right_w = game ? 170.0f : (e.games > 0 ? 260.0f : 80.0f);
        g.text_mid(Font::SemiBold, ts(30), rx + 92, cy, game ? (on ? kWhite : kCyan) : (on ? kWhite : kSoft),
                   Align::Left, fit(g, Font::SemiBold, ts(30), e.label, rw - 120 - right_w));
        if (game)
            g.text_mid(Font::Regular, ts(24), rx + rw - 30, cy, on ? kWhite : kLavender, Align::Right,
                       size_text(e.size));
        else
        {
            if (e.games > 0)
                g.text_mid(Font::SemiBold, ts(22), rx + rw - 70, cy, kCyan, Align::Right,
                           plural(e.games, "1 game", "{n} games"));
            if (on)
                g.glyph(Glyph::Arrow, rx + rw - 36, cy, 22, kCyan, kPi * 0.5f);
        }
        ry += row_h;
    }
    if (browse_first_ > 0)
        g.glyph(Glyph::Arrow, x + w - 30, y + 180, 18, rgba(0x58B8FF), 0);
    if (browse_first_ + kVisible < n)
        g.glyph(Glyph::Arrow, x + w - 30, y + h - 30, 18, rgba(0x58B8FF), kPi);

    /* Copying a game: a box over the list. */
    if (g_copy.state.load() == 1)
    {
        g.panel(x, y, w, h, rgba(0x02040C, 0.7f), 1, kR);
        const float bw = 900, bh = 300, bx = 960 - bw * 0.5f, by = 400;
        g.panel(bx, by, bw, bh, rgba(0x13308A, 0.95f), 1, kR, rgba(0x8BD9FF), 2.2f, 12);
        g.text_mid(Font::Bold, ts(36), 960, by + 64, kWhite, Align::Center, tr("Copying to the console"));
        g.text_mid(Font::Regular, ts(24), 960, by + 118, kSoft, Align::Center,
                   fit(g, Font::Regular, ts(24), browse_copy_name_, bw - 100));
        const long long done = g_copy.done.load(), total = std::max(1LL, g_copy.total.load());
        const float frac = std::clamp(float(double(done) / double(total)), 0.0f, 1.0f);
        g.panel(bx + 60, by + 160, bw - 120, 26, rgba(0x07102E, 0.7f), 1, 13, rgba(0x3D5AB0, 0.9f), 1.4f);
        g.panel(bx + 63, by + 163, std::max(20.0f, (bw - 126) * frac), 20, rgba(0x5CD3FF), 0.8f, 10);
        g.text_mid(Font::Regular, ts(22), 960, by + 230, kLavender, Align::Center,
                   size_text(done) + " / " + size_text(total));
        draw_prompts({{Glyph::Circle, "Stop"}}, {}, "");
        return;
    }

    std::vector<std::pair<Glyph, std::string>> right;
    if (browse_images_)
    {
        const bool on_picture =
            browse_row_ < n && browse_entries_[std::size_t(browse_row_)].kind == BrowseEntry::Picture;
        right.push_back({Glyph::Triangle, browse_path_.empty() ? "Whole System" : "Drives"});
        draw_prompts({{Glyph::Cross, on_picture ? "Use This Picture" : "Open"}, {Glyph::Circle, "Back"}}, right, "");
        return;
    }
    if (!browse_path_.empty() && browse_path_ != "/")
        right.push_back({Glyph::Square, "Use This Folder"});
    right.push_back({Glyph::Triangle, browse_path_.empty() ? "Whole System" : "Drives"});
    const bool on_game = browse_row_ < n && browse_entries_[std::size_t(browse_row_)].kind == BrowseEntry::Game;
    const bool can_copy = on_game && data_dir_ != PORPOISE_APP "/porpoise" && browse_path_ != data_dir_ + "/games";
    std::vector<std::pair<Glyph, std::string>> left;
    if (!on_game && n > 0)
        left.push_back({Glyph::Cross, "Open"});
    if (can_copy)
        left.push_back({Glyph::Cross, "Copy to Console"});
    left.push_back({Glyph::Circle, browse_path_.empty() || browse_path_ == "/" ? "Cancel" : "Up"});
    draw_prompts(left, right, "");
}

} // namespace porpoise::ui
