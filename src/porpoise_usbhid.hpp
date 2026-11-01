/* Porpoise - real Wii Remotes through a Mayflash DolphinBar, as USB HID.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Dolphin talks to a real Wii Remote through hidapi (Core/HW/WiimoteReal/
 * IOhidapi.cpp): hid_enumerate, hid_open_path, hid_read_timeout, hid_write,
 * hid_close. The PS5 has no hidapi, no libusb in a title and no hidraw, but it
 * is FreeBSD underneath and every USB device has a ugen node (/dev/ugenB.A)
 * with FreeBSD's USB_FS_* and USB_DO_REQUEST ioctls, which BlueV
 * (github.com/0xZ3n0x/BlueV) uses on the console for its Bluetooth adapter.
 * So porpoise_usbhid.cpp implements those hidapi functions over ugen, with C
 * linkage, in the title; the Dolphin core leaves them undefined and the core
 * loader binds them here (tools/core-imports.py), as it does every other
 * import. Dolphin's own Wii Remote code then runs unchanged: the protocol, the
 * Nunchuk, Classic Controller, MotionPlus, the IR camera, speaker data,
 * finding a Remote, losing it and finding it again.
 *
 * Only Nintendo's ids are listed (057e:0306, the Remote and the DolphinBar's
 * Wii Remote mode; 057e:0330, the Remote Plus): those are the only devices
 * Dolphin would take, and no other USB device is ever opened. The DolphinBar
 * shows its four Remotes as four HID interfaces; each is a hidapi device
 * "ugenB.A:iN". A DolphinBar slot without a Remote stalls output reports,
 * which hid_write reports as -1 with errno EPIPE: Dolphin's scanner and its
 * once-a-second write test read that as "no Remote here" (IOhidapi.cpp).
 *
 * Nothing touches the console's own Bluetooth chip (ugen0.2) or a root hub.
 * Also built into tools/dolphinbar-probe (the console test payloads) and,
 * with PORPOISE_USBHID_HOST_TEST, into the PC tests (tests/usbhid), which
 * replace the system calls with a simulated DolphinBar. */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace porpoise::usbhid
{
/* Where the module's diagnostics go (one line, no newline). Null: nowhere. */
using LogFn = void (*)(const char *line);
void set_log(LogFn fn);

/* When the console's own driver has an interface of the DolphinBar, both read
 * its reports and each gets only some. true: detach the console's driver from
 * the DolphinBar's interfaces when they're opened (only those; unplugging the
 * bar gives them back). Default false until a console shows it's needed. */
void set_detach_kernel_driver(bool on);

/* Settings: the real Wii Remotes' own speakers (all of them, in games too).
 * Off: their speaker data is held back (never sent) and the speaker kept off
 * and muted. Nothing else changes. On by default. */
void set_speaker_enabled(bool on);
bool speaker_enabled();

/* A ugen node another part of Porpoise has open (the Switch 2's GameCube
 * controller, porpoise_ns2gc.cpp): never opened here to see what it is, and
 * not counted among the nodes access was refused to. */
void set_node_in_use(const std::string &node, bool in_use);

/* What the module sees, for Porpoise's screens and the test payloads. */
struct InterfaceStatus
{
    int number = -1;          /* bInterfaceNumber */
    std::uint8_t ep_in = 0;   /* interrupt IN endpoint address */
    std::uint8_t ep_out = 0;  /* interrupt OUT endpoint, 0: output reports go as SET_REPORT */
    int kernel_driver = -1;   /* -1 unknown, 0 none, 1 the console's driver is attached */
    bool open = false;        /* a hid_device has it */
    bool remote = false;      /* the last output report was taken (a Remote is there) */
    std::uint32_t reports_in = 0, writes_ok = 0, writes_failed = 0, read_errors = 0;
    /* The diagnosis (Controllers & Accessories, and dolphinbar.log in a game). */
    int last_read_status = 0;          /* the last failed read's USB_ERR_* */
    std::uint32_t write_retries = 0;   /* writes that failed once and went through the second time */
    std::uint32_t speaker_reports = 0; /* 0x18, the Remote's speaker data */
    std::uint32_t speaker_dropped = 0; /* of those, refused: dropped, not a disconnect */
    std::uint32_t speaker_held_back = 0; /* not sent: the speaker is switched off in the settings */
    std::uint32_t reports_taken = 0;   /* reports a reader (Dolphin, the menus' monitor) took */
    std::uint32_t queue_drops = 0;     /* reports nobody took in time: the oldest went */
    std::uint32_t slow_writes = 0;     /* writes that took 50 ms or more */
    std::uint32_t write_max_ms = 0, write_avg_us = 0;
    std::uint32_t read_restarts = 0;   /* reads restarted after input stopped with the Remote still there */
    int reads_pending = 0;             /* interrupt IN transfers armed now */
    double last_input_age = -1, last_output_age = -1; /* seconds; -1 never */
};
struct DeviceStatus
{
    std::string node; /* "ugen1.3" */
    std::uint16_t vid = 0, pid = 0;
    std::string product;
    bool descriptors_read = false; /* false: the DolphinBar's usual layout was assumed */
    std::vector<std::uint8_t> config; /* the configuration descriptor as read, for the test payloads */
    std::vector<InterfaceStatus> interfaces;
};
std::vector<DeviceStatus> status();
/* USB nodes the last hid_enumerate couldn't open to read what they are, with
 * the errno: a DolphinBar there would be missed (access refused). */
std::vector<std::pair<std::string, int>> unreadable();
/* Calls into the console's USB stack running for min_ms or longer (what,
 * where, how long): one that never comes back can't log itself. */
std::vector<std::string> stuck_calls(unsigned min_ms);
/* Porpoise is closing: every device's transfers stopped, then closed (a node
 * whose transfers don't stop is left open, said in the log). */
void shutdown();

/* In a game with real Wii Remotes: HOME held for a second is taken out of the
 * game's input and counted (take_home_hold: true once per hold); a short
 * press still reaches the game. */
void set_home_menu(bool on);
bool take_home_hold();
/* In a game: an attachment plugged into or pulled out of a Remote, settled
 * (the interface's number; true: plugged in), once each. */
bool take_attachment_event(int &interface_number, bool &plugged);
/* The buttons (RemoteButton bits) of the first DolphinBar slot with a Wii
 * Remote, from the reports whoever reads them (the in-game menu). */
std::uint16_t remote_buttons();
/* The latest report of the DolphinBar's interface with this number, as it
 * came (whoever reads them), and how old it is: its length, 0 if none. */
int latest_report(int interface_number, std::uint8_t *out, int max, unsigned &age_ms);

/* One open hid_device's interface, as status() has it (false: not open). */
bool interface_stats(const void *hid_device, InterfaceStatus &out);
/* status() as text, one line per device and interface. */
std::string describe();

#ifdef PORPOISE_USBHID_HOST_TEST
/* The PC tests' system calls (tests/usbhid). */
struct SysOps
{
    int (*open)(const char *path, bool nonblock);
    int (*close)(int fd);
    int (*ioctl)(int fd, unsigned long request, void *arg);
    bool (*list_dev)(std::vector<std::string> &names); /* the names in /dev */
    void (*sleep_us)(unsigned us);
    std::uint64_t (*now_ms)();
};
void set_sys_ops(const SysOps &ops);
/* FreeBSD's ioctl numbers and structures as the module uses them. */
namespace abi
{
unsigned long fs_init();
unsigned long fs_uninit();
unsigned long fs_open();
unsigned long fs_close();
unsigned long fs_start();
unsigned long fs_stop();
unsigned long fs_complete();
unsigned long do_request();
unsigned long get_full_desc();
unsigned long device_info_ps5();
unsigned long device_info();
unsigned long iface_driver_active();
unsigned long iface_driver_detach();
} // namespace abi
#endif
} // namespace porpoise::usbhid
