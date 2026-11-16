/* Porpoise on a desktop (Windows): the window, controllers, keyboard and
 * sound through SDL 3, behind the same console calls the PS5 build makes.
 *
 * The shared code talks to the PS5's pad, user, audio and system services
 * (scePad*, sceUserService*, sceAudioOut*, sceSystemService*). Here each of
 * those is answered from SDL: a controller is a "user", its state a pad
 * sample in the console's layout, the speakers an audio stream. So the
 * launcher, the in-game menu, the Wii Remote on a DualSense and the rest run
 * unchanged.
 *
 * Players: player 1 is the keyboard and the first controller together; each
 * further controller is the next player.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "porpoise_platform.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "porpoise_jailbreak.hpp"
#include "trace.hpp"

namespace
{
SDL_Window *g_window = nullptr;
std::atomic<bool> g_quit{false};
std::atomic<bool> g_resized{false};
bool g_in_game = false;
/* Keys pressed since the last read, so a tap shorter than a frame still counts. */
bool g_tapped[SDL_SCANCODE_COUNT] = {};
bool g_fullscreen = false;
std::string g_base;

/* ---- Controllers ------------------------------------------------------------------------- */

/* The console's pad sample (porpoise_pad.cpp), 120 bytes. */
struct PadSample
{
    std::uint32_t buttons;
    std::uint8_t left_x, left_y, right_x, right_y;
    std::uint8_t left_trigger, right_trigger;
    std::uint8_t padding[2];
    float orientation[4];
    float acceleration[3];     /* g: x right, y up out of the face, z toward the player */
    float angular_velocity[3]; /* rad/s, the same axes */
    std::uint8_t touch_count;
    std::uint8_t touch_reserved[7];
    struct
    {
        std::uint16_t x, y;
        std::uint8_t id;
        std::uint8_t reserved[3];
    } touch[2];
    std::int32_t connected;
    std::uint64_t timestamp_us;
    std::uint8_t extension[16];
    std::uint8_t connected_count;
    std::uint8_t remaining[15];
};
static_assert(sizeof(PadSample) == 120, "the console's pad samples are 120 bytes");

constexpr std::uint32_t pad_l3 = 0x000002u, pad_r3 = 0x000004u, pad_options = 0x000008u, pad_up = 0x000010u,
                        pad_right = 0x000020u, pad_down = 0x000040u, pad_left = 0x000080u, pad_l2 = 0x000100u,
                        pad_r2 = 0x000200u, pad_l1 = 0x000400u, pad_r1 = 0x000800u, pad_triangle = 0x001000u,
                        pad_circle = 0x002000u, pad_cross = 0x004000u, pad_square = 0x008000u,
                        pad_touch_pad = 0x100000u;

/* Connected controllers, in the order they arrived: controller n is user n + 1. */
std::vector<SDL_Gamepad *> g_pads;
std::mutex g_pads_mutex;

void add_pad(SDL_JoystickID id)
{
    if (!SDL_IsGamepad(id))
        return;
    SDL_Gamepad *pad = SDL_OpenGamepad(id);
    if (!pad)
        return;
    std::lock_guard<std::mutex> lock(g_pads_mutex);
    for (SDL_Gamepad *p : g_pads)
        if (p == pad)
            return;
    /* The gyroscope and accelerometer, for the Wii Remote. */
    if (SDL_GamepadHasSensor(pad, SDL_SENSOR_ACCEL))
        SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_ACCEL, true);
    if (SDL_GamepadHasSensor(pad, SDL_SENSOR_GYRO))
        SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_GYRO, true);
    g_pads.push_back(pad);
    char line[256];
    std::snprintf(line, sizeof line, "desktop: controller %zu: %s", g_pads.size(), SDL_GetGamepadName(pad));
    ps5::debug::mark(line);
}

void remove_pad(SDL_JoystickID id)
{
    std::lock_guard<std::mutex> lock(g_pads_mutex);
    for (auto it = g_pads.begin(); it != g_pads.end(); ++it)
        if (SDL_GetGamepadID(*it) == id)
        {
            SDL_CloseGamepad(*it);
            g_pads.erase(it);
            ps5::debug::mark("desktop: a controller left");
            return;
        }
}

SDL_Gamepad *pad_of(int user)
{
    std::lock_guard<std::mutex> lock(g_pads_mutex);
    const std::size_t i = std::size_t(user - 1);
    return user >= 1 && i < g_pads.size() ? g_pads[i] : nullptr;
}

std::uint8_t axis_byte(SDL_Gamepad *pad, SDL_GamepadAxis axis)
{
    const int v = SDL_GetGamepadAxis(pad, axis); /* -32768..32767 */
    return std::uint8_t((v + 32768) >> 8);
}

std::uint8_t trigger_byte(SDL_Gamepad *pad, SDL_GamepadAxis axis)
{
    const int v = SDL_GetGamepadAxis(pad, axis); /* 0..32767 */
    return std::uint8_t(std::clamp(v, 0, 32767) * 255 / 32767);
}

void fill_from_pad(SDL_Gamepad *pad, PadSample &s)
{
    struct
    {
        SDL_GamepadButton button;
        std::uint32_t bit;
    } static const kButtons[] = {
        {SDL_GAMEPAD_BUTTON_SOUTH, pad_cross},          {SDL_GAMEPAD_BUTTON_EAST, pad_circle},
        {SDL_GAMEPAD_BUTTON_WEST, pad_square},          {SDL_GAMEPAD_BUTTON_NORTH, pad_triangle},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, pad_l1},     {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, pad_r1},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, pad_l3},        {SDL_GAMEPAD_BUTTON_RIGHT_STICK, pad_r3},
        {SDL_GAMEPAD_BUTTON_START, pad_options},        {SDL_GAMEPAD_BUTTON_TOUCHPAD, pad_touch_pad},
        {SDL_GAMEPAD_BUTTON_BACK, pad_touch_pad}, /* Xbox View / DualSense Create: the touch pad's press */
        {SDL_GAMEPAD_BUTTON_DPAD_UP, pad_up},           {SDL_GAMEPAD_BUTTON_DPAD_DOWN, pad_down},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, pad_left},       {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, pad_right},
    };
    for (const auto &b : kButtons)
        if (SDL_GetGamepadButton(pad, b.button))
            s.buttons |= b.bit;
    s.left_x = axis_byte(pad, SDL_GAMEPAD_AXIS_LEFTX);
    s.left_y = axis_byte(pad, SDL_GAMEPAD_AXIS_LEFTY);
    s.right_x = axis_byte(pad, SDL_GAMEPAD_AXIS_RIGHTX);
    s.right_y = axis_byte(pad, SDL_GAMEPAD_AXIS_RIGHTY);
    s.left_trigger = trigger_byte(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    s.right_trigger = trigger_byte(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    if (s.left_trigger > 24)
        s.buttons |= pad_l2;
    if (s.right_trigger > 24)
        s.buttons |= pad_r2;

    /* SDL's sensor axes are the console's: x right, y up out of the face, z
     * toward the player; m/s^2 made g. */
    float accel[3] = {0, 0, 0}, gyro[3] = {0, 0, 0};
    if (SDL_GetGamepadSensorData(pad, SDL_SENSOR_ACCEL, accel, 3))
        for (int i = 0; i < 3; ++i)
            s.acceleration[i] = accel[i] / SDL_STANDARD_GRAVITY;
    else
        s.acceleration[1] = 1.0f; /* no sensor: lying flat and still */
    if (SDL_GetGamepadSensorData(pad, SDL_SENSOR_GYRO, gyro, 3))
        for (int i = 0; i < 3; ++i)
            s.angular_velocity[i] = gyro[i];
    s.orientation[3] = 1.0f;

    /* The touch pad: the console reads 0..1919 across and about 0..1030 down. */
    if (SDL_GetNumGamepadTouchpads(pad) > 0)
    {
        bool down = false;
        float x = 0, y = 0, pressure = 0;
        if (SDL_GetGamepadTouchpadFinger(pad, 0, 0, &down, &x, &y, &pressure) && down)
        {
            s.touch_count = 1;
            s.touch[0].x = std::uint16_t(std::clamp(x, 0.0f, 1.0f) * 1919.0f);
            s.touch[0].y = std::uint16_t(std::clamp(y, 0.0f, 1.0f) * 1030.0f);
        }
    }
}

/* Player 1 on the keyboard:
 *   arrows: D-pad      WASD: left stick    IJKL: right stick
 *   Enter: Cross       Backspace: Circle   Space: Square    Tab: Triangle
 *   Q / E: L1 / R1     1 / 3: L2 / R2      F1: Options      F2: touch pad
 *   Escape: back in the menus, the in-game menu in a game. */
void fill_from_keyboard(PadSample &s)
{
    const bool *k = SDL_GetKeyboardState(nullptr);
    if (!k)
        return;
    struct
    {
        SDL_Scancode key;
        std::uint32_t bit;
    } static const kKeys[] = {
        {SDL_SCANCODE_UP, pad_up},          {SDL_SCANCODE_DOWN, pad_down},      {SDL_SCANCODE_LEFT, pad_left},
        {SDL_SCANCODE_RIGHT, pad_right},    {SDL_SCANCODE_RETURN, pad_cross},   {SDL_SCANCODE_KP_ENTER, pad_cross},
        {SDL_SCANCODE_BACKSPACE, pad_circle}, {SDL_SCANCODE_SPACE, pad_square}, {SDL_SCANCODE_TAB, pad_triangle},
        {SDL_SCANCODE_Q, pad_l1},           {SDL_SCANCODE_E, pad_r1},           {SDL_SCANCODE_1, pad_l2},
        {SDL_SCANCODE_3, pad_r2},           {SDL_SCANCODE_F1, pad_options},     {SDL_SCANCODE_F2, pad_touch_pad},
        {SDL_SCANCODE_Z, pad_l3},           {SDL_SCANCODE_C, pad_r3},
    };
    auto down = [&](SDL_Scancode code) { return k[code] || g_tapped[code]; };
    for (const auto &key : kKeys)
        if (down(key.key))
            s.buttons |= key.bit;
    if (down(SDL_SCANCODE_ESCAPE))
        s.buttons |= g_in_game ? (pad_options | pad_touch_pad) : pad_circle;
    if (k[SDL_SCANCODE_1])
        s.left_trigger = 255;
    if (k[SDL_SCANCODE_3])
        s.right_trigger = 255;
    auto stick = [&](SDL_Scancode neg, SDL_Scancode pos, std::uint8_t &value) {
        const int v = (k[pos] ? 127 : 0) - (k[neg] ? 128 : 0);
        if (v != 0)
            value = std::uint8_t(128 + v);
    };
    stick(SDL_SCANCODE_A, SDL_SCANCODE_D, s.left_x);
    stick(SDL_SCANCODE_W, SDL_SCANCODE_S, s.left_y);
    stick(SDL_SCANCODE_J, SDL_SCANCODE_L, s.right_x);
    stick(SDL_SCANCODE_I, SDL_SCANCODE_K, s.right_y);
    std::memset(g_tapped, 0, sizeof g_tapped);
}

/* ---- Sound out ---------------------------------------------------------------------------- */

struct Port
{
    SDL_AudioStream *stream = nullptr;
    std::uint32_t grain = 256;
    int channels = 2;
};
constexpr int kPorts = 8;
Port g_ports[kPorts];

/* ---- Sound in (the microphone) ------------------------------------------------------------ */

struct InPort
{
    SDL_AudioStream *stream = nullptr;
    std::uint32_t grain = 256;
};
InPort g_in_ports[2];

int audio_in_open(std::int32_t, std::uint32_t, std::uint32_t, std::uint32_t len, std::uint32_t freq, std::uint32_t)
{
    for (int i = 0; i < 2; ++i)
        if (!g_in_ports[i].stream)
        {
            SDL_AudioSpec spec{SDL_AUDIO_S16, 1, int(freq)};
            g_in_ports[i].stream =
                SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, nullptr, nullptr);
            if (!g_in_ports[i].stream)
            {
                ps5::debug::mark((std::string("desktop: no microphone: ") + SDL_GetError()).c_str());
                return -1;
            }
            g_in_ports[i].grain = len;
            SDL_ResumeAudioStreamDevice(g_in_ports[i].stream);
            return i;
        }
    return -1;
}

/* A grain of the microphone, as the console's call: it waits for one. */
int audio_in_input(std::int32_t handle, void *dest)
{
    if (handle < 0 || handle >= 2 || !g_in_ports[handle].stream)
        return -1;
    InPort &p = g_in_ports[handle];
    const int bytes = int(p.grain) * 2;
    for (int waited = 0; SDL_GetAudioStreamAvailable(p.stream) < bytes; ++waited)
    {
        if (waited > 100)
            return 0; /* nothing for a while: no sound */
        SDL_Delay(1);
    }
    const int got = SDL_GetAudioStreamData(p.stream, dest, bytes);
    return got > 0 ? got / 2 : 0;
}

int audio_in_close(std::int32_t handle)
{
    if (handle < 0 || handle >= 2 || !g_in_ports[handle].stream)
        return -1;
    SDL_DestroyAudioStream(g_in_ports[handle].stream);
    g_in_ports[handle].stream = nullptr;
    return 0;
}

/* ---- Languages ---------------------------------------------------------------------------- */

/* The PS5's language id for the computer's first preferred language. */
int system_language()
{
    int count = 0;
    SDL_Locale **locales = SDL_GetPreferredLocales(&count);
    int id = 1; /* English */
    if (locales && count > 0 && locales[0])
    {
        const std::string lang = locales[0]->language ? locales[0]->language : "";
        const std::string country = locales[0]->country ? locales[0]->country : "";
        if (lang == "ja") id = 0;
        else if (lang == "fr") id = 2;
        else if (lang == "es") id = (country.empty() || country == "ES") ? 3 : 20;
        else if (lang == "de") id = 4;
        else if (lang == "it") id = 5;
        else if (lang == "nl") id = 6;
        else if (lang == "pt") id = country == "BR" ? 17 : 7;
        else if (lang == "ru") id = 8;
        else if (lang == "ko") id = 9;
        else if (lang == "zh") id = (country == "TW" || country == "HK" || country == "MO") ? 10 : 11;
        else if (lang == "pl") id = 16;
        else if (lang == "tr") id = 19;
    }
    SDL_free(locales);
    return id;
}

void toggle_fullscreen()
{
    g_fullscreen = !g_fullscreen;
    SDL_SetWindowFullscreen(g_window, g_fullscreen);
    if (g_fullscreen)
        SDL_HideCursor();
    else
        SDL_ShowCursor();
}
} // namespace

/* ---- The platform ------------------------------------------------------------------------- */

namespace porpoise::platform
{
bool init(int argc, char **argv)
{
    /* The program's folder is Porpoise's home: everything is found from it. */
    if (const char *base = SDL_GetBasePath())
    {
        g_base = base;
#ifdef _WIN32
        SetCurrentDirectoryA(base);
#else
        if (chdir(base) != 0)
            std::perror("chdir");
#endif
    }
    bool fullscreen = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--fullscreen") == 0 || std::strcmp(argv[i], "-f") == 0)
            fullscreen = true;

    SDL_SetHint(SDL_HINT_JOYSTICK_ENHANCED_REPORTS, "1"); /* the DualSense's rumble, light bar and motion */
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1"); /* the program's own icon on the window */
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
    SDL_SetAppMetadata("Porpoise", "2.0", "dev.ripalda.porpoise");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO))
    {
        ps5::debug::mark((std::string("desktop: SDL_Init failed: ") + SDL_GetError()).c_str());
        return false;
    }
    g_window = SDL_CreateWindow("Porpoise", 1280, 720,
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!g_window)
    {
        ps5::debug::mark((std::string("desktop: no window: ") + SDL_GetError()).c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Porpoise",
                                 "Porpoise couldn't open its window. It needs a graphics card with Vulkan "
                                 "and an up-to-date graphics driver.",
                                 nullptr);
        return false;
    }
    SDL_SetWindowMinimumSize(g_window, 640, 360);
    SDL_SetWindowAspectRatio(g_window, 16.0f / 9.0f, 16.0f / 9.0f);
    if (fullscreen)
        toggle_fullscreen();
    /* Controllers already plugged in. */
    int count = 0;
    if (SDL_JoystickID *ids = SDL_GetGamepads(&count))
    {
        for (int i = 0; i < count; ++i)
            add_pad(ids[i]);
        SDL_free(ids);
    }
    ps5::debug::mark("desktop: window open");
    return true;
}

void pump()
{
    SDL_Event e;
    while (SDL_PollEvent(&e))
        switch (e.type)
        {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            g_quit = true;
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            g_resized = true;
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            add_pad(e.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            remove_pad(e.gdevice.which);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (e.key.scancode < SDL_SCANCODE_COUNT)
                g_tapped[e.key.scancode] = true;
            if (!e.key.repeat && (e.key.key == SDLK_F11 || (e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT))))
                toggle_fullscreen();
            break;
        default:
            break;
        }
}

bool quit_requested()
{
    return g_quit.load();
}

SDL_Window *window()
{
    return g_window;
}

bool take_resized()
{
    return g_resized.exchange(false);
}

double display_hz()
{
    const SDL_DisplayMode *mode = g_window ? SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(g_window)) : nullptr;
    return mode && mode->refresh_rate > 1.0f ? double(mode->refresh_rate) : 60.0;
}

std::string absolute(const std::string &path)
{
    std::string p = path;
    if (p.rfind("./", 0) == 0)
        p = p.substr(2);
    else if (p == ".")
        p.clear();
#ifdef _WIN32
    const bool whole = p.size() > 1 && p[1] == ':';
#else
    const bool whole = !p.empty() && p[0] == '/';
#endif
    if (whole)
        return p;
    std::string out = g_base + p;
    while (out.size() > 1 && (out.back() == '/' || out.back() == '\\'))
        out.pop_back();
    return out;
}

void set_in_game(bool in_game)
{
    g_in_game = in_game;
}

std::vector<std::string> drives()
{
    std::vector<std::string> out;
#ifdef _WIN32
    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i)
        if (mask & (1u << i))
            out.push_back(std::string(1, char('A' + i)) + ":/");
#else
    out.push_back("/");
#endif
    return out;
}

std::vector<std::pair<std::string, std::string>> user_folders()
{
    std::vector<std::pair<std::string, std::string>> out;
    const struct
    {
        SDL_Folder folder;
        const char *label;
    } kFolders[] = {{SDL_FOLDER_DOWNLOADS, "Downloads"}, {SDL_FOLDER_DOCUMENTS, "Documents"}, {SDL_FOLDER_DESKTOP, "Desktop"}};
    for (const auto &f : kFolders)
        if (const char *path = SDL_GetUserFolder(f.folder))
        {
            std::string p = path;
            std::replace(p.begin(), p.end(), '\\', '/');
            while (p.size() > 3 && p.back() == '/')
                p.pop_back();
            out.emplace_back(f.label, p);
        }
    return out;
}

void shutdown()
{
    for (Port &p : g_ports)
        if (p.stream)
            SDL_DestroyAudioStream(p.stream), p.stream = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_pads_mutex);
        for (SDL_Gamepad *pad : g_pads)
            SDL_CloseGamepad(pad);
        g_pads.clear();
    }
    if (g_window)
        SDL_DestroyWindow(g_window);
    g_window = nullptr;
    SDL_Quit();
}
} // namespace porpoise::platform

/* ---- The console calls, answered ---------------------------------------------------------- */

extern "C"
{
    /* Vulkan comes through SDL's loader (no vulkan-1 import). */
    VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char *name)
    {
        static PFN_vkGetInstanceProcAddr real =
            reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
        return real ? real(instance, name) : nullptr;
    }

    /* Users: 1 is the keyboard and the first controller, 2.. the others. */
    std::int32_t sceUserServiceInitialize(const void *) { return 0; }
    std::int32_t sceUserServiceGetInitialUser(std::int32_t *user)
    {
        *user = 1;
        return 0;
    }
    std::int32_t sceUserServiceGetLoginUserIdList(std::int32_t *ids)
    {
        std::lock_guard<std::mutex> lock(g_pads_mutex);
        ids[0] = 1;
        for (int i = 1; i < 4; ++i)
            ids[i] = std::size_t(i) < g_pads.size() ? i + 1 : -1;
        return 0;
    }

    std::int32_t scePadInit() { return 0; }
    std::int32_t scePadOpen(std::int32_t user, std::int32_t, std::int32_t, const void *)
    {
        if (user == 1 || pad_of(user))
            return user;
        return -1;
    }
    std::int32_t scePadGetHandle(std::int32_t user, std::int32_t, std::int32_t) { return scePadOpen(user, 0, 0, nullptr); }
    std::int32_t scePadClose(std::int32_t) { return 0; }
    std::int32_t scePadRead(std::int32_t handle, void *samples, std::int32_t capacity)
    {
        if (capacity < 1)
            return 0;
        SDL_Gamepad *pad = pad_of(handle);
        if (!pad && handle != 1)
            return -1; /* that controller went away */
        PadSample s{};
        s.left_x = s.left_y = s.right_x = s.right_y = 128;
        s.acceleration[1] = 1.0f;
        s.orientation[3] = 1.0f;
        if (pad)
            fill_from_pad(pad, s);
        if (handle == 1 && SDL_GetKeyboardFocus() == g_window)
            fill_from_keyboard(s);
        s.connected = 1;
        s.connected_count = 1;
        s.timestamp_us = SDL_GetTicksNS() / 1000;
        std::memcpy(samples, &s, sizeof s);
        return 1;
    }
    struct VibrationParam
    {
        std::uint8_t large, small;
    };
    std::int32_t scePadSetVibration(std::int32_t handle, const VibrationParam *param)
    {
        if (SDL_Gamepad *pad = pad_of(handle))
            SDL_RumbleGamepad(pad, Uint16(param->large * 257), Uint16(param->small * 257), 200);
        return 0;
    }
    std::int32_t scePadSetVibrationMode(std::int32_t, std::int32_t) { return 0; }
    struct LightBarParam
    {
        std::uint8_t r, g, b;
    };
    std::int32_t scePadSetLightBar(std::int32_t handle, const LightBarParam *param)
    {
        if (SDL_Gamepad *pad = pad_of(handle))
            SDL_SetGamepadLED(pad, param->r, param->g, param->b);
        return 0;
    }
    std::int32_t scePadResetLightBar(std::int32_t) { return 0; }
    std::int32_t scePadSetTriggerEffect(std::int32_t, const void *) { return 0; } /* the PS5's adaptive triggers */
    std::int32_t scePadSetMotionSensorState(std::int32_t, bool) { return 0; }

    std::int32_t sceKernelUsleep(std::uint32_t us)
    {
        SDL_DelayNS(Uint64(us) * 1000);
        return 0;
    }
    int sceKernelGetCurrentCpu() { return 0; }

    /* Sound: the main output, as a stream to the default device. The
     * controller's speaker (type 4) isn't offered: its sounds stay on the TV's. */
    std::int32_t sceAudioOutInit() { return 0; }
    std::int32_t sceAudioOutOpen(std::int32_t, std::int32_t type, std::int32_t, std::uint32_t len, std::uint32_t freq,
                                 std::uint32_t param)
    {
        if (type != 0)
            return -1;
        for (int i = 0; i < kPorts; ++i)
            if (!g_ports[i].stream)
            {
                Port &p = g_ports[i];
                const std::uint8_t format = static_cast<std::uint8_t>(param & 0xff);
                p.channels = format == 0 ? 1 : (format == 2 || format == 4) ? 8 : 2;
                p.grain = len;
                SDL_AudioSpec spec{SDL_AUDIO_S16, p.channels, int(freq)};
                p.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
                if (!p.stream)
                {
                    ps5::debug::mark((std::string("desktop: no sound: ") + SDL_GetError()).c_str());
                    return -1;
                }
                SDL_ResumeAudioStreamDevice(p.stream);
                return i;
            }
        return -1;
    }
    /* A grain out; like the console's, it returns once the device has room,
     * so the caller is paced by the sound card. */
    std::int32_t sceAudioOutOutput(std::int32_t handle, const void *data)
    {
        if (handle < 0 || handle >= kPorts || !g_ports[handle].stream)
            return -1;
        Port &p = g_ports[handle];
        if (!data)
            return 0;
        const int bytes = int(p.grain) * p.channels * 2;
        SDL_PutAudioStreamData(p.stream, data, bytes);
        while (SDL_GetAudioStreamQueued(p.stream) > 2 * bytes)
            SDL_DelayNS(500000);
        return 0;
    }
    std::int32_t sceAudioOutClose(std::int32_t handle)
    {
        if (handle < 0 || handle >= kPorts || !g_ports[handle].stream)
            return -1;
        SDL_DestroyAudioStream(g_ports[handle].stream);
        g_ports[handle].stream = nullptr;
        return 0;
    }

    /* The microphone module (porpoise_mic.cpp looks its calls up by name). */
    int sceSysmoduleLoadModuleInternal(std::uint32_t) { return 0; }
    int sceKernelLoadStartModule(const char *path, std::size_t, const void *, std::uint32_t, void *, int *result)
    {
        if (result)
            *result = 0;
        return path && std::strstr(path, "AudioIn") ? 0x7a10 : -1;
    }
    int sceKernelDlsym(int handle, const char *name, void **address)
    {
        *address = nullptr;
        if (handle != 0x7a10)
            return -1;
        if (std::strcmp(name, "sceAudioInOpen") == 0)
            *address = reinterpret_cast<void *>(&audio_in_open);
        else if (std::strcmp(name, "sceAudioInInput") == 0)
            *address = reinterpret_cast<void *>(&audio_in_input);
        else if (std::strcmp(name, "sceAudioInClose") == 0)
            *address = reinterpret_cast<void *>(&audio_in_close);
        return *address ? 0 : -1;
    }

    /* The system: the language, and leaving. */
    int sceSystemServiceHideSplashScreen() { return 0; }
    int sceSystemServiceParamGetInt(int param, int *value)
    {
        if (param != 1)
            return -1;
        *value = system_language();
        return 0;
    }
    int sceSystemServiceLoadExec(const char *, const char *const *)
    {
        /* Leaving: the logs are flushed and the process ends; Windows frees
         * the window, the GPU and the controllers (a teardown with the
         * swapchain still alive can hang). */
        std::fflush(nullptr);
        std::_Exit(0);
    }

    /* The core: a DLL. */
    void *ps5_core_dlopen(const char *path, int)
    {
#ifdef _WIN32
        std::string p = porpoise::platform::absolute(path);
        std::replace(p.begin(), p.end(), '/', '\\');
        return reinterpret_cast<void *>(LoadLibraryA(p.c_str()));
#else
        return dlopen(porpoise::platform::absolute(path).c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    }
    void *ps5_core_dlsym(void *handle, const char *name)
    {
#ifdef _WIN32
        return reinterpret_cast<void *>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
        return dlsym(handle, name);
#endif
    }
    int ps5_core_dlclose(void *handle)
    {
#ifdef _WIN32
        return FreeLibrary(reinterpret_cast<HMODULE>(handle)) ? 0 : -1;
#else
        return dlclose(handle);
#endif
    }
    char *ps5_core_dlerror()
    {
        static char text[256];
#ifdef _WIN32
        const DWORD code = GetLastError();
        if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, text,
                            sizeof text, nullptr))
            std::snprintf(text, sizeof text, "error %lu", static_cast<unsigned long>(code));
#else
        const char *e = dlerror();
        std::snprintf(text, sizeof text, "%s", e ? e : "unknown");
#endif
        return text;
    }

    /* The console's diagnostics and thread placement: nothing to do here. */
    void ps5_vulkan_profile_init() {}
    void ps5_crash_report_install() {}
    void ps5_core_threads_start() {}
    void ps5_sampler_start() {}
    void ps5_open_permissions() {}
    void ps5_memory_report(const char *, std::size_t, int) {}
    void ps5_core_threads_set_affinity(unsigned long long) {}
}

/* The sandbox and the jailbreak daemons are the PS5's: a desktop's folders
 * are all reachable. */
namespace porpoise::jailbreak
{
bool data_reachable()
{
    return true;
}
void prepare() {}
bool ensure()
{
    return true;
}
bool data_visible()
{
    return true;
}
bool stay_wanted()
{
    return false;
}
void set_stay(bool) {}
std::string closed_last()
{
    return "";
}
void forget_closed() {}
void stage(const char *) {}
void survived() {}
} // namespace porpoise::jailbreak
