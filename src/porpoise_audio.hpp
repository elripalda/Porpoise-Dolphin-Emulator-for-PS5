/* Porpoise - game audio to the console's speakers.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <cstddef>
#include <cstdint>

namespace porpoise::audio
{
bool open();
void close();
/* The rate the core produces, from retro_system_av_info. */
void set_source_rate(double hz);
double source_rate();
/* Interleaved stereo s16 frames from the core. Never blocks. */
void push(const std::int16_t *frames, std::size_t count);
/* 0.0 .. 1.0, applied at output. */
/* How much sound is kept queued: 0 low, 1 normal, 2 safe (Settings > Audio). */
void set_buffer(int level);
/* The queue depth the resampler aims for, in 48 kHz frames. */
std::size_t target_frames();
/* Drawing the sound out when the game runs slow, rather than crackling. */
void set_stretching(bool on);
void set_volume(float volume);
void set_muted(bool muted);
/* Drop what is queued (pause, menu, loading). */
void flush();
/* Frames waiting for the speakers (48 kHz). */
std::size_t queued();
/* Blocks while more than `frames` are waiting, at most timeout_ms: the
 * speakers are the clock that keeps a game at its real speed. Returns the
 * time spent waiting, in microseconds. */
long wait_below(std::size_t frames, int timeout_ms);

/* 2.1.2: the game's sound pulled from Dolphin's own mixer at the speakers'
 * pace (the patched core's porpoise_audio_* functions), so the mixer fills the
 * gaps when a game runs slow, as on a PC, instead of the queue running dry. */
using PullFn = std::size_t (*)(std::int16_t *stereo, std::size_t frames, unsigned out_rate);
using PullSurroundFn = std::size_t (*)(float *six_ch, std::size_t frames, unsigned out_rate);
using QueueFn = int (*)(unsigned *queued, unsigned *target, unsigned long long *pushed);
using ConfigFn = int (*)(int pull, int buffer_ms, int fill_gaps);
void start_pull(PullFn pull, PullSurroundFn pull_surround, QueueFn queue, ConfigFn config,
                int buffer_ms, bool fill_gaps, bool surround);
/* Settings > Audio while the game runs: the mixer's buffer, gap filling, and surround. */
void set_pull_config(int buffer_ms, bool fill_gaps);
void set_surround(bool surround);
bool surround();
/* True when sceAudioOut opened an 8-channel surround port. */
bool surround_output();
/* Before the core goes: no call into it after this returns. */
void stop_pull();
bool pulling();
/* The in-game menu: the game's sound stops; Porpoise's own still plays. */
void set_pull_paused(bool paused);
/* Fast forward: the game's sound is drained, not heard. */
void set_fast_forward(bool on);
/* Grains the mixer had nothing new for since the last call (the log). */
unsigned take_gaps();
/* The mixer's queue and its length, in granules (the log). */
bool pull_queue(unsigned &queued, unsigned &target);
} // namespace porpoise::audio
