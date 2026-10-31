/* Porpoise - the Mayflash DolphinBar and its Wii Remotes in Porpoise's own
 * menus: Settings > Controllers & Accessories and the controller test
 * (ui_app_controllers.cpp).
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A monitor that, while those screens are open, finds the DolphinBar through
 * the same hidapi as the Dolphin core (porpoise_usbhid.cpp), opens its four
 * Wii Remote interfaces and talks to each Remote itself: a status request to
 * see whether a Remote is there, its battery, its attachment (Nunchuk,
 * Classic Controller, Wii MotionPlus, a Nunchuk or Classic Controller through
 * a MotionPlus), its calibration, then continuous reports with buttons,
 * motion, the IR camera's dots and the attachment's data.
 *
 * Only one of the monitor and the Dolphin core may have the DolphinBar: the
 * core's run (porpoise_core.cpp) calls hold_for_game() before it loads
 * Dolphin, which stops the monitor, closes every interface it had and keeps
 * it from starting again until release_after_game(). The interfaces are then
 * free for Dolphin's scanner; hid_open_path itself refuses an interface that
 * is open already. Nothing here detaches a system driver or touches any other
 * USB device.
 *
 * What the screens show is only what was read: "connected" means the Remote
 * answered (a report came back), not that the DolphinBar took a report. */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace porpoise::dolphinbar
{
/* ---- the log: /data/porpoise/dolphinbar.log -------------------------------------------- */
/* One line, time-stamped; the file starts afresh with the first line of a run. */
void log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* Where log() writes (tests): "" turns the file off. */
void set_log_path(const std::string &path);
/* porpoise_usbhid's own lines go to the same file. */
void route_usbhid_log();

/* ---- what is known ----------------------------------------------------------------------- */
enum class BarState
{
    NotConnected,  /* no 057e:0306 among the USB devices */
    UsbDetected,   /* seen, not opened (the monitor isn't running) */
    AccessFailed,  /* seen, but its interfaces couldn't be opened */
    Ready,         /* its interfaces are open */
    HeldByGame,    /* a game has it (Dolphin's own scanner) */
};
enum class SlotState
{
    Closed,      /* the interface isn't open (see error) */
    NoRemote,    /* the DolphinBar refuses reports for this slot: no Remote */
    Waiting,     /* the DolphinBar took a report, the Remote hasn't answered yet */
    Connected,   /* the Remote answers */
    Lost,        /* it answered before and stopped (switched off, out of range) */
};
enum class Extension
{
    None,
    Unknown,     /* something is plugged in that didn't identify itself */
    Nunchuk,
    Classic,     /* Classic Controller or Classic Controller Pro */
    MotionPlus,  /* a Wii MotionPlus (or the Wii Remote Plus's own), nothing in it */
    MotionPlusNunchuk,
    MotionPlusClassic,
    Other,       /* identified, but not one of those (a Balance Board, a guitar...) */
};
const char *extension_name(Extension e); /* English, for tr() and the log */

/* Wii Remote buttons, as in its core button bytes (byte 1 << 8 | byte 2). */
enum RemoteButton : std::uint16_t
{
    RbLeft = 0x0100,
    RbRight = 0x0200,
    RbDown = 0x0400,
    RbUp = 0x0800,
    RbPlus = 0x1000,
    RbTwo = 0x0001,
    RbOne = 0x0002,
    RbB = 0x0004,
    RbA = 0x0008,
    RbMinus = 0x0010,
    RbHome = 0x0080,
};
/* Classic Controller buttons, pressed = 1 (its bytes 4 and 5, inverted). */
enum ClassicButton : std::uint16_t
{
    CbRT = 0x0002,
    CbPlus = 0x0004,
    CbHome = 0x0008,
    CbMinus = 0x0010,
    CbLT = 0x0020,
    CbDown = 0x0040,
    CbRight = 0x0080,
    CbUp = 0x0100,
    CbLeft = 0x0200,
    CbZR = 0x0400,
    CbX = 0x0800,
    CbA = 0x1000,
    CbY = 0x2000,
    CbB = 0x4000,
    CbZL = 0x8000,
};

struct IrDot
{
    bool visible = false;
    int x = 0, y = 0; /* 0..1023, 0..767 as the camera sees */
};

struct Remote
{
    SlotState state = SlotState::Closed;
    int interface_number = -1;
    int open_errno = 0;                 /* Closed: why hid_open_path failed */
    std::uint32_t reports = 0;          /* input reports, all kinds */
    double last_report_age = -1;        /* seconds since the last one; -1 none yet */
    bool reporting = false;             /* continuous reports are coming */
    /* status (0x20) */
    bool have_status = false;
    int battery_percent = -1;           /* -1 unknown */
    bool battery_low = false;
    int leds = 0;
    /* the core */
    std::uint16_t buttons = 0;          /* RemoteButton */
    int accel_raw[3] = {0, 0, 0};       /* 10 bits */
    bool calibrated = false;            /* the Remote's own calibration was read */
    float accel_g[3] = {0, 0, 0};       /* x, y, z in g (calibrated, else estimated) */
    float pitch = 0, roll = 0;          /* degrees, from gravity */
    bool ir_on = false;                 /* the IR camera was set up */
    IrDot ir[4];
    /* the attachment */
    Extension ext = Extension::None;
    std::uint8_t ext_id[6] = {};
    bool ext_id_read = false;
    bool ext_identifying = false;      /* being identified right now */
    int ext_error = 0;                 /* Unknown: the Remote's register error, -1 no answer, -2 not sent */
    bool mp_ext_connected = false;     /* the MotionPlus says something is plugged into it */
    bool motion_plus_seen = false;      /* a MotionPlus answered (built in or plugged in) */
    /* Nunchuk */
    int nc_stick[2] = {128, 128};       /* 0..255 */
    /* the stick's own calibration (0xa40028..2d): each axis's lowest, centre
     * and highest, as the Nunchuk keeps them (nunchuk_stick) */
    int nc_stick_min[2] = {32, 32}, nc_stick_mid[2] = {128, 128}, nc_stick_max[2] = {224, 224};
    bool nc_c = false, nc_z = false;
    int nc_accel_raw[3] = {512, 512, 512};
    float nc_accel_g[3] = {0, 0, 0};
    /* Classic Controller */
    std::uint16_t cc_buttons = 0;       /* ClassicButton */
    int cc_left[2] = {32, 32};          /* 0..63 */
    int cc_right[2] = {16, 16};         /* 0..31 */
    int cc_lt = 0, cc_rt = 0;           /* 0..31 */
    /* MotionPlus */
    int mp_raw[3] = {8192, 8192, 8192}; /* yaw, roll, pitch, 14 bits */
    bool mp_slow[3] = {false, false, false};
    float mp_dps[3] = {0, 0, 0};        /* degrees per second, roughly */
    std::uint32_t mp_reports = 0, ext_reports = 0;
    /* the speaker test */
    bool speaker_on = false;           /* set up and switched on */
    bool speaker_playing = false;
    int speaker_volume = 1;            /* 0..kSpeakerVolumes-1 */
    int speaker_tone = 0;              /* the last tone played */
    bool speaker_setup_failed = false; /* a register write of the set-up wasn't acknowledged */
    std::uint32_t spk_sent = 0, spk_refused = 0; /* the last tone's speaker reports */
    float spk_rate = 0;                /* reports a second, as sent */
    float spk_interval_min = 0, spk_interval_avg = 0, spk_interval_max = 0; /* ms between them */
    std::uint32_t spk_write_max_ms = 0;
    /* calibration: the Remote's (EEPROM) and the Nunchuk's (its 0xa40020) */
    int cal_zero[3] = {512, 512, 512}, cal_one[3] = {616, 616, 616};
    int nc_zero[3] = {512, 512, 512}, nc_one[3] = {716, 716, 716};
    bool nc_calibrated = false;
};

struct Status
{
    BarState bar = BarState::NotConnected;
    bool monitoring = false;
    std::string node;                   /* "ugen1.3" */
    std::uint16_t vid = 0, pid = 0;
    std::string product;
    bool descriptors_read = false;
    std::vector<std::string> interfaces; /* one line each, from porpoise_usbhid */
    std::vector<int> kernel_driver;     /* per interface: -1 unknown, 0 none, 1 the console's */
    /* Nodes that couldn't be opened to be identified (errno): maybe the bar,
     * with access refused. */
    std::vector<std::pair<std::string, int>> unreadable;
    Remote remote[4];
    std::uint32_t generation = 0;       /* changes with every plug, unplug or slot change */
};
Status status();
#ifdef PORPOISE_UI_PREVIEW
/* tools/ui-preview: what status() answers (null: the real one). */
void preview_status(const Status *s);
#endif

/* ---- the monitor ----------------------------------------------------------------------------- */
/* Wanted while Porpoise's controller screens are open: true starts it (once),
 * false stops it and closes everything. Called every frame; cheap. */
void monitor(bool wanted);
/* The Dolphin core is about to take the DolphinBar: stops the monitor (and
 * waits for it) and keeps it off. And when the game has ended. */
void hold_for_game();
void release_after_game();
bool held_for_game();

/* How much the monitor does with each Remote. In Porpoise's menus it runs
 * light (the LEDs and buttons only, reports when a button changes: the menu
 * Remote below); with the controller test open, full (motion, IR, the
 * attachment). Leaving full mode turns the camera, the MotionPlus and the
 * speaker off again. */
void set_full_detail(bool full);

/* The menu Remote: the Wii Remote connected first (of those connected now)
 * may work Porpoise's menus beside the DualSense. Its buttons (RemoteButton),
 * 0 when there is none or a game has the DolphinBar. Its slot, or -1. */
std::uint16_t menu_buttons();
/* The menu Remote's Nunchuk, when it has one: C, Z and the stick (-1..1, up
 * and right +1). */
struct MenuNunchuk
{
    bool present = false, c = false, z = false;
    float x = 0, y = 0;
};
MenuNunchuk menu_nunchuk();
/* The menu Remote has nothing plugged in: it's held sideways (the D-pad on
 * the left, 1 and 2 under the right thumb), as Wii games have it without one. */
bool menu_sideways();
int menu_remote();

/* Notices: "Wii Controller Port n connected / disconnected", one for each
 * confirmed change (a Remote that answered, one the DolphinBar refuses twice,
 * one Dolphin connects to or takes off a game's port), shown in order for
 * about three seconds, up to four at once. port: 1-4. */
struct Notice
{
    int port = 0;
    bool connected = false;     /* or the attachment plugged in */
    double age = 0;             /* seconds it has been on screen */
    bool attachment = false;    /* about the Remote's attachment, not the Remote */
    std::string what;           /* the attachment's name, if known (extension_name) */
};
/* Porpoise is closing: the monitor stopped and the USB devices closed, waited
 * for at most max_ms (a call that never comes back out of the console's USB
 * stack mustn't keep Porpoise from closing: it's said in the log instead).
 * True when everything was closed in time. */
bool close_for_exit(unsigned max_ms);

void push_notice(int port, bool connected);
void push_attachment_notice(int port, bool plugged, const std::string &what);
std::vector<Notice> notices();
void clear_notices();
constexpr double kNoticeSeconds = 3.0;

/* Things to try on a Remote from the test screen. */
void rumble(int slot, int ms);
void refresh(int slot); /* a status request, and the attachment identified again */

/* The speaker test: short tones in the Remote's own format (4-bit Yamaha
 * ADPCM at 3000 Hz, 20 bytes a report, 75 reports a second), at a moderate
 * volume. The speaker is switched off again when the screen closes. */
constexpr int kSpeakerVolumes = 5;
constexpr std::uint8_t kSpeakerVolume[kSpeakerVolumes] = {0x08, 0x10, 0x18, 0x20, 0x30};
constexpr int kSpeakerTones = 4;
const char *speaker_tone_name(int tone); /* English, for tr() */
void speaker_play(int slot, int tone);
void speaker_power(int slot, bool on);
void speaker_volume(int slot, int level);

/* The speaker's format (pure; the monitor's and the PC tests'). */
struct Adpcm
{
    int predictor = 0, step = 127;
};
/* Two samples a byte, the first in the high nibble: what the Remote's
 * decoder (as Dolphin's WiimoteEmu/Speaker.cpp emulates it) turns back. */
std::uint8_t adpcm_encode_pair(Adpcm &state, std::int16_t first, std::int16_t second);
std::int16_t adpcm_decode_nibble(Adpcm &state, std::uint8_t nibble);
/* A test tone's samples [from, from + n) at 3000 Hz; false past its end. */
bool speaker_tone_samples(int tone, int from, std::int16_t *out, int n);
constexpr int kSpeakerRate = 3000, kSpeakerSamplesPerReport = 40;

/* ---- the protocol (pure; the monitor's and the PC tests') -------------------------------- */
/* An input report as the Remote sent it (report id first) into r. */
void decode_report(Remote &r, const std::uint8_t *report, int length);
/* The attachment's 6 bytes as Extension e sends them. */
void decode_extension(Remote &r, const std::uint8_t *data);
Extension identify(const std::uint8_t id[6]);
/* An accelerometer calibration block: zero x y z (8 high bits), their low
 * bits, one g x y z, their low bits (the Remote's EEPROM 0x16, the
 * Nunchuk's 0xa40020). False when it makes no sense (left as it was). */
bool parse_calibration(const std::uint8_t data[8], int zero[3], int one[3]);
/* The Nunchuk's 16 calibration bytes: the stick's part, if it makes sense. */
bool parse_stick_calibration(const std::uint8_t data[16], Remote &r);
/* The Nunchuk's stick, -1..1 (up and right +1) after its calibration, with
 * a small dead zone in the middle. */
float nunchuk_stick(const Remote &r, int axis);
} // namespace porpoise::dolphinbar
