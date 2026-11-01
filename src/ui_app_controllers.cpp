/* Porpoise UI - Controllers & Accessories: the DualSense controllers, the
 * Mayflash DolphinBar W010 and its Wii Remotes 1-4 with their attachments,
 * the Switch 2's GameCube controller (USB),
 * what each is doing now, a live test of every button, stick, trigger,
 * motion sensor and the IR camera, and a diagnosis when something is wrong.
 * Settings > Controllers & Accessories, before any game.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Everything shown comes from porpoise_dolphinbar.cpp's monitor, which runs
 * only while this screen (or that settings section) is open and lets go of
 * the DolphinBar before a game starts. Nothing is made up: a Remote is
 * "connected" once it has answered, and a value is drawn only once it was
 * read. */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "porpoise_bt.hpp"
#include "porpoise_dolphinbar.hpp"
#include "porpoise_ns2gc.hpp"
#include "porpoise_pad.hpp"
#include "porpoise_usbhid.hpp"
#include "ui_app.hpp"
#include "ui_app_common.hpp"
#include "ui_i18n.hpp"

namespace porpoise::ui
{
using namespace look;
using namespace porpoise::pad;
namespace db = porpoise::dolphinbar;
namespace gc = porpoise::ns2gc;
namespace bt = porpoise::bt;

namespace
{
/* DualSense, DolphinBar, Wii Remote 1-4, the speaker test, the Switch 2's GameCube controller, a USB
 * Bluetooth adapter, the Wii Remotes setting */
constexpr int kItems = 10;
constexpr int kSpeakerItem = 6;
constexpr int kGcItem = 7;
constexpr int kBtItem = 8;
constexpr float kListX = 90, kListY = 250, kListW = 540, kItemH = 62, kItemGap = 6;
constexpr float kPanelX = 670, kPanelY = 250, kPanelW = 1160, kPanelH = 712;

const Color kGood = rgba(0x7CF0B4);
const Color kWarn = rgba(0xFFD27A);
const Color kOff = rgba(0x8E9AC8);

std::string hex4(unsigned v)
{
    char b[8];
    std::snprintf(b, sizeof b, "%04X", v & 0xffff);
    return b;
}

std::string hex2(unsigned v)
{
    char b[8];
    std::snprintf(b, sizeof b, "0x%02X", v & 0xff);
    return b;
}

/* A one-line status and its colour, for the list and the settings rows. */
struct Line
{
    std::string text;
    Color colour;
};

Line bar_line(const db::Status &s)
{
    switch (s.bar)
    {
    case db::BarState::NotConnected:
        /* A node that couldn't be opened may be any USB device (the DualSense's
         * too): not said to be the DolphinBar. */
        if (!s.unreadable.empty())
            return {tr("Not found (USB access refused to some devices)"), kWarn};
        return {s.monitoring ? tr("Not connected") : tr("Not checked yet"), kOff};
    case db::BarState::UsbDetected: return {tr("USB device detected"), kSoft};
    case db::BarState::AccessFailed: return {tr("USB access failed"), kDanger};
    case db::BarState::Ready: return {tr("Ready"), kGood};
    case db::BarState::HeldByGame: return {tr("In use by the game"), kSoft};
    }
    return {"", kSoft};
}

/* A device's entry is shown only while it's plugged in (not everyone has a
 * DolphinBar or the Switch 2's GameCube controller); the DualSense always. */
bool shown(int item, const db::Status &d, const gc::Status &c, const bt::Status &b)
{
    if (item == kGcItem)
        return c.link != gc::Link::NotConnected;
    if (item == kBtItem)
        return b.phase != bt::Phase::Idle && b.phase != bt::Phase::Searching && b.phase != bt::Phase::NotFound;
    if (item >= 1) /* the DolphinBar, its Remotes and their speaker, the Wii Remotes setting */
        return d.bar != db::BarState::NotConnected;
    return true;
}

std::vector<int> shown_items()
{
    const db::Status d = db::status();
    const gc::Status c = gc::status();
    const bt::Status b = bt::status();
    std::vector<int> out;
    for (int i = 0; i < kItems; ++i)
        if (shown(i, d, c, b))
            out.push_back(i);
    return out;
}

Line gc_line(const gc::Status &s)
{
    switch (s.link)
    {
    case gc::Link::NotConnected: return {tr("Not connected"), kOff};
    case gc::Link::Opening:
    case gc::Link::Starting: return {tr("Setting up\xE2\x80\xA6"), kWarn};
    case gc::Link::Failed: return {tr("Set-up failed"), kDanger};
    case gc::Link::Ready: return {s.bluetooth ? tr("Connected (Bluetooth)") : tr("Connected (USB)"), kGood};
    }
    return {"", kOff};
}

Line bt_line(const bt::Status &s)
{
    switch (s.phase)
    {
    case bt::Phase::Idle: return {tr("Not checked yet"), kOff};
    case bt::Phase::Searching: return {tr("Checking\xE2\x80\xA6"), kWarn};
    case bt::Phase::NotFound: return {tr("Not connected"), kOff};
    case bt::Phase::Starting: return {tr("Setting up\xE2\x80\xA6"), kWarn};
    case bt::Phase::Scanning:
        if (s.link == bt::Link::Ready)
            return {tr("Controller connected"), kGood};
        if (s.link != bt::Link::None)
            return {tr("Connecting\xE2\x80\xA6"), kWarn};
        return {tr("Ready") + " \xC2\xB7 " + s.chip, kIcy};
    case bt::Phase::Done: return {s.chip, kGood};
    case bt::Phase::Failed: return {tr("Set-up failed"), kDanger};
    }
    return {"", kOff};
}

std::string ext_label(db::Extension e) { return tr(db::extension_name(e)); }

Line remote_line(const db::Status &s, int i)
{
    const db::Remote &r = s.remote[i];
    if (s.bar != db::BarState::Ready)
        return {tr("No DolphinBar"), kOff};
    switch (r.state)
    {
    case db::SlotState::Closed: return {tr("Interface not opened"), kDanger};
    case db::SlotState::NoRemote: return {tr("Not connected"), kOff};
    case db::SlotState::Waiting: return {tr("Waiting for an answer"), kWarn};
    case db::SlotState::Lost: return {tr("Disconnected"), kWarn};
    case db::SlotState::Connected:
    {
        std::string t = tr("Connected");
        if (r.ext_identifying)
            t += " \xC2\xB7 " + tr("Identifying the attachment\xE2\x80\xA6");
        else if (r.ext != db::Extension::None)
            t += " \xC2\xB7 " + ext_label(r.ext);
        if (r.battery_percent >= 0)
            t += " \xC2\xB7 " + std::to_string(r.battery_percent) + " %";
        return {t, kGood};
    }
    }
    return {"", kSoft};
}

int connected_remotes(const db::Status &s)
{
    int n = 0;
    for (const db::Remote &r : s.remote)
        n += r.state == db::SlotState::Connected;
    return s.bar == db::BarState::Ready ? n : 0;
}

/* What's wrong, worst first; empty: nothing found. */
std::vector<Line> diagnose(const db::Status &s)
{
    std::vector<Line> out;
    if (!s.monitoring && s.bar != db::BarState::HeldByGame)
        return out;
    if (s.bar == db::BarState::NotConnected)
    {
        if (s.unreadable.empty())
            out.push_back({tr("DolphinBar not found: plug it into a USB port of the PS5, set to mode 4."), kWarn});
        else
            for (const auto &u : s.unreadable)
                out.push_back({trf("A USB device ({node}) is there, but access was refused (error {errno}). It "
                                   "could be the DolphinBar.",
                                   {{"node", u.first}, {"errno", std::to_string(u.second)}}),
                               kDanger});
        return out;
    }
    if (s.bar == db::BarState::AccessFailed)
        out.push_back({tr("The DolphinBar is there, but its HID interfaces could not be opened (access refused)."),
                       kDanger});
    for (int i = 0; i < 4; ++i)
    {
        const db::Remote &r = s.remote[i];
        const std::string n = std::to_string(i + 1);
        if (r.state == db::SlotState::Closed && r.open_errno)
            out.push_back({trf("HID interface {n} not opened (error {errno}).",
                               {{"n", std::to_string(r.interface_number)}, {"errno", std::to_string(r.open_errno)}}),
                           kDanger});
        else if (r.state == db::SlotState::Waiting)
            out.push_back({trf("Wii Remote {n}: the DolphinBar takes data for it, but the Remote doesn't answer.",
                               {{"n", n}}),
                           kWarn});
        else if (r.state == db::SlotState::Lost)
            out.push_back({trf("Wii Remote {n}: disconnected (switched off, out of range or flat batteries). It is "
                               "found again by itself.",
                               {{"n", n}}),
                           kWarn});
        else if (r.state == db::SlotState::Connected && !r.reporting && r.last_report_age > 2.0)
            out.push_back({trf("Wii Remote {n}: no input packets received for a while.", {{"n", n}}), kWarn});
        if (r.state == db::SlotState::Connected && r.ext == db::Extension::Unknown && !r.ext_identifying)
            out.push_back({trf("Wii Remote {n}: the attachment doesn't answer ({why}). Plug it in again, then press "
                               "Triangle.",
                               {{"n", n},
                                {"why", r.ext_error == -1   ? tr("no answer")
                                          : r.ext_error == -2 ? tr("not sent")
                                                              : trf("error {e}", {{"e", std::to_string(r.ext_error)}})}}),
                           kWarn});
    }
    for (std::size_t k = 0; k < s.kernel_driver.size(); ++k)
        if (s.kernel_driver[k] == 1)
        {
            out.push_back({trf("The console's own driver uses interface {n}: inputs may go missing.",
                               {{"n", std::to_string(k)}}),
                           kWarn});
            break;
        }
    if (s.bar == db::BarState::Ready && connected_remotes(s) == 0 &&
        std::none_of(std::begin(s.remote), std::end(s.remote),
                     [](const db::Remote &r) { return r.state == db::SlotState::Waiting; }))
        out.push_back({tr("No Wii Remote connected: press a button on a Remote paired with the DolphinBar, or pair "
                          "it (the DolphinBar's sync button, then 1 + 2)."),
                       kWarn});
    return out;
}

/* A pill: a button's name, lit while it's held. */
void pill(Gfx &g, float ts_scale, float x, float y, float w, const std::string &label, bool on)
{
    g.panel(x, y, w, 40, on ? rgba(0x5CD3FF, 0.9f) : rgba(0x0A1236, 0.65f), 1, 20,
            on ? kIcy : rgba(0x3D5AB0, 0.8f), on ? 2.5f : 1.4f, on ? 10 : 0);
    /* The D-pad's directions as arrows (the fonts have no arrow characters):
     * "^" up, "v" down, "<" left, ">" right. */
    const Color ink = on ? rgba(0x081040) : kSoft;
    if (label.size() == 1 && std::strchr("^v<>", label[0]))
    {
        const float turn = label[0] == '^' ? 0 : label[0] == '>' ? kPi * 0.5f : label[0] == 'v' ? kPi : kPi * 1.5f;
        g.glyph(Glyph::Arrow, x + w * 0.5f, y + 20, 16, ink, turn);
        return;
    }
    g.text_mid(Font::Bold, ts_scale * 20, x + w * 0.5f, y + 20, ink, Align::Center, label);
}

/* A bar from the middle: v in -1..1. */
void centre_bar(Gfx &g, float x, float y, float w, float v, Color c)
{
    g.panel(x, y, w, 14, rgba(0x0A1236, 0.7f), 1, 7, rgba(0x3D5AB0, 0.6f), 1.0f);
    const float half = w * 0.5f, len = std::clamp(v, -1.0f, 1.0f) * half;
    const float bx = len >= 0 ? x + half : x + half + len;
    if (std::fabs(len) > 1)
        g.panel(bx, y + 2, std::fabs(len), 10, c, 1, 5);
    g.panel(x + half - 1, y - 3, 2, 20, kSoft, 1, 1);
}

/* A bar from the left: v in 0..1. */
void level_bar(Gfx &g, float x, float y, float w, float v, Color c)
{
    g.panel(x, y, w, 14, rgba(0x0A1236, 0.7f), 1, 7, rgba(0x3D5AB0, 0.6f), 1.0f);
    const float len = std::clamp(v, 0.0f, 1.0f) * (w - 4);
    if (len > 1)
        g.panel(x + 2, y + 2, len, 10, c, 1, 5);
}

/* A stick in its ring: x and y in -1..1 (y up). */
void stick(Gfx &g, float cx, float cy, float r, float x, float y, Color c)
{
    g.panel(cx - r, cy - r, 2 * r, 2 * r, rgba(0x0A1236, 0.7f), 1, r, rgba(0x3D5AB0, 0.9f), 1.6f);
    g.panel(cx - 1, cy - r + 6, 2, 2 * r - 12, rgba(0x3D5AB0, 0.6f), 1, 1);
    g.panel(cx - r + 6, cy - 1, 2 * r - 12, 2, rgba(0x3D5AB0, 0.6f), 1, 1);
    const float px = cx + std::clamp(x, -1.0f, 1.0f) * (r - 10), py = cy - std::clamp(y, -1.0f, 1.0f) * (r - 10);
    g.panel(px - 10, py - 10, 20, 20, c, 1, 10, kWhite, 2.0f, 8);
}
} // namespace

/* ---- the settings section's rows -------------------------------------------------------- */

std::string App::controller_row_value(int which, std::string *help) const
{
    const db::Status s = db::status();
    if (which == 0)
    {
        const int n = porpoise::pad::connected_count();
        return plural(n, "1 connected", "{n} connected");
    }
    if (which == 1)
    {
        if (help && s.bar == db::BarState::Ready)
            *help = trf("{node} \xC2\xB7 USB {vid}:{pid} \xC2\xB7 {n} interfaces. Cross opens the test and the "
                        "diagnosis.",
                        {{"node", s.node},
                         {"vid", hex4(s.vid)},
                         {"pid", hex4(s.pid)},
                         {"n", std::to_string(s.interfaces.size())}});
        return bar_line(s).text;
    }
    if (which == 6)
        return gc_line(gc::status()).text;
    if (which == 7)
        return bt_line(bt::status()).text;
    return remote_line(s, which - 2).text;
}

std::string App::controllers_present() const
{
    const std::vector<int> items = shown_items();
    std::string out;
    for (int i : items)
        out += char('a' + i);
    return out;
}

void App::refresh_controller_rows()
{
    /* a device plugged in or out: its rows come or go */
    if (controllers_present() != ctl_rows_present_)
    {
        const int row = settings_row_;
        build_settings();
        settings_row_ = std::clamp(row, 0, int(rows_.size()) - 1);
    }
    for (SettingRow &r : rows_)
        if (r.live >= 0)
        {
            std::string help;
            const std::string v = controller_row_value(r.live, &help);
            if (r.values.empty() || r.values[0] != v)
                r.values = {v};
            if (!help.empty())
                r.help = help;
        }
}

void App::preview_controllers(int select, bool section)
{
    if (section)
    {
        tab_ = Tab::Settings;
        screen_ = Screen::Main;
        build_settings();
        for (int i = 0; i < section_count(); ++i)
            if (section_name(i) == "Controllers & Accessories")
                rail_ = i;
        on_rail_ = false;
        settings_row_ = first_row_of("Controllers & Accessories") + 2;
        refresh_controller_rows();
        return;
    }
    tab_ = Tab::Settings;
    open_controllers(select);
}

/* ---- the screen ---------------------------------------------------------------------------- */

void App::open_controllers(int select)
{
    ctl_sel_ = std::clamp(select, 0, kItems - 1);
    ctl_back_ = screen_;
    ctl_diag_logged_.clear();
    db::monitor(true);
    open_screen(Screen::Controllers);
    sfx(Sound::DetailsFlip);
}

App::Action App::update_controllers(bool up, bool down, bool left, bool right)
{
    const int before = ctl_sel_;
    const std::vector<int> items = shown_items();
    auto at = std::find(items.begin(), items.end(), ctl_sel_);
    if (at == items.end())
        ctl_sel_ = 0; /* what was chosen is gone */
    else if (up && at != items.begin())
        ctl_sel_ = *(at - 1);
    else if (down && at + 1 != items.end())
        ctl_sel_ = *(at + 1);
    if (ctl_sel_ != before)
        sfx(Sound::MenuScroll);
    if (ctl_sel_ == kItems - 1 && (left || right || pressed(BtnCross)))
    {
        /* The setting itself: real Wii Remotes or the DualSense, in every game
         * that doesn't have its own. */
        settings_->wii_source = settings_->wii_source ? 0 : 1;
        draft_.wii_source = base_.wii_source = settings_->wii_source;
        settings_->save(settings_path_);
        db::log("Controllers & Accessories: Wii Remotes in games now %s",
                settings_->wii_source ? "real (DolphinBar)" : "the DualSense");
        sfx(Sound::MovingTab);
    }
    if (ctl_sel_ == kSpeakerItem)
    {
        /* left / right: the Remote; Cross: play; Triangle: the next signal;
         * Square: the speaker on / off; L1 / R1: volume */
        if (left || right)
        {
            ctl_spk_slot_ = (ctl_spk_slot_ + (left ? 3 : 1)) % 4;
            sfx(Sound::MenuScroll);
        }
        const db::Remote r = db::status().remote[ctl_spk_slot_];
        if (r.state == db::SlotState::Connected)
        {
            if (pressed(BtnCross))
                db::speaker_play(ctl_spk_slot_, ctl_spk_tone_);
            if (pressed(BtnTriangle))
            {
                ctl_spk_tone_ = (ctl_spk_tone_ + 1) % db::kSpeakerTones;
                sfx(Sound::MenuScroll);
            }
            if (pressed(BtnSquare))
                db::speaker_power(ctl_spk_slot_, !r.speaker_on);
            if (pressed(BtnL1) && r.speaker_volume > 0)
                db::speaker_volume(ctl_spk_slot_, r.speaker_volume - 1);
            if (pressed(BtnR1) && r.speaker_volume + 1 < db::kSpeakerVolumes)
                db::speaker_volume(ctl_spk_slot_, r.speaker_volume + 1);
        }
    }
    if (ctl_sel_ == kBtItem && pressed(BtnCross))
    {
        bt::clear_found();
        sfx(Sound::MenuScroll);
    }
    /* its rumble: Square, or its own X (on its page its buttons aren't the menu's) */
    static bool gc_x_was = false;
    const bool gc_x = (gc::pad().buttons & gc::GcX) != 0;
    if (ctl_sel_ == kGcItem && (pressed(BtnSquare) || (gc_x && !gc_x_was)) && gc::connected())
    {
        gc::test_rumble(400);
        sfx(Sound::MenuScroll);
    }
    gc_x_was = gc_x;
    const int slot = ctl_sel_ - 2;
    if (slot >= 0 && slot < 4)
    {
        if (pressed(BtnSquare))
        {
            db::rumble(slot, 400);
            sfx(Sound::MenuScroll);
        }
        if (pressed(BtnTriangle))
        {
            db::refresh(slot);
            sfx(Sound::MenuScroll);
        }
    }
    /* The diagnosis, when it changes, into dolphinbar.log too. */
    {
        std::string all;
        for (const Line &l : diagnose(db::status()))
            all += l.text + "\n";
        if (all != ctl_diag_logged_)
        {
            ctl_diag_logged_ = all;
            db::log("Diagnosis: %s", all.empty() ? "no problem found" : all.c_str());
        }
    }
    if (pressed(BtnCircle))
    {
        open_screen(ctl_back_);
        sfx(Sound::DetailsFlip);
        if (tab_ == Tab::Settings && ctl_back_ == Screen::Main)
        {
            const int row = settings_row_;
            build_settings();
            settings_row_ = std::clamp(row, 0, int(rows_.size()) - 1);
        }
    }
    return Action::None;
}

void App::draw_controllers(double time)
{
    (void)time;
    Gfx &g = *g_;
    const db::Status s = db::status();
    const float k = ts(1.0f);

    g.text(Font::Bold, ts(54), 90, 132, kWhite, Align::Left, tr("Controllers & Accessories"));
    {
        const int pads = porpoise::pad::connected_count();
        std::string sub = plural(pads, "1 DualSense", "{n} DualSense controllers");
        if (s.bar != db::BarState::NotConnected)
            sub += "   \xC2\xB7   " + tr("Mayflash DolphinBar") + ": " + bar_line(s).text;
        if (s.bar == db::BarState::Ready)
            sub += "   \xC2\xB7   " + plural(connected_remotes(s), "1 Wii Remote", "{n} Wii Remotes");
        if (gc::connected())
            sub += "   \xC2\xB7   " + tr("Switch 2 GameCube Controller");
        g.text(Font::SemiBold, ts(28), 92, 200, kLavender, Align::Left, fit(g, Font::SemiBold, ts(28), sub, 1740));
    }

    /* ---- the list: what's plugged in ---- */
    const std::vector<int> items = shown_items();
    for (std::size_t row = 0; row < items.size(); ++row)
    {
        const int i = items[row];
        const float y = kListY + float(row) * (kItemH + kItemGap) + (i == kItems - 1 ? 18 : 0);
        const bool on = i == ctl_sel_;
        std::string name;
        Line st;
        if (i == 0)
        {
            name = tr("DualSense");
            const int n = porpoise::pad::connected_count();
            st = {plural(n, "1 connected", "{n} connected"), n ? kGood : kOff};
        }
        else if (i == 1)
        {
            name = tr("Mayflash DolphinBar W010");
            st = bar_line(s);
        }
        else if (i < kSpeakerItem)
        {
            name = trf("Wii Remote {n}", {{"n", std::to_string(i - 1)}});
            st = remote_line(s, i - 2);
        }
        else if (i == kSpeakerItem)
        {
            name = tr("Wii Remote Speaker");
            const db::Remote &r = s.remote[ctl_spk_slot_];
            st = r.state != db::SlotState::Connected
                     ? Line{trf("Wii Remote {n}: not connected", {{"n", std::to_string(ctl_spk_slot_ + 1)}}), kOff}
                 : r.speaker_playing ? Line{tr("Playing"), kGood}
                 : r.speaker_on      ? Line{tr("On"), kGood}
                                     : Line{tr("Off"), kSoft};
        }
        else if (i == kGcItem)
        {
            name = tr("Switch 2 GameCube Controller");
            st = gc_line(gc::status());
        }
        else if (i == kBtItem)
        {
            name = tr("USB Bluetooth Adapter");
            st = bt_line(bt::status());
        }
        else
        {
            name = tr("Wii Remotes in games");
            st = {settings_->wii_source ? tr("Real Wii Remotes (DolphinBar)") : tr("DualSense"), kIcy};
        }
        g.panel(kListX, y, kListW, kItemH, on ? rgba(0x1C48D8, 0.55f) : rgba(0x0A1236, 0.55f), 1, kR,
                on ? kIcy : rgba(0x3D5AB0, 0.7f), on ? 3.0f : 1.4f, on ? 12 : 0);
        g.text_mid(Font::Bold, ts(26), kListX + 26, y + 20, kWhite, Align::Left,
                   fit(g, Font::Bold, ts(26), name, kListW - 50));
        g.panel(kListX + 26, y + 37, 12, 12, st.colour, 1, 6);
        g.text_mid(Font::SemiBold, ts(21), kListX + 48, y + 43, st.colour, Align::Left,
                   fit(g, Font::SemiBold, ts(21), st.text, kListW - 80));
        if (i == kItems - 1 && on)
        {
            g.glyph(Glyph::Arrow, kListX + kListW - 60, y + 31, 18, kIcy, kPi * 1.5f);
            g.glyph(Glyph::Arrow, kListX + kListW - 28, y + 31, 18, kIcy, kPi * 0.5f);
        }
    }

    /* ---- the panel ---- */
    g.panel(kPanelX, kPanelY, kPanelW, kPanelH, rgba(0x13256F, 0.5f), 0.6f, kR, rgba(0x5A8CFF, 0.8f), 2.0f, 0, 0.15f);
    const float px = kPanelX + 34, pw = kPanelW - 68;
    float y = kPanelY + 40;
    auto heading = [&](const std::string &t, float hx, float hy) {
        g.text(Font::Bold, ts(26), hx, hy, kIcy, Align::Left, t);
    };
    auto para = [&](const std::string &t, Color c, float hx, float &hy, float w) {
        for (const std::string &l : wrap(g, Font::Regular, ts(23), t, w, 4))
        {
            g.text(Font::Regular, ts(23), hx, hy, c, Align::Left, l);
            hy += 34;
        }
    };

    if (ctl_sel_ == 0)
    {
        heading(tr("DualSense"), px, y);
        y += 54;
        for (int p = 0; p < porpoise::pad::kMaxPlayers; ++p)
        {
            const bool c = porpoise::pad::connected(p);
            g.text(Font::SemiBold, ts(24), px, y, c ? kGood : kOff, Align::Left,
                   trf("Player {n}", {{"n", std::to_string(p + 1)}}) + ": " + (c ? tr("Connected") : tr("Not connected")));
            y += 40;
        }
        y += 20;
        heading(tr("Live test (player 1)"), px, y);
        y += 54;
        const std::uint32_t b = porpoise::pad::state(0).buttons;
        static const struct
        {
            std::uint32_t bit;
            const char *name;
        } kPad[] = {{BtnCross, "Cross"},   {BtnCircle, "Circle"}, {BtnSquare, "Square"}, {BtnTriangle, "Triangle"},
                    {BtnUp, "Up"},         {BtnDown, "Down"},     {BtnLeft, "Left"},     {BtnRight, "Right"},
                    {BtnL1, "L1"},         {BtnR1, "R1"},         {BtnL2, "L2"},         {BtnR2, "R2"},
                    {BtnL3, "L3"},         {BtnR3, "R3"},         {BtnOptions, "Options"}, {BtnTouch, "Touch Pad"}};
        for (int i = 0; i < 16; ++i)
            pill(g, k, px + float(i % 4) * 270, y + float(i / 4) * 56, 250, tr(kPad[i].name), b & kPad[i].bit);
        y += 4 * 56 + 20;
        para(tr("The DualSense keeps opening Porpoise's menu (Options + touch pad), also with real Wii Remotes."),
             kSoft, px, y, pw);
    }
    else if (ctl_sel_ == 1)
    {
        heading(tr("Mayflash DolphinBar W010"), px, y);
        auto draw_diagnosis = [&] {
            y += 14;
            heading(tr("Diagnosis"), px, y);
            y += 44;
            const auto d = diagnose(s);
            if (d.empty())
            {
                g.text(Font::SemiBold, ts(23), px, y, kGood, Align::Left,
                       s.monitoring ? tr("No problem found.") : tr("Checking\xE2\x80\xA6"));
                y += 36;
            }
            for (const Line &l : d)
            {
                if (y > kPanelY + kPanelH - 60)
                    break;
                g.panel(px, y + 9, 10, 10, l.colour, 1, 5);
                float yy = y;
                for (const std::string &w : wrap(g, Font::Regular, ts(22), l.text, pw - 30, 2))
                {
                    g.text(Font::Regular, ts(22), px + 22, yy, l.colour, Align::Left, w);
                    yy += 30;
                }
                y = yy + 6;
            }
        };        const Line st = bar_line(s);
        g.text(Font::Bold, ts(26), px + pw, y, st.colour, Align::Right, st.text);
        y += 52;
        auto row = [&](const std::string &label, const std::string &value) {
            g.text(Font::SemiBold, ts(23), px, y, kLavender, Align::Left, label);
            g.text(Font::Regular, ts(23), px + 330, y, kWhite, Align::Left, fit(g, Font::Regular, ts(23), value, pw - 340));
            y += 36;
        };
        if (!s.node.empty())
        {
            row(tr("USB device"), "057E:0306 " + tr("expected") + " \xC2\xB7 " + tr("found") + " " + hex4(s.vid) + ":" +
                                      hex4(s.pid));
            row(tr("Node"), "/dev/" + s.node);
            row(tr("Product name"), s.product.empty() ? tr("(none)") : s.product);
            row(tr("Descriptors"), s.descriptors_read ? tr("read from the device")
                                                      : tr("not readable: the usual layout is assumed"));
            row(tr("Firmware version"), tr("not readable over USB"));
            /* the diagnosis first: it's what matters when something's wrong */
            draw_diagnosis();
            y += 6;
            heading(tr("Interfaces"), px, y);
            y += 44;
            for (const std::string &line : s.interfaces)
            {
                if (y > kPanelY + kPanelH - 60)
                    break;
                int num = 0, driver = -1, open = 0, last_status = 0;
                unsigned in = 0, out = 0, reports = 0, w_ok = 0, w_bad = 0, r_err = 0, restarts = 0, spk_drop = 0,
                         w_max = 0, retries = 0;
                float in_age = -1, out_age = -1;
                std::sscanf(line.c_str(), "%d|%x|%x|%d|%d|%u|%u|%u|%u|%d|%u|%f|%f|%u|%u|%u", &num, &in, &out, &driver,
                            &open, &reports, &w_ok, &w_bad, &r_err, &last_status, &restarts, &in_age, &out_age,
                            &spk_drop, &w_max, &retries);
                std::string t = trf("Interface {n}", {{"n", std::to_string(num)}}) + "   IN " + hex2(in) + "   OUT " +
                                (out ? hex2(out) : std::string("\xE2\x80\x93")) + "   " +
                                (driver == 1 ? tr("system driver attached") : driver == 0 ? tr("no system driver") : "") +
                                "   " + (open ? tr("open") : tr("closed")) + "   " +
                                plural(reports, "1 packet", "{n} packets");
                g.text(Font::Regular, ts(22), px + 10, y, open ? kWhite : kOff, Align::Left,
                       fit(g, Font::Regular, ts(22), t, pw - 20));
                y += 30;
                if (open && (reports || w_ok || w_bad))
                {
                    auto age = [](float a) {
                        char b[24];
                        if (a < 0)
                            return std::string("\xE2\x80\x93");
                        std::snprintf(b, sizeof b, "%.1f s", a);
                        return std::string(b);
                    };
                    const std::string d =
                        trf("input {in} ago, output {out} ago, writes {ok} ok / {bad} failed / {re} retried, read errors {re2}, reads restarted {rs}, slowest write {ms} ms",
                            {{"in", age(in_age)},
                             {"out", age(out_age)},
                             {"ok", std::to_string(w_ok)},
                             {"bad", std::to_string(w_bad)},
                             {"re", std::to_string(retries)},
                             {"re2", std::to_string(r_err)},
                             {"rs", std::to_string(restarts)},
                             {"ms", std::to_string(w_max)}});
                    g.text(Font::Regular, ts(19), px + 30, y, (w_bad || r_err) ? kWarn : kSoft, Align::Left,
                           fit(g, Font::Regular, ts(19), d, pw - 40));
                    y += 30;
                }
            }
        }
        else
        {
            y += 10;
            para(tr("Plug the Mayflash DolphinBar into a USB port of the PS5 and set it to mode 4 (Wii Remote "
                    "mode). It shows up here by itself."),
                 kSoft, px, y, pw);
        }
        if (s.node.empty())
            draw_diagnosis();
    }

    else if (ctl_sel_ < kSpeakerItem)
    {
        const int slot = ctl_sel_ - 2;
        const db::Remote &r = s.remote[slot];
        heading(trf("Wii Remote {n}", {{"n", std::to_string(slot + 1)}}), px, y);
        const Line st = remote_line(s, slot);
        g.text(Font::Bold, ts(26), px + pw, y, st.colour, Align::Right, fit(g, Font::Bold, ts(26), st.text, 700));
        y += 52;
        if (r.state != db::SlotState::Connected || s.bar != db::BarState::Ready)
        {
            std::string why;
            if (s.bar != db::BarState::Ready)
                why = tr("The DolphinBar isn't ready (see Mayflash DolphinBar W010).");
            else if (r.state == db::SlotState::Closed)
                why = trf("Its HID interface could not be opened (error {errno}).",
                          {{"errno", std::to_string(r.open_errno)}});
            else if (r.state == db::SlotState::Waiting)
                why = tr("The DolphinBar takes data for this slot, but the Remote hasn't answered yet.");
            else if (r.state == db::SlotState::Lost)
                why = tr("It was connected and has stopped answering (switched off, out of range or the batteries "
                         "are flat). It's found again by itself.");
            else
                why = tr("No Remote in this slot. Turn on a Remote paired with the DolphinBar (any button), or pair "
                         "one: the DolphinBar's sync button, then 1 + 2 on the Remote. The slot is the player LED it "
                         "lights.");
            para(why, kSoft, px, y, pw);
        }
        else
        {
            /* left: the buttons and the attachment; right: motion, IR, MotionPlus */
            const float lx = px, rx = px + 560, cw = 520;
            float ly = y, ry = y;
            heading(tr("Buttons"), lx, ly);
            ly += 44;
            const std::uint16_t b = r.buttons;
            const struct
            {
                std::uint16_t bit;
                const char *name;
            } kRow1[] = {{db::RbUp, "^"}, {db::RbDown, "v"}, {db::RbLeft, "<"}, {db::RbRight, ">"}},
              kRow2[] = {{db::RbA, "A"}, {db::RbB, "B"}, {db::RbOne, "1"}, {db::RbTwo, "2"}},
              kRow3[] = {{db::RbMinus, "-"}, {db::RbHome, "HOME"}, {db::RbPlus, "+"}};
            for (int i = 0; i < 4; ++i)
                pill(g, k, lx + float(i) * 128, ly, 116, kRow1[i].name, b & kRow1[i].bit);
            ly += 52;
            for (int i = 0; i < 4; ++i)
                pill(g, k, lx + float(i) * 128, ly, 116, kRow2[i].name, b & kRow2[i].bit);
            ly += 52;
            for (int i = 0; i < 3; ++i)
                pill(g, k, lx + float(i) * 171, ly, 160, kRow3[i].name, b & kRow3[i].bit);
            ly += 64;
            std::string info;
            if (r.battery_percent >= 0)
                info = trf("Battery {p} %", {{"p", std::to_string(r.battery_percent)}}) +
                       (r.battery_low ? " (" + tr("low") + ")" : "");
            else
                info = tr("Battery: not reported yet");
            info += "   \xC2\xB7   " + plural(r.reports, "1 packet", "{n} packets");
            g.text(Font::Regular, ts(21), lx, ly, r.battery_low ? kWarn : kSoft, Align::Left,
                   fit(g, Font::Regular, ts(21), info, cw));
            ly += 44;

            heading(tr("Attachment") + ": " +
                        (r.ext_identifying ? tr("Identifying the attachment\xE2\x80\xA6") : ext_label(r.ext)),
                    lx, ly);
            ly += 48;
            const bool nunchuk = r.ext == db::Extension::Nunchuk || r.ext == db::Extension::MotionPlusNunchuk;
            const bool classic = r.ext == db::Extension::Classic || r.ext == db::Extension::MotionPlusClassic;
            if (nunchuk && r.ext_reports > 0)
            {
                const float sx = porpoise::dolphinbar::nunchuk_stick(r, 0), sy = porpoise::dolphinbar::nunchuk_stick(r, 1);
                stick(g, lx + 80, ly + 80, 76, sx, sy, kCyan);
                g.text(Font::Regular, ts(21), lx + 180, ly + 6, kWhite, Align::Left,
                       "X " + std::to_string(r.nc_stick[0]) + "   Y " + std::to_string(r.nc_stick[1]));
                pill(g, k, lx + 180, ly + 46, 90, "C", r.nc_c);
                pill(g, k, lx + 282, ly + 46, 90, "Z", r.nc_z);
                char a[96];
                std::snprintf(a, sizeof a, "%+.2f  %+.2f  %+.2f g", r.nc_accel_g[0], r.nc_accel_g[1], r.nc_accel_g[2]);
                g.text(Font::Regular, ts(21), lx + 180, ly + 104, kSoft, Align::Left,
                       tr("Motion") + ": " + a + (r.nc_calibrated ? "" : " (~)"));
                ly += 176;
            }
            else if (classic && r.ext_reports > 0)
            {
                stick(g, lx + 60, ly + 64, 56, (float(r.cc_left[0]) - 32) / 28.0f, (float(r.cc_left[1]) - 32) / 28.0f,
                      kCyan);
                stick(g, lx + 190, ly + 64, 56, (float(r.cc_right[0]) - 16) / 14.0f,
                      (float(r.cc_right[1]) - 16) / 14.0f, kCyan);
                g.text(Font::Regular, ts(19), lx + 270, ly, kSoft, Align::Left, "L " + std::to_string(r.cc_lt));
                level_bar(g, lx + 330, ly + 8, 180, float(r.cc_lt) / 31.0f, kCyan);
                g.text(Font::Regular, ts(19), lx + 270, ly + 36, kSoft, Align::Left, "R " + std::to_string(r.cc_rt));
                level_bar(g, lx + 330, ly + 44, 180, float(r.cc_rt) / 31.0f, kCyan);
                const struct
                {
                    std::uint16_t bit;
                    const char *name;
                } kCc[] = {{db::CbA, "a"},     {db::CbB, "b"},       {db::CbX, "x"},       {db::CbY, "y"},
                           {db::CbZL, "ZL"},   {db::CbZR, "ZR"},     {db::CbLT, "L"},      {db::CbRT, "R"},
                           {db::CbUp, "^"}, {db::CbDown, "v"}, {db::CbLeft, "<"},
                           {db::CbRight, ">"}, {db::CbMinus, "-"}, {db::CbHome, "HOME"},
                           {db::CbPlus, "+"}};
                const float gy = ly + 136;
                for (int i = 0; i < 15; ++i)
                    pill(g, k, lx + float(i % 5) * 104, gy + float(i / 5) * 48, 96, kCc[i].name, r.cc_buttons & kCc[i].bit);
                ly = gy + 3 * 48 + 10;
            }
            else if (r.ext_identifying)
                para(tr("Reading what's plugged in\xE2\x80\xA6"), kSoft, lx, ly, cw);
            else if (nunchuk || classic)
            {
                para(tr("Waiting for the attachment's data\xE2\x80\xA6"), kSoft, lx, ly, cw);
            }
            else if (r.ext == db::Extension::Unknown)
            {
                const std::string why = r.ext_error == -1   ? tr("no answer")
                                        : r.ext_error == -2 ? tr("not sent")
                                                            : trf("error {e}", {{"e", std::to_string(r.ext_error)}});
                para(trf("The attachment doesn't answer ({why}). Plug it in firmly, then press Triangle to look again.",
                         {{"why", why}}),
                     kWarn, lx, ly, cw);
            }
            else if (r.ext == db::Extension::Other)
            {
                para(trf("An attachment this test doesn't show (id {id}). It may still work in games.",
                         {{"id", [&] {
                               char b[24];
                               std::snprintf(b, sizeof b, "%02X%02X%02X%02X%02X%02X", r.ext_id[0], r.ext_id[1],
                                             r.ext_id[2], r.ext_id[3], r.ext_id[4], r.ext_id[5]);
                               return std::string(b);
                           }()}}),
                     kSoft, lx, ly, cw);
            }
            else if (r.ext == db::Extension::None)
                para(tr("Plug in a Nunchuk or Classic Controller: it's recognized by itself."), kSoft, lx, ly, cw);

            heading(tr("Motion"), rx, ry);
            ry += 44;
            const char *axes[3] = {"X", "Y", "Z"};
            for (int i = 0; i < 3; ++i)
            {
                char v[32];
                std::snprintf(v, sizeof v, "%+.2f g", r.accel_g[i]);
                g.text(Font::SemiBold, ts(21), rx, ry - 4, kLavender, Align::Left, axes[i]);
                centre_bar(g, rx + 40, ry + 2, 320, r.accel_g[i] / 2.5f, kCyan);
                g.text(Font::Regular, ts(21), rx + 380, ry - 4, kWhite, Align::Left, v);
                ry += 34;
            }
            {
                char t[96];
                std::snprintf(t, sizeof t, "%+.0f\xC2\xB0 / %+.0f\xC2\xB0", r.pitch, r.roll);
                g.text(Font::Regular, ts(21), rx, ry, kSoft, Align::Left,
                       tr("Tilt (pitch / roll)") + ": " + t + (r.calibrated ? "" : " (~)"));
                ry += 44;
            }
            heading(tr("IR pointer"), rx, ry);
            const int dots = int(std::count_if(std::begin(r.ir), std::end(r.ir), [](const db::IrDot &d) { return d.visible; }));
            g.text(Font::Regular, ts(21), rx + cw, ry + 4, r.ir_on ? (dots ? kGood : kSoft) : kWarn, Align::Right,
                   r.ir_on ? plural(dots, "1 light seen", "{n} lights seen") : tr("camera off"));
            ry += 44;
            const float fw = 320, fh = 240; /* the camera's 1024 x 768 */
            g.panel(rx, ry, fw, fh, rgba(0x050A24, 0.85f), 1, 10, rgba(0x3D5AB0, 0.9f), 1.6f);
            float sumx = 0, sumy = 0;
            for (const db::IrDot &d : r.ir)
                if (d.visible)
                {
                    /* The camera sees the bar mirrored: drawn as the player points. */
                    const float dx = rx + fw - float(d.x) / 1023.0f * fw, dy = ry + float(d.y) / 767.0f * fh;
                    g.panel(dx - 7, dy - 7, 14, 14, kWarn, 1, 7, kWhite, 1.5f, 10);
                    sumx += dx;
                    sumy += dy;
                }
            if (dots > 0)
                g.glyph(Glyph::Pointer, sumx / float(dots), sumy / float(dots), 24, kIcy, 0);
            else if (r.ir_on)
                g.text_mid(Font::Regular, ts(20), rx + fw * 0.5f, ry + fh * 0.5f, kOff, Align::Center,
                           fit(g, Font::Regular, ts(20), tr("Point the Remote at the DolphinBar"), fw - 20));
            ry += fh + 22;
            if (r.ext == db::Extension::MotionPlus || r.ext == db::Extension::MotionPlusNunchuk ||
                r.ext == db::Extension::MotionPlusClassic)
            {
                heading(tr("Wii MotionPlus"), rx, ry);
                ry += 42;
                const char *names[3] = {"Yaw", "Roll", "Pitch"};
                for (int i = 0; i < 3 && r.mp_reports > 0; ++i)
                {
                    char v[32];
                    std::snprintf(v, sizeof v, "%+.0f \xC2\xB0/s", r.mp_dps[i]);
                    g.text(Font::SemiBold, ts(20), rx, ry - 4, kLavender, Align::Left, tr(names[i]));
                    centre_bar(g, rx + 80, ry + 2, 280, r.mp_dps[i] / 600.0f, kCyan);
                    g.text(Font::Regular, ts(20), rx + 380, ry - 4, kWhite, Align::Left, v);
                    ry += 30;
                }
                if (r.mp_reports == 0)
                    g.text(Font::Regular, ts(21), rx, ry, kSoft, Align::Left, tr("Waiting for its data\xE2\x80\xA6"));
            }
            else if (r.motion_plus_seen)
                g.text(Font::Regular, ts(21), rx, ry, kSoft, Align::Left, tr("Wii MotionPlus found"));
        }
    }
    else if (ctl_sel_ == kSpeakerItem)
    {
        const int slot = ctl_spk_slot_;
        const db::Remote &r = s.remote[slot];
        heading(tr("Wii Remote Speaker"), px, y);
        g.text(Font::Bold, ts(26), px + pw, y, kIcy, Align::Right,
               trf("Wii Remote {n}", {{"n", std::to_string(slot + 1)}}));
        g.glyph(Glyph::Arrow, px + pw - 250, y + 16, 16, kIcy, kPi * 1.5f);
        g.glyph(Glyph::Arrow, px + pw + 14, y + 16, 16, kIcy, kPi * 0.5f);
        y += 56;
        if (!porpoise::usbhid::speaker_enabled())
        {
            para(tr("Real Wii Remote Speakers is off in the settings (Controllers & Accessories): nothing is sent to "
                    "the speakers."),
                 kWarn, px, y, pw);
            y += 12;
        }
        if (r.state != db::SlotState::Connected || s.bar != db::BarState::Ready)
        {
            para(tr("Choose a connected Wii Remote with left and right. The test plays short tones through its own "
                    "speaker, in the format a Wii sends."),
                 kSoft, px, y, pw);
        }
        else
        {
            auto row = [&](const std::string &label, const std::string &value, Color c) {
                g.text(Font::SemiBold, ts(23), px, y, kLavender, Align::Left, label);
                g.text(Font::Regular, ts(23), px + 360, y, c, Align::Left, fit(g, Font::Regular, ts(23), value, pw - 370));
                y += 38;
            };
            row(tr("Speaker"), r.speaker_playing ? tr("Playing") : r.speaker_on ? tr("On") : tr("Off"),
                r.speaker_on ? kGood : kSoft);
            if (r.speaker_setup_failed)
                row(tr("Set-up"), tr("not acknowledged by the Remote"), kWarn);
            {
                char v[16];
                std::snprintf(v, sizeof v, "0x%02X", db::kSpeakerVolume[r.speaker_volume]);
                row(tr("Volume"), std::to_string(r.speaker_volume + 1) + " / " + std::to_string(db::kSpeakerVolumes) +
                                      "   (" + v + ")",
                    kWhite);
            }
            row(tr("Test signal"), tr(db::speaker_tone_name(ctl_spk_tone_)), kWhite);
            row(tr("Format"), tr("4-bit ADPCM, 3000 Hz, 20 bytes a packet, 75 packets a second"), kSoft);
            y += 14;
            heading(tr("Last test signal"), px, y);
            y += 46;
            if (r.spk_sent == 0)
                g.text(Font::Regular, ts(23), px, y, kSoft, Align::Left, tr("None played yet."));
            else
            {
                char a[160];
                row(tr("Audio packets"),
                    trf("{ok} sent, {bad} refused", {{"ok", std::to_string(r.spk_sent - r.spk_refused)},
                                                     {"bad", std::to_string(r.spk_refused)}}),
                    r.spk_refused ? kWarn : kGood);
                std::snprintf(a, sizeof a, "%.1f / s", r.spk_rate);
                row(tr("Rate (75 / s expected)"), a, std::fabs(r.spk_rate - 75.0f) < 8 ? kGood : kWarn);
                std::snprintf(a, sizeof a, "%.1f / %.1f / %.1f ms", r.spk_interval_min, r.spk_interval_avg,
                              r.spk_interval_max);
                row(tr("Interval min / avg / max (13.3 ms expected)"), a, r.spk_interval_max > 40 ? kWarn : kWhite);
                std::snprintf(a, sizeof a, "%u ms", r.spk_write_max_ms);
                row(tr("Slowest USB write"), a, r.spk_write_max_ms >= 50 ? kWarn : kWhite);
            }
        }
    }
    else if (ctl_sel_ == kGcItem)
    {
        const gc::Status c = gc::status();
        heading(tr("Switch 2 GameCube Controller"), px, y);
        const Line st = gc_line(c);
        g.text(Font::Bold, ts(26), px + pw, y, st.colour, Align::Right, fit(g, Font::Bold, ts(26), st.text, 600));
        y += 52;
        auto row = [&](float rx, float &ry, const std::string &label, const std::string &value, Color col) {
            g.text(Font::SemiBold, ts(22), rx, ry, kLavender, Align::Left, label);
            g.text(Font::Regular, ts(22), rx + 220, ry, col, Align::Left, fit(g, Font::Regular, ts(22), value, 320));
            ry += 34;
        };
        if (c.link != gc::Link::Ready)
        {
            std::string why;
            Color col = kSoft;
            if (c.link == gc::Link::NotConnected)
                why = tr("Plug the GameCube controller for Nintendo Switch 2 into a USB port of the PS5 with a USB-C "
                         "data cable. It shows up here by itself and plays player 1's GameCube controller in games, "
                         "beside the DualSense; HOME opens Porpoise's menu.");
            else if (c.link == gc::Link::Failed)
            {
                why = trf("Setting it up failed: {step} (error {errno}). It's tried again by itself; if it stays, "
                          "unplug it and plug it in again.",
                          {{"step", c.failed_step}, {"errno", std::to_string(c.failed_errno)}});
                col = kWarn;
            }
            else
                why = tr("Found. Reading its calibration and starting its input\xE2\x80\xA6");
            para(why, col, px, y, pw);
            if (!c.node.empty())
            {
                y += 14;
                row(px, y, tr("USB device"), "057E:2073   /dev/" + c.node, kWhite);
                row(px, y, tr("Product name"), c.product.empty() ? tr("(none)") : c.product, kWhite);
                row(px, y, tr("Set-up"),
                    trf("{ok} of {n} commands answered",
                        {{"ok", std::to_string(c.commands_ok)}, {"n", std::to_string(c.commands_sent)}}),
                    c.commands_ok == c.commands_sent ? kWhite : kWarn);
                row(px, y, tr("System driver"),
                    c.kernel_driver[0] == 1 ? tr("system driver attached")
                    : c.kernel_driver[0] == 0 ? tr("no system driver")
                                              : "?",
                    kWhite);
            }
        }
        else
        {
            /* left: the buttons and what it is; right: the sticks, the triggers */
            const float lx = px, rx = px + 590;
            float ly = y, ry = y;
            heading(tr("Buttons"), lx, ly);
            ly += 44;
            static const struct
            {
                std::uint32_t bit;
                const char *name;
            } kPills[16] = {{gc::GcA, "A"},         {gc::GcB, "B"},       {gc::GcX, "X"},         {gc::GcY, "Y"},
                            {gc::GcL, "L"},         {gc::GcR, "R"},       {gc::GcZ, "Z"},         {gc::GcZL, "ZL"},
                            {gc::GcUp, "^"},        {gc::GcDown, "v"},    {gc::GcLeft, "<"},      {gc::GcRight, ">"},
                            {gc::GcStart, "START"}, {gc::GcHome, "HOME"}, {gc::GcCapture, "Capture"}, {gc::GcC, "C"}};
            for (int i = 0; i < 16; ++i)
                pill(g, k, lx + float(i % 4) * 134, ly + float(i / 4) * 52, 122,
                     kPills[i].bit == gc::GcCapture ? tr(kPills[i].name) : kPills[i].name, c.buttons & kPills[i].bit);
            ly += 4 * 52 + 16;
            char raw[96];
            std::snprintf(raw, sizeof raw, "%02X %02X %02X", c.raw_buttons[0], c.raw_buttons[1], c.raw_buttons[2]);
            row(lx, ly, tr("Button bytes"), raw, kSoft);
            std::snprintf(raw, sizeof raw, "%.0f / s", c.report_rate);
            row(lx, ly, tr("Reports"), raw, c.report_rate > 30 ? kGood : kWarn);
            row(lx, ly, tr("Calibration"),
                c.stick_calibration ? (c.trigger_calibration ? tr("its own") : tr("its own (sticks only)"))
                                    : tr("not read: estimated"),
                c.stick_calibration ? kWhite : kWarn);
            if (!c.serial.empty())
                row(lx, ly, tr("Serial number"), c.serial, kWhite);
            if (c.bluetooth)
                row(lx, ly, tr("Bluetooth"), c.node, kWhite);
            else
                row(lx, ly, tr("USB device"), "057E:2073   /dev/" + c.node, kWhite);
            ly += 10;
            para(tr("In games: player 1's GameCube controller, beside the DualSense. HOME opens Porpoise's menu."),
                 kSoft, lx, ly, 540);

            heading(tr("Sticks"), rx, ry);
            ry += 44;
            const float r = 62;
            stick(g, rx + r + 20, ry + r, r, c.lx, c.ly, kIcy);
            stick(g, rx + 3 * r + 120, ry + r, r, c.rx, c.ry, rgba(0xFFD24A));
            ry += 2 * r + 14;
            g.text(Font::SemiBold, ts(21), rx + r + 20, ry, kSoft, Align::Center, tr("Control Stick"));
            g.text(Font::SemiBold, ts(21), rx + 3 * r + 120, ry, kSoft, Align::Center, tr("C-Stick"));
            ry += 34;
            std::snprintf(raw, sizeof raw, "%d %d   %d %d", c.raw_stick[0], c.raw_stick[1], c.raw_stick[2],
                          c.raw_stick[3]);
            g.text(Font::Regular, ts(20), rx, ry, kSoft, Align::Left, tr("Raw") + ": " + raw);
            ry += 44;
            heading(tr("Triggers"), rx, ry);
            ry += 46;
            const float bw = 330;
            g.text_mid(Font::Bold, ts(22), rx, ry + 7, kWhite, Align::Left, "L");
            level_bar(g, rx + 40, ry, bw, c.lt, kIcy);
            std::snprintf(raw, sizeof raw, "%3.0f %%  (%d)", c.lt * 100.0f, c.raw_trigger[0]);
            g.text_mid(Font::Regular, ts(20), rx + 60 + bw, ry + 7, kSoft, Align::Left, raw);
            ry += 36;
            g.text_mid(Font::Bold, ts(22), rx, ry + 7, kWhite, Align::Left, "R");
            level_bar(g, rx + 40, ry, bw, c.rt, kIcy);
            std::snprintf(raw, sizeof raw, "%3.0f %%  (%d)", c.rt * 100.0f, c.raw_trigger[1]);
            g.text_mid(Font::Regular, ts(20), rx + 60 + bw, ry + 7, kSoft, Align::Left, raw);
            ry += 46;
            g.text(Font::SemiBold, ts(22), rx, ry, c.rumbling ? kGood : kSoft, Align::Left,
                   tr("Rumble") + ": " + (c.rumbling ? tr("On") : tr("Off")));
        }
    }
    else if (ctl_sel_ == kBtItem)
    {
        const bt::Status b = bt::status();
        heading(tr("USB Bluetooth Adapter"), px, y);
        const Line st = bt_line(b);
        g.text(Font::Bold, ts(26), px + pw, y, st.colour, Align::Right, fit(g, Font::Bold, ts(26), st.text, 600));
        y += 52;
        auto row = [&](const std::string &label, const std::string &value, Color col) {
            g.text(Font::SemiBold, ts(22), px, y, kLavender, Align::Left, label);
            g.text(Font::Regular, ts(22), px + 260, y, col, Align::Left, fit(g, Font::Regular, ts(22), value, pw - 270));
            y += 34;
        };
        if (b.phase == bt::Phase::Idle || b.phase == bt::Phase::NotFound)
        {
            para(tr("The Switch 2's GameCube controller without its cable: plug a USB Bluetooth adapter into the "
                    "PS5 (TP-Link UB600 or another with a Realtek RTL8761B/C). It's found by itself and given its "
                    "firmware. Then press the SYNC button on top of the controller: it connects and "
                    "plays as by cable. The PS5's own Bluetooth isn't used."),
                 kSoft, px, y, pw);
        }
        else
        {
            char v[96];
            std::snprintf(v, sizeof v, "%04X:%04X   /dev/%s", b.vid, b.pid, b.node.c_str());
            row(tr("USB device"), v, kWhite);
            if (!b.product.empty())
                row(tr("Product name"), b.product, kWhite);
            if (b.have_version)
            {
                std::snprintf(v, sizeof v, "   (HCI %02X rev %04X, LMP %02X sub %04X)", b.hci_ver, b.hci_rev,
                              b.lmp_ver, b.lmp_subver);
                row(tr("Chip"), b.chip + v, kWhite);
            }
            if (b.rom_version >= 0)
                row(tr("ROM version"), std::to_string(b.rom_version), kWhite);
            if (!b.firmware.empty())
                row(tr("Firmware"), b.firmware, b.firmware_ok ? kGood : kWarn);
            if (b.have_addr)
                row(tr("Address"), bt::address_text(b.bd_addr) + (b.le ? "   Bluetooth LE" : ""), kWhite);
            if (b.phase == bt::Phase::Failed)
                row(tr("Failed"), b.step + " (" + std::to_string(b.error) + ")", kDanger);
            if (b.phase == bt::Phase::Scanning)
            {
                std::string link;
                Color col = kSoft;
                if (b.link == bt::Link::None)
                    link = tr("Press the SYNC button on top of the controller.");
                else
                {
                    char d[96];
                    std::snprintf(d, sizeof d, "   %.2f ms, MTU %d, %d/%d, %u", b.interval_x125 * 1.25, b.att_mtu,
                                  b.commands_ok, b.commands_sent, b.notifications);
                    link = bt::address_text(b.peer) + "   " +
                           (b.link == bt::Link::Ready ? tr("connected") : b.link_step) + d;
                    col = b.link == bt::Link::Ready ? kGood : kWarn;
                }
                row(tr("Controller"), link, col);
                if (b.link == bt::Link::None && !b.link_step.empty())
                    row("", b.link_step + (b.last_disconnect >= 0
                                               ? "  (" + std::to_string(b.last_disconnect) + ")"
                                               : std::string()),
                        kWarn);
            }
            y += 10;
            if (b.phase == bt::Phase::Scanning || b.phase == bt::Phase::Done)
            {
                heading(tr("Bluetooth LE devices"), px, y);
                y += 44;
                if (b.found.empty())
                    g.text(Font::Regular, ts(22), px, y, kSoft, Align::Left, tr("None yet."));
                int shown = 0;
                for (const bt::Found &f : b.found)
                {
                    if (shown++ == 7)
                        break;
                    std::string line = bt::address_text(f.addr) + "   " + std::to_string(f.rssi) + " dBm   ";
                    if (f.nintendo)
                        line += "Nintendo   ";
                    line += f.name.empty() ? "" : "\xE2\x80\x9C" + f.name + "\xE2\x80\x9D";
                    g.text(Font::Regular, ts(22), px, y, f.nintendo ? kGood : kWhite, Align::Left,
                           fit(g, Font::Regular, ts(22), line, pw));
                    y += 32;
                }
            }
        }
    }
    else
    {
        heading(tr("Wii Remotes in games"), px, y);
        y += 54;
        g.text(Font::Bold, ts(30), px, y, kWhite, Align::Left,
               settings_->wii_source ? tr("Real Wii Remotes (DolphinBar)") : tr("DualSense"));
        y += 60;
        para(tr("DualSense: your controller plays the Wii Remote. Real Wii Remotes: original Wii Remotes and Wii "
                "Remote Plus through a Mayflash DolphinBar (mode 4) in a USB port, up to four, with their own "
                "Nunchuk, Classic Controller, MotionPlus, pointer and motion; the DualSense still opens Porpoise's "
                "menu. A Remote is found when it's connected to the DolphinBar. Applies when the game starts or "
                "this changes."),
             kSoft, px, y, pw);
        y += 20;
        para(tr("A game can have its own choice in its settings (Wii Remote)."), kSoft, px, y, pw);
    }

    /* ---- the prompts ---- */
    if (ctl_sel_ == kSpeakerItem)
        draw_prompts({{Glyph::DPad, "Choose"}, {Glyph::Cross, "Play"}, {Glyph::Triangle, "Signal"}, {Glyph::Square, "On / Off"}},
                     {{Glyph::L1, "Volume"}, {Glyph::Circle, "Back"}}, "");
    else if (ctl_sel_ >= 2 && ctl_sel_ < kSpeakerItem && s.remote[ctl_sel_ - 2].state == db::SlotState::Connected)
        draw_prompts({{Glyph::DPad, "Choose"}, {Glyph::Square, "Rumble"}, {Glyph::Triangle, "Check Again"}},
                     {{Glyph::Circle, "Back"}}, "");
    else if (ctl_sel_ == kGcItem && gc::connected())
        draw_prompts({{Glyph::DPad, "Choose"}, {Glyph::Square, "Rumble"}}, {{Glyph::Circle, "Back"}}, "");
    else if (ctl_sel_ == kBtItem)
        draw_prompts({{Glyph::DPad, "Choose"}, {Glyph::Cross, "Clear List"}}, {{Glyph::Circle, "Back"}}, "");
    else if (ctl_sel_ == kItems - 1)
        draw_prompts({{Glyph::DPad, "Choose"}, {Glyph::Cross, "Change"}}, {{Glyph::Circle, "Back"}}, "");
    else
        draw_prompts({{Glyph::DPad, "Choose"}}, {{Glyph::Circle, "Back"}}, "");
}

/* ---- Wii Remote Setup with real Wii Remotes ------------------------------------------------ */
namespace
{
const Color kRemoteBody = rgba(0xF2F4F8), kRemoteEdge = rgba(0xC9D3FF), kRemoteKey = rgba(0x3A3F4A);

void round_dot(Gfx &g, float cx, float cy, float d, Color fill)
{
    g.panel(cx - d * 0.5f, cy - d * 0.5f, d, d, fill, 1, d * 0.5f);
}

/* A Wii Remote, drawn: upright (pointer up) or sideways (D-pad on the left). */
void draw_wii_remote(Gfx &g, float cx, float cy, bool sideways)
{
    const float len = 360, wid = 86;
    /* along: from the top (upright) or from the left (sideways) */
    auto at = [&](float along, float across) {
        return sideways ? std::pair<float, float>{cx - len * 0.5f + along, cy + across}
                        : std::pair<float, float>{cx + across, cy - len * 0.5f + along};
    };
    if (sideways)
        g.panel(cx - len * 0.5f, cy - wid * 0.5f, len, wid, kRemoteBody, 0.9f, 32, kRemoteEdge, 2.0f);
    else
        g.panel(cx - wid * 0.5f, cy - len * 0.5f, wid, len, kRemoteBody, 0.9f, 32, kRemoteEdge, 2.0f);
    /* D-pad: one whole cross */
    {
        const auto [x, y] = at(70, 0);
        const float l = 52, a = 17;
        g.panel(x - l * 0.5f, y - a * 0.5f, l, a, kRemoteKey, 1, 4);
        g.panel(x - a * 0.5f, y - l * 0.5f, a, l, kRemoteKey, 1, 4);
    }
    {
        const auto [x, y] = at(140, 0);
        round_dot(g, x, y, 42, kRemoteKey);
        g.text_mid(Font::Bold, 22, x, y, kRemoteBody, Align::Center, "A");
    }
    for (int k = -1; k <= 1; ++k)
    {
        const auto [x, y] = sideways ? at(205 + k * 0.0f, k * 24.0f) : at(205, k * 24.0f);
        round_dot(g, x, y, k == 0 ? 20 : 16, k == 0 ? rgba(0x5CA8FF) : kRemoteKey);
    }
    for (int k = 0; k < 2; ++k)
    {
        const auto [x, y] = at(270 + k * 38.0f, 0);
        round_dot(g, x, y, 28, kRemoteKey);
        g.text_mid(Font::Bold, 16, x, y, kRemoteBody, Align::Center, k == 0 ? "1" : "2");
    }
    for (int k = 0; k < 4; ++k)
    {
        const auto [x, y] = at(len - 14, (k - 1.5f) * 13.0f);
        round_dot(g, x, y, 6, k == 0 ? rgba(0x5CD3FF) : rgba(0xB8C4E8));
    }
}

/* A Nunchuk beside it: its stick, C and Z, and the cable to the Remote's foot. */
void draw_nunchuk(Gfx &g, float cx, float cy, float remote_cx, float remote_bottom)
{
    const float w = 92, h = 180;
    const Color cable = rgba(0xDDE3F2);
    const float down = remote_bottom + 40;
    g.panel(remote_cx - 2, remote_bottom, 4, down - remote_bottom, cable, 1, 2);
    g.panel(std::min(remote_cx, cx) - 2, down - 2, std::abs(cx - remote_cx) + 4, 4, cable, 1, 2);
    g.panel(cx - 2, cy + h * 0.5f, 4, down - (cy + h * 0.5f), cable, 1, 2);
    g.panel(cx - w * 0.5f, cy - h * 0.5f, w, h, kRemoteBody, 0.9f, 44, kRemoteEdge, 2.0f);
    round_dot(g, cx, cy - h * 0.5f + 52, 50, rgba(0xD5DAE6));
    round_dot(g, cx, cy - h * 0.5f + 52, 30, kRemoteKey);
    g.text_mid(Font::Bold, 18, cx + 30, cy - h * 0.5f + 104, kRemoteKey, Align::Center, "C");
    g.text_mid(Font::Bold, 18, cx + 30, cy - h * 0.5f + 132, kRemoteKey, Align::Center, "Z");
}
} // namespace

void App::draw_real_remote_setup(double)
{
    Gfx &g = *g_;
    g.panel(0, 0, 1920, 1080, rgba(0x02040C, 0.93f), 1, 0);
    g.text_mid(Font::Bold, ts(40), 90, 70, kWhite, Align::Left, tr("Wii Remote Setup"));
    g.text_mid(Font::Regular, ts(24), 90, 112, kLavender, Align::Left,
               tr("Real Wii Remotes (DolphinBar)") + (ws_game_ ? "   \xE2\x80\xA2   " + ws_game_->title : std::string()));

    /* What the DolphinBar sees now: the first Remote's attachment. */
    const db::Status st = db::status();
    int with_nunchuk = -1; /* unknown */
    for (const db::Remote &r : st.remote)
        if (r.state == db::SlotState::Connected)
        {
            with_nunchuk = r.ext == db::Extension::Nunchuk || r.ext == db::Extension::MotionPlusNunchuk ? 1 : 0;
            break;
        }

    struct Way
    {
        float cx;
        bool nunchuk;
        const char *title, *text;
    } ways[2] = {
        {600, true, "With a Nunchuk", "Hold the Remote upright and point it at the screen, the Nunchuk in your other hand."},
        {1320, false, "Without a Nunchuk", "Hold the Remote sideways, the D-pad on the left, when the game asks for it."},
    };
    for (const Way &w : ways)
    {
        const bool now = with_nunchuk >= 0 && (with_nunchuk == 1) == w.nunchuk;
        g.panel(w.cx - 320, 180, 640, 700, rgba(0x0A1236, now ? 0.9f : 0.6f), 0.9f, kR,
                now ? kCyan : rgba(0x3D4F9E, 0.8f), now ? 2.5f : 1.4f);
        g.text_mid(Font::Bold, ts(32), w.cx, 230, kWhite, Align::Center, tr(w.title));
        if (now)
            g.text_mid(Font::SemiBold, ts(20), w.cx, 268, kCyan, Align::Center, tr("Connected now"));
        if (w.nunchuk)
        {
            draw_wii_remote(g, w.cx - 70, 520, false);
            draw_nunchuk(g, w.cx + 110, 560, w.cx - 70, 700);
        }
        else
            draw_wii_remote(g, w.cx, 520, true);
        const auto lines = wrap(g, Font::Regular, ts(24), tr(w.text), 560, 3);
        float y = 790;
        for (const std::string &l : lines)
        {
            g.text_mid(Font::Regular, ts(24), w.cx, y, kSoft, Align::Center, l);
            y += 34;
        }
    }
    const auto lines = wrap(g, Font::Regular, ts(24),
                            tr("Real Wii Remotes need no setting up here: the game sees what's plugged in and how "
                               "the Remote is held, as on a Wii."),
                            1500, 2);
    float y = 925;
    for (const std::string &l : lines)
    {
        g.text_mid(Font::Regular, ts(24), 960, y, kLavender, Align::Center, l);
        y += 34;
    }
    draw_prompts({{Glyph::Triangle, "DualSense Setup"}}, {{Glyph::Circle, "Back"}}, "");
}
} // namespace porpoise::ui
