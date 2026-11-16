/* Porpoise - game audio to the console's speakers.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AudioOut calls and their order follow Mihawk's PS5 RetroArch (src/audio_ps5.cpp),
 * which follows ProsperoLight's moonlight_stream.cpp (GPL-3.0-or-later): a
 * 48 kHz s16 port written 256 frames at a time by its own thread, the blocking
 * write being what paces it. Opened as 8-channel (5.1 surround mirrored to
 * both side and rear beds) with stereo fallback.
 *
 * Dolphin pushes samples at its own rate once a frame (32 kHz by default). They
 * are resampled to 48 kHz here, linearly, with a ratio nudged by at most 0.5%
 * toward a target queue depth. That absorbs the difference between the game's
 * frame rate (59.94 Hz) and the display's (60 Hz) without pops or drift.
 *
 * 2.1.2: the game's sound is pulled instead (start_pull), at the speakers' pace,
 * from Dolphin's own mixer, as Dolphin's audio backends on a PC do. When a game
 * runs slow, the mixer fills the gap with the sound it just played, faded, rather
 * than this queue running dry: no crackle. The mixer resamples to 48 kHz itself
 * (6-point Hermite), and the rate it is told it plays at is nudged by at most
 * 0.4% to keep its queue at its length (the 59.94 against 60 Hz difference).
 * The queue here then holds only Porpoise's own sounds (the in-game menu's). */
#include "porpoise_audio.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <pthread.h>
#include <time.h>

#include "title_threads.hpp"
#include "trace.hpp"

extern "C"
{
    std::int32_t sceAudioOutInit();
    std::int32_t sceAudioOutOpen(std::int32_t, std::int32_t, std::int32_t, std::uint32_t,
                                 std::uint32_t, std::uint32_t);
    std::int32_t sceAudioOutOutput(std::int32_t, const void *);
    std::int32_t sceAudioOutClose(std::int32_t);
}

namespace
{
constexpr unsigned out_rate = 48000;
constexpr std::size_t grain = 256;                 /* frames per AudioOut write */
constexpr std::size_t ring_frames = 16384;         /* ~340 ms */
constexpr std::uint32_t format_s16_stereo = 1;     /* SCE_AUDIO_OUT_PARAM_FORMAT_S16_STEREO */
constexpr std::uint32_t format_s16_8ch = 2;        /* SCE_AUDIO_OUT_PARAM_FORMAT_S16_8CH */
std::size_t g_target_frames = 2304;                  /* ~48 ms queued (Settings > Audio > Audio buffer) */
bool stretching = false;                           /* Settings > Audio > Audio stretching */
constexpr std::uint32_t already_initialized = 0x8026000e;

struct Audio
{
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_t thread{};
    bool thread_started = false;
    std::atomic<bool> stop{false};
    std::atomic<bool> dead{false}; /* the port refused a write: no one is listening any more */
    int port = -1;
    int output_channels = 8;
    std::int16_t ring[ring_frames * 2] = {};
    std::size_t head = 0, count = 0; /* guarded by mutex */
    alignas(16) std::int16_t out[grain * 8] = {};
    alignas(16) std::int16_t stereo_tmp[grain * 2] = {};
    alignas(16) float surround_tmp[grain * 6] = {};
    /* Resampler, touched only by push(). */
    double source_rate = 32000.0;
    double position = 0.0;            /* fractional read position between prev and next */
    std::int16_t prev[2] = {0, 0};
    std::atomic<float> volume{1.0f};
    std::atomic<bool> muted{false};
    /* Pulling (start_pull): the core's functions, guarded by pull_lock so
     * stop_pull can't return while one is running (the core is unloaded
     * after). */
    pthread_mutex_t pull_lock = PTHREAD_MUTEX_INITIALIZER;
    porpoise::audio::PullFn pull = nullptr;
    porpoise::audio::PullSurroundFn pull_surround = nullptr;
    porpoise::audio::QueueFn queue = nullptr;
    porpoise::audio::ConfigFn config = nullptr;
    std::atomic<bool> pull_paused{false};
    std::atomic<bool> fast_forward{false};
    std::atomic<bool> surround{false};
    int pull_buffer_ms = 80;
    bool pull_fill_gaps = true;
    double rate_error = 0.0; /* the mixer queue's distance from its aim, smoothed (worker only) */
    std::atomic<unsigned> gaps{0}; /* grains the mixer had no new sound for (for the log) */
};

Audio *g = nullptr;

int effective_pull_buffer_ms(const Audio &a)
{
    const bool dpl2 = a.output_channels == 8 && a.pull_surround != nullptr &&
                      a.surround.load(std::memory_order_relaxed);
    /* DPL2 High decodes 2048-frame blocks (~42.7 ms at 48 kHz); keep at least
     * 120 ms buffered (aim ~72 ms) so a 2048-frame pull never drains the FIFO. */
    return dpl2 ? std::max(a.pull_buffer_ms, 120) : a.pull_buffer_ms;
}

void *worker(void *)
{
    Audio &a = *g;
    const int out_ch = a.output_channels;
    while (!a.stop.load(std::memory_order_relaxed))
    {
        /* The game's sound, from the core's mixer (start_pull). */
        bool pulled = false;
        bool pulled_surround = false;
        pthread_mutex_lock(&a.pull_lock);
        if (a.pull && !a.pull_paused.load(std::memory_order_relaxed))
        {
            unsigned queued = 0, target = 0;
            unsigned long long made = 0;
            if (a.queue && a.queue(&queued, &target, &made) && target > 0)
            {
                /* Aim for 60% of the length the buffer asks for: a frame's sound
                 * arrives at once, so the queue swings by about four granules. */
                const double aim = target * 0.6;
                const double error = std::clamp((double(queued) - aim) / aim, -1.0, 1.0);
                a.rate_error += (error - a.rate_error) * 0.02; /* about a second */
                if (queued == 0)
                    a.gaps.fetch_add(1, std::memory_order_relaxed);
            }
            /* A fuller queue: tell the mixer it plays slightly slower, so it
             * takes slightly more of the game's sound per frame. */
            const double factor = 1.0 - std::clamp(a.rate_error * 0.004, -0.004, 0.004);
            const unsigned rate = static_cast<unsigned>(std::lround(out_rate * factor));
            const bool want_surround = out_ch == 8 && a.pull_surround != nullptr &&
                                       a.surround.load(std::memory_order_relaxed);
            if (want_surround)
            {
                const std::size_t got = a.pull_surround(a.surround_tmp, grain, rate);
                if (got < grain)
                    std::memset(a.surround_tmp + got * 6, 0, (grain - got) * 6 * sizeof(float));
                if (a.fast_forward.load(std::memory_order_relaxed))
                    std::memset(a.surround_tmp, 0, sizeof a.surround_tmp);
                /* Dolphin SurroundDecoder order: 0 FL, 1 FR, 2 FC, 3 LFE, 4 BL, 5 BR.
                 * PS5 S16_8CH order: 0 FL, 1 FR, 2 FC, 3 LFE, 4 LS, 5 RS, 6 LRS, 7 RRS.
                 * Mirror BL/BR to both side and rear beds for 5.1 / 7.1 / Atmos. */
                for (std::size_t s = 0; s < grain; ++s)
                {
                    const float *in = &a.surround_tmp[s * 6];
                    std::int16_t *dst = &a.out[s * 8];
                    for (int c = 0; c < 6; ++c)
                        dst[c] = static_cast<std::int16_t>(std::clamp(in[c], -1.0f, 1.0f) * 32767.0f);
                    dst[6] = dst[4];
                    dst[7] = dst[5];
                }
                pulled_surround = true;
            }
            else
            {
                std::int16_t *dst = out_ch == 2 ? a.out : a.stereo_tmp;
                const std::size_t got = a.pull(dst, grain, rate);
                if (got < grain)
                    std::memset(dst + got * 2, 0, (grain - got) * 2 * sizeof(std::int16_t));
                if (a.fast_forward.load(std::memory_order_relaxed))
                    std::memset(dst, 0, grain * 2 * sizeof(std::int16_t)); /* drained, not heard */
            }
            pulled = true;
        }
        pthread_mutex_unlock(&a.pull_lock);
        if (pulled_surround)
        {
            /* Porpoise's own sounds (the in-game menu's) onto FL/FR. */
            pthread_mutex_lock(&a.mutex);
            const std::size_t take = std::min(grain, a.count);
            std::size_t tail = (a.head + ring_frames - a.count) % ring_frames;
            for (std::size_t i = 0; i < take; ++i)
            {
                a.out[i * 8 + 0] = std::int16_t(std::clamp(int(a.out[i * 8 + 0]) + a.ring[tail * 2], -32768, 32767));
                a.out[i * 8 + 1] =
                    std::int16_t(std::clamp(int(a.out[i * 8 + 1]) + a.ring[tail * 2 + 1], -32768, 32767));
                tail = (tail + 1) % ring_frames;
            }
            a.count -= take;
            pthread_mutex_unlock(&a.mutex);
        }
        else if (pulled)
        {
            std::int16_t *dst = out_ch == 2 ? a.out : a.stereo_tmp;
            pthread_mutex_lock(&a.mutex);
            const std::size_t take = std::min(grain, a.count);
            std::size_t tail = (a.head + ring_frames - a.count) % ring_frames;
            for (std::size_t i = 0; i < take * 2; i += 2)
            {
                dst[i] = std::int16_t(std::clamp(int(dst[i]) + a.ring[tail * 2], -32768, 32767));
                dst[i + 1] = std::int16_t(std::clamp(int(dst[i + 1]) + a.ring[tail * 2 + 1], -32768, 32767));
                tail = (tail + 1) % ring_frames;
            }
            a.count -= take;
            pthread_mutex_unlock(&a.mutex);
            if (out_ch == 8)
            {
                std::memset(a.out, 0, sizeof a.out);
                for (std::size_t i = 0; i < grain; ++i)
                {
                    a.out[i * 8 + 0] = dst[i * 2 + 0];
                    a.out[i * 8 + 1] = dst[i * 2 + 1];
                }
            }
        }
        else
        {
            std::int16_t *dst = out_ch == 2 ? a.out : a.stereo_tmp;
            pthread_mutex_lock(&a.mutex);
            const std::size_t take = std::min(grain, a.count);
            std::size_t tail = (a.head + ring_frames - a.count) % ring_frames;
            for (std::size_t i = 0; i < take; ++i)
            {
                dst[i * 2] = a.ring[tail * 2];
                dst[i * 2 + 1] = a.ring[tail * 2 + 1];
                tail = (tail + 1) % ring_frames;
            }
            a.count -= take;
            pthread_mutex_unlock(&a.mutex);
            if (take < grain)
                std::memset(dst + take * 2, 0, (grain - take) * 2 * sizeof(std::int16_t));
            if (out_ch == 8)
            {
                std::memset(a.out, 0, sizeof a.out);
                for (std::size_t i = 0; i < take; ++i)
                {
                    a.out[i * 8 + 0] = dst[i * 2 + 0];
                    a.out[i * 8 + 1] = dst[i * 2 + 1];
                }
            }
        }

        const float volume = a.muted.load(std::memory_order_relaxed)
                                 ? 0.0f
                                 : a.volume.load(std::memory_order_relaxed);
        if (volume < 0.999f)
            for (std::size_t i = 0; i < grain * static_cast<std::size_t>(out_ch); ++i)
                a.out[i] = static_cast<std::int16_t>(a.out[i] * volume);

        if (sceAudioOutOutput(a.port, a.out) < 0)
        {
            ps5::debug::mark("audio: output failed; audio stops");
            a.dead = true; /* push and wait_below stop waiting on it */
            break;
        }
    }
    return nullptr;
}

void put(Audio &a, std::int16_t l, std::int16_t r)
{
    /* Caller holds the mutex. A full ring drops the oldest frame. */
    a.ring[a.head * 2] = l;
    a.ring[a.head * 2 + 1] = r;
    a.head = (a.head + 1) % ring_frames;
    if (a.count < ring_frames)
        ++a.count;
}
} // namespace

namespace porpoise::audio
{
bool open()
{
    if (g)
        return true;
    g = new Audio();
    const int init = sceAudioOutInit();
    if (init < 0 && static_cast<std::uint32_t>(init) != already_initialized)
    {
        ps5::debug::mark_value("audio: sceAudioOutInit failed", init);
        return false;
    }
    /* 0xff: the system user; type 0: main output; param 2: s16 8ch surround,
     * falling back to param 1: s16 stereo if opening 8ch fails. */
    g->output_channels = 8;
    g->port = sceAudioOutOpen(0xff, 0, 0, grain, out_rate, format_s16_8ch);
    if (g->port < 0)
    {
        ps5::debug::mark_value("audio: 8ch sceAudioOutOpen failed, falling back to stereo", g->port);
        g->output_channels = 2;
        g->port = sceAudioOutOpen(0xff, 0, 0, grain, out_rate, format_s16_stereo);
    }
    if (g->port < 0)
    {
        ps5::debug::mark_value("audio: sceAudioOutOpen failed", g->port);
        return false;
    }
    g->thread_started = create_title_thread(&g->thread, worker, nullptr) == 0;
    ps5::debug::mark_value(g->output_channels == 8 ? "audio: 48 kHz 8ch port open, thread"
                                                   : "audio: 48 kHz stereo port open, thread",
                           g->thread_started ? 1 : 0);
    return g->thread_started;
}

void close()
{
    if (!g)
        return;
    g->stop.store(true);
    if (g->thread_started)
        pthread_join(g->thread, nullptr);
    if (g->port >= 0)
    {
        (void)sceAudioOutOutput(g->port, nullptr);
        (void)sceAudioOutClose(g->port);
    }
    delete g;
    g = nullptr;
}

void set_source_rate(double hz)
{
    if (g && hz > 1000.0)
        g->source_rate = hz;
}

double source_rate()
{
    return g ? g->source_rate : 48000.0;
}

void push(const std::int16_t *frames, std::size_t count)
{
    if (!g || !g->thread_started || g->dead.load(std::memory_order_relaxed) || count == 0)
        return;
    Audio &a = *g;
    pthread_mutex_lock(&a.mutex);
    /* Nudge the ratio toward the target depth: a fuller queue plays slightly
     * slower input (fewer output frames), an emptier one slightly more. */
    const double fill_error = (static_cast<double>(g_target_frames) - static_cast<double>(a.count)) /
                              static_cast<double>(g_target_frames);
    /* Stretching: when the queue runs low (the game is running slow), the
     * sound is drawn out by up to 8% (a little lower) instead of running dry
     * and crackling; otherwise at most 0.5%, which no one hears. */
    double nudge = std::clamp(fill_error * 0.005, -0.005, 0.005);
    if (stretching && fill_error > 0.5)
        nudge = std::min(0.08, 0.0025 + (fill_error - 0.5) * 0.155); /* continuous from the usual nudge: no jump */
    const double step = a.source_rate / out_rate * (1.0 - nudge);

    for (std::size_t i = 0; i < count; ++i)
    {
        const std::int16_t nl = frames[i * 2], nr = frames[i * 2 + 1];
        while (a.position < 1.0)
        {
            const double t = a.position;
            put(a, static_cast<std::int16_t>(a.prev[0] + (nl - a.prev[0]) * t),
                static_cast<std::int16_t>(a.prev[1] + (nr - a.prev[1]) * t));
            a.position += step;
        }
        a.position -= 1.0;
        a.prev[0] = nl;
        a.prev[1] = nr;
    }
    pthread_mutex_unlock(&a.mutex);
}

void set_buffer(int level)
{
    g_target_frames = level <= 0 ? 1152 : level >= 2 ? 4608 : 2304; /* ~24, 48 or 96 ms */
}

std::size_t target_frames()
{
    return g_target_frames;
}

void set_stretching(bool on)
{
    stretching = on;
}

void set_volume(float volume)
{
    if (g)
        g->volume.store(std::clamp(volume, 0.0f, 1.0f));
}

void set_muted(bool muted)
{
    if (g)
        g->muted.store(muted);
}

std::size_t queued()
{
    if (!g)
        return 0;
    pthread_mutex_lock(&g->mutex);
    const std::size_t n = g->count;
    pthread_mutex_unlock(&g->mutex);
    return n;
}

long wait_below(std::size_t frames, int timeout_ms)
{
    if (!g || !g->thread_started || g->dead.load(std::memory_order_relaxed))
        return 0;
    long waited = 0;
    while (queued() > frames && waited < long(timeout_ms) * 1000)
    {
        timespec ts{0, 500000}; /* half a millisecond: a 256-frame grain is 5.3 ms */
        nanosleep(&ts, nullptr);
        waited += 500;
    }
    return waited;
}

void flush()
{
    if (!g)
        return;
    pthread_mutex_lock(&g->mutex);
    g->count = 0;
    pthread_mutex_unlock(&g->mutex);
}

void start_pull(PullFn pull, PullSurroundFn pull_surround, QueueFn queue, ConfigFn config,
                int buffer_ms, bool fill_gaps, bool surround)
{
    if (!g || !pull)
        return;
    pthread_mutex_lock(&g->pull_lock);
    g->config = config;
    g->queue = queue;
    g->pull = pull;
    g->pull_surround = pull_surround;
    g->pull_buffer_ms = buffer_ms;
    g->pull_fill_gaps = fill_gaps;
    g->surround.store(surround, std::memory_order_relaxed);
    const int effective_ms = effective_pull_buffer_ms(*g);
    if (config)
        config(1, effective_ms, fill_gaps ? 1 : 0);
    g->rate_error = 0.0;
    g->pull_paused = false;
    g->fast_forward = false;
    g->gaps = 0;
    pthread_mutex_unlock(&g->pull_lock);
    flush();
    ps5::debug::mark_value("audio: the game's sound is pulled from Dolphin's mixer; buffer ms", effective_ms);
    if (surround && g->output_channels == 8 && pull_surround)
        ps5::debug::mark("audio: 5.1 DPL2 surround pull active on 8ch output");
}

void set_pull_config(int buffer_ms, bool fill_gaps)
{
    if (!g)
        return;
    pthread_mutex_lock(&g->pull_lock);
    g->pull_buffer_ms = buffer_ms;
    g->pull_fill_gaps = fill_gaps;
    if (g->pull && g->config)
        g->config(1, effective_pull_buffer_ms(*g), fill_gaps ? 1 : 0);
    pthread_mutex_unlock(&g->pull_lock);
}

void set_surround(bool surround)
{
    if (!g)
        return;
    pthread_mutex_lock(&g->pull_lock);
    g->surround.store(surround, std::memory_order_relaxed);
    if (g->pull && g->config)
        g->config(1, effective_pull_buffer_ms(*g), g->pull_fill_gaps ? 1 : 0);
    pthread_mutex_unlock(&g->pull_lock);
}

bool surround()
{
    return g && g->surround.load(std::memory_order_relaxed);
}

bool surround_output()
{
    return g && g->output_channels == 8;
}

void stop_pull()
{
    if (!g)
        return;
    pthread_mutex_lock(&g->pull_lock);
    if (g->config)
        g->config(0, 80, 1); /* the core pushes again, as before (the next game may be Classic) */
    g->pull = nullptr;
    g->pull_surround = nullptr;
    g->queue = nullptr;
    g->config = nullptr;
    g->surround.store(false, std::memory_order_relaxed);
    pthread_mutex_unlock(&g->pull_lock);
}

bool pulling()
{
    if (!g)
        return false;
    pthread_mutex_lock(&g->pull_lock);
    const bool on = g->pull != nullptr;
    pthread_mutex_unlock(&g->pull_lock);
    return on;
}

void set_pull_paused(bool paused)
{
    if (g)
        g->pull_paused.store(paused);
}

void set_fast_forward(bool on)
{
    if (g)
        g->fast_forward.store(on);
}

unsigned take_gaps()
{
    return g ? g->gaps.exchange(0) : 0;
}

bool pull_queue(unsigned &queued, unsigned &target)
{
    queued = target = 0;
    if (!g)
        return false;
    pthread_mutex_lock(&g->pull_lock);
    unsigned long long made = 0;
    const bool ok = g->pull && g->queue && g->queue(&queued, &target, &made);
    pthread_mutex_unlock(&g->pull_lock);
    return ok;
}
} // namespace porpoise::audio
