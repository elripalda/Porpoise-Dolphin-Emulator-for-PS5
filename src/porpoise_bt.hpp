/* Porpoise - a USB Bluetooth adapter of the player's own (not the console's
 * Bluetooth), for the Switch 2's GameCube controller without its cable.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A USB Bluetooth adapter is a standard HCI device (Bluetooth Core, Vol 4,
 * Part B): commands as class requests on the control endpoint, events on
 * the interrupt IN endpoint, data on the bulk endpoints. Porpoise talks HCI
 * to it itself over its ugen node (porpoise_ugen.hpp); nothing of the
 * console's own Bluetooth (ugen0.2) is used or touched.
 *
 * One thread, started with Porpoise, looks for an adapter (a node is opened
 * once to see what it is). Found, it reads what chip it is, gives a Realtek
 * RTL8761B / C its firmware as Linux's btrtl driver does (assets/bluetooth,
 * from linux-firmware), then listens for Bluetooth LE advertisements. A
 * Switch 2 GameCube controller advertising (its pair button pressed) is
 * connected to: its GATT notifications and command channel as
 * ndeadly/switch2_controller_research documents them (the official
 * set-up, without the pairing), its input handed to porpoise_ns2gc as if
 * by cable. Lost, it is listened for again. What is advertised nearby is
 * kept for Controllers & Accessories > USB Bluetooth Adapter. Everything
 * goes to the controllers' log (dolphinbar.log), lines "bt:". */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace porpoise::bt
{
enum class Phase
{
    Idle,        /* not run yet */
    Searching,   /* looking for an adapter */
    NotFound,    /* none plugged in (looked for again every few seconds) */
    Starting,    /* reading the chip, its firmware going up */
    Scanning,    /* ready: listening for Bluetooth LE devices */
    Done,        /* (unused) */
    Failed,      /* a step failed (step, error): tried again after a while */
};

/* The controller's link, while the adapter is ready. */
enum class Link
{
    None,       /* waiting for a Switch 2 GameCube controller to advertise */
    Connecting, /* LE Create Connection sent */
    SettingUp,  /* connected: its notifications and set-up */
    Ready,      /* its input arrives */
};

struct Found
{
    std::uint8_t addr[6] = {}; /* as shown: addr[0] first */
    bool random = false;       /* a random address */
    int rssi = 0;              /* dBm */
    std::string name;
    int company = -1;          /* the manufacturer data's company id, -1 none */
    bool nintendo = false;     /* company 0x0553 */
    std::vector<std::uint8_t> maker; /* the manufacturer data after the id, up to 24 bytes */
    unsigned seen = 0;         /* advertisements */
};

struct Status
{
    Phase phase = Phase::Idle;
    std::string node, product, step;
    int error = 0;
    std::uint16_t vid = 0, pid = 0;
    int kernel_driver = -1;
    /* Read Local Version Information */
    bool have_version = false;
    int hci_ver = 0, hci_rev = 0, lmp_ver = 0, lmp_subver = 0, manufacturer = 0;
    std::string chip;       /* "Realtek RTL8761BU", ... */
    int rom_version = -1;
    std::string firmware;   /* what happened to it: file and size, or why not */
    bool firmware_ok = false;
    int fw_lmp_subver = 0;  /* after the upload */
    std::uint8_t bd_addr[6] = {};
    bool have_addr = false;
    bool le = false;        /* the controller supports LE */
    double scan_left = 0;   /* (unused) */
    std::vector<Found> found; /* in the order first seen */
    /* the controller */
    Link link = Link::None;
    std::uint8_t peer[6] = {};
    std::string link_step;  /* what the set-up is doing, or why the last one failed */
    int att_mtu = 23;
    int interval_x125 = 0;  /* the connection interval in 1.25 ms */
    int commands_ok = 0, commands_sent = 0;
    std::uint32_t notifications = 0;
    int last_disconnect = -1; /* the reason of the last disconnection */
    std::uint32_t generation = 0;
};
Status status();
#ifdef PORPOISE_UI_PREVIEW
void preview_status(const Status *s);
#endif
/* Started once, with Porpoise (cheap to call again). */
void start();
/* Controllers & Accessories: forget what was seen nearby (it fills again). */
void clear_found();
/* Porpoise is closing: the controller disconnected, the adapter closed,
 * waited for at most max_ms. */
bool close_for_exit(unsigned max_ms);

/* ---- pure; the thread's and the PC tests' ---------------------------------------------------- */
/* Realtek's patch file (an "epatch", signature "Realtech") and config into
 * what is uploaded, for the chip's ROM version: as btrtl.c
 * (rtlbt_parse_firmware). Empty with why, when it doesn't fit. */
std::vector<std::uint8_t> realtek_patch(const std::vector<std::uint8_t> &fw, const std::vector<std::uint8_t> &config,
                                        int rom_version, std::string &why);
std::string chip_name(int manufacturer, int lmp_subver, int hci_rev);
/* One LE Advertising Report's data (AD structures) into f. */
void parse_advertising(const std::uint8_t *data, int length, Found &f);
std::string address_text(const std::uint8_t addr[6]);
/* A Switch 2 GameCube controller's advertisement: Nintendo's manufacturer
 * data with its USB ids 057e:2073. */
bool is_gamecube_controller(const Found &f);
} // namespace porpoise::bt
