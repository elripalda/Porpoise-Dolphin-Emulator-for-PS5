/* Porpoise - the emulator core, hosted through the libretro API.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <string>

#include "porpoise_pad.hpp"
#include "porpoise_paths.hpp"

#include <vector>

namespace porpoise::core
{
struct Paths
{
#ifdef PORPOISE_DESKTOP
    const char *core = "./cores/dolphin_libretro.dll";
#else
    const char *core = PORPOISE_APP "/cores/dolphin_libretro.so";
#endif
    const char *system = PORPOISE_APP "/system";       /* Dolphin reads <system>/dolphin-emu/Sys */
    const char *saves = PORPOISE_APP "/savefiles";
    const char *assets = PORPOISE_APP "/content";
    const char *options = PORPOISE_APP "/porpoise/options.ini";
    const char *options_reference = PORPOISE_APP "/porpoise/options-reference.txt";
    const char *log = PORPOISE_APP "/porpoise/core.log";
};

/* What the launcher does while the game starts and runs. All optional. */
struct Hooks
{
    void *user = nullptr;
    /* A step of the launch, to show on the launch screen (draw and present). */
    void (*status)(const char *text, void *user) = nullptr;
    /* A long step with a measure (Wii System Update's titles): text and 0..1. */
    void (*progress)(const char *text, unsigned done, unsigned total, void *user) = nullptr;
    /* The launcher's device is about to go / the core's device is ready. */
    void (*device_closing)(void *user) = nullptr;
    void (*device_ready)(void *user) = nullptr;
    /* Called before each presented frame: core_frame is false until the game
     * has drawn its first picture (show the launch screen until then). */
    void (*frame)(bool core_frame, double fps, void *user) = nullptr;
    /* The in-game menu (Options + touch pad). opened: the game has just been
     * paused. paused: called once a frame while it is, after the pad was read;
     * returns what to do next. */
    void (*opened)(void *user) = nullptr;
    int (*paused)(void *user) = nullptr;
    /* Leaving the game: called each frame of the fade to black (amount 0..1),
     * before the frame hook, so the closing work happens behind it. */
    void (*leaving)(float amount, void *user) = nullptr;
};

/* What the paused hook can answer. */
enum Menu
{
    kMenuStay = 0,   /* keep the menu up */
    kMenuResume = 1, /* back to the game */
    kMenuLibrary = 2,
    kMenuHome = 3,   /* close Porpoise */
    kMenuRestart = 4, /* start the game over (and forget its quick-resume state) */
    kMenuRelaunch = 5, /* back to the library, and the host starts the game afresh (settings read at its start) */
    kMenuChangeDisc = 6, /* 3.1: the game's other disc in, then back to the game */
};

/* How a game ended. */
enum class Exit
{
    Failed,  /* it never started */
    Library, /* back to Porpoise's library */
    Home,    /* close Porpoise */
};

/* Why the last run_game ended in Exit::Failed: which part didn't start, and
 * the reason in its own words (the loader's, or Dolphin's last error). */
enum class Failure
{
    None,
    Core,     /* Dolphin itself didn't load (its code, or memory for it) */
    File,     /* the game's file couldn't be read */
    Game,     /* Dolphin refused to boot the game */
    Graphics, /* Dolphin's graphics didn't start */
};
Failure last_failure();
std::string last_failure_reason();

/* While a game runs: a Dolphin option changed from the in-game menu (the core
 * picks it up on its next frame), and Porpoise's own screen filter. */
void set_option(const char *key, const char *value);
void set_picture(int filter, float strength);
/* The sticks turned around (0 no, 1 up-down, 2 left-right, 3 both). */
void set_stick_invert(int main_stick, int c_stick);
/* 4.0: the GameCube port player 1's controller is in (0..3); the player who
 * would be there takes port 1. Takes effect at the next frame, mid-game too. */
void set_player1_port(int port);
/* The control stick's feel (Settings::stick_sensitivity, stick_deadzone). */
void set_stick_shape(int sensitivity, int deadzone);
/* Fast forward: 1 is normal speed, 2 or 4 run that many frames for each shown. */
void set_fast_forward(int factor);
/* 3.1: a game on two discs (the library's Disc 2 handed over too): how many,
 * and which one is in. 0 when there is no game running or one disc. */
unsigned disc_count();
unsigned disc_index();
/* 4.0: the game's own discs (those it started with), the file of another
 * game's disc that's in now ("" for one of its own), and what the next Change
 * Disc puts in: one of its own discs (index) or another game's disc file. */
unsigned own_disc_count();
std::string disc_path_in();
void choose_disc(int index, const std::string &path);
/* The fast forward in effect now (1: none). */
int fast_forward();
/* Save states, while a game is paused in the in-game menu. serialize() only
 * takes the state (on the game's thread); writing it can happen elsewhere. */
bool serialize(std::vector<unsigned char> &data);
bool save_state(const char *path);
bool load_state(const char *path);
/* The game's last picture (RGBA) and its shape, for a save state's thumbnail. */
bool capture_picture(std::vector<unsigned char> &rgba, unsigned &width, unsigned &height);
float picture_aspect();

struct Playback
{
    float volume = 1.0f;
    bool muted = false;
    int filter = 0;        /* porpoise::vk screen filter */
    float strength = 0.6f; /* 0..1 */
    const char *load_state = nullptr; /* a save state to load once the game is up */
    /* Quick resume (beta): leaving from the in-game menu saves the game here;
     * null when it is off. */
    const char *resume_path = nullptr;
    /* 3.1: with the game's other discs in its disc list, their own resume
     * files (disc 2 first): leaving with one of them in keeps the point in
     * that disc's file. A disc with none keeps no resume point. */
    const std::vector<std::string> *disc_resume_paths = nullptr;
    /* A Wii game: how the DualSense plays the Wii Remote (pad.hpp). */
    porpoise::pad::WiiConfig wii;
    /* The debug folder (Settings > System > Debug logs), or null. With
     * motion_log, a line per frame of the controller's motion goes to
     * <debug_dir>/motion-<date>-<time>.csv, for tuning (developer options). */
    const char *debug_dir = nullptr;
    bool motion_log = false;
    /* The Wii Remotes' speakers on the controllers' (porpoise_speaker): the
     * ports are opened by the caller; this starts and stops the sending. */
    bool controller_speakers = false;
    /* 2.1.2: the game's sound pulled from Dolphin's mixer, which fills the
     * gaps when a game runs slow (Settings > Audio; off is Classic, 2.1's). */
    bool audio_pull = true;
    int audio_buffer_ms = 80;
    bool audio_fill = true;
    bool audio_surround = false;
};
/* The Wii Remote's settings changed in the in-game menu. */
void set_wii(const porpoise::pad::WiiConfig &config);

/* Load the core, boot the game, run it until the player leaves it. The
 * core is unloaded again afterwards, so the next game starts it fresh. */
/* Whether the last start's Wii System Update didn't finish (asking clears it). */
bool take_update_failed();

Exit run_game(const char *game_path, const Paths &paths = Paths{}, const Hooks &hooks = Hooks{},
              const Playback &playback = Playback{});
} // namespace porpoise::core
