/* Porpoise UI - the in-game menu: Options + touch pad pauses the game and
 * slides this in from the left, in the launcher's own glass.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Five tabs, L1 / R1 between them:
 *   Game      resume, save states (three slots, with a picture each), fast
 *             forward, volume, quit
 *   Video     resolution, widescreen, aspect, the screen filter and border...
 *   Graphics  shader compilation and the other Dolphin graphics options
 *   Controls  the button layout, Customize buttons (the mapping screen, over
 *             the game), vibration, and the controller with a line out to
 *             every button, saying which GameCube button it is.
 *   Patches   how the game plays in widescreen this time (its 16:9 code, its
 *             own option, the emulated hack or 4:3), and every cheat and patch
 *             it has, each with its switch: so nothing on screen is a mystery.
 *   Achievements  with RetroAchievements on and a set for the game: every
 *             achievement, unlocked or not (ui_app_achievements.cpp).
 * Changes are saved as the game's own settings and take effect at once
 * (main applies them as take_menu_change() reports them). */
#include <algorithm>
#include <cstring>
#include <cmath>
#include <ctime>
#include <sys/stat.h>

#include "porpoise_borders.hpp"
#include "porpoise_pad.hpp"
#include "porpoise_states.hpp"
#include "ui_app.hpp"
#include "porpoise_gfxmods.hpp"
#include "ui_app_common.hpp"
#include "ui_cheats.hpp"
#include "ui_i18n.hpp"
#include "ui_setups.hpp"
#include "ui_widescreen.hpp"

namespace porpoise::ui
{
using namespace look;
using namespace porpoise::pad;

namespace
{
enum TabId
{
    kTabGame,
    kTabVideo,
    kTabGraphics,
    kTabAudio,
    kTabControls,
    kTabPatches,
    kTabCount,
};
const char *const kTabNames[kTabCount] = {"Game", "Video", "Graphics", "Audio", "Controls", "Patches"};

/* The Audio tab's Sound preset row (Settings::audio_preset), worked out from
 * the game's settings each time its rows are. */
int g_audio_preset = 0;
/* How the game's sound is running (pulled from Dolphin's mixer, or Classic):
 * the rows under the preset are that way's, since a change between the two
 * only takes at the next start. */
bool g_sound_pulled = true;
/* The game in the menu's ID: its graphics mods (porpoise_gfxmods). */
std::string g_menu_game_id;
int g_menu_discs = 0, g_menu_disc = 0; /* App::set_menu_discs */
std::vector<int> g_menu_disc_numbers;                         /* the game's own discs' numbers */
std::vector<std::pair<std::string, int>> g_menu_other_discs; /* 4.0: other games' discs */
int g_menu_disc_pick = 0, g_menu_disc_chosen = -1;

/* What a row does. */
enum class Kind
{
    Resume,
    Save,
    Load,
    FastForward,
    Library,
    Home,
    Restart, /* start the game over (a second press confirms) */
    Customize,
    SaveSetup, /* this game's video and graphics, kept as a setup */
    UseSetup,  /* a setup's settings for this game */
    Int,  /* a setting with a list of values */
    Bool, /* a setting that is on or off */
    Border,
    Info,  /* a fact to read (its help says more) */
    Cheat, /* one of the game's codes: on or off, from the next start */
    Shot,  /* Take Screenshot */
    Disc,  /* 3.1: Change Disc (a game on two discs) */
};

struct Row
{
    Kind kind;
    const char *key; /* the setting's key, for Int / Bool / Border */
    const char *label;
    int *iv = nullptr;
    bool *bv = nullptr;
    int min = 0;
    std::vector<std::string> values = {}; /* English; translated when drawn */
    bool gap_before = false;
    int index = -1;        /* Cheat: which of the game's codes */
    std::string text = {}; /* a label as it is (a code's name), when label is null */
};

/* What the Patches tab shows. */
struct Patches
{
    const std::vector<Cheat> *cheats = nullptr;
};

const std::vector<std::string> kAdjust = {"-5", "-4", "-3", "-2", "-1", "0", "+1", "+2", "+3", "+4", "+5"};
const std::vector<std::string> kPercent = {"0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"};

std::vector<Row> rows_for(int tab, Settings &p, bool wii = false, const Patches *patches = nullptr)
{
    const bool wii_game = wii; /* (wii turns false below for a GameCube controller in a Wii game) */
    std::vector<Row> r;
    switch (tab)
    {
    case kTabGame:
        r.push_back({Kind::Resume, "", "Resume"});
        r.push_back({Kind::Save, "", "Save State\xE2\x80\xA6"});
        r.push_back({Kind::Load, "", "Load State\xE2\x80\xA6"});
        r.push_back({Kind::Shot, "", "Take Screenshot"});
        r.push_back({Kind::FastForward, "", "Fast Forward"});
        if (g_menu_discs > 1 || !g_menu_other_discs.empty())
            r.push_back({Kind::Disc, "", "Change Disc"});
        r.push_back({Kind::Restart, "", "Start Over"});
        r.push_back({Kind::Library, "", "Quit to Library", nullptr, nullptr, 0, {}, true});
        r.push_back({Kind::Home, "", "Close Porpoise"});
        break;
    case kTabVideo:
        r.push_back({Kind::Int, "resolution", "Internal Resolution", &p.resolution, nullptr, 1,
                     {"1x (480p)", "2x (720p)", "3x (1080p)", "4x (1440p)", "5x (1800p)", "6x (4K)"}});
#ifndef PORPOISE_DESKTOP
        r.push_back({Kind::Bool, "frame_gen", "Frame Generation (Beta)", nullptr, &p.frame_gen, 0, {"Off", "On"}});
        r.push_back({Kind::Bool, "fg_steady30", "Steady 30 FPS", nullptr, &p.fg_steady30, 0, {"Off", "On"}});
#endif
        r.push_back({Kind::Int, "wide", "Widescreen", &p.wide, nullptr, 0, {"Auto", "On", "Off", "Universal (Beta)"}});
        r.push_back({Kind::Int, "aspect", "Aspect Ratio", &p.aspect, nullptr, 0,
                     {"Auto", "Force 16:9", "Force 4:3", "Stretch to Fill"}});
        r.push_back({Kind::Int, "antialiasing", "Anti-Aliasing", &p.antialiasing, nullptr, 0,
                     {"Off", "2x MSAA", "4x MSAA", "8x MSAA", "2x SSAA", "4x SSAA", "8x SSAA"}});
        r.push_back({Kind::Int, "anisotropy", "Anisotropic Filtering", &p.anisotropy, nullptr, 0,
                     {"1x", "2x", "4x", "8x", "16x"}});
        r.push_back({Kind::Int, "screen_filter", "Screen Filter", &p.screen_filter, nullptr, 0,
                     {"Smooth", "Sharp", "Sharpen (CAS)", "CRT", "Arcade CRT", "VHS", "Soft VHS", "8-Bit", "Pocket",
                      "Scanlines", "Shadow Mask", "LCD", "FSR 1", "16-Bit", "NTSC Composite", "Aperture Grille", "Smooth Edges (FXAA)"}});
        r.push_back({Kind::Int, "filter_strength", "Filter Strength", &p.filter_strength, nullptr, 1,
                     {"10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"}});
        r.push_back({Kind::Int, "bloom", "Extra Bloom", &p.bloom, nullptr, 0, {"Off", "Low", "Medium", "High"}});
        r.push_back({Kind::Int, "color_saturation", "Saturation", &p.color_saturation, nullptr, 0, kAdjust});
        r.push_back({Kind::Int, "color_contrast", "Contrast", &p.color_contrast, nullptr, 0, kAdjust});
        r.push_back({Kind::Int, "color_warmth", "Warmth", &p.color_warmth, nullptr, 0, kAdjust});
        r.push_back({Kind::Border, "border", "Border"});
        r.push_back({Kind::Bool, "fps_overlay", "FPS Counter", nullptr, &p.fps_overlay, 0, {"Off", "On"}});
        break;
    case kTabGraphics:
        r.push_back({Kind::Int, "shader_mode", "Shader Compilation", &p.shader_mode, nullptr, 0,
                     {"Synchronous", "Ubershaders", "Async Ubershaders", "Async, Skip Drawing"}});
        r.push_back({Kind::Int, "texture_cache", "Texture Cache Accuracy", &p.texture_cache, nullptr, 0,
                     {"Fast", "Middle", "Safe"}});
        r.push_back({Kind::Int, "texture_filter", "Texture Filtering", &p.texture_filter, nullptr, 0,
                     {"Game's Own", "Nearest (Sharp)", "Linear (Smooth)"}});
        r.push_back({Kind::Int, "resampling", "Output Resampling", &p.resampling, nullptr, 0,
                     {"Default", "Bilinear", "B-Spline", "Mitchell-Netravali", "Catmull-Rom", "Sharp Bilinear",
                      "Area Sampling"}});
        r.push_back({Kind::Bool, "pixel_lighting", "Per-Pixel Lighting", nullptr, &p.pixel_lighting, 0, {"Off", "On"}});
        r.push_back({Kind::Bool, "disable_fog", "Disable Fog", nullptr, &p.disable_fog, 0, {"Off", "On"}});
        r.push_back({Kind::Bool, "crop_overscan", "Crop Overscan", nullptr, &p.crop_overscan, 0, {"Off", "On"}});
        r.push_back({Kind::Bool, "skip_dupes", "Skip Duplicate Frames", nullptr, &p.skip_dupes, 0, {"Off", "On"}});
        {
            /* Dolphin's built-in graphics mods, for the games that have them. */
            const porpoise::gfxmods::Offer mods = porpoise::gfxmods::offer(g_menu_game_id);
            if (mods.bloom)
                r.push_back({Kind::Int, "gfx_bloom", "Bloom", &p.gfx_bloom, nullptr, 0,
                             mods.own_bloom ? std::vector<std::string>{"Game's Own", "Off", "Blurred"}
                                            : std::vector<std::string>{"Game's Own", "Off", "Blurred",
                                                                       "Native Resolution"}});
            if (mods.dof)
                r.push_back({Kind::Int, "gfx_dof", "Depth of Field", &p.gfx_dof, nullptr, 0,
                             {"Game's Own", "Off", "Blurred", "Native Resolution"}});
            if (mods.hud)
                r.push_back({Kind::Bool, "gfx_hud", "Hide the HUD", nullptr, &p.gfx_hud, 0, {"Off", "On"}});
            if (!mods.extra_title.empty())
                r.push_back({Kind::Bool, "gfx_extra", "Native Resolution Goop", nullptr, &p.gfx_extra, 0,
                             {"Off", "On"}});
        }
        r.push_back({Kind::SaveSetup, "", "Save as a Setup", nullptr, nullptr, 0, {}, true});
        r.push_back({Kind::UseSetup, "", "Use a Setup"});
        break;
    case kTabAudio:
    {
        r.push_back({Kind::Int, "volume", "Volume", &p.volume, nullptr, 0, kPercent});
        r.push_back({Kind::Bool, "muted", "Mute Game", nullptr, &p.muted, 0, {"Off", "On"}});
        g_audio_preset = p.audio_preset();
        std::vector<std::string> presets = {"Smooth", "Responsive", "Extra Smooth", "Classic (2.1)"};
        if (g_audio_preset == Settings::kAudioCustom)
            presets.push_back("Custom");
        r.push_back({Kind::Int, "audio_preset", "Sound Preset", &g_audio_preset, nullptr, 0, presets, true});
        if (g_sound_pulled)
        {
            r.push_back({Kind::Int, "audio_buffer", "Audio Buffer", &p.audio_buffer, nullptr, 0,
                         {"40 ms", "80 ms", "160 ms"}});
            r.push_back({Kind::Bool, "audio_fill", "Fill Audio Gaps", nullptr, &p.audio_fill, 0, {"Off", "On"}});
        }
        else
        {
            r.push_back({Kind::Int, "audio_buffer", "Audio Buffer", &p.audio_buffer, nullptr, 0,
                         {"Low", "Normal", "Safe"}});
            r.push_back({Kind::Bool, "audio_stretch", "Audio Stretching", nullptr, &p.audio_stretch, 0,
                         {"Off", "On"}});
        }
        r.push_back({Kind::Bool, "dsp_accurate", "Accurate Audio", nullptr, &p.dsp_accurate, 0, {"Off", "On"}, true});
        if (wii)
            r.push_back({Kind::Int, "wiimote_speaker", "Wii Remote Speaker", &p.wiimote_speaker, nullptr, 0,
                         {"Off", "TV", "Controller"}});
        break;
    }
    case kTabControls:
        if (wii && p.wii_controller == porpoise::pad::WiiGameCube)
        {
            /* A GameCube controller in a Wii game: the GameCube's buttons, no
             * pointer or motion. */
            r.push_back({Kind::Int, "wii_controller", "Wii Controller", &p.wii_controller, nullptr, 0,
                         {"Remote + Nunchuk", "Remote", "Remote Sideways", "Classic Controller",
                          "Two Controllers (Alpha)", "GameCube Controller"}});
            wii = false; /* then the GameCube's rows */
        }
        if (!wii)
        {
            r.push_back({Kind::Int, "button_layout", "Button Layout", &p.button_layout, nullptr, 0,
                         {"GameCube", "PlayStation", "My Layout 1", "My Layout 2", "My Layout 3", "My Layout 4"}});
            r.push_back({Kind::Customize, "", "Customize Buttons"});
            if (!wii_game)
                r.push_back({Kind::Int, "p1_port", "Player 1's Port", &p.p1_port, nullptr, 0,
                             {"Port 1", "Port 2", "Port 3", "Port 4"}});
            r.push_back({Kind::Int, "invert_main", "Invert the Control Stick", &p.invert_main, nullptr, 0,
                         {"Off", "Up and Down", "Left and Right", "Both"}});
            r.push_back({Kind::Int, "invert_c", "Invert the C-Stick", &p.invert_c, nullptr, 0,
                         {"Off", "Up and Down", "Left and Right", "Both"}});
            r.push_back({Kind::Int, "stick_sensitivity", "Stick Sensitivity", &p.stick_sensitivity, nullptr, 0,
                         {"Normal", "Lower", "Low", "Lowest"}});
            r.push_back({Kind::Int, "stick_deadzone", "Stick Dead Zone", &p.stick_deadzone, nullptr, 0,
                         {"Off", "Small", "Medium", "Large"}});
        }
        if (wii)
        {
            /* The Wii Remote: put right while playing. */
            r.push_back({Kind::Int, "wii_source", "Wii Remotes", &p.wii_source, nullptr, 0,
                         {"DualSense", "Real Wii Remotes (DolphinBar)"}});
            r.push_back({Kind::Bool, "real_wiimote_speaker", "Real Wii Remote Speakers", nullptr,
                         &p.real_wiimote_speaker, 0, {"Off", "On"}});
            r.push_back({Kind::Customize, "wii_recal", "Recalibrate the Pointer"});
            r.push_back({Kind::Customize, "wii_setup", "Wii Remote Setup"});
            {
                std::vector<std::string> presets = {"None"};
                const auto &names = Settings::wii_preset_names();
                for (int i = 0; i < Settings::kWiiPresets; ++i)
                {
                    const Settings::WiiPreset &w = p.wii_presets[i];
                    presets.push_back(std::to_string(i + 1) + ": " +
                                      (w.used ? tr(names[std::size_t(w.name)]) : tr("empty")));
                }
                r.push_back({Kind::Int, "wii_preset", "Wii Preset", &p.wii_preset, nullptr, 0, presets});
            }
            r.push_back({Kind::Int, "wii_controller", "Wii Controller", &p.wii_controller, nullptr, 0,
                         {"Remote + Nunchuk", "Remote", "Remote Sideways", "Classic Controller",
                          "Two Controllers (Alpha)", "GameCube Controller"}});
            r.push_back({Kind::Int, "wii_pointer", "Pointer", &p.wii_pointer, nullptr, 0,
                         {"Gyro", "Touch Pad", "Right Stick"}});
            r.push_back({Kind::Int, "wii_speed", "Pointer Speed", &p.wii_speed, nullptr, 0,
                         {"Your screen", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"}});
            r.push_back({Kind::Int, "wii_grip", "Grip", &p.wii_grip, nullptr, 0,
                         {"Auto", "Both Hands", "Upright, trigger to the TV", "Upright, facing you"}});
            r.push_back({Kind::Bool, "wii_motion", "Motion", nullptr, &p.wii_motion, 0, {"Off", "On"}});
            r.push_back({Kind::Bool, "wii_shake", "Flick to Shake", nullptr, &p.wii_shake, 0, {"Off", "On"}});
            r.push_back({Kind::Int, "wii_motion_plus", "Wii MotionPlus", &p.wii_motion_plus, nullptr, 0,
                         {"Auto", "On", "Off"}});
        }
        r.push_back({Kind::Bool, "rumble", "Vibration", nullptr, &p.rumble, 0, {"Off", "On"}});
        r.push_back({Kind::Int, "turbo", "Turbo Button", &p.turbo, nullptr, 0, kTurboValues});
        r.push_back({Kind::Int, "quick_slot", "Quick Save Buttons", &p.quick_slot, nullptr, 0, kQuickSlotValues});
        if (!wii) /* the GameCube's L and R (also the GameCube controller in a Wii game) */
            r.push_back({Kind::Int, "trigger_feel", "Trigger Click", &p.trigger_feel, nullptr, 0, kTriggerFeelValues});
        break;
    case kTabPatches:
        r.push_back({Kind::Info, "ws_status", "Widescreen"});
        if (!patches || !patches->cheats || patches->cheats->empty())
            r.push_back({Kind::Info, "no_codes", "Cheats and Patches"});
        else
            for (std::size_t i = 0; i < patches->cheats->size(); ++i)
            {
                Row row{Kind::Cheat, "cheat", nullptr};
                row.index = int(i);
                const std::string &name = (*patches->cheats)[i].name;
                row.text = name.size() > 1 && name[0] == '$' ? name.substr(1) : name;
                row.gap_before = i == 0;
                r.push_back(row);
            }
        break;
    default:
        break;
    }
    return r;
}

/* The help line under the rows. */
const char *help_for(const Row &row, const Settings &p)
{
    if (row.key == std::string("resolution") && p.resolution >= 4)
        return "The highest resolutions may slow some games down.";
    if (row.key == std::string("screen_filter"))
        return "Porpoise's own filter on the way to the TV. CRT and Arcade CRT look best at 1080p or above.";
    if (row.key == std::string("wide"))
        return "Takes effect the next time the game starts. On, for a game without a widescreen code, uses the "
               "emulated widescreen hack, which can glitch at the screen edges.";
    if (row.key == std::string("border"))
        return "Fills the bars beside a 4:3 picture (widescreen off). Add your own PNGs to /data/porpoise/borders.";
    if (row.key == std::string("shader_mode"))
        return "Async Ubershaders hide the stutter when a game draws something new. Takes effect after a restart of the game.";
    if (row.key == std::string("texture_cache"))
        return "Safe fixes some games' text and effects; Fast is quickest.";
    if (row.kind == Kind::FastForward)
        return "Runs the game 2x or 4x faster until you turn it off. The game's sound pauses meanwhile.";
    if (row.kind == Kind::Save)
        return "Choose one of three slots to save this moment in.";
    if (row.kind == Kind::Load)
        return "Choose a saved moment to go back to.";
    if (row.kind == Kind::SaveSetup)
        return "Keeps this game's Video and Graphics settings as a setup, to use on other games.";
    if (row.kind == Kind::UseSetup)
        return "Puts a setup's Video and Graphics settings on this game.";
    if (row.kind == Kind::Customize && row.key == std::string("wii_recal"))
        return "Point at the middle and two corners again: after moving, or if the pointer feels off.";
    if (row.kind == Kind::Customize && row.key == std::string("wii_setup"))
        return "The whole setup: the Wii controller, how to hold it, your screen, and (advanced) fine-tuning.";
    if (row.key == std::string("wii_preset"))
        return "A Wii Remote set-up kept under a name (made in the setup's Fine-tune page, Advanced).";
    if (row.key == std::string("audio_preset"))
        return g_sound_pulled ? "Smooth covers the gaps when the game runs slow, as Dolphin does on a PC. Responsive: "
                              "less delay. Extra Smooth: for games that slow down often. Classic: 2.1's sound, from "
                              "the next start."
                            : "Classic is 2.1's sound. Smooth, Responsive and Extra Smooth cover the gaps when the "
                              "game runs slow; they apply the next time the game starts.";
    if (row.key == std::string("turbo"))
        return kTurboHelp;
    if (row.key == std::string("quick_slot"))
        return kQuickSlotHelp;
    if (row.key == std::string("trigger_feel"))
        return kTriggerFeelHelp;
    if (row.key == std::string("gfx_bloom"))
        return kGfxBloomHelp;
    if (row.key == std::string("gfx_dof"))
        return kGfxDofHelp;
    if (row.key == std::string("gfx_hud"))
        return kGfxHudHelp;
    if (row.key == std::string("gfx_extra"))
        return kGfxGoopHelp;
    if (row.key == std::string("audio_buffer"))
        return "How much sound is kept ready. More holds off crackling when a game slows down, for a little delay.";
    if (row.key == std::string("audio_fill"))
        return "When a game runs slow, the sound it just played covers the gap, faded, instead of a crackle. Off "
               "leaves the gap silent.";
    if (row.key == std::string("audio_stretch"))
        return "When a game slows down, its sound slows with it, slightly lower, instead of crackling.";
    if (row.key == std::string("dsp_accurate"))
        return "Dolphin's exact sound chip: fixes wrong sound in a few games, but needs much more of the processor. "
               "Takes effect the next time the game starts.";
    if (row.key == std::string("wiimote_speaker"))
        return "Sounds from the Wii Remote's own speaker: in the TV's sound, or from the controller. Takes effect the "
               "next time the game starts.";
    if (row.kind == Kind::Customize)
        return "Your own layouts: change any button on a picture of the DualSense.";
    return "Changes here are saved for this game.";
}

/* How this run of the game plays in widescreen (Settings::ws_plan, as
 * ui_widescreen's Plan): the value shown, and what it means. */
const char *ws_value(int plan, bool wii)
{
    if (wii)
        return "The Wii's Setting";
    switch (plan)
    {
    case 1: return "16:9 Code";
    case 4: return "16:9 Code + Hack";
    case 2: return "In the game's options";
    case 3: return "Emulated Hack";
    case 5: return "Universal (Beta)";
    default: return "Off: 4:3";
    }
}
const char *ws_help(int plan, bool wii)
{
    if (wii)
        return "Wii games are 16:9 when Wii Widescreen is on in Porpoise's settings, as on a Wii.";
    switch (plan)
    {
    case 1:
        return "This game runs in true 16:9 with its widescreen code, so nothing pops in at the edges. The code is "
               "listed below.";
    case 4: return "This game's widescreen code needs Dolphin's widescreen hack as well, so both are on.";
    case 2:
        return "This game has a 16:9 option of its own: turn it on in the game's options, and the picture follows.";
    case 3: return "Dolphin's emulated widescreen hack is on. Things at the edges of the screen may pop in and out.";
    case 5:
        return "Porpoise's universal widescreen (beta) widens the game's own 3D view to 16:9 when it finds how, and "
               "uses Dolphin's widescreen hack when it doesn't. The menus and HUD may look stretched.";
    default: return "This game plays in 4:3. Widescreen, in the Video tab, changes that the next time it starts.";
    }
}

/* The schematic (assets/ui/controller-lines.png, tools/make-controller-art.py):
 * its crop of Zacksly's 4096x2160 drawing, and where each line ends there. */
constexpr float kLinesX0 = 780, kLinesY0 = 150, kLinesW = 2540, kLinesH = 1610;
struct LineEnd
{
    float x, y;
    bool left; /* the label goes to the left of the end */
};
enum EndId
{
    kEndL,
    kEndR,
    kEndTouch,
    kEndCreate,
    kEndOptions,
    kEndDPad,
    kEndFace,
    kEndLStick,
    kEndRStick,
    kEndCount,
};
constexpr LineEnd kEnds[kEndCount] = {
    {1023.36f, 229.18f, true},   {3072.64f, 230.91f, false}, {1885.13f, 237.25f, true},
    {969.74f, 544.5f, true},     {3126.26f, 544.5f, false},  {860.62f, 1015.03f, true},
    {3154.78f, 1015.03f, false}, {864.88f, 1430.69f, true},  {3231.12f, 1430.69f, false},
};

std::string slot_date(long long t)
{
    if (t <= 0)
        return "";
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    localtime_r(&tt, &tm);
    char buf[48];
    std::strftime(buf, sizeof buf, "%Y-%m-%d  %H:%M", &tm);
    return buf;
}
} // namespace

/* ---- save-state slots ------------------------------------------------------------------- */

void App::menu_free_slots()
{
    free_badges();
    for (Texture *&t : menu_slot_tex_)
        if (t)
        {
            g_->free_texture(t);
            t = nullptr;
        }
}

void App::load_slots(const Game *game)
{
    menu_free_slots();
    if (!game)
        return;
    const std::string key = Library::key_of(*game);
    for (int i = 0; i < porpoise::states::kSlots; ++i)
    {
        const porpoise::states::Slot s = porpoise::states::slot(key, i);
        menu_slot_used_[i] = s.exists;
        menu_slot_time_[i] = s.time;
        menu_slot_tex_[i] = s.exists ? g_->texture_file(s.picture_path) : nullptr;
    }
}

void App::menu_state_done(MenuRequest::Kind kind, int slot, bool ok)
{
    menu_busy_ = {};
    menu_busy_handed_ = false;
    menu_confirm_ = false;
    if (kind == MenuRequest::Save)
    {
        menu_note_ = ok ? trf("Saved to slot {n}.", {{"n", std::to_string(slot + 1)}})
                        : tr("The game's state couldn't be saved.");
        load_slots(menu_game_);
        menu_slots_mode_ = 0; /* back to the list */
        sfx(ok ? Sound::LaunchGame : Sound::MovingTab);
    }
    else
    {
        menu_note_ = ok ? trf("Slot {n} loaded.", {{"n", std::to_string(slot + 1)}})
                        : tr("That slot couldn't be loaded.");
        sfx(ok ? Sound::LaunchGame : Sound::MovingTab);
        if (ok)
        {
            /* Straight back into the game, at that moment. */
            menu_slots_mode_ = 0;
            menu_closing_ = true;
            menu_answer_ = 1;
            if (settings_->reduced_motion)
                menu_anim_ = 0;
        }
    }
    menu_note_time_ = time_;
}

App::MenuRequest App::take_menu_request()
{
    /* Handed over once the "Saving..." it shows has been drawn, so the
     * player sees it while the game's state is taken; or after a moment
     * anyway, when the menu isn't being drawn (before the game's first
     * picture). */
    if (menu_busy_.kind == MenuRequest::None || menu_busy_handed_ ||
        (menu_busy_frames_ < 1 && time_ - menu_busy_start_ < 0.25))
        return {};
    menu_busy_handed_ = true;
    return menu_busy_;
}

/* ---- opening and input ------------------------------------------------------------------ */

void App::set_menu_discs(std::vector<int> own, int current, std::vector<std::pair<std::string, int>> others)
{
    g_menu_disc_numbers = std::move(own);
    g_menu_discs = int(g_menu_disc_numbers.size());
    g_menu_other_discs = std::move(others);
    g_menu_disc = current;
    /* A game on two discs: the menu opens on its other disc (Cross puts it
     * in, as in 3.1). Otherwise on the disc that's in, so Cross does nothing
     * until another one is picked. */
    const int count = g_menu_discs;
    if (count > 1 && current >= 0 && current < count)
        g_menu_disc_pick = (current + 1) % count;
    else
        g_menu_disc_pick = current >= 0 ? current : 0;
    g_menu_disc_chosen = -1;
}

int App::take_menu_disc()
{
    const int chosen = g_menu_disc_chosen;
    g_menu_disc_chosen = -1;
    return chosen;
}

void App::open_game_menu(Game *game, Settings *play)
{
    menu_game_ = game;
    menu_play_ = play;
    if (play)
        menu_draft_ = menu_base_ = *play; /* changes wait for Apply */
    menu_prompt_ = 0;
    menu_row_ = 0;
    menu_tab_ = kTabGame;
    menu_anim_ = 0;
    menu_closing_ = false;
    menu_answer_ = 0;
    menu_note_.clear();
    menu_restart_armed_ = false;
    map_in_game_ = false;
    menu_slots_mode_ = 0;
    menu_confirm_ = false;
    menu_busy_ = {};
    menu_busy_handed_ = false;
    menu_borders_ = porpoise::borders::list();
    if (menu_wide_open_ == -2) /* the first pause of this run */
        menu_wide_open_ = play ? play->wide : -1;
    /* The Patches tab: the game's codes and their switches as it has them. */
    menu_cheats_.clear();
    menu_cheat_on_.clear();
    if (game && play && game->id.size() == 6 && !sys_dir_.empty())
    {
        /* <data>/cheats, where the player's own codes go, is theirs to make: an
         * empty one made here was taken for a broken online patch. */
        menu_cheats_ = cheats_for(sys_dir_, game->id, data_dir_ + "/cheats", game->platform == "Wii");
        for (const Cheat &c : menu_cheats_)
            menu_cheat_on_.push_back(c.default_on ? play->get(cheat_key(c, false)) != "1"
                                                  : play->get(cheat_key(c, true)) == "1");
        /* What's on first, the widescreen code in use at the top; the rest in
         * their lists' order. */
        std::vector<std::size_t> order(menu_cheats_.size());
        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        auto rank = [&](std::size_t i) {
            return !menu_cheat_on_[i] ? (menu_cheats_[i].own ? 2 : 3)
                   : widescreen::is_widescreen_code(menu_cheats_[i].name) ? 0
                                                                          : 1;
        };
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return rank(a) < rank(b); });
        std::vector<Cheat> cheats;
        std::vector<char> on;
        for (std::size_t i : order)
        {
            cheats.push_back(menu_cheats_[i]);
            on.push_back(menu_cheat_on_[i]);
        }
        /* At most 80, after the sort: what's on (and the player's own) stays. */
        if (cheats.size() > 80)
        {
            cheats.resize(80);
            on.resize(80);
        }
        menu_cheats_.swap(cheats);
        menu_cheat_on_.swap(on);
    }
    load_slots(menu_game_);
    ach_focus_ = 0;
    ach_scroll_ = 0;
    load_achievements(game ? game->id : std::string(), true);
    prev_ = held_ = raw_held_ = raw_prev_ = 0xFFFFFFFFu; /* buttons still down from the shortcut don't count */
    sfx(Sound::DetailsFlip);
}

int App::update_game_menu(const Input &in, double dt)
{
    time_ += dt;
    prev_ = held_;
    const std::uint32_t gc = gc_menu_bits(); /* the Switch 2's GameCube controller */
    follow_last_controller(in.held, in.wii, true, gc);
    held_ = in.held | in.wii | gc;
    raw_prev_ = raw_held_;
    raw_held_ = in.held;
    if (in.stick_y < -0.55f) held_ |= BtnUp;
    if (in.stick_y > 0.55f) held_ |= BtnDown;
    if (in.stick_x < -0.55f) held_ |= BtnLeft;
    if (in.stick_x > 0.55f) held_ |= BtnRight;
    const bool calm = settings_->reduced_motion;
    if (menu_closing_)
    {
        menu_anim_ = calm ? 0.0f : std::max(0.0f, menu_anim_ - float(dt) * 6.0f);
        if (menu_anim_ <= 0.0f)
            menu_free_slots();
        return menu_anim_ <= 0.0f ? menu_answer_ : 0;
    }
    menu_anim_ = calm ? 1.0f : std::min(1.0f, menu_anim_ + float(dt) * 5.0f);
    if (!menu_play_)
        return 1;

    bool up = nav(BtnUp, rep_up_, dt), down = nav(BtnDown, rep_down_, dt);
    const bool left = nav(BtnLeft, rep_left_, dt), right = nav(BtnRight, rep_right_, dt);

    /* The mapping screen or the Wii Remote setup, over the game. */
    if (map_in_game_)
    {
        update_mapping(up, down, left, right);
        return 0;
    }
    if (ws_in_game_)
    {
        update_wii_setup(up, down, left, right);
        return 0;
    }
    /* Saving or loading: nothing to do but wait. */
    if (menu_busy_.kind != MenuRequest::None)
        return 0;
    /* Choosing a slot to save to, or to load. */
    if (menu_slots_mode_)
    {
        const bool saving = menu_slots_mode_ == 1;
        if (left || right)
        {
            menu_slot_ = (menu_slot_ + (left ? porpoise::states::kSlots - 1 : 1)) % porpoise::states::kSlots;
            menu_confirm_ = false;
            sfx(Sound::MenuScroll);
        }
        if (pressed(BtnCircle))
        {
            menu_slots_mode_ = 0;
            menu_confirm_ = false;
            sfx(Sound::MovingTab);
            return 0;
        }
        if (pressed(BtnCross))
        {
            if (!saving && !menu_slot_used_[menu_slot_])
            {
                menu_note_ = tr("That slot is empty.");
                menu_note_time_ = time_;
                sfx(Sound::MovingTab);
            }
            else if (saving && menu_slot_used_[menu_slot_] && !menu_confirm_)
            {
                menu_confirm_ = true; /* a second press replaces it */
                sfx(Sound::MovingTab);
            }
            else
            {
                menu_busy_.kind = saving ? MenuRequest::Save : MenuRequest::Load;
                menu_busy_.slot = menu_slot_;
                menu_busy_frames_ = 0;
                menu_busy_handed_ = false;
                menu_busy_start_ = time_;
                sfx(Sound::DetailsFlip);
            }
        }
        return 0;
    }

    /* L1 / R1: the tabs (Achievements last, when the game has a set). */
    const int tabs = kTabCount + (menu_has_achievements() ? 1 : 0);
    if (menu_tab_ >= tabs)
        menu_tab_ = kTabGame;
    if (!menu_prompt_ && (pressed(BtnL1) || pressed(BtnR1)))
    {
        menu_tab_ = (menu_tab_ + (pressed(BtnL1) ? tabs - 1 : 1)) % tabs;
        menu_row_ = 0;
        menu_confirm_ = false;
        sfx(Sound::MovingTab);
        return 0;
    }
    if (menu_tab_ == kTabCount && !menu_prompt_)
    {
        /* Achievements: the list, fresh every couple of seconds. */
        if (menu_game_ && time_ - ach_loaded_at_ > 2.0)
            load_achievements(menu_game_->id, true);
        if (!menu_has_achievements())
        {
            menu_tab_ = kTabGame; /* the set went (the core lost it) */
            menu_row_ = 0;
            return 0;
        }
        if (achievements_nav(up, down))
            sfx(Sound::MenuScroll);
        const bool shortcut = (held_ & (BtnOptions | BtnTouch)) == (BtnOptions | BtnTouch) &&
                              (prev_ & (BtnOptions | BtnTouch)) != (BtnOptions | BtnTouch);
        if (pressed(BtnCircle) || shortcut)
        {
            sfx(Sound::DetailsFlip);
            menu_closing_ = true;
            menu_answer_ = 1;
            if (calm)
            {
                menu_anim_ = 0;
                menu_free_slots();
                return 1;
            }
        }
        return 0;
    }
    Settings &p = menu_draft_; /* the rows change it; Apply hands it to the game */
    Patches patches;
    patches.cheats = &menu_cheats_;
    g_menu_game_id = menu_game_ ? menu_game_->id : std::string();
    std::vector<Row> rows = rows_for(menu_tab_, p, menu_game_ && menu_game_->platform == "Wii", &patches);
    const int count = int(rows.size());
    if (menu_prompt_)
        up = down = false; /* the question is answered first */
    if (up || down)
        menu_confirm_ = false; /* "press again to replace" is for the row it was said on */
    if (up)
    {
        menu_row_ = (menu_row_ + count - 1) % count;
        sfx(Sound::MenuScroll);
    }
    if (down)
    {
        menu_row_ = (menu_row_ + 1) % count;
        sfx(Sound::MenuScroll);
    }
    menu_row_ = std::clamp(menu_row_, 0, count - 1);
    if (up || down)
        menu_restart_armed_ = false;

    auto close = [&](int answer) {
        sfx(answer == 1 ? Sound::DetailsFlip : Sound::MovingTab);
        menu_closing_ = true;
        menu_answer_ = answer;
        if (calm)
        {
            menu_anim_ = 0;
            menu_free_slots();
        }
        return calm ? answer : 0;
    };
    const bool combo = (held_ & (BtnOptions | BtnTouch)) == (BtnOptions | BtnTouch) &&
                       (prev_ & (BtnOptions | BtnTouch)) != (BtnOptions | BtnTouch);
    if (menu_prompt_)
    {
        /* The question over the menu: left / right, Cross, Circle. */
        if ((left && menu_prompt_choice_ != 1) || (right && menu_prompt_choice_ != 0))
        {
            menu_prompt_choice_ = left ? 1 : 0;
            sfx(Sound::MenuScroll);
        }
        if (pressed(BtnCircle))
        {
            menu_prompt_ = 0; /* keep editing, or Later */
            sfx(Sound::MovingTab);
            return 0;
        }
        if (!pressed(BtnCross))
            return 0;
        const int prompt = menu_prompt_;
        menu_prompt_ = 0;
        if (prompt == 2)
        {
            if (menu_prompt_choice_ != 1)
                return 0; /* Later */
            /* Start the game afresh: back to the library for a moment, and
             * straight in again with the new settings read at its start. */
            menu_relaunch_ = true;
            return close(5);
        }
        const int answer = menu_prompt_answer_;
        if (menu_prompt_choice_ == 1)
        {
            apply_menu_pending();
            if (menu_prompt_ == 2)
                return 0; /* applied, and the game needs starting over: that question first */
        }
        else
            menu_draft_ = *menu_play_; /* Discard */
        return close(answer);
    }
    const bool waiting = !menu_draft_.changed_keys(*menu_play_).empty();
    if (pressed(BtnSquare) && waiting)
    {
        apply_menu_pending();
        return 0;
    }
    if (pressed(BtnCircle) || combo)
    {
        if (waiting)
        {
            menu_prompt_ = 1;
            menu_prompt_choice_ = 1;
            menu_prompt_answer_ = 1;
            sfx(Sound::DetailsFlip);
            return 0;
        }
        return close(1);
    }
    /* Leaving the game with changes not applied: asked first too. */
    auto leave = [&](int answer) {
        if (!waiting)
            return close(answer);
        menu_prompt_ = 1;
        menu_prompt_choice_ = 1;
        menu_prompt_answer_ = answer;
        sfx(Sound::DetailsFlip);
        return 0;
    };

    const Row &row = rows[std::size_t(menu_row_)];
    const bool cross = pressed(BtnCross);
    const int dir = left ? -1 : (right || cross) ? 1 : 0;
    std::string key;
    switch (row.kind)
    {
    case Kind::Resume:
        if (cross)
            return close(1);
        break;
    case Kind::Library:
        if (cross)
            return leave(2);
        break;
    case Kind::Home:
        if (cross)
            return leave(3);
        break;
    case Kind::Disc:
    {
        /* Left / right: which disc (the game's own, then other games'); Cross
         * puts it in. */
        const int total = g_menu_discs + int(g_menu_other_discs.size());
        if ((left || right) && total > 1)
        {
            g_menu_disc_pick = (g_menu_disc_pick + (left ? total - 1 : 1)) % total;
            sfx(Sound::MenuScroll);
        }
        else if (cross && total > 0)
        {
            if (g_menu_disc_pick == g_menu_disc)
            {
                sfx(Sound::MenuScroll); /* in already */
                break;
            }
            g_menu_disc_chosen = g_menu_disc_pick;
            sfx(Sound::LaunchGame);
            return close(6); /* porpoise::core::kMenuChangeDisc */
        }
        break;
    }
    case Kind::Restart:
        if (cross)
        {
            /* Once to ask, again to do it: the game boots afresh. */
            if (menu_restart_armed_)
            {
                menu_restart_armed_ = false;
                return close(4);
            }
            menu_restart_armed_ = true;
            menu_note_ = tr("Press Cross again to start the game over. Unsaved progress is lost.");
            sfx(Sound::MenuScroll);
        }
        break;
    case Kind::Shot:
        if (cross)
        {
            menu_shot_ = true; /* the host takes it, between two of the menu's frames */
            sfx(Sound::LaunchGame);
        }
        break;
    case Kind::Save:
    case Kind::Load:
        if (cross)
        {
            const bool any = menu_slot_used_[0] || menu_slot_used_[1] || menu_slot_used_[2];
            if (row.kind == Kind::Load && !any)
            {
                menu_note_ = tr("No save states for this game yet.");
                menu_note_time_ = time_;
                sfx(Sound::MovingTab);
                break;
            }
            menu_slots_mode_ = row.kind == Kind::Save ? 1 : 2;
            menu_confirm_ = false;
            if (row.kind == Kind::Load && !menu_slot_used_[menu_slot_])
            {
                /* Start on the newest slot there is. */
                long long newest = -1;
                for (int i = 0; i < porpoise::states::kSlots; ++i)
                    if (menu_slot_used_[i] && menu_slot_time_[i] > newest)
                    {
                        newest = menu_slot_time_[i];
                        menu_slot_ = i;
                    }
            }
            sfx(Sound::DetailsFlip);
        }
        break;
    case Kind::FastForward:
        if (dir)
        {
            menu_ff_ = (menu_ff_ + (dir < 0 ? 2 : 1)) % 3;
            menu_change_ = "fast_forward";
            sfx(Sound::MenuScroll);
        }
        break;
    case Kind::Customize:
        if (cross && std::strcmp(row.key, "wii_recal") == 0)
            open_wii_setup_in_game(true);
        else if (cross && std::strcmp(row.key, "wii_setup") == 0)
            open_wii_setup_in_game(false);
        else if (cross)
            open_mapping_in_game();
        break;
    case Kind::SaveSetup:
    case Kind::UseSetup:
        if (left || right)
        {
            menu_setup_ = (menu_setup_ + (left ? setups::kCount - 1 : 1)) % setups::kCount;
            menu_confirm_ = false;
            sfx(Sound::MenuScroll);
        }
        else if (cross)
        {
            const setups::Setup su = setups::get(menu_setup_);
            const std::string n = std::to_string(menu_setup_ + 1);
            if (row.kind == Kind::SaveSetup)
            {
                if (su.exists && !menu_confirm_)
                {
                    menu_confirm_ = true;
                    menu_note_ = trf("Press Cross again to replace setup {n}.", {{"n", n}});
                    menu_note_time_ = time_;
                    sfx(Sound::MovingTab);
                    break;
                }
                menu_confirm_ = false;
                const bool ok = setups::save(menu_setup_, p, menu_game_ ? menu_game_->title : "");
                menu_note_ = ok ? trf("Saved as setup {n}.", {{"n", n}}) : tr("The setup couldn't be saved.");
                menu_note_time_ = time_;
                sfx(ok ? Sound::LaunchGame : Sound::MovingTab);
            }
            else if (!su.exists)
            {
                menu_note_ = tr("That setup is empty.");
                menu_note_time_ = time_;
                sfx(Sound::MovingTab);
            }
            else if (menu_game_)
            {
                std::vector<std::string> keys = Settings::keys_in(game_settings_path(*menu_game_));
                if (setups::apply(menu_setup_, p, &keys))
                {
                    menu_borders_ = porpoise::borders::list();
                    menu_note_ = trf("Setup {n} is ready: Square applies it.", {{"n", n}});
                    menu_note_time_ = time_;
                    sfx(Sound::LaunchGame);
                }
            }
        }
        break;
    case Kind::Info:
        break;
    case Kind::Cheat:
        if (dir && menu_game_ && row.index >= 0 && row.index < int(menu_cheats_.size()))
        {
            /* As Game settings does it (ui_app_settings): named in
             * [<kind>_Enabled] when on and Dolphin doesn't turn it on by
             * itself, in [<kind>_Disabled] when off and Dolphin would. A
             * cheat (not a patch) needs Dolphin's cheats on for the game. */
            const Cheat &c = menu_cheats_[std::size_t(row.index)];
            const bool on = !menu_cheat_on_[std::size_t(row.index)];
            menu_cheat_on_[std::size_t(row.index)] = on;
            for (const std::string &k : {cheat_key(c, true), cheat_key(c, false)})
                p.forget(k);
            const std::string k = on && !c.default_on ? cheat_key(c, true) : !on && c.default_on ? cheat_key(c, false) : "";
            if (!k.empty())
                p.set(k, "1");
            if (on && c.kind != "OnFrame")
                p.cheats = true; /* a cheat (not a patch) needs the game's cheats on (Apply saves it) */
            sfx(Sound::MenuScroll); /* waits for Apply; takes effect the next time the game starts */
        }
        break;
    case Kind::Int:
        if (dir)
        {
            const int n = int(row.values.size());
            int v = *row.iv + dir;
            if (cross && !left && !right && v > row.min + n - 1)
                v = row.min; /* Cross goes round */
            v = std::clamp(v, row.min, row.min + n - 1);
            if (v != *row.iv)
            {
                *row.iv = v;
                key = row.key;
            }
        }
        break;
    case Kind::Bool:
        if (dir)
        {
            *row.bv = !*row.bv;
            key = row.key;
        }
        break;
    case Kind::Border:
        if (dir && !menu_borders_.empty())
        {
            int i = 0;
            for (std::size_t b = 0; b < menu_borders_.size(); ++b)
                if (menu_borders_[b].name == p.border)
                    i = int(b);
            const int n = int(menu_borders_.size());
            i = (i + (dir < 0 ? n - 1 : 1)) % n;
            p.border = menu_borders_[std::size_t(i)].name;
            key = row.key;
        }
        break;
    }
    std::vector<std::string> also;
    bool save_key = true;
    if (key == "audio_preset")
    {
        /* A preset sets the sound rows; they are what is saved. */
        p.use_audio_preset(g_audio_preset);
        also = {"audio_pull", "audio_buffer", "audio_fill", "audio_stretch"};
        save_key = false;
    }
    if (key == "wide" && p.widescreen)
    {
        p.widescreen = false; /* 2.0's hack switch gives way to the choice */
        also.push_back("widescreen");
    }
    if (key == "wii_preset")
    {
        /* A preset brings its whole set-up. */
        if (p.wii_preset > 0 && p.use_wii_preset(p.wii_preset - 1))
            also = {"wii_controller", "wii_grip", "wii_pointer", "wii_speed", "wii_screen_x", "wii_screen_y",
                    "wii_smooth", "wii_reach"};
    }
    (void)save_key; /* the keys that differ are what Apply saves */
    if (!key.empty())
        sfx(Sound::MenuScroll); /* waits for Apply (Square, or when the menu closes) */
    /* What you see change (a border, a screen filter) is used and saved at once. */
    if (!key.empty() && applies_at_once(key) && !menu_draft_.changed_keys(*menu_play_).empty())
    {
        std::vector<std::string> only;
        for (const std::string &k : menu_draft_.changed_keys(*menu_play_))
            if (applies_at_once(k))
                only.push_back(k);
        apply_menu_keys(only, true);
    }
    return 0;
}

namespace
{
/* In a game: changes that take effect only when it starts again. */
bool needs_game_restart(const std::string &key)
{
    return key == "wide" || key == "shader_mode" || key == "dsp_accurate" || key == "audio_pull" ||
           key.rfind("gfx_", 0) == 0 || key.rfind("dolphin.", 0) == 0 || key == "cheats" || key == "fast_float" || key == "accurate_fma" ||
           key == "wait_shaders";
}
} // namespace

void App::apply_menu_pending()
{
    if (menu_play_)
        apply_menu_keys(menu_draft_.changed_keys(*menu_play_), false);
}

void App::apply_menu_keys(const std::vector<std::string> &pending, bool quiet)
{
    if (!menu_play_ || pending.empty())
        return;
    menu_play_->copy_keys(menu_draft_, pending);
    if (menu_game_)
    {
        /* Saved as this game's own settings, so they stick next time. */
        std::vector<std::string> keys = Settings::keys_in(game_settings_path(*menu_game_));
        for (const std::string &k : pending)
            if (std::find(keys.begin(), keys.end(), k) == keys.end() && k != "p1_port")
                keys.push_back(k); /* (Player 1's Port from here is for this time only) */
        /* A code switched on needs the game's cheats on, saved: this run may
         * have them on only for its widescreen code. */
        const bool code = std::any_of(pending.begin(), pending.end(),
                                      [](const std::string &k) { return k.rfind("dolphin.", 0) == 0; });
        if (code && menu_play_->cheats && std::find(keys.begin(), keys.end(), "cheats") == keys.end())
            keys.push_back("cheats");
        /* A code's switch back to its default is no key at all. */
        keys.erase(std::remove_if(keys.begin(), keys.end(),
                                  [&](const std::string &k) {
                                      return k.rfind("dolphin.", 0) == 0 && menu_play_->get(k).empty();
                                  }),
                   keys.end());
        mkdir((data_dir_ + "/game-settings").c_str(), 0777);
        menu_play_->save_keys(game_settings_path(*menu_game_), keys);
    }
    /* The host applies each in turn (take_menu_change). */
    std::string restart;
    bool wii = false;
    for (const std::string &k : pending)
    {
        if (k.rfind("wii_", 0) == 0)
            wii = true;
        else if (k.rfind("dolphin.", 0) != 0)
            menu_changes_.push_back(k);
        if (needs_game_restart(k))
            restart = k;
    }
    if (wii)
        menu_changes_.push_back("wii_setup"); /* the whole Wii Remote set-up, pointer included */
    if (quiet)
        return; /* the draft keeps any other change, still waiting */
    menu_draft_ = *menu_play_;
    sfx(Sound::LaunchGame);
    if (!restart.empty())
    {
        menu_prompt_ = 2;
        menu_prompt_choice_ = 1;
    }
    else
    {
        menu_note_ = tr("Applied.");
        menu_note_time_ = time_;
    }
}

void App::set_sound_pulled(bool pulled)
{
    g_sound_pulled = pulled;
}

/* ---- drawing ---------------------------------------------------------------------------- */

/* The controller with a line out to each button, and at each line's end the
 * GameCube button it is. w is the drawing's width; the labels go beside it. */
void App::draw_controller_lines(float x, float y, float w, const Mapping &m)
{
    Gfx &g = *g_;
    if (!lines_art_tried_)
    {
        lines_art_tried_ = true;
        lines_art_ = g.texture_file(g.asset_dir() + "/ui/controller-lines.png", 2048);
    }
    if (!lines_art_)
        return;
    const float k = w / kLinesW, h = kLinesH * k;
    g.image(lines_art_, x, y, w, h, g.tone(rgba(0xBFD8FF, 0.95f)));

    int on_control[CtlCount];
    std::fill(std::begin(on_control), std::end(on_control), -1);
    for (int gc = 0; gc < GcCount; ++gc)
        if (m.control[gc] >= 0 && m.control[gc] < CtlCount)
            on_control[int(m.control[gc])] = gc;

    /* One label line: a DualSense icon and what it is (a chip, or a word). */
    struct Item
    {
        Icon icon;
        int control; /* -1: no chip */
        std::string text;
    };
    const float ih = 30, chip_h = 24, gap = 6;
    auto item_w = [&](const Item &it) {
        float iw = ih + gap;
        if (!it.text.empty())
            iw += g.measure(Font::SemiBold, ts(18), it.text);
        else
            iw += gc_chip_width(it.control >= 0 ? on_control[it.control] : -1, chip_h);
        return iw;
    };
    auto draw_group = [&](EndId end, const std::vector<Item> &items) {
        const LineEnd &e = kEnds[end];
        const float ex = x + (e.x - kLinesX0) * k, ey = y + (e.y - kLinesY0) * k;
        float iy = ey - (float(items.size()) - 1) * ih * 0.5f;
        if (end == kEndTouch)
            iy = ey - 6; /* the touch pad's line ends going up: sit beside its end */
        for (const Item &it : items)
        {
            const float iw = item_w(it);
            float ix = e.left ? ex - 10 - iw : ex + 10;
            g.icon(it.icon, ix + ih * 0.5f, iy, ih + 6, rgba(0xDCE8FF));
            ix += ih + gap;
            if (!it.text.empty())
                g.text_mid(Font::SemiBold, ts(18), ix, iy, kLavender, Align::Left, it.text);
            else
            {
                const int gc = it.control >= 0 ? on_control[it.control] : -1;
                draw_gc_chip(gc, ix + gc_chip_width(gc, chip_h) * 0.5f, iy, chip_h);
            }
            iy += ih;
        }
    };
    draw_group(kEndL, {{Icon::L1, CtlL1, ""}, {Icon::L2, CtlL2, ""}});
    draw_group(kEndR, {{Icon::R1, CtlR1, ""}, {Icon::R2, CtlR2, ""}});
    draw_group(kEndTouch, {{Icon::TouchPad, CtlTouch, ""}});
    draw_group(kEndCreate, {{Icon::Create, -1, ""}});
    draw_group(kEndOptions, {{Icon::Options, CtlOptions, ""}});
    draw_group(kEndDPad, {{Icon::DPadUp, CtlUp, ""}, {Icon::DPadDown, CtlDown, ""}, {Icon::DPadLeft, CtlLeft, ""},
                          {Icon::DPadRight, CtlRight, ""}});
    draw_group(kEndFace, {{Icon::Triangle, CtlTriangle, ""}, {Icon::Square, CtlSquare, ""},
                          {Icon::Circle, CtlCircle, ""}, {Icon::Cross, CtlCross, ""}});
    draw_group(kEndLStick, {{Icon::LStick, -1, tr("Control Stick")}, {Icon::L3, CtlL3, ""}});
    draw_group(kEndRStick, {{Icon::RStick, -1, tr("C-Stick")}, {Icon::R3, CtlR3, ""}});
}

void App::draw_game_menu(double time)
{
    apply_look();
    {
        /* Over the game: liquid glass would bend the menus' room, which
         * isn't behind it here. */
        Look over_game = g_->look();
        over_game.panel_style = 0;
        g_->set_look(over_game);
    }
    if (!menu_play_)
        return;
    Gfx &g = *g_;
    /* The Revolution look: the menu in white over the game. */
    struct Tone
    {
        Gfx &g;
        ~Tone() { g.set_tone(false); }
    } tone{g};
    g.set_tone(revolution());
    if (ws_in_game_)
    {
        g.set_layer();
        draw_wii_setup(time);
        return;
    }
    if (map_in_game_)
    {
        g.set_layer();
        draw_mapping(time);
        return;
    }
    const float t = ease_out(menu_anim_);
    Settings &p = menu_draft_; /* the rows change it; Apply hands it to the game */
    const std::vector<std::string> waiting = menu_draft_.changed_keys(*menu_play_);
    const Color kPending = rgba(0xFFC857);
    g.set_layer();
    g.panel(0, 0, 1920, 1080, rgba(0x02040C, 0.55f * t), 1, 0);

    /* The panel slides in from the left. */
    const float w = 880, x = 40, y = 40, h = 1000;
    g.set_layer(-(w + 80) * (1.0f - t), 0, 0.3f + 0.7f * t);
    Glass face;
    face.tint = rgba(0x13308A, 0.88f);
    face.rim = rgba(0x8BD9FF);
    face.radius = kR;
    face.rim_w = 2.2f;
    face.glow = 12;
    face.phase = 0.2f;
    glass_block(g, x + w * 0.5f, y + h * 0.5f, w, h, 22, 0, 0, 36, face);

    /* Header: the game's cover and name. */
    float hy = y + 34;
    if (menu_game_)
    {
        Texture *cover = cover_of(*menu_game_);
        const float cw = 84, ch = 118, cx = x + 40, cy = hy;
        if (cover)
        {
            float uv[4];
            cover_uv(cover, cw, ch, uv);
            Corner c[4] = {{cx, cy, 1}, {cx + cw, cy, 1}, {cx + cw, cy + ch, 1}, {cx, cy + ch, 1}};
            g.quad3d(cover, c, cw, ch, kWhite, kR * 0.6f, false, false, uv);
            gloss_over(g, c, cw, ch, kR * 0.6f, 0.2f);
        }
        else
            g.panel(cx, cy, cw, ch, kTileFill, 0.6f, kR * 0.6f, kEdge, 1.6f);
        g.text_mid(Font::SemiBold, ts(21), cx + cw + 28, cy + 22, kCyan, Align::Left, tr("PAUSED"), 3.0f);
        const auto lines = wrap(g, Font::Bold, ts(32), menu_game_->title, w - cw - 110, 2);
        float ly = cy + 62;
        for (const std::string &l : lines)
        {
            g.text_mid(Font::Bold, ts(32), cx + cw + 28, ly, kWhite, Align::Left, l);
            ly += 40;
        }
        hy = cy + ch + 24;
    }

    /* The tabs, L1 and R1 at their sides. */
    const int tabs = kTabCount + (menu_has_achievements() ? 1 : 0);
    {
        const float ty = hy + 28, tab_h = 50;
        float size = tabs > kTabCount ? ts(22) : ts(24);
        const float pad = tabs > kTabCount ? 28.0f : 36.0f;
        const char *names[kTabCount + 1] = {kTabNames[0], kTabNames[1], kTabNames[2], kTabNames[3],
                                            kTabNames[4], kTabNames[5], "Achievements"};
        float widths[kTabCount + 1], total = 0;
        for (int pass = 0; pass < 2; ++pass)
        {
            total = 0;
            for (int i = 0; i < tabs; ++i)
            {
                widths[i] = g.measure(Font::Bold, size, tr(names[i])) + pad;
                total += widths[i];
            }
            /* Longer names (some languages'): smaller, to fit between L1 and R1. */
            const float room = w - 150;
            if (total <= room)
                break;
            size *= room / total;
        }
        float tx = x + (w - total) * 0.5f;
        g.glyph(Glyph::L1, tx - 44, ty, 40, rgba(0xE8F0FF));
        g.glyph(Glyph::R1, tx + total + 44, ty, 40, rgba(0xE8F0FF));
        for (int i = 0; i < tabs; ++i)
        {
            const bool on = i == menu_tab_;
            if (on)
                g.panel(tx, ty - tab_h * 0.5f, widths[i], tab_h, rgba(0x1F63F0), 0.62f, kR, rgba(0x7FD9FF), 1.6f, 6,
                        0.35f);
            g.text_mid(on ? Font::Bold : Font::SemiBold, size, tx + widths[i] * 0.5f, ty, on ? kWhite : kSoft,
                       Align::Center, tr(names[i]));
            tx += widths[i];
        }
        hy = ty + tab_h * 0.5f + 16;
    }
    g.panel(x + 36, hy, w - 72, 1.5f, rgba(0x6F8FE0, 0.5f), 1, 0);
    hy += 10;

    if (menu_tab_ == kTabCount && tabs > kTabCount)
    {
        /* Achievements: the list here, the one in focus beside the menu. */
        draw_achievement_list(x + 48, hy + 18, w - 96, y + h - 84 - (hy + 18));
        g.set_layer(0, 0, t);
        draw_achievement_card(1000, 120, 840, 840);
        g.set_layer(-(w + 80) * (1.0f - t), 0, 0.3f + 0.7f * t);
        const float py = y + h - 50, size = ts(22);
        float px = x + 52;
        for (const auto &pr : std::vector<std::pair<Glyph, std::string>>{{Glyph::DPad, tr("Browse")},
                                                                         {Glyph::Circle, tr("Resume")}})
        {
            g.glyph(pr.first, px + 15, py, 30, kWhite);
            px += 42;
            px += g.text_mid(Font::SemiBold, size, px, py, kWhite, Align::Left, pr.second) + 40;
        }
        const std::string tabs_label = tr("Tabs");
        const float tw = g.measure(Font::SemiBold, size, tabs_label), right = x + w - 48;
        g.text_mid(Font::SemiBold, size, right, py, kWhite, Align::Right, tabs_label);
        if (g.has_icons())
        {
            g.glyph(Glyph::R1, right - tw - 30, py, 30, kWhite);
            g.glyph(Glyph::L1, right - tw - 76, py, 30, kWhite);
        }
        g.set_layer();
        return;
    }

    /* Rows. */
    Patches patches;
    patches.cheats = &menu_cheats_;
    const bool wii = menu_game_ && menu_game_->platform == "Wii";
    g_menu_game_id = menu_game_ ? menu_game_->id : std::string();
    const std::vector<Row> rows = rows_for(menu_tab_, p, wii, &patches);
    if (rows.empty())
    {
        g.set_layer();
        return;
    }
    const float row_h = 56, rx = x + 24, rw = w - 48;
    float ry = hy;
    /* A long tab (Patches; Graphics or Controls with a game's extra rows): a
     * window on its rows, around the focus. */
    int first = 0, last = int(rows.size());
    {
        const int visible = std::max(3, int((y + h - (menu_tab_ == kTabPatches ? 200 : 150) - hy) / row_h));
        if (last > visible)
        {
            first = std::clamp(menu_row_ - visible / 2, 0, last - visible);
            last = first + visible;
        }
        if (first > 0)
            g.glyph(Glyph::Arrow, x + w * 0.5f, hy - 2, 14, kCyan, 0.0f);
    }
    for (int i = first; i < last; ++i)
    {
        const Row &row = rows[std::size_t(i)];
        if (row.gap_before && i > first)
        {
            g.panel(x + 36, ry + 6, w - 72, 1.5f, rgba(0x6F8FE0, 0.5f), 1, 0);
            ry += 14;
        }
        const float cy = ry + row_h * 0.5f;
        const bool on = i == menu_row_;
        if (on)
            g.panel(rx, ry + 4, rw, row_h - 8, rgba(0x1D45B8, 0.9f), 0.7f, kR, kIcy, 2.2f, 8, 0.18f);
        const std::string label = row.label ? tr(row.label) : row.text;
        bool row_waiting = false;
        if (row.key && *row.key)
            row_waiting = std::find(waiting.begin(), waiting.end(), std::string(row.key)) != waiting.end() ||
                          (std::string(row.key) == "audio_preset" &&
                           std::find_if(waiting.begin(), waiting.end(), [](const std::string &k) {
                               return k.rfind("audio_", 0) == 0;
                           }) != waiting.end());
        if (row.kind == Kind::Cheat && row.index >= 0 && row.index < int(menu_cheats_.size()))
        {
            const Cheat &c = menu_cheats_[std::size_t(row.index)];
            for (const std::string &k : {cheat_key(c, true), cheat_key(c, false)})
                row_waiting |= std::find(waiting.begin(), waiting.end(), k) != waiting.end();
        }
        if (row_waiting)
            g.panel(rx + 8, cy - 6, 12, 12, kPending, 1, 6); /* changed, not applied yet */
        g.text_mid(on ? Font::Bold : Font::SemiBold, ts(27), rx + 28, cy, on ? kWhite : kSoft, Align::Left,
                   row.label ? label : fit(g, on ? Font::Bold : Font::SemiBold, ts(27), label, rw * 0.62f));
        std::string value;
        bool arrows = true;
        switch (row.kind)
        {
        case Kind::Info:
            arrows = false;
            value = row.key == std::string("ws_status") ? tr(ws_value(p.ws_plan, wii)) : tr("None");
            break;
        case Kind::Cheat:
            value = tr(row.index >= 0 && row.index < int(menu_cheat_on_.size()) && menu_cheat_on_[std::size_t(row.index)]
                           ? "On"
                           : "Off");
            break;
        case Kind::FastForward:
            value = menu_ff_ == 0 ? tr("Off") : menu_ff_ == 1 ? "2x" : "4x";
            break;
        case Kind::Disc:
        {
            const int pick = g_menu_disc_pick;
            const auto disc = [](int number) { return trf("Disc {n}", {{"n", std::to_string(number + 1)}}); };
            if (pick < g_menu_discs)
                value = disc(g_menu_disc_numbers[std::size_t(pick)]);
            else if (pick - g_menu_discs < int(g_menu_other_discs.size()))
            {
                const auto &[title, number] = g_menu_other_discs[std::size_t(pick - g_menu_discs)];
                value = title;
                if (number > 0)
                    value += "  \xE2\x80\xA2  " + disc(number);
            }
            if (pick == g_menu_disc) /* (first, so a long title doesn't cut it off) */
                value = tr("In the Drive") + "  \xE2\x80\xA2  " + value;
            arrows = g_menu_discs + int(g_menu_other_discs.size()) > 1;
            break;
        }
        case Kind::SaveSetup:
        case Kind::UseSetup:
        {
            const setups::Setup su = setups::get(menu_setup_);
            value = trf("Setup {n}", {{"n", std::to_string(menu_setup_ + 1)}});
            if (su.exists && !su.from.empty())
                value += "  \xE2\x80\xA2  " + su.from;
            else if (!su.exists)
                value += "  \xE2\x80\xA2  " + tr("Empty");
            break;
        }
        case Kind::Int:
            value = tr(row.values[std::size_t(std::clamp(*row.iv - row.min, 0, int(row.values.size()) - 1))]);
            /* Widescreen: what the setting gives this game, as it runs now. */
            if (row.key == std::string("wide") && *row.iv == menu_wide_open_)
                value += "  \xE2\x80\xA2  " + tr(ws_value(p.ws_plan, wii));
            if (row.key == std::string("button_layout") && *row.iv >= LayoutOwn)
                value = trf("My Layout {n}", {{"n", std::to_string(*row.iv - LayoutOwn + 1)}});
            break;
        case Kind::Bool:
            value = tr(*row.bv ? "On" : "Off");
            break;
        case Kind::Border:
            value = tr("None");
            for (const porpoise::borders::Border &b : menu_borders_)
                if (b.name == p.border)
                    value = b.built_in ? tr(b.label) : b.label;
            break;
        default:
            arrows = false;
            break;
        }
        if (!value.empty())
        {
            const float right = rx + rw - 24;
            value = fit(g, Font::Bold, ts(25), value, rw * 0.45f);
            g.text_mid(Font::Bold, ts(25), right - (on ? 30 : 0), cy, row_waiting ? kPending : on ? kWhite : kSoft,
                       Align::Right, value);
            if (on && arrows)
            {
                const float vw = g.measure(Font::Bold, ts(25), value);
                g.glyph(Glyph::Arrow, right - 30 - vw - 22, cy, 16, kCyan, -kPi * 0.5f);
                g.glyph(Glyph::Arrow, right - 8, cy, 16, kCyan, kPi * 0.5f);
            }
        }
        else if (on)
            g.glyph(Glyph::Arrow, rx + rw - 30, cy, 20, kWhite, kPi * 0.5f);
        ry += row_h;
    }
    if (last < int(rows.size()))
        g.glyph(Glyph::Arrow, x + w * 0.5f, ry + 2, 14, kCyan, kPi);

    const Row &focus = rows[std::size_t(std::clamp(menu_row_, 0, int(rows.size()) - 1))];
    float below = ry + 14;
    if (menu_tab_ == kTabGame)
    {
        /* The three slots, with what each one saw: chosen from while
         * saving or loading, a glance otherwise. */
        const bool picking = menu_slots_mode_ != 0;
        const bool saving = menu_slots_mode_ == 1;
        const float sw = 200, sh = 112, sgap = 24, sx0 = x + (w - (sw * 3 + sgap * 2)) * 0.5f;
        if (picking)
        {
            g.text_mid(Font::Bold, ts(24), x + 48, below + 8, kWhite, Align::Left,
                       tr(saving ? "Save to which slot?" : "Load which slot?"));
            below += 30;
        }
        for (int i = 0; i < porpoise::states::kSlots; ++i)
        {
            const float sx = sx0 + float(i) * (sw + sgap), sy = below + 4;
            const bool on = picking && i == menu_slot_;
            const bool dim = picking && !saving && !menu_slot_used_[i]; /* nothing to load there */
            const bool replace = on && saving && menu_confirm_;
            const Color edge = replace ? rgba(0xFFB347) : on ? kIcy : rgba(0x5A68A8, dim ? 0.4f : 0.8f);
            g.panel(sx - 4, sy - 4, sw + 8, sh + 8, rgba(0x07102E, dim ? 0.35f : 0.6f), 1, kR * 0.7f, edge,
                    on ? 2.8f : 1.4f, on ? 12 : 0);
            if (menu_slot_tex_[i])
            {
                /* Fit the picture inside the card, at its own shape. */
                const float a = float(menu_slot_tex_[i]->width) / float(std::max(1, menu_slot_tex_[i]->height));
                float iw = sw, ih = sw / a;
                if (ih > sh)
                {
                    ih = sh;
                    iw = sh * a;
                }
                g.image(menu_slot_tex_[i], sx + (sw - iw) * 0.5f, sy + (sh - ih) * 0.5f, iw, ih,
                        on || !picking ? kWhite : rgba(0xFFFFFF, 0.7f), kR * 0.5f);
            }
            else
                g.text_mid(Font::SemiBold, ts(21), sx + sw * 0.5f, sy + sh * 0.5f, with_alpha(kLavender, 0.8f),
                           Align::Center, menu_slot_used_[i] ? tr("Saved") : tr("Empty"));
            const float lw = g.text_mid(Font::Bold, ts(19), sx + 4, sy + sh + 22, on ? kWhite : kSoft, Align::Left,
                                        trf("Slot {n}", {{"n", std::to_string(i + 1)}}));
            if (menu_slot_used_[i])
            {
                /* The date, and the time when there is room. */
                std::string when = slot_date(menu_slot_time_[i]);
                if (g.measure(Font::Regular, ts(17), when) > sw - lw - 16)
                    when = when.substr(0, when.find("  "));
                g.text_mid(Font::Regular, ts(17), sx + sw - 2, sy + sh + 22, kLavender, Align::Right,
                           fit(g, Font::Regular, ts(17), when, sw - lw - 14));
            }
        }
        below += sh + 44;
    }
    if (menu_tab_ == kTabControls && menu_game_ && menu_game_->platform == "Wii" &&
        p.wii_controller != porpoise::pad::WiiGameCube)
    {
        /* Beside the menu: how the DualSense is the Wii controller, as set. */
        g.set_layer();
        /* With Grip on Auto, the hold as it is read right now. */
        const porpoise::pad::State p1 = porpoise::pad::snapshot(0), p2 = porpoise::pad::snapshot(1);
        draw_wii_controls(960, 40, 920, 1000, p.wii_config(true), t, p1.motion.valid ? p1.motion.pose : -1,
                          p2.motion.valid ? p2.motion.pose : -1);
        g.set_layer(-(w + 80) * (1.0f - t), 0, 0.3f + 0.7f * t);
    }
    else if (menu_tab_ == kTabControls)
    {
        /* The controller and its buttons, as this game has them: smaller
         * when the rows leave less room above the help line. */
        const float room = (y + h - 112 - 10) - (below + 36);
        const float aw = std::min(560.0f, room * kLinesW / kLinesH);
        if (aw > 200)
        {
            draw_controller_lines(x + (w - aw) * 0.5f, below + 36, aw, p.mapping());
            below += 36 + kLinesH * aw / kLinesW;
        }
    }

    /* A note after a save or load, what Cross will do while picking a slot,
     * else the row's help. */
    {
        const double since = time_ - menu_note_time_;
        bool note = !menu_note_.empty() && since < 4.0;
        std::string help;
        if (focus.kind == Kind::Cheat && focus.index >= 0 && focus.index < int(menu_cheats_.size()))
            help = cheat_help(menu_cheats_[std::size_t(focus.index)]) + "  " +
                   tr("Takes effect the next time the game starts.");
        else if (focus.key == std::string("ws_status"))
            help = tr(ws_help(p.ws_plan, wii));
        else if (focus.key == std::string("no_codes"))
            help = trf("Dolphin lists none for this game. Your own codes go in /data/porpoise/cheats/{id}.ini.",
                       {{"id", menu_game_ ? menu_game_->id : std::string("<ID>")}});
        else
            help = tr(help_for(focus, p));
        std::string text = note ? menu_note_ : help;
        bool warn = false;
        if (!note && !waiting.empty() && !menu_slots_mode_)
        {
            text = plural((long long)waiting.size(), "1 change waiting: Square applies it.",
                          "{n} changes waiting: Square applies them.");
            warn = note = true;
        }
        if (menu_slots_mode_ && !note)
        {
            const std::string n = std::to_string(menu_slot_ + 1);
            if (menu_slots_mode_ == 2)
                text = menu_slot_used_[menu_slot_] ? trf("Cross loads slot {n}. The game goes back to that moment.", {{"n", n}})
                                                   : tr("That slot is empty.");
            else if (!menu_slot_used_[menu_slot_])
                text = trf("Cross saves this moment in slot {n}.", {{"n", n}});
            else if (!menu_confirm_)
                text = trf("Slot {n} has a save state. Cross, then Cross again, replaces it.", {{"n", n}});
            else
            {
                text = trf("Press Cross again to replace slot {n}.", {{"n", n}});
                warn = true;
            }
            note = true;
        }
        const int max_lines = menu_tab_ == kTabPatches ? 3 : 2;
        const float ny = std::min(below + 10, y + h - 112 - (max_lines - 2) * 30.0f);
        const auto lines = wrap(g, Font::Regular, ts(22), text, w - 96, max_lines);
        float ly = ny;
        for (const std::string &l : lines)
        {
            g.text_mid(note ? Font::SemiBold : Font::Regular, ts(22), x + 48, ly,
                       warn ? rgba(0xFFC266) : note ? kCyan : kLavender, Align::Left, l);
            ly += 30;
        }
    }

    /* The prompts, smaller, inside the panel. */
    {
        float px = x + 52;
        const float py = y + h - 50, size = ts(22);
        std::vector<std::pair<Glyph, std::string>> prompts = {{Glyph::Cross, tr("Select")},
                                                              {Glyph::Circle, tr("Resume")}};
        if (menu_slots_mode_)
            prompts = {{Glyph::DPad, tr("Slot")},
                       {Glyph::Cross, tr(menu_slots_mode_ == 1 ? "Save" : "Load")},
                       {Glyph::Circle, tr("Back")}};
        else if (!waiting.empty())
            prompts.push_back({Glyph::Square, tr("Apply")});
        for (std::size_t i = 0; i < prompts.size(); ++i)
        {
            if (i > 0)
            {
                g.panel(px - 2, py - 16, 1.5f, 32, rgba(0x6F8FE0, 0.6f), 1, 0);
                px += 26;
            }
            g.glyph(prompts[i].first, px + 15, py, 30, kWhite);
            px += 42;
            px += g.text_mid(Font::SemiBold, size, px, py, kWhite, Align::Left, prompts[i].second) + 30;
        }
        /* L1 / R1 tabs, on the right. */
        const std::string tabs = tr("Tabs");
        const float tw = g.measure(Font::SemiBold, size, tabs);
        const float right = x + w - 48;
        g.text_mid(Font::SemiBold, size, right, py, kWhite, Align::Right, tabs);
        if (g.has_icons())
        {
            g.glyph(Glyph::R1, right - tw - 30, py, 30, kWhite);
            g.glyph(Glyph::L1, right - tw - 76, py, 30, kWhite);
        }
    }

    /* A question over the menu: Apply or Discard before it closes, or
     * starting the game over for changes that need it. */
    if (menu_prompt_)
    {
        g.panel(x + 8, y + 8, w - 16, h - 16, rgba(0x050A24, 0.72f), 1, kR);
        const float bw = w - 120, bh = 310, bx = x + 60, by = y + h * 0.5f - bh * 0.5f;
        g.panel(bx, by, bw, bh, rgba(0x0F1F63, 0.96f), 0.9f, kR, rgba(0x8BD9FF), 2.0f, 12, 0.2f);
        const bool restart = menu_prompt_ == 2;
        g.text_mid(Font::Bold, ts(32), bx + bw * 0.5f, by + 60, kWhite, Align::Center,
                   tr(restart ? "Restart the game to finish?" : "Apply your changes?"));
        const std::string message =
            restart ? (menu_play_->quick_resume
                           ? tr("Some of the changes take effect when the game starts again. Quick Resume brings you "
                                "back to this moment.")
                           : tr("Some of the changes take effect when the game starts again. Unsaved progress is lost; "
                                "a save state keeps it."))
                    : plural((long long)waiting.size(), "You changed 1 setting. Circle keeps editing.",
                             "You changed {n} settings. Circle keeps editing.");
        float ly = by + 120;
        for (const std::string &l : wrap(g, Font::Regular, ts(24), message, bw - 60, 3))
        {
            g.text_mid(Font::Regular, ts(24), bx + bw * 0.5f, ly, kLavender, Align::Center, l);
            ly += 34;
        }
        const std::string names[2] = {tr(restart ? "Restart Now" : "Apply"), tr(restart ? "Later" : "Discard")};
        const float cw = (bw - 90) * 0.5f, ch = 64, cyb = by + bh - 70;
        for (int b = 0; b < 2; ++b)
        {
            const bool lit = (b == 0) == (menu_prompt_choice_ == 1);
            const float cx = bx + 30 + b * (cw + 30);
            g.panel(cx, cyb - ch * 0.5f, cw, ch, lit ? rgba(0x1F63F0, 0.95f) : rgba(0x07102E, 0.6f), 0.8f, kR,
                    lit ? rgba(0x8BD9FF) : rgba(0x3D5AB0, 0.85f), lit ? 2.0f : 1.4f);
            g.text_mid(Font::Bold, ts(26), cx + cw * 0.5f, cyb, lit ? kWhite : kSoft, Align::Center,
                       fit(g, Font::Bold, ts(26), names[b], cw - 24));
        }
    }

    /* Saving or loading: the panel waits under a veil, with a spinner. */
    if (menu_busy_.kind != MenuRequest::None)
    {
        ++menu_busy_frames_;
        g.panel(x + 8, y + 8, w - 16, h - 16, rgba(0x050A24, 0.72f), 1, kR);
        const float cx = x + w * 0.5f, cy = y + h * 0.5f - 20;
        const float spin = float(time - menu_busy_start_) * 7.0f;
        for (int i = 0; i < 10; ++i)
        {
            const float a = float(i) / 10.0f * 2.0f * kPi;
            const float fade = std::fmod(float(i) / 10.0f - spin / (2.0f * kPi) + 10.0f, 1.0f);
            g.blob(cx + std::cos(a) * 46, cy + std::sin(a) * 46, 22, 22, rgba(0x8BD9FF, 0.25f + 0.75f * fade));
        }
        const std::string n = std::to_string(menu_busy_.slot + 1);
        g.text_mid(Font::Bold, ts(30), cx, cy + 100, kWhite, Align::Center,
                   menu_busy_.kind == MenuRequest::Save ? trf("Saving to slot {n}\xE2\x80\xA6", {{"n", n}})
                                                        : trf("Loading slot {n}\xE2\x80\xA6", {{"n", n}}));
        g.text_mid(Font::Regular, ts(22), cx, cy + 142, kLavender, Align::Center,
                   tr("This can take a few seconds."));
    }
    g.set_layer();
}

void App::return_from_game()
{
    menu_free_slots();
    details_ach_for_.clear(); /* the game just played may have unlocked some */
    ach_live_ = false;
    menu_game_ = nullptr;
    menu_play_ = nullptr;
    map_in_game_ = false;
    menu_wide_open_ = -2;
    menu_cheats_.clear();
    menu_cheat_on_.clear();
    menu_ff_ = 0;
    launch_ = nullptr;
    screen_ = Screen::Main;
    tab_ = Tab::Library;
    screen_anim_ = 1.0f;
    held_ = prev_ = 0xFFFFFFFFu; /* the button that quit doesn't press anything here */
    cards_scanned_ = false;
    save_scan_.reset(); /* a scan already running read the old state */
    lift_ = 0.4f;
}
} // namespace porpoise::ui
