/* Porpoise - Nintendo's GameCube controller for the Switch 2 (Nintendo Switch
 * Online, 057e:2073), over USB, as a GameCube controller in games.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Plugged in by its USB-C cable it is a USB device with two interfaces: a HID
 * interface (0) whose interrupt IN carries 64-byte input reports, and a
 * vendor interface (1) with a bulk OUT and a bulk IN for commands. It sends
 * no input until it's told to (SDL's HIDAPI Switch 2 driver,
 * src/joystick/hidapi/SDL_hidapi_switch2.c, has the sequence): its factory
 * and user calibration are read from its flash, the commands that set up its
 * features and the report format are sent, then input reports stream.
 *
 * One thread, started with Porpoise, looks for it among the ugen nodes
 * (once a second while there is none; a node is opened only once to see
 * what it is), talks to it over that node's USB_FS_* transfer table
 * (porpoise_ugen.hpp) and keeps what it reads here: the screens show it, the
 * game takes it as player 1's GameCube controller beside the DualSense
 * (porpoise_core.cpp). No other device is ever opened for long, and the
 * console's own Bluetooth chip (ugen0.2) never. Everything it does goes to
 * the controllers' log (/data/porpoise/dolphinbar.log), lines "ns2gc:". */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace porpoise::ns2gc
{
constexpr std::uint16_t kVendor = 0x057e, kProduct = 0x2073;

/* Its buttons. */
enum Button : std::uint32_t
{
    GcA = 1u << 0,
    GcB = 1u << 1,
    GcX = 1u << 2,
    GcY = 1u << 3,
    GcZ = 1u << 4,
    GcZL = 1u << 5,
    GcL = 1u << 6, /* the L trigger's click at the end of its travel */
    GcR = 1u << 7,
    GcStart = 1u << 8,
    GcHome = 1u << 9,
    GcCapture = 1u << 10,
    GcC = 1u << 11,
    GcUp = 1u << 12,
    GcDown = 1u << 13,
    GcLeft = 1u << 14,
    GcRight = 1u << 15,
};
constexpr int kButtons = 16;
const char *button_name(int bit); /* English, for tr(): bit 0..15 */

enum class Link
{
    NotConnected, /* no 057e:2073 among the USB devices */
    Opening,      /* found; its node is being opened */
    Starting,     /* its calibration read and its set-up sent */
    Ready,        /* input reports arrive */
    Failed,       /* something failed (step, error): tried again after a while */
};

struct Status
{
    Link link = Link::NotConnected;
    std::string node; /* "ugen1.4" */
    std::string product;
    std::string serial;
    /* What failed, when link is Failed (or a step that went wrong on the way). */
    std::string failed_step;
    int failed_errno = 0;
    int kernel_driver[2] = {-1, -1}; /* per interface: -1 unknown, 0 none, 1 the console's */
    bool driver_detached = false;    /* the console's driver was taken off interface 0 */
    int commands_ok = 0, commands_sent = 0; /* the set-up's commands acknowledged */
    bool stick_calibration = false;  /* its own (factory or user) */
    bool trigger_calibration = false;
    bool rumble_out = false;         /* rumble goes over an interrupt OUT (else SET_REPORT) */
    /* what was read */
    std::uint32_t buttons = 0;       /* Button */
    float lx = 0, ly = 0, rx = 0, ry = 0; /* the control stick and the C-stick, -1..1, up and right + */
    float lt = 0, rt = 0;            /* the analog triggers, 0..1 */
    int raw_stick[4] = {0, 0, 0, 0}; /* 12 bits: lx ly rx ry, as sent */
    int raw_trigger[2] = {0, 0};
    std::uint8_t raw_buttons[3] = {0, 0, 0};
    std::uint8_t report_id = 0;
    int report_length = 0;
    std::uint32_t reports = 0;
    float report_rate = 0;           /* a second, over the last second */
    double last_report_age = -1;     /* seconds; -1 none yet */
    std::uint32_t read_errors = 0;
    int last_read_status = 0;
    bool rumbling = false;
    /* Connected by Bluetooth (a USB adapter, porpoise_bt.cpp) rather than by
     * its cable; node is then its address. A cable plugged in wins. */
    bool bluetooth = false;
    std::uint32_t generation = 0;    /* changes when it's plugged in or out */
};
Status status();
bool connected(); /* input arrives */
#ifdef PORPOISE_UI_PREVIEW
/* tools/ui-preview: what status() answers (null: the real one). */
void preview_status(const Status *s);
#endif

/* Started once (cheap to call again); the thread runs until close_for_exit. */
void start();
/* Porpoise is closing: the thread stopped, the transfers stopped and the
 * node closed, waited for at most max_ms. True when all of it was. */
bool close_for_exit(unsigned max_ms);

/* The controller as a GameCube controller, for the game: Button bits, the
 * sticks -32768..32767 (libretro's way: y + down), the triggers 0..0x7fff.
 * connected false: nothing (all 0). */
struct Pad
{
    bool connected = false;
    std::uint32_t buttons = 0;
    std::int16_t lx = 0, ly = 0, rx = 0, ry = 0;
    std::int16_t lt = 0, rt = 0;
};
Pad pad();
/* HOME pressed (once for each press): Porpoise's menu, in a game. */
bool take_home_press();
/* The game's rumble: its motor on or off. */
void set_rumble(bool on);
/* The test screen: the motor on for ms. */
void test_rumble(unsigned ms);

/* ---- by Bluetooth (porpoise_bt.cpp) ------------------------------------------------------- */
/* The controller connected or lost by Bluetooth: its address and calibration
 * (read over its command channel). */
void bt_link(bool up, const std::string &address, const struct Calibration *cal);
/* One of its Bluetooth input reports: 0x0A, as notified on handle 0x000E, or
 * (common) the common report 0x05 of handle 0x000A, which is its USB report
 * without the report id. */
void bt_input(const std::uint8_t *report, int length, bool common = false);
/* Whether its motor should run now (the game's or the test's): the Bluetooth
 * link sends it. */
bool rumble_wanted();

/* ---- the protocol (pure; the thread's and the PC tests') -------------------------------- */
/* Its 64-byte input report into s (buttons, sticks, triggers); false when it
 * is too short to be one. */
struct Calibration
{
    /* each axis's centre and its reach below and above (12 bits), lx ly rx ry */
    int centre[4] = {2048, 2048, 2048, 2048}, below[4] = {0, 0, 0, 0}, above[4] = {0, 0, 0, 0};
    int trigger_zero[2] = {0, 0};
};
bool decode_report(const std::uint8_t *report, int length, const Calibration &cal, Status &s);
/* The same for its Bluetooth report 0x0A (no report id: counter, power,
 * buttons 2..4, the sticks at 5 and 8, the triggers at 12 and 13). */
bool decode_report_0a(const std::uint8_t *report, int length, const Calibration &cal, Status &s);
/* A stick's 9 calibration bytes (centre, above, below, packed 12-bit pairs)
 * into axes x and x + 1 of cal; false when they make no sense. */
bool parse_stick_calibration(const std::uint8_t *data, Calibration &cal, int x);
/* A command for interface 1's bulk OUT: command, sub-command and its data
 * (header: command, 0x91 for USB, 0, sub-command, 0, data length, 0, 0). */
int make_command(std::uint8_t *out, std::uint8_t command, std::uint8_t sub, const std::uint8_t *data, int length);
/* The rumble report for interface 0 (64 bytes): report 3, a sequence number,
 * the motor on (1) or stopped (2). */
void make_rumble(std::uint8_t out[64], unsigned sequence, bool on);

#ifdef PORPOISE_NS2GC_HOST_TEST
/* The PC tests' system calls (tests/ns2gc). */
struct SysOps
{
    int (*open)(const char *path, bool nonblock);
    int (*close)(int fd);
    int (*ioctl)(int fd, unsigned long request, void *arg);
    bool (*list_dev)(std::vector<std::string> &names);
    void (*sleep_us)(unsigned us);
    std::uint64_t (*now_ms)();
};
void set_sys_ops(const SysOps &ops);
#endif
/* Where its lines go (null: the controllers' log, dolphinbar.log). */
void set_log(void (*fn)(const char *line));
} // namespace porpoise::ns2gc
