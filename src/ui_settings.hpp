/* Porpoise - settings: Porpoise's own, and the Dolphin options it manages.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "porpoise_pad.hpp"

namespace porpoise
{
struct Settings
{
    /* Games */
    bool auto_search = true;     /* look in the usual folders and on USB drives */
    bool download_covers = true; /* box art from GameTDB */
    bool download_info = true;   /* descriptions, developers, dates from GameTDB */
    std::vector<std::string> folders; /* folders the player added */

    /* Video */
    int resolution = 3;      /* dolphin_efb_scale 1..6: 1080p; 4x and up are experimental */
    bool widescreen = false; /* 2.0's widescreen hack switch: now only read, as Widescreen On (wide_mode) */
    int wide = 0;            /* Widescreen: 0 Auto, 1 On, 2 Off (ui_widescreen.hpp) */
    /* How the game being started runs (ui_widescreen Plan, as an int; -1 the
     * launcher): decides dolphin_widescreen_hack (3, 4) and the aspect (1:
     * 16:9). Not saved. */
    int ws_plan = -1;
    int aspect = 3;          /* dolphin_aspect_ratio 0..3: Auto, 16:9, 4:3, Stretch */
    int anisotropy = 2;      /* dolphin_max_anisotropy 0..4 (1x..16x) */
    int texture_filter = 0;  /* dolphin_force_texture_filtering_mode 0..2 */
    int antialiasing = 0;    /* dolphin_anti_aliasing 0..6 */
    int resampling = 0;      /* dolphin_enhance_output_resampling 0..6 */
    /* Porpoise's own picture, drawn over the game's: how it is scaled and
     * filtered on the way to the TV, and what fills the bars beside a 4:3
     * picture. */
    int screen_filter = 0;   /* Smooth, Sharp, Sharpen, CRT, Arcade CRT, VHS, Soft VHS, 8-bit, Pocket */
    int filter_strength = 6; /* 1..10 */
    /* 3.0 Enhancements on the picture: bloom 0..3 (Off, Low, Medium, High);
     * saturation, contrast and warmth 0..10, 5 as the game has it. */
    int bloom = 0;
    int color_saturation = 5, color_contrast = 5, color_warmth = 5;
    bool enh_advanced = false; /* Enhancements shows every option, not just the everyday ones */
    /* The values porpoise::vk::set_picture_adjust takes. */
    void picture_adjust(float out[4]) const
    {
        static const float kBloom[4] = {0.0f, 0.25f, 0.45f, 0.7f};
        out[0] = 1.0f + float(color_saturation - 5) * 0.12f;
        out[1] = 1.0f + float(color_contrast - 5) * 0.06f;
        out[2] = float(color_warmth - 5) * 0.03f;
        out[3] = kBloom[bloom < 0 ? 0 : bloom > 3 ? 3 : bloom];
    }
    std::string border;      /* "" none, or a border's name (built in, or a PNG in /data/porpoise/borders) */
    std::string online_dns;  /* Wii games online: the DNS server to ask ("" the console's own; global only) */
    bool fps_overlay = false;
    int output_res = 0;           /* the picture sent to the TV: 0 the PS5's own, 1 1080p, 2 1440p, 3 4K (next start) */
    bool vsync = true;            /* frames on the TV's own vblank (porpoise_pacer); off: Porpoise's timer */
    /* 3.1: frame generation (beta): in-between frames from the game's own
     * (porpoise_framegen). Steady 30 takes the game's frames 30 a second at
     * the most. The rest are developer options, for tuning it. */
    bool frame_gen = false;
    bool fg_steady30 = false;
    int fg_multiplier = 0;   /* 0 2x, 1 3x, 2 4x */
    int fg_pacing = 0;       /* 0 steady frame rates only, 1 whenever there is room */
    bool fg_safety = true;   /* none for a while when they cost the game its speed */
    bool fg_debug_view = false;
    bool fg_log = false;     /* a CSV of every run in the debug folder */
    bool fps_detail = false; /* the FPS overlay with the frame's times and frame generation's state */
    /* Graphics (advanced) */
    int shader_mode = 2;          /* dolphin_shader_compilation_mode 0..3: async ubershaders */
    bool threaded_gpu = false;    /* RADV_THREADED_RECORDING for the game's device (beta; off until it measures faster) */
    int texture_cache = 0;        /* dolphin_texture_cache_accuracy: Fast, Middle, Safe */
    bool pixel_lighting = false;  /* dolphin_pixel_lighting */
    bool disable_fog = false;     /* dolphin_disable_fog */
    bool crop_overscan = false;   /* dolphin_crop_overscan */
    bool custom_textures = false; /* dolphin_load_custom_textures (+ prefetch) */
    bool skip_dupes = true;       /* dolphin_skip_dupe_frames */
    /* Dolphin's built-in graphics mods (porpoise_gfxmods), per game: bloom and
     * depth of field 0 the game's own, 1 off, 2 blurred, 3 native resolution;
     * hide the HUD; the game's own mod. */
    int gfx_bloom = 0;
    int gfx_dof = 0;
    bool gfx_hud = false;
    bool gfx_extra = false;
    /* Whether the game being started has a mod on: dolphin_mods_enabled. Not saved. */
    bool gfx_mods_on = false;
    bool fast_states = true;      /* save states leave out Dolphin's GPU texture cache (GFX.ini) */
    bool quick_resume = false;     /* leaving a game keeps where it was; it picks up there next time (beta) */
    /* Audio */
    int volume = 10; /* 0..10 */
    bool muted = false;
    /* Porpoise's own sound (the menus, not the games) */
    bool menu_music = true;
    int music_volume = 4; /* 0..10: a quiet bed by default */
    bool menu_sounds = true;
    int sound_set = 0;      /* the menus' sounds: porpoise::sfx::Set (0 Crisp, 1 Soft, 2 Porpoise's own) */
    int sounds_volume = 8;
    /* Controls */
    /* 0 GameCube, 1 PlayStation, 2..5 the player's own layouts 1..4. */
    int button_layout = 1; /* PlayStation by default (1.1 build 14) */
    /* Sticks turned around (0 no, 1 up-down, 2 left-right, 3 both): for games
     * whose camera goes the other way. */
    int invert_main = 0, invert_c = 0;
    int p1_port = 0; /* 4.0: the GameCube port player 1's controller is in (0..3; Psycho Mantis) */
    /* The control stick's feel: how much a small push moves (0 normal, 1 lower,
     * 2 low, 3 lowest: a curve, a full push still full), and a dead zone in
     * the middle (0 off, 1 small, 2 medium, 3 large). For twitchy steering. */
    int stick_sensitivity = 0, stick_deadzone = 0;
    /* The player's own layouts, kept with the global settings (a game only
     * picks one): a porpoise::pad::Control for each GameCube input, in
     * porpoise::pad::GcInput order. Each starts as the GameCube layout. */
    static constexpr int kPresets = 4;
    int presets[kPresets][porpoise::pad::GcCount] = {
        {0, 2, 1, 3, 5, 6, 7, 10, 12, 13, 14, 15}, {0, 2, 1, 3, 5, 6, 7, 10, 12, 13, 14, 15},
        {0, 2, 1, 3, 5, 6, 7, 10, 12, 13, 14, 15}, {0, 2, 1, 3, 5, 6, 7, 10, 12, 13, 14, 15}};
    bool rumble = true;
    /* The player's own Wii buttons (global only), one table per Wii controller
     * (porpoise::pad::wii_button_set): which DualSense control does what
     * Porpoise's layout puts on each control. Each starts as Porpoise's. */
    static constexpr int kWiiButtonSets = 4;
    int wii_buttons[kWiiButtonSets][porpoise::pad::CtlCount] = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}};
    /* A second set of them ("Mine 2"); wii_buttons above is "Mine 1". */
    int wii_buttons2[kWiiButtonSets][porpoise::pad::CtlCount] = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}};
    /* Which Wii buttons games use: 0 Porpoise's (never changed), 1 Mine 1,
     * 2 Mine 2. Mine 1 starts as Porpoise's, so 1 changes nothing until the
     * player edits it (and keeps what 3.0 Alpha Build 5 and before did). */
    int wii_buttons_use = 1;
    static constexpr int kWiiButtonSlots = 3;
    /* The tables of a slot (nullptr for Porpoise's), and the ones in use. */
    int (*wii_slot(int slot))[porpoise::pad::CtlCount]
    {
        return slot == 1 ? wii_buttons : slot == 2 ? wii_buttons2 : nullptr;
    }
    const int (*wii_buttons_in_use() const)[porpoise::pad::CtlCount];
    /* Wii games: the DualSense as the Wii Remote (0), or real Wii Remotes
     * through a Mayflash DolphinBar (1; WiiConfig::real_remotes). */
    int wii_source = 0;
    /* The real Wii Remotes' own speakers (porpoise_usbhid: off, no speaker data
     * goes to them at all). Not the TV's sound or the DualSense's speaker. */
    bool real_wiimote_speaker = true;
    /* Wii games: how the DualSense plays the Wii Remote (porpoise::pad::WiiConfig). */
    int wii_controller = 0; /* Remote + Nunchuk, Remote, sideways, Classic, two controllers, GameCube */
    int wii_pointer = 0;    /* gyro, touch pad, right stick */
    int wii_speed = 5;      /* the gyro pointer's speed, 1..10; 0: as measured for the screen (wii_screen_x/y) */
    int wii_grip = 0;       /* auto; both hands; upright, trigger to the TV; upright, facing you (pad::WiiGrip) */
    bool wii_motion = true; /* the DualSense's motion is the Remote's */
    bool wii_shake = true;  /* a flick shakes the Remote */
    int wii_motion_plus = 0; /* Wii MotionPlus: Auto (the games that need one), On, Off */
    bool wii_invert_x = false, wii_invert_y = false;
    /* The Wii Remote setup: the screen's half-width and half-height as angles
     * from where the player sits, in tenths of a degree (0: not measured); and
     * whether to offer the setup before each Wii game. */
    int wii_screen_x = 0, wii_screen_y = 0;
#ifdef PORPOISE_DESKTOP
    bool wii_setup_ask = false; /* a computer's controller may have no motion to aim with */
#else
    bool wii_setup_ask = true;
#endif
    bool wii_setup_advanced = false; /* the setup's extra pages: size and distance, fine-tuning, presets */
    int wii_smooth = 1;              /* pointer smoothing: off, light, medium, strong */
    int wii_reach = 100;             /* percent */
    int wii_range = 0; /* the pointer's range: 0 Auto, 1 Standard, 2 Wide, 3 Extra Wide (WiiConfig::range) */
    int wii_size = 27, wii_distance = 30; /* the screen's diagonal (inches) and how far away (tenths of feet) */
    /* Wii presets: a whole Wii Remote set-up kept under a name (global only). */
    static constexpr int kWiiPresets = 4;
    struct WiiPreset
    {
        bool used = false;
        int name = 0; /* index into wii_preset_names() */
        int controller = 0, grip = 0, pointer = 0, speed = 5, screen_x = 0, screen_y = 0, smooth = 1, reach = 100;
    };
    WiiPreset wii_presets[kWiiPresets];
    int wii_preset = 0; /* the one in use: 0 none, 1..4 */
    void save_wii_preset(int slot, int name);
    bool use_wii_preset(int slot);
    bool motion_readout = false; /* in a game: the controller's motion on screen (developer options) */
    bool debug_logs = true;      /* test builds: /data/porpoise/debug */
    /* Developer options: unlocked in About, for tuning the Wii Remote. Without
     * them the readout, inverted pointer and motion logs are off. */
    bool developer = false;
    /* 4.0: every setting in Settings and a game's settings; off shows the ones most
     * players need (App::simplify_rows). */
    bool all_settings = false;
    bool beta_updates = false; /* Updates offer pre-releases too (a beta build always does) */
    bool perf_profile = false; /* the sampling profiler (/app0/ps5-sampler.txt) from the next start */
    bool setup_checked = false; /* the first-start setup check has been shown */
    bool motion_logs = true; /* a CSV of the controller's motion per Wii game (developer options) */
    /* System */
    int cpu_clock = 5;       /* index into the clock list (50% .. 300%): 100% */
    bool dual_core = true;   /* dolphin_main_cpu_thread */
    bool accurate_fma = false; /* dolphin_accurate_fmadds: exact multiply-add rounding (slow on the PS5) */
    bool own_cores = true;     /* the emulated CPU on a core of its own (porpoise_main.cpp, keep_cores) */
    bool fast_disc = false;  /* dolphin_fast_disc_speed */
    bool fast_float = false; /* dolphin_fast_float_math: float results not rounded to single after every operation */
    bool wait_shaders = true; /* dolphin_wait_for_shaders: the game's known shaders built before it starts */
    bool cheats = false;     /* dolphin_cheats_enabled */
    int language = 0;        /* 0..9: English first */
    bool progressive = true; /* dolphin_progressive_scan */
    /* Interface */
    bool reduced_motion = false;
    bool large_text = false; /* 2.0's Larger text: read once into text_size */
    int loaded_version = 0; /* settings.ini's settings_version when read (not saved): for one-time changes */
    int ui_theme = 0;  /* the theme: porpoise::ui::ThemeId (0 Porpoise, 1 Revolution, ...) */
    int ui_palette = 0; /* the theme's colours: 0 its own, n palette n - 1 (ui_theme.hpp) */
    int ui_font = 0;    /* the menus' font: 0 the theme's own, n font_set(n - 1) */
    int lib_view = 0;   /* the library: 0 cover flow, 1 wheel, 2 disc flow, 3 shelf, 4 box, 5 list, 6 stack,
                           7 helix, 8 spines, 9 spotlight */
    bool recent_dock = true; /* before 3.0 Alpha 7: Recently Played on or off (recent_style now) */
    int recent_style = 1;    /* Recently Played along the bottom of the library: 0 off, 1 dock, 2 floating, 3 discs */
    int mc_view = 0;    /* Memory Cards: 0 the cards side by side, 1 one card's blocks, 2 saves by game, 3 cubes,
                           4 retro TV */
    int sc_games = 0;       /* Star Cube's Games page: 0 spinning discs, 1 covers */
    bool ff_buttons = true; /* touch pad + R1 / R2: fast forward in games */
    int quick_slot = 0;     /* touch pad + L1 saves to this slot, + L2 loads it: 0 off, 1..3 */
    bool shot_buttons = true; /* touch pad + Square: a screenshot */
    int turbo = 0;          /* the turbo button: 0 none, then kTurboControls */
    int trigger_feel = 2;   /* adaptive triggers as the GameCube's L and R: 0 off, 1 light, 2 firm, 3 extra firm */
    /* Each player's light bar colour (porpoise::pad::kLightColours): blue, red, green, pink. */
    int light_1 = 0, light_2 = 1, light_3 = 2, light_4 = 3;
    /* The controls turbo can be on, after "none" (porpoise::pad::Control). */
    static constexpr int kTurboControls[8] = {porpoise::pad::CtlCross, porpoise::pad::CtlCircle,
                                              porpoise::pad::CtlSquare, porpoise::pad::CtlTriangle,
                                              porpoise::pad::CtlL1, porpoise::pad::CtlR1,
                                              porpoise::pad::CtlL2, porpoise::pad::CtlR2};
    int turbo_control() const { return turbo > 0 && turbo <= 8 ? kTurboControls[turbo - 1] : -1; }
    bool sandbox_notice = true; /* the "can't reach /data" message at start (PS5) */
    int testing_notice = 0;     /* the test build whose notice was seen (look::kNoticeId) */
    bool stay_sandboxed = false; /* don't ask the jailbreak to free Porpoise (a file in /app0, not settings.ini) */
    int console = 0;        /* a game's own: 0 as detected, 1 GameCube, 2 Wii (its controls) */
    /* Sound (beta): Dolphin's exact DSP (LLE), the Wii Remote's speaker in the
     * TV's sound, a fuller buffer, stretching through slowdowns. */
    bool dsp_accurate = false;
    int wiimote_speaker = 0;   /* 0 off, 1 in the TV's sound, 2 on the controller's speaker */
    int audio_buffer = 1;   /* 0 low, 1 normal, 2 safe (40, 80, 160 ms pulled) */
    bool audio_stretch = false; /* Classic only */
    /* 2.1.2: the game's sound pulled from Dolphin's mixer, which fills the
     * gaps when a game runs slow; off is Classic (2.1's, pushed). */
    bool audio_pull = true;
    bool audio_fill = true;
    /* Sound presets (Settings > Audio): Smooth, Responsive, Extra smooth,
     * Classic; kAudioCustom when the rows match none. */
    static constexpr int kAudioPresets = 4;
    static constexpr int kAudioCustom = 4;
    int audio_preset() const;
    void use_audio_preset(int preset);
    /* The mixer's buffer for audio_buffer, pulled (ms). */
    int audio_buffer_ms() const;
    /* The microphone (beta): the DualSense's, as the GameCube Microphone and
     * the Wii Speak. */
    bool microphone = false;
    /* The console's own settings (beta). */
    bool wii_widescreen = true;
    bool pal60 = true;
    int sensor_bar = 0;     /* 0 below the TV, 1 above */
    bool wii_menu_boot = false; /* Wii discs start from the player's own Wii Menu */
    bool gc_bios = false;   /* the GameCube's start-up, from the player's own IPL.bin */
    bool wii_online = false; /* WiiConnect24 through WiiLink (Dolphin.ini: Core/EnableWiiLink) */
    /* Wii games online on a custom server: its DNS server (online_dns), and
     * the https-to-http code added to every Wii game (3.0 Alpha 5). */
    bool online_custom = false;
    bool online_https = true; /* with a custom server: the https-to-http code (a server that takes https turns it off) */
    /* Accessibility */
    int text_size = 0;          /* 0 normal, 1 large, 2 larger, 3 smaller */
    int colour_filter = 0;      /* 0 off, 1 red-weak, 2 green-weak, 3 blue-weak, 4 greyscale */
    bool colour_filter_games = false; /* the same filter on the game's picture */
    bool high_contrast = false;
    bool bold_focus = false;    /* the chosen item's edge thicker and brighter */
    bool big_prompts = false;   /* the button hints along the bottom, larger */
    bool still_background = false;
    int ui_layout = 0; /* Revolution's home: 0 a grid of tiles, 1 the cover flow */
    bool ui_pointer = true; /* Revolution: point with the controller's motion (the touch pad toggles it) */
    /* Dolphin's own per-game settings set by Porpoise, as "dolphin.<Section>.<Key>"
     * and its value (e.g. dolphin.Video_Hacks.EFBToTextureEnable = True). Kept
     * in a game's settings file only, and written for Dolphin before the game
     * starts (write_dolphin_game_ini). */
    std::vector<std::pair<std::string, std::string>> dolphin;
    int ui_language = 0; /* 0 follows the PS5, else porpoise::ui::Language + 1 (see ui_i18n.hpp) */

    /* Reads key = value lines. overlay: only the keys present change (a
     * game's own settings on top of the global ones). */
    bool load(const std::string &path, bool overlay = false);
    bool save(const std::string &path) const;
    /* Writes only the given keys (a game's own settings). */
    bool save_keys(const std::string &path, const std::vector<std::string> &keys) const;
    /* Keys present in a settings file. */
    static std::vector<std::string> keys_in(const std::string &path);
    /* Writes the Dolphin keys into options.ini, keeping any other lines. */
    /* The Widescreen setting (2.0's hack switch is read into it). */
    int wide_mode() const { return wide; }
    bool write_core_options(const std::string &options_ini) const;
    /* Back to how Porpoise ships, keeping the game folders. */
    void reset();
    /* A game's settings file from 1.0, brought up to date: its own buttons
     * become one of the player's layouts in global (true when global changed
     * and must be saved), Smooth / Sharp becomes the screen filter. */
    static bool migrate_game_file(const std::string &path, Settings &global);
    /* Drops a dolphin.* setting (Dolphin's own value applies again). */
    void forget(const std::string &key);
    /* Writes Dolphin's per-game file (User/GameSettings/<ID>.ini) from the
     * dolphin.* settings; removes it when there are none. */
    bool write_dolphin_game_ini(const std::string &path) const;
    /* One setting by its key, as in the file (false: no such setting). */
    bool set(const std::string &key, const std::string &value);
    /* Its value as the file has it ("" for no such setting). */
    std::string get(const std::string &key) const;
    /* The keys whose values differ from before's (settings not yet applied). */
    std::vector<std::string> changed_keys(const Settings &before) const;
    /* Takes those keys' values from another (an edit being applied). */
    void copy_keys(const Settings &from, const std::vector<std::string> &keys);
    /* The Wii Remote's settings, for a Wii game (active) or not. */
    /* game_id: the game playing, for the Wii MotionPlus's Auto. */
    porpoise::pad::WiiConfig wii_config(bool active, const std::string &game_id = "") const;
    static const std::vector<std::string> &wii_preset_names();
    /* The buttons in effect: a ready-made layout, or one of the player's own. */
    porpoise::pad::Mapping mapping() const;
    /* The player's own layout 0..3 being used, or -1 for a ready-made one. */
    int preset_in_use() const { return button_layout >= 2 ? button_layout - 2 : -1; }
    /* The Dolphin options these settings make, as key and value. */
    std::vector<std::pair<std::string, std::string>> core_options() const;
};
} // namespace porpoise
