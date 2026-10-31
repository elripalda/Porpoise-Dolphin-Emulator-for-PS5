/* Porpoise UI - the button-mapping screen: the DualSense, drawn, with the
 * GameCube button each of its controls is, and a list to change them.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Settings > Controls > Customize buttons, a game's own settings, and the
 * in-game menu's Controls tab all open it. The player keeps four layouts of
 * their own ("My Layout 1".."4", with the global settings); the top row picks
 * which one to edit. Up and down pick a GameCube button; Cross waits for the
 * DualSense control to give it, and the control's old button moves to where
 * the new one was, so every button stays reachable. Changing a button puts
 * that layout to use for whatever opened the screen (every game, or one).
 * The last two rows start the layout over from a ready-made one.
 *
 * The controller is Zacksly's (PS5 Button Icons and Controls, CC BY 3.0). */
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sys/stat.h>

#include "porpoise_pad.hpp"
#include "ui_app.hpp"
#include "porpoise_states.hpp"
#include <ctime>
#include <unistd.h>
#include "ui_app_common.hpp"
#include "ui_i18n.hpp"

namespace porpoise::ui
{
using namespace look;
using namespace porpoise::pad;

namespace
{
/* Rows: the layout to edit, the twelve GameCube inputs, then "start from"
 * the two ready-made layouts. */
constexpr int kSelectRow = 0;
constexpr int kFirstButton = 1;
constexpr int kFromGameCube = kFirstButton + GcCount;
constexpr int kFromPlayStation = kFromGameCube + 1;
constexpr int kMapRows = kFromPlayStation + 1;
constexpr double kCaptureSeconds = 6.0;

/* The controller art (assets/ui/dualsense.png, from Zacksly's drawing by
 * tools/make-controller-art.py) is 1400x900 in these units whatever its
 * pixel size; these are its controls' centres there, in Control order, and
 * where each one's label sits (an offset from the centre). */
constexpr float kArtW = 1400, kArtH = 900;
struct Spot
{
    float x, y, lx, ly;
};
constexpr Spot kSpots[CtlCount] = {
    {1035, 399, 0, 66},     /* Cross */
    {1114, 319, 74, 0},     /* Circle */
    {955, 319, -74, 0},     /* Square */
    {1035, 240, 62, -50},   /* Triangle */
    {372, 140, -190, 22},   /* L1 */
    {1028, 140, 190, 22},   /* R1 */
    {366, 112, -170, -30},  /* L2 */
    {1034, 112, 170, -30},  /* R2 */
    {525, 468, 0, 100},     /* L3 */
    {873, 468, 0, 100},     /* R3 */
    {949, 198, 46, -72},    /* Options */
    {700, 233, 0, 0},       /* Touch pad */
    {363, 250, 0, 0},       /* Up */
    {363, 388, 0, 0},       /* Down */
    {299, 319, 0, 0},       /* Left */
    {428, 319, 0, 0},       /* Right */
};

/* The GameCube buttons: a short label for the chips, a name for the list, and
 * a colour in the spirit of the controller's own. */
struct GcInfo
{
    const char *chip;
    const char *name;
    std::uint32_t colour;
};
constexpr GcInfo kGc[GcCount] = {
    {"A", "A Button", 0x34C77B},     {"B", "B Button", 0xF2536B},    {"X", "X Button", 0xC9D3E6},
    {"Y", "Y Button", 0xC9D3E6},     {"Z", "Z Button", 0x8E6CFF},    {"L", "L Trigger", 0x9AA8C7},
    {"R", "R Trigger", 0x9AA8C7},    {"START", "Start", 0x9AA8C7},   {"", "D-Pad Up", 0x9AA8C7},
    {"", "D-Pad Down", 0x9AA8C7},    {"", "D-Pad Left", 0x9AA8C7},   {"", "D-Pad Right", 0x9AA8C7},
};
/* The arrow's rotation for the D-pad inputs (Glyph::Arrow points up at 0). */
float arrow_rotation(int gc)
{
    switch (gc)
    {
    case GcDown: return kPi;
    case GcLeft: return -kPi * 0.5f;
    case GcRight: return kPi * 0.5f;
    default: return 0;
    }
}

/* How a DualSense control is written on a keycap ("" for the face buttons,
 * which are drawn as their symbols). */
std::string control_label(int c)
{
    switch (c)
    {
    case CtlL1: return "L1";
    case CtlR1: return "R1";
    case CtlL2: return "L2";
    case CtlR2: return "R2";
    case CtlL3: return "L3";
    case CtlR3: return "R3";
    case CtlOptions: return tr("OPTIONS");
    case CtlTouch: return tr("TOUCH PAD");
    case CtlUp: return tr("D-Pad Up");
    case CtlDown: return tr("D-Pad Down");
    case CtlLeft: return tr("D-Pad Left");
    case CtlRight: return tr("D-Pad Right");
    default: return "";
    }
}

Glyph face_glyph(int c)
{
    switch (c)
    {
    case CtlCircle: return Glyph::Circle;
    case CtlSquare: return Glyph::Square;
    case CtlTriangle: return Glyph::Triangle;
    default: return Glyph::Cross;
    }
}

bool is_face(int c)
{
    return c == CtlCross || c == CtlCircle || c == CtlSquare || c == CtlTriangle;
}

std::string layout_name(int layout)
{
    if (layout >= LayoutOwn)
        return trf("My Layout {n}", {{"n", std::to_string(layout - LayoutOwn + 1)}});
    return tr(layout == LayoutPlayStation ? "PlayStation" : "GameCube");
}

/* A DualSense control's own icon, or its name on a keycap without the atlas. */
Icon control_icon(int c)
{
    switch (c)
    {
    case CtlCross: return Icon::Cross;
    case CtlCircle: return Icon::Circle;
    case CtlSquare: return Icon::Square;
    case CtlTriangle: return Icon::Triangle;
    case CtlL1: return Icon::L1;
    case CtlR1: return Icon::R1;
    case CtlL2: return Icon::L2;
    case CtlR2: return Icon::R2;
    case CtlL3: return Icon::L3;
    case CtlR3: return Icon::R3;
    case CtlOptions: return Icon::Options;
    case CtlTouch: return Icon::TouchPad;
    case CtlUp: return Icon::DPadUp;
    case CtlDown: return Icon::DPadDown;
    case CtlLeft: return Icon::DPadLeft;
    case CtlRight: return Icon::DPadRight;
    default: return Icon::Count;
    }
}

std::string control_name(int c)
{
    if (is_face(c))
        return tr(c == CtlCross ? "Cross" : c == CtlCircle ? "Circle" : c == CtlSquare ? "Square" : "Triangle");
    return control_label(c);
}
} // namespace

/* A GameCube button's coloured chip (an arrow for the D-pad). */
void App::draw_gc_chip(int gc, float cx, float cy, float h, bool on)
{
    Gfx &g = *g_;
    if (gc < 0 || gc >= GcCount)
    {
        g.text_mid(Font::Bold, h * 0.6f, cx, cy, with_alpha(kLavender, 0.7f), Align::Center, "\xE2\x80\x93");
        return;
    }
    const std::string label = kGc[gc].chip;
    const float w = label.empty() ? h : std::max(h, g.measure(Font::Bold, h * 0.55f, label) + h * 0.7f);
    g.panel(cx - w * 0.5f, cy - h * 0.5f, w, h, rgba(kGc[gc].colour), 0.8f, h * 0.5f,
            on ? kWhite : rgba(0xFFFFFF, 0.6f), on ? 2.0f : 1.2f);
    if (label.empty())
        g.glyph(Glyph::Arrow, cx, cy, h * 0.5f, rgba(0x0A1236), arrow_rotation(gc));
    else
        g.text_mid(Font::Bold, h * 0.55f, cx, cy, rgba(0x0A1236), Align::Center, label);
}

float App::gc_chip_width(int gc, float h)
{
    if (gc < 0 || gc >= GcCount || !kGc[gc].chip[0])
        return h;
    return std::max(h, g_->measure(Font::Bold, h * 0.55f, kGc[gc].chip) + h * 0.7f);
}

/* A DualSense control at the right of a row: its icon (Zacksly's), or a keycap. */
void App::draw_control(int control, float right, float cy, bool on, float size)
{
    Gfx &g = *g_;
    const Icon ic = control_icon(control);
    if (g.has_icons() && ic != Icon::Count)
    {
        g.icon(ic, right - size * 0.5f, cy, size, on ? kWhite : rgba(0xDCE6FF));
        return;
    }
    if (is_face(control))
        g.glyph(face_glyph(control), right - 18, cy, size * 0.66f, kWhite);
    else
        draw_keycap(right, cy, control_label(control), on, size * 0.62f);
}

/* ---- opening and saving ------------------------------------------------------------------- */

void App::open_mapping()
{
    const bool game = screen_ == Screen::GameSettings && game_for_;
    map_target_ = game ? &game_ : settings_;
    map_game_ = game ? game_for_ : nullptr;
    map_in_game_ = false;
    map_return_ = game ? Screen::GameSettings : Screen::Main;
    begin_mapping();
    open_screen(Screen::Mapping);
}

void App::open_mapping_in_game()
{
    map_target_ = menu_play_;
    map_game_ = menu_game_;
    map_in_game_ = true;
    begin_mapping();
}

void App::begin_mapping()
{
    /* Edit the layout in use, or the first one. */
    const int own = map_target_ ? map_target_->preset_in_use() : -1;
    map_preset_ = own >= 0 ? own : 0;
    map_wii_slot_ = map_target_ ? std::clamp(map_target_->wii_buttons_use, 0, 2) : 1;
    map_row_ = own >= 0 ? kFirstButton : kSelectRow;
    map_capture_ = false;
    map_note_.clear();
    sfx(Sound::DetailsFlip);
}

void App::close_mapping()
{
    map_capture_ = false;
    sfx(Sound::DetailsFlip);
    if (map_in_game_)
    {
        map_in_game_ = false;
        return;
    }
    if (map_return_ == Screen::GameSettings)
    {
        build_game_settings();
        open_screen(Screen::GameSettings);
    }
    else
    {
        build_settings();
        open_screen(Screen::Main);
    }
}

/* The layouts live with the global settings; which one is in use belongs to
 * whatever opened the screen. */
void App::save_mapping(bool layout_changed, const char *key)
{
    /* The layouts are the global settings'; every copy of the settings that
     * is on screen or in play gets them, so none puts old ones back. */
    for (Settings *other : {map_target_, &game_, &game_base_, menu_play_, &menu_draft_, &menu_base_})
        if (other && other != settings_)
        {
            std::memcpy(other->presets, settings_->presets, sizeof settings_->presets);
            std::memcpy(other->wii_buttons, settings_->wii_buttons, sizeof settings_->wii_buttons);
            std::memcpy(other->wii_buttons2, settings_->wii_buttons2, sizeof settings_->wii_buttons2);
        }
    if (map_game_ && map_target_ != settings_)
    {
        if (layout_changed)
        {
            const std::string path = game_settings_path(*map_game_);
            std::vector<std::string> keys = Settings::keys_in(path);
            if (std::find(keys.begin(), keys.end(), key) == keys.end())
                keys.push_back(key);
            if (map_target_ == &game_)
            {
                /* Saved: what's in effect for the game now, not a change
                 * waiting for Apply (a Discard would undo it on screen only). */
                if (std::find(game_keys_.begin(), game_keys_.end(), key) == game_keys_.end())
                    game_keys_.push_back(key);
                if (std::find(game_keys_base_.begin(), game_keys_base_.end(), key) == game_keys_base_.end())
                    game_keys_base_.push_back(key);
                game_base_.copy_keys(game_, {key});
            }
            mkdir((data_dir_ + "/game-settings").c_str(), 0777);
            /* From what's in effect: Game Settings' own changes wait for
             * Apply, and aren't written by this. */
            (map_target_ == &game_ ? game_base_ : *map_target_).save_keys(path, keys);
        }
    }
    settings_->save(settings_path_);
    if (map_in_game_)
    {
        /* In effect already: the in-game menu's rows show it as it is. */
        menu_draft_.copy_keys(*map_target_, {key});
        menu_base_.copy_keys(*map_target_, {key});
        menu_change_ = key;
    }
    else
        porpoise::pad::set_wii_buttons(map_target_->wii_buttons_in_use()); /* for the guides' pictures */
}

void App::use_preset(int preset)
{
    const bool changed = map_target_->button_layout != LayoutOwn + preset;
    map_target_->button_layout = LayoutOwn + preset;
    save_mapping(changed);
    map_note_ = map_game_ && map_target_ != settings_
                    ? trf("{layout} is on for this game.", {{"layout", layout_name(LayoutOwn + preset)}})
                    : trf("{layout} is on.", {{"layout", layout_name(LayoutOwn + preset)}});
    map_note_time_ = time_;
}

void App::assign_control(int gc, int control)
{
    int *row = settings_->presets[map_preset_];
    const int old = row[gc];
    for (int i = 0; i < GcCount; ++i)
        if (i != gc && row[i] == control)
            row[i] = old; /* the control's old button takes this one's place */
    row[gc] = control;
    use_preset(map_preset_);
    map_note_ = trf("{button} is now on {control}.", {{"button", tr(kGc[gc].name)}, {"control", control_name(control)}});
    map_note_time_ = time_;
}

void App::start_preset_from(int layout)
{
    const Mapping m = porpoise::pad::preset(layout);
    for (int i = 0; i < GcCount; ++i)
        settings_->presets[map_preset_][i] = m.control[i];
    use_preset(map_preset_);
    map_note_ = trf("{layout} now starts from the {base} layout.",
                    {{"layout", layout_name(LayoutOwn + map_preset_)}, {"base", layout_name(layout)}});
    map_note_time_ = time_;
}

/* ---- input --------------------------------------------------------------------------------- */

App::Action App::update_mapping(bool up, bool down, bool left, bool right)
{
    if (map_kind_ > 0)
        return update_wii_mapping(up, down, left, right);
    const bool global = map_target_ == settings_;
    const Action changed = global ? Action::SettingsChanged : Action::None;
    if (!map_capture_ && switch_map_kind())
        return Action::None;
    if (map_capture_)
    {
        /* The press that started the capture doesn't count: wait for every
         * button to be let go, then take the first one pressed. */
        const std::uint32_t mask = 0xFFFFu;
        if (time_ - map_capture_start_ > kCaptureSeconds)
        {
            map_capture_ = false;
            map_note_ = tr("Nothing pressed; the button is unchanged.");
            map_note_time_ = time_;
            return Action::None;
        }
        if (!map_armed_)
        {
            map_armed_ = (raw_held_ & mask) == 0;
            if (map_armed_)
                map_capture_start_ = time_; /* the countdown starts once the hands are free */
            return Action::None;
        }
        const std::uint32_t fresh = (raw_held_ & ~raw_prev_) & mask;
        if (fresh)
            for (int c = 0; c < CtlCount; ++c)
                if (fresh & control_bit(c))
                {
                    assign_control(map_row_ - kFirstButton, c);
                    map_capture_ = false;
                    sfx(Sound::LaunchGame);
                    return changed;
                }
        return Action::None;
    }

    if (up && map_row_ > 0)
    {
        --map_row_;
        sfx(Sound::MenuScroll);
    }
    if (down && map_row_ + 1 < kMapRows)
    {
        ++map_row_;
        sfx(Sound::MenuScroll);
    }
    if (map_row_ == kSelectRow && (left || right))
    {
        map_preset_ = (map_preset_ + (left ? Settings::kPresets - 1 : 1)) % Settings::kPresets;
        sfx(Sound::MovingTab);
    }
    if (pressed(BtnCircle))
    {
        close_mapping();
        return Action::None;
    }
    if (pressed(BtnCross))
    {
        if (map_row_ == kSelectRow)
        {
            use_preset(map_preset_);
            sfx(Sound::LaunchGame);
            return changed;
        }
        if (map_row_ >= kFirstButton && map_row_ < kFromGameCube)
        {
            map_capture_ = true;
            map_armed_ = false;
            map_capture_start_ = time_;
            sfx(Sound::DetailsFlip);
        }
        else
        {
            start_preset_from(map_row_ == kFromGameCube ? LayoutGameCube : LayoutPlayStation);
            sfx(Sound::LaunchGame);
            return changed;
        }
    }
    return Action::None;
}

/* ---- drawing ------------------------------------------------------------------------------- */

void App::draw_keycap(float right, float cy, const std::string &label, bool on, float height)
{
    Gfx &g = *g_;
    const float size = height * 0.52f;
    const float w = std::max(height * 1.5f, g.measure(Font::Bold, size, label) + height * 0.8f);
    g.panel(right - w, cy - height * 0.5f, w, height, rgba(0x07102E, on ? 0.65f : 0.45f), 1, kR * 0.8f,
            on ? rgba(0xE8F2FF) : rgba(0xA9B8E8, 0.85f), 1.8f);
    g.text_mid(Font::Bold, size, right - w * 0.5f, cy, kWhite, Align::Center, label);
}

void App::draw_mapping(double time)
{
    Gfx &g = *g_;
    if (!map_target_)
        return;
    if (map_kind_ > 0)
    {
        draw_wii_mapping(time);
        return;
    }
    const Settings &s = *map_target_;
    /* The screen shows the layout being edited, whichever is in use. */
    Mapping m{};
    for (int i = 0; i < GcCount; ++i)
        m.control[i] = static_cast<std::int8_t>(std::clamp(settings_->presets[map_preset_][i], 0, CtlCount - 1));
    const bool for_game = map_game_ && map_target_ != settings_;
    const bool editing_in_use = s.button_layout == LayoutOwn + map_preset_;
    if (map_in_game_)
        g.panel(0, 0, 1920, 1080, rgba(0x02040C, 0.78f), 1, 0);

    /* Left: the controller. */
    const float lx = 90, ly = 136, lw = 830, lh = 800;
    g.panel(lx, ly, lw, lh, rgba(0x0A1236, 0.55f), 0.85f, kR, rgba(0x3D4F9E, 0.9f), 1.6f, 0, 0.10f);
    g.text_mid(Font::Bold, ts(46), lx + 46, ly + 64, kWhite, Align::Left, tr("Buttons"));
    g.text_mid(Font::Regular, ts(26), lx + 46, ly + 112, kLavender, Align::Left,
               fit(g, Font::Regular, ts(26),
                   for_game ? trf("{game}: {layout}", {{"game", map_game_->title}, {"layout", layout_name(s.button_layout)}})
                            : trf("Every game: {layout}", {{"layout", layout_name(s.button_layout)}}),
                   lw - 92));

    if (!pad_art_tried_)
    {
        pad_art_tried_ = true;
        pad_art_ = g.texture_file(g.asset_dir() + "/ui/dualsense.png");
    }
    const float aw = 760, ah = aw * kArtH / kArtW, ax = lx + (lw - aw) * 0.5f, ay = ly + 160;
    const float k = aw / kArtW;
    auto at = [&](float x, float y) { return std::pair<float, float>{ax + x * k, ay + y * k}; };
    if (pad_art_)
        g.image(pad_art_, ax, ay, aw, ah, g.tone(rgba(0xDDEBFF))); /* line art: dark on the light look */

    /* Which GameCube input is on each control. */
    int on_control[CtlCount];
    std::fill(std::begin(on_control), std::end(on_control), -1);
    for (int gc = 0; gc < GcCount; ++gc)
        if (m.control[gc] >= 0 && m.control[gc] < CtlCount)
            on_control[int(m.control[gc])] = gc;
    const int focus_gc = map_row_ >= kFirstButton && map_row_ < kFromGameCube ? map_row_ - kFirstButton : -1;
    const int focus_ctl = focus_gc >= 0 ? m.control[focus_gc] : -1;
    const float pulse = settings_->reduced_motion ? 1.0f : 0.75f + 0.25f * std::sin(float(time) * 5.0f);

    /* The focused control glows; while waiting for a press, every control does. */
    for (int c = 0; c < CtlCount; ++c)
    {
        const bool lit = map_capture_ ? true : c == focus_ctl;
        if (!lit)
            continue;
        const auto [x, y] = at(kSpots[c].x, kSpots[c].y);
        const float r = map_capture_ ? 30 : 46;
        g.blob(x, y, r * 2, r * 2, rgba(0x5CD3FF, (map_capture_ ? 0.35f : 0.75f) * pulse));
    }
    /* A chip on each control with the GameCube button it is. */
    for (int c = 0; c < CtlCount; ++c)
    {
        const int gc = on_control[c];
        if (gc < 0)
            continue;
        const bool dpad_home = gc >= GcUp && int(m.control[gc]) == CtlUp + (gc - GcUp);
        if (dpad_home && gc != focus_gc)
            continue; /* the D-pad as the D-pad needs no labels */
        const auto [x, y] = at(kSpots[c].x + kSpots[c].lx, kSpots[c].y + kSpots[c].ly);
        const bool on = gc == focus_gc;
        const float h = on ? 40 : 34;
        const std::string label = kGc[gc].chip;
        const float w = label.empty() ? h : std::max(h, g.measure(Font::Bold, h * 0.55f, label) + h * 0.7f);
        g.panel(x - w * 0.5f, y - h * 0.5f, w, h, with_alpha(rgba(kGc[gc].colour), on ? 1.0f : 0.92f), 0.8f, h * 0.5f,
                on ? kWhite : rgba(0xFFFFFF, 0.7f), on ? 2.4f : 1.4f, on ? 10 : 0, 0.3f);
        if (label.empty())
            g.glyph(Glyph::Arrow, x, y, h * 0.5f, rgba(0x0A1236), arrow_rotation(gc));
        else
            g.text_mid(Font::Bold, h * 0.55f, x, y, rgba(0x0A1236), Align::Center, label);
    }
    {
        /* The sticks, with their icons. */
        const float sy = ly + lh - 56;
        if (g.has_icons())
        {
            const std::string a = tr("Control Stick"), b = tr("C-Stick");
            const float wa = g.measure(Font::Regular, ts(24), a), wb = g.measure(Font::Regular, ts(24), b);
            const float total = 44 + 10 + wa + 50 + 44 + 10 + wb;
            float x = lx + (lw - total) * 0.5f;
            g.icon(Icon::LStick, x + 22, sy, 50, kLavender);
            x += 54;
            x += g.text_mid(Font::Regular, ts(24), x, sy, kLavender, Align::Left, a) + 50;
            g.icon(Icon::RStick, x + 22, sy, 50, kLavender);
            g.text_mid(Font::Regular, ts(24), x + 54, sy, kLavender, Align::Left, b);
        }
        else
            g.text_mid(Font::Regular, ts(24), lx + lw * 0.5f, sy, kLavender, Align::Center,
                       tr("Left stick: control stick   \xE2\x80\xA2   Right stick: C-stick"));
    }

    /* Right: the list. */
    const float px = 950, py = 136, pw = 880, ph = 800;
    g.panel(px, py, pw, ph, rgba(0x0F1F63, 0.62f), 0.75f, kR, rgba(0x4C6FD8, 0.9f), 1.8f, 0, 0.12f);
    const float row_h = 42, row_x = px + 22, row_w = pw - 44;
    float y = py + 70; /* under the controller tabs */
    for (int i = 0; i < kMapRows; ++i)
    {
        if (i == kFirstButton || i == kFromGameCube)
        {
            g.panel(row_x + 20, y + 8, row_w - 40, 1.5f, rgba(0x3D4F9E, 0.8f), 1, 0);
            y += 18;
        }
        const bool on = i == map_row_;
        const float h_row = i == kSelectRow ? row_h + 12 : row_h;
        const float cy = y + h_row * 0.5f;
        if (on)
            g.panel(row_x, y + 2, row_w, h_row - 4, rgba(0x1D45B8, 0.88f), 0.7f, kR, kIcy, 2.2f, 8, 0.18f);
        const float right = row_x + row_w - 22;
        if (i == kSelectRow)
        {
            /* ◀ My layout N ▶, and whether it's the one in use. */
            g.text_mid(Font::SemiBold, ts(27), row_x + 34, cy, on ? kWhite : kSoft, Align::Left, tr("Layout"));
            const std::string name = layout_name(LayoutOwn + map_preset_);
            const float nw = g.measure(Font::Bold, ts(28), name);
            const float ncx = row_x + row_w * 0.5f + 40;
            g.text_mid(Font::Bold, ts(28), ncx, cy, kWhite, Align::Center, name);
            if (on)
            {
                g.glyph(Glyph::Arrow, ncx - nw * 0.5f - 28, cy, 18, kCyan, -kPi * 0.5f);
                g.glyph(Glyph::Arrow, ncx + nw * 0.5f + 28, cy, 18, kCyan, kPi * 0.5f);
            }
            if (editing_in_use)
            {
                const std::string in_use = tr("In Use");
                const float bw = g.measure(Font::Bold, ts(20), in_use) + 30;
                g.panel(right - bw, cy - 16, bw, 32, rgba(0x2FB574, 0.85f), 0.8f, 16, rgba(0xBDF5D8, 0.9f), 1.4f);
                g.text_mid(Font::Bold, ts(20), right - bw * 0.5f, cy, kWhite, Align::Center, in_use);
            }
        }
        else if (i < kFromGameCube)
        {
            /* The GameCube button's chip, its name, and its control. */
            const int gc = i - kFirstButton;
            const float h = 30, cx = row_x + 40;
            const std::string label = kGc[gc].chip;
            const float w = label.empty() ? h : std::max(h, g.measure(Font::Bold, h * 0.55f, label) + h * 0.7f);
            g.panel(cx - w * 0.5f, cy - h * 0.5f, w, h, rgba(kGc[gc].colour), 0.8f, h * 0.5f, rgba(0xFFFFFF, 0.6f), 1.2f);
            if (label.empty())
                g.glyph(Glyph::Arrow, cx, cy, h * 0.5f, rgba(0x0A1236), arrow_rotation(gc));
            else
                g.text_mid(Font::Bold, h * 0.55f, cx, cy, rgba(0x0A1236), Align::Center, label);
            g.text_mid(Font::SemiBold, ts(27), row_x + 92, cy, on ? kWhite : kSoft, Align::Left, tr(kGc[gc].name));
            if (on && map_capture_)
                g.text_mid(Font::Bold, ts(25), right, cy, kCyan, Align::Right, tr("Press a button\xE2\x80\xA6"));
            else
                draw_control(m.control[gc], right, cy, on, 54);
        }
        else
        {
            const bool from_gc = i == kFromGameCube;
            g.text_mid(Font::SemiBold, ts(26), row_x + 34, cy, on ? kWhite : kSoft, Align::Left,
                       tr(from_gc ? "Start Over from the GameCube Layout" : "Start Over from the PlayStation Layout"));
            g.text_mid(Font::Regular, ts(21), right, cy, with_alpha(kLavender, 0.9f), Align::Right,
                       tr(from_gc ? "Circle A \xE2\x80\xA2 Cross B" : "Cross A \xE2\x80\xA2 Circle B"));
        }
        y += h_row;
    }
    /* A line after a change, fading out. */
    const double since = time_ - map_note_time_;
    if (!map_note_.empty() && since < 4.0)
    {
        const float a = since < 3.0 ? 1.0f : float(4.0 - since);
        g.text_mid(Font::SemiBold, ts(24), px + pw * 0.5f, py + ph - 32, with_alpha(kCyan, a), Align::Center,
                   fit(g, Font::SemiBold, ts(24), map_note_, pw - 60));
    }

    draw_map_tabs();
    if (map_capture_)
    {
        const int left = int(std::ceil(kCaptureSeconds - (time_ - map_capture_start_)));
        draw_prompts({}, {}, trf("Press the DualSense button for {button} ({n})",
                                 {{"button", tr(kGc[map_row_ - kFirstButton].name)},
                                  {"n", std::to_string(std::max(1, left))}}));
    }
    else if (map_row_ == kSelectRow)
        draw_prompts({{Glyph::DPad, "Browse"}, {Glyph::Cross, "Use This Layout"}, {Glyph::Circle, "Back"}},
                     {{kKeyL1R1, "Controller"}}, "");
    else if (map_row_ < kFromGameCube)
        draw_prompts({{Glyph::DPad, "Browse"}, {Glyph::Cross, "Change"}, {Glyph::Circle, "Back"}},
                     {{kKeyL1R1, "Controller"}}, "");
    else
        draw_prompts({{Glyph::DPad, "Browse"}, {Glyph::Cross, "Start Over"}, {Glyph::Circle, "Back"}},
                     {{kKeyL1R1, "Controller"}}, "");
}

/* ---- a Wii game's controls ------------------------------------------------------------- */

namespace
{
/* A Wii input's chip text and its name in the list. */
struct WiiInfo
{
    const char *chip;
    const char *name;
    std::uint32_t colour;
};
constexpr std::uint32_t kWiiWhite = 0xEEF3FF, kWiiBlue = 0x7FD9FF, kWiiAmber = 0xFFC85C, kWiiGreen = 0x6BE3A8;
constexpr WiiInfo kWiiInfo[WiInputCount] = {
    {"A", "A Button", kWiiBlue},
    {"B", "B, the trigger", kWiiBlue},
    {"1", "1 Button", kWiiWhite},
    {"2", "2 Button", kWiiWhite},
    {"-", "Minus", kWiiWhite},
    {"+", "Plus", kWiiWhite},
    {"HOME", "HOME Button", kWiiWhite},
    {"", "D-Pad Up", kWiiWhite},
    {"", "D-Pad Down", kWiiWhite},
    {"", "D-Pad Left", kWiiWhite},
    {"", "D-Pad Right", kWiiWhite},
    {"SHAKE", "Shakes the Remote", kWiiAmber},
    {"C", "Nunchuk C", kWiiWhite},
    {"Z", "Nunchuk Z", kWiiWhite},
    {"SHAKE", "Shakes the Nunchuk", kWiiAmber},
    {"a", "a button", kWiiBlue},
    {"b", "b button", kWiiBlue},
    {"x", "x button", kWiiWhite},
    {"y", "y button", kWiiWhite},
    {"ZL", "ZL Button", kWiiWhite},
    {"ZR", "ZR Button", kWiiWhite},
    {"L", "L Trigger", kWiiWhite},
    {"R", "R Trigger", kWiiWhite},
    {"CENTER", "Hold: center and level", kWiiGreen},
};
float wii_arrow(int input)
{
    switch (input)
    {
    case WiDown: return kPi;
    case WiLeft: return -kPi * 0.5f;
    case WiRight: return kPi * 0.5f;
    default: return 0;
    }
}
/* A Wii input's name, chip, colour and arrow (the Wii buttons' list). */
const char *wii_input_name(int input)
{
    return input >= 0 && input < WiInputCount ? kWiiInfo[input].name : "";
}
const char *wii_input_chip(int input)
{
    return input >= 0 && input < WiInputCount ? kWiiInfo[input].chip : "";
}
std::uint32_t wii_input_colour(int input)
{
    return input >= 0 && input < WiInputCount ? kWiiInfo[input].colour : kWiiWhite;
}
float wii_input_arrow(int input)
{
    return wii_arrow(input);
}
const char *stick_name(int role)
{
    switch (role)
    {
    case StickDPad: return "D-Pad";
    case StickNunchuk: return "Nunchuk Stick";
    case StickClassicLeft: return "Left Stick";
    case StickClassicRight: return "Right Stick";
    case StickPointer: return "Aims the pointer";
    case StickTilt: return "Tilts the Remote";
    default: return "";
    }
}
const char *wii_controller_name(int controller)
{
    switch (controller)
    {
    case WiiRemote: return "Remote";
    case WiiSideways: return "Remote Sideways";
    case WiiClassic: return "Classic Controller";
    case WiiTwoControllers: return "Two Controllers (Alpha)";
    case WiiGameCube: return "GameCube Controller";
    default: return "Remote + Nunchuk";
    }
}
} // namespace

namespace
{
const char *pose_words(int pose)
{
    switch (pose)
    {
    case PoseTriggerRight: return "Upright in your right hand, the trigger edge toward the TV";
    case PoseTriggerLeft: return "Upright in your left hand, the trigger edge toward the TV";
    case PoseFacingRight: return "Upright in your right hand, its face toward you";
    case PoseFacingLeft: return "Upright in your left hand, its face toward you";
    default: return "Face up, in both hands";
    }
}
} // namespace

/* One DualSense, drawn as held, with each Wii input on its control. (bx, by)
 * is the top left of its box; art_h the art's long side on screen. Returns
 * the box's width and height. */
std::pair<float, float> App::draw_wii_pad(const WiiLayout &lay, int pose, float bx, float by, float art_h,
                                          float alpha)
{
    Gfx &g = *g_;
    const bool right_up = pose == PoseTriggerRight || pose == PoseFacingRight;
    const bool left_up = pose == PoseTriggerLeft || pose == PoseFacingLeft;
    const bool upright = right_up || left_up;
    const float k = art_h / kArtW;
    const float bw = upright ? kArtH * k : kArtW * k, bh = upright ? kArtW * k : kArtH * k;
    /* Art units -> screen: the right grip up is a quarter turn anticlockwise. */
    auto at = [&](float u, float v) -> std::pair<float, float> {
        if (right_up)
            return {bx + v * k, by + (kArtW - u) * k};
        if (left_up)
            return {bx + (kArtH - v) * k, by + u * k};
        return {bx + u * k, by + v * k};
    };
    if (!pad_art_tried_)
    {
        pad_art_tried_ = true;
        pad_art_ = g.texture_file(g.asset_dir() + "/ui/dualsense.png");
    }
    if (pad_art_)
    {
        const Color tint = with_alpha(rgba(0xDDEBFF), alpha);
        if (!upright)
            g.image(pad_art_, bx, by, bw, bh, g.tone(tint));
        else
        {
            const auto tl = at(0, 0), tr_ = at(kArtW, 0), br = at(kArtW, kArtH), bl = at(0, kArtH);
            const Corner c[4] = {{tl.first, tl.second, 1}, {tr_.first, tr_.second, 1}, {br.first, br.second, 1},
                                 {bl.first, bl.second, 1}};
            g.quad3d(pad_art_, c, kArtW * k, kArtH * k, g.tone(tint), 0, false, false);
        }
    }
    const float chip_h = std::clamp(52 * k / 0.48f, 24.0f, 44.0f);
    /* The shoulder buttons sit on the edge, one behind the other: their tags
     * go outside the outline (art units), joined to the button by a line. */
    auto shoulder = [](int c, float &u, float &v) {
        switch (c)
        {
        case CtlL2: u = 300; v = 18; return true;
        case CtlL1: u = 110; v = 128; return true;
        case CtlR2: u = 1100; v = 18; return true;
        case CtlR1: u = 1290; v = 128; return true;
        default: return false;
        }
    };
    for (int i = 0; i < lay.count; ++i)
    {
        const WiiBinding &b = lay.binds[i];
        if (b.control < 0 || b.control >= CtlCount)
            continue;
        float u, v;
        if (shoulder(b.control, u, v))
        {
            const auto [sx, sy] = at(kSpots[b.control].x, kSpots[b.control].y - 14);
            const auto [tx, ty] = at(u, v);
            const float dx = tx - sx, dy = ty - sy, len = std::sqrt(dx * dx + dy * dy);
            if (len > 1)
            {
                const float nx = -dy / len * 1.3f, ny = dx / len * 1.3f;
                const Corner c[4] = {{sx + nx, sy + ny, 1}, {tx + nx, ty + ny, 1}, {tx - nx, ty - ny, 1},
                                     {sx - nx, sy - ny, 1}};
                g.quad3d(nullptr, c, len, 2.6f, with_alpha(rgba(0x8BD9FF), 0.85f * alpha), 0, false, false);
            }
            g.blob(sx, sy, 18, 18, with_alpha(rgba(0x8BD9FF), 0.9f * alpha));
            const WiiInfo &info = kWiiInfo[b.input];
            const std::string label = info.chip;
            const float cw = label.size() <= 1 ? chip_h : g.measure(Font::Bold, chip_h * 0.46f, label) + chip_h * 0.8f;
            const float pw = cw + chip_h + 14;
            g.panel(tx - pw * 0.5f, ty - chip_h * 0.5f - 5, pw, chip_h + 10, with_alpha(rgba(0x13308A), 0.96f * alpha),
                    0.8f, (chip_h + 10) * 0.5f, with_alpha(rgba(0x8BD9FF), 0.9f * alpha), 1.4f);
            if (g.has_icons())
                g.icon(control_icon(b.control), tx - pw * 0.5f + chip_h * 0.5f + 6, ty, chip_h + 4,
                       with_alpha(rgba(0xDCE8FF), alpha));
            wii_chip(b.input, tx + pw * 0.5f - cw * 0.5f - 6, ty, chip_h, alpha);
            continue;
        }
        const auto [cx, cy] = at(kSpots[b.control].x, kSpots[b.control].y);
        wii_chip(b.input, cx, cy, chip_h, alpha);
    }
    /* The sticks: a word just below each, clear of its L3 / R3 chip. */
    auto stick_tag = [&](int role, float u, float v) {
        if (role == StickNothing)
            return;
        const auto [cx, sy] = at(u, v);
        const float cy = sy + chip_h * 1.2f;
        const std::string t = tr(stick_name(role));
        const float tw = g.measure(Font::SemiBold, ts(20), t) + 24;
        g.panel(cx - tw * 0.5f, cy - 16, tw, 32, with_alpha(rgba(0x13308A), 0.94f * alpha), 0.8f, 16,
                with_alpha(rgba(0x8BD9FF), 0.9f * alpha), 1.4f);
        g.text_mid(Font::SemiBold, ts(20), cx, cy, with_alpha(kWhite, alpha), Align::Center, t);
    };
    stick_tag(lay.left_stick, 525, 468);
    stick_tag(lay.right_stick, 873, 468);
    return {bw, bh};
}

/* A Wii input's chip: its letter, an arrow, or a drawn - / +. */
void App::wii_chip(int input, float cx, float cy, float ch, float alpha)
{
    Gfx &g = *g_;
    const WiiInfo &info = kWiiInfo[input];
    const std::string label = info.chip;
    const float cw = label.size() <= 1 ? ch : g.measure(Font::Bold, ch * 0.46f, label) + ch * 0.8f;
    g.panel(cx - cw * 0.5f, cy - ch * 0.5f, cw, ch, with_alpha(rgba(info.colour), 0.97f * alpha), 0.8f, ch * 0.5f,
            with_alpha(kWhite, 0.85f * alpha), 1.6f, 6, 0.35f);
    const Color ink = with_alpha(rgba(0x0A1236), alpha);
    if (label.empty())
        g.glyph(Glyph::Arrow, cx, cy, ch * 0.5f, ink, wii_arrow(input));
    else if (input == WiMinus || input == WiPlus)
    {
        const float bar = ch * 0.40f, th = std::max(2.0f, ch * 0.10f);
        g.panel(cx - bar * 0.5f, cy - th * 0.5f, bar, th, ink, 1, th * 0.5f);
        if (input == WiPlus)
            g.panel(cx - th * 0.5f, cy - bar * 0.5f, th, bar, ink, 1, th * 0.5f);
    }
    else
        g.text_mid(Font::Bold, label.size() == 1 ? ch * 0.62f : ch * 0.46f, cx, cy, ink, Align::Center, label);
}

/* The list beside or below a drawing. Plain buttons go in a grid of
 * "control -> Wii button" pairs; the rest (shake, centring, the D-pad and the
 * sticks) get a line with words. Controls with the same job share an entry. */
float App::draw_wii_list(const WiiLayout &lay, float lx, float ly, float lw, int columns, float alpha)
{
    Gfx &g = *g_;
    struct Entry
    {
        std::vector<Icon> icons;
        int input = -1;
        std::string text;
    };
    std::vector<Entry> plain, worded;
    auto add = [&](Icon icon, int input, const std::string &text) {
        std::vector<Entry> &list = text.empty() ? plain : worded;
        for (Entry &e : list)
            if (e.input == input && e.text == text && e.icons.size() < 3)
            {
                e.icons.push_back(icon);
                return;
            }
        list.push_back({{icon}, input, text});
    };
    bool dpad_listed = false;
    for (int i = 0; i < lay.count; ++i)
    {
        const WiiBinding &b = lay.binds[i];
        const bool arrow_home = b.input >= WiUp && b.input <= WiRight && b.control == CtlUp + (b.input - WiUp);
        if (arrow_home)
        {
            if (!dpad_listed)
                add(Icon::DPad, -1, tr("D-Pad"));
            dpad_listed = true;
            continue;
        }
        const bool simple = b.input <= WiHome || b.input == WiC || b.input == WiZ || (b.input >= WiClA && b.input <= WiClR);
        add(control_icon(b.control), b.input, simple ? std::string() : tr(kWiiInfo[b.input].name));
    }
    if (lay.left_stick != StickNothing)
        add(Icon::LStick, -1, tr(stick_name(lay.left_stick)));
    if (lay.right_stick != StickNothing)
        add(Icon::RStick, -1, tr(stick_name(lay.right_stick)));

    const float icon = 38, ch = 30, row_h = 44;
    auto chip_w = [&](int input) {
        const std::string label = kWiiInfo[input].chip;
        return label.size() <= 1 ? ch : g.measure(Font::Bold, ch * 0.46f, label) + ch * 0.8f;
    };
    auto draw_icons = [&](const Entry &e, float x, float y) {
        for (std::size_t k = 0; k < e.icons.size(); ++k)
        {
            if (k > 0)
            {
                g.text_mid(Font::Regular, ts(20), x - 2, y, with_alpha(kLavender, alpha), Align::Left, "/");
                x += 12;
            }
            if (g.has_icons())
                g.icon(e.icons[k], x + icon * 0.5f, y, icon, with_alpha(rgba(0xDCE8FF), alpha));
            x += icon + 4;
        }
        return x;
    };
    /* The grid: as many pairs to a row as fit. */
    float x = lx, y = ly;
    const float cell = std::max(124.0f, lw / float(std::max(1, columns * 3)));
    for (const Entry &e : plain)
    {
        const float need = float(e.icons.size()) * (icon + 16) + 26 + chip_w(e.input);
        const float take = std::ceil(need / cell) * cell;
        if (x > lx && x + take > lx + lw + 1)
        {
            x = lx;
            y += row_h;
        }
        float cx = draw_icons(e, x, y);
        g.glyph(Glyph::Arrow, cx + 8, y, 12, with_alpha(kLavender, 0.8f * alpha), kPi * 0.5f);
        cx += 22;
        wii_chip(e.input, cx + chip_w(e.input) * 0.5f, y, ch, alpha);
        x += take;
    }
    if (!plain.empty())
        y += row_h;
    /* The worded ones, in columns. */
    const float col_w = lw / float(columns);
    int n = 0;
    for (const Entry &e : worded)
    {
        const int col = n % columns, row = n / columns;
        ++n;
        const float ix = lx + col * col_w, iy = y + row * row_h;
        float tx = draw_icons(e, ix, iy) + 8;
        if (e.input >= 0)
        {
            wii_chip(e.input, tx + chip_w(e.input) * 0.5f, iy, ch, alpha);
            tx += chip_w(e.input) + 12;
        }
        g.text_mid(Font::SemiBold, ts(23), tx, iy, with_alpha(kSoft, alpha), Align::Left,
                   fit(g, Font::SemiBold, ts(23), e.text, ix + col_w - tx - 8));
    }
    return (y - ly) + float((n + columns - 1) / columns) * row_h;
}

void App::draw_wii_controls(float x, float y, float w, float h, const WiiConfig &wii, float alpha, int live_pose,
                            int live_pose_second)
{
    Gfx &g = *g_;
    if (alpha <= 0.01f)
        return;
    g.panel(x, y, w, h, rgba(0x0A1236, 0.86f * alpha), 0.85f, kR, with_alpha(rgba(0x3D4F9E), 0.9f * alpha), 1.6f, 0,
            0.10f);
    if (wii.controller == WiiGameCube)
    {
        /* The GameCube's own layout, as in a GameCube game. */
        g.text_mid(Font::Bold, ts(40), x + 40, y + 52, with_alpha(kWhite, alpha), Align::Left,
                   tr(wii_controller_name(wii.controller)));
        const auto lines = wrap(g, Font::Regular, ts(23), tr(kGameCubeOnWiiHelp), w - 80, 3);
        float ly = y + 96;
        for (const std::string &l : lines)
        {
            g.text_mid(Font::Regular, ts(23), x + 40, ly, with_alpha(kLavender, alpha), Align::Left, l);
            ly += 32;
        }
        const float aw = std::min(w - 80.0f, 760.0f);
        const Settings *buttons = ws_in_game_ && menu_play_ ? menu_play_ : settings_;
        draw_controller_lines(x + (w - aw) * 0.5f, ly + 30, aw, buttons->mapping());
        return;
    }
    const bool two = wii.controller == WiiTwoControllers;
    const int pose = live_pose >= 0 ? live_pose : expected_pose(wii, false);
    const std::string auto_word = wii.grip == GripAuto ? tr("Auto: ") : "";
    g.text_mid(Font::Bold, ts(40), x + 40, y + 52, with_alpha(kWhite, alpha), Align::Left,
               tr(wii_controller_name(wii.controller)));

    if (two)
    {
        /* Two DualSenses, mirror images: the Nunchuk on the left, the Remote on the right. */
        const int pose2 = live_pose_second >= 0 ? live_pose_second : expected_pose(wii, true);
        g.text_mid(Font::Regular, ts(23), x + 40, y + 96, with_alpha(kLavender, alpha), Align::Left,
                   fit(g, Font::Regular, ts(23), auto_word + tr("one in each hand, both stood on end"), w - 80));
        const WiiLayout left = wii_layout(wii, true, pose2), right = wii_layout(wii, false, pose);
        const float art_h = std::min(400.0f, h * 0.38f);
        const float pw = kArtH * art_h / kArtW; /* an upright pad's width */
        const float gap = (w - 2 * pw) / 3;
        const float ay = y + 140;
        draw_wii_pad(left, pose2, x + gap, ay, art_h, alpha);
        draw_wii_pad(right, pose, x + 2 * gap + pw, ay, art_h, alpha);
        const float hy = ay + art_h + 30;
        g.text_mid(Font::Bold, ts(24), x + 40, hy, with_alpha(kWhite, alpha), Align::Left, tr("Left hand: the Nunchuk"));
        g.text_mid(Font::Bold, ts(24), x + w * 0.5f + 10, hy, with_alpha(kWhite, alpha), Align::Left,
                   tr("Right hand: the Remote"));
        draw_wii_list(left, x + 40, hy + 50, w * 0.5f - 60, 1, alpha);
        draw_wii_list(right, x + w * 0.5f + 10, hy + 50, w * 0.5f - 50, 1, alpha);
    }
    else
    {
        g.text_mid(Font::Regular, ts(23), x + 40, y + 96, with_alpha(kLavender, alpha), Align::Left,
                   fit(g, Font::Regular, ts(23), auto_word + tr(pose_words(pose)), w - 80));
        const WiiLayout lay = wii_layout(wii, false, pose);
        const bool upright = pose != PoseFlat;
        if (upright)
        {
            const float art_h = std::min(h - 230.0f, 620.0f);
            const bool right_up = pose == PoseTriggerRight || pose == PoseFacingRight;
            const auto [bw, bh] = draw_wii_pad(lay, pose, x + (right_up ? 80 : 30), y + 150, art_h, alpha);
            (void)bh;
            const float lx = x + (right_up ? 80 : 30) + bw + (right_up ? 60 : 140);
            draw_wii_list(lay, lx, y + 160, x + w - 30 - lx, 1, alpha);
        }
        else
        {
            const float art_w = std::min({w - 120.0f, 680.0f, (h - 420.0f) * kArtW / kArtH});
            const float art_h = art_w; /* the long side */
            const auto [bw, bh] = draw_wii_pad(lay, pose, x + (w - art_w) * 0.5f, y + 130, art_h, alpha);
            (void)bw;
            draw_wii_list(lay, x + 50, y + 130 + bh + 30, w - 100, 2, alpha);
        }
    }

    /* How the motion works. */
    std::string how;
    const bool held = wii.controller != WiiSideways && wii.controller != WiiClassic && wii.controller != WiiGameCube;
    if (wii.pointer == PointerGyro && held)
        how = pose_left_hand(pose) ? tr("Point at the screen to aim. Hold L1 a moment to center the pointer and level the hold.")
                                   : tr("Point at the screen to aim. Hold R1 a moment to center the pointer and level the hold.");
    else if (wii.pointer == PointerTouch && held)
        how = tr("Slide a finger on the touch pad to aim. Flick the controller to shake.");
    else
        how = tr("Tilt and flick the controller: the game feels it.");
    const auto lines = wrap(g, Font::Regular, ts(23), how, w - 80, 2);
    float hy = y + h - 34 - 30 * float(lines.size() - 1);
    for (const std::string &l : lines)
    {
        g.text_mid(Font::Regular, ts(23), x + 40, hy, with_alpha(kLavender, alpha), Align::Left, l);
        hy += 30;
    }
}

/* ---- Settings > Wii Remote > How to hold it ------------------------------------------- */

namespace
{
const char *wii_howto(int controller)
{
    switch (controller)
    {
    case WiiRemote:
        return "Stand the DualSense on end in your right hand with the trigger edge toward the TV: R2 under your "
               "index finger is B, Cross under your thumb is A. Or hold it face up in both hands; with Grip on Auto, "
               "Porpoise reads which. To start, hold it the way you'll play, point at the middle of the "
               "screen and hold R1 for a moment: that hold becomes the Remote held level. Do it again whenever the "
               "pointer wanders.";
    case WiiSideways:
        return "Hold the DualSense face up in both hands, like an NES pad. Tilt it left and right to steer. 1 and 2 "
               "are Square and Cross.";
    case WiiClassic:
        return "Hold it as a normal pad: the buttons are where a Classic Controller has them.";
    case WiiGameCube:
        return kGameCubeOnWiiHelp;
    case WiiTwoControllers:
        return "One DualSense in each hand, both stood on end with their faces toward you. The right one is the "
               "Remote (its grips pointing right): aim with its back. Holding R1 a moment centers the pointer and levels both controllers as you hold them. The left one is the Nunchuk (its "
               "grips pointing left): its stick, C and Z, and its motion. Both feel the game's rumble. The second "
               "controller needs a second signed-in user.";
    default:
        return "Hold the DualSense face up in both hands. The right half is the Remote, the left half the Nunchuk: "
               "the left stick is its stick, L1 is C, L2 is Z. Point the controller at the screen to aim; hold R1 a moment to center.";
    }
}
} // namespace

void App::open_wii_guide()
{
    const bool game = screen_ == Screen::GameSettings && game_for_;
    guide_target_ = game ? &game_ : settings_;
    guide_return_ = game ? Screen::GameSettings : Screen::Main;
    guide_controller_ = std::clamp(guide_target_->wii_controller, 0, int(WiiControllerCount) - 1);
    sfx(Sound::DetailsFlip);
    open_screen(Screen::WiiGuide);
}

App::Action App::update_wii_guide(bool left, bool right)
{
    if (left || right)
    {
        guide_controller_ = (guide_controller_ + (left ? WiiControllerCount - 1 : 1)) % WiiControllerCount;
        sfx(Sound::MovingTab);
    }
    if (pressed(BtnCircle) || pressed(BtnCross))
    {
        sfx(Sound::DetailsFlip);
        if (guide_return_ == Screen::GameSettings)
        {
            build_game_settings();
            open_screen(Screen::GameSettings);
        }
        else
            open_screen(Screen::Main);
    }
    return Action::None;
}

void App::draw_wii_guide(double)
{
    Gfx &g = *g_;
    if (!guide_target_)
        return;
    WiiConfig wii = guide_target_->wii_config(true);
    wii.controller = guide_controller_;
    /* Left: what it is and how to hold it; right: the drawing. */
    const float lx = 90, ly = 136, lw = 750, lh = 800;
    g.panel(lx, ly, lw, lh, rgba(0x0F1F63, 0.62f), 0.75f, kR, rgba(0x4C6FD8, 0.9f), 1.8f, 0, 0.12f);
    g.text_mid(Font::Bold, ts(46), lx + 44, ly + 64, kWhite, Align::Left, tr("How to Hold It"));
    {
        /* Beta, plainly. */
        const std::string beta = tr("Beta");
        const float bw = g.measure(Font::Bold, ts(20), beta) + 28;
        const float bx = lx + 44 + g.measure(Font::Bold, ts(46), tr("How to Hold It")) + 20;
        g.panel(bx, ly + 64 - 17, bw, 34, rgba(0xFFC85C, 0.9f), 0.8f, 17, rgba(0xFFE7B0), 1.4f);
        g.text_mid(Font::Bold, ts(20), bx + bw * 0.5f, ly + 64, rgba(0x2A1A00), Align::Center, beta);
    }
    /* The controller, chosen with left / right. */
    const std::string name = tr(wii_controller_name(guide_controller_));
    const float ny = ly + 140;
    g.panel(lx + 40, ny - 30, lw - 80, 60, rgba(0x1D45B8, 0.88f), 0.7f, kR, kIcy, 2.2f, 8, 0.18f);
    g.text_mid(Font::Bold, ts(30), lx + lw * 0.5f, ny, kWhite, Align::Center, name);
    const float nw = g.measure(Font::Bold, ts(30), name);
    g.glyph(Glyph::Arrow, lx + lw * 0.5f - nw * 0.5f - 34, ny, 20, kCyan, -kPi * 0.5f);
    g.glyph(Glyph::Arrow, lx + lw * 0.5f + nw * 0.5f + 34, ny, 20, kCyan, kPi * 0.5f);
    const bool in_use = guide_controller_ == guide_target_->wii_controller;
    g.text_mid(Font::Regular, ts(22), lx + lw * 0.5f, ny + 52, in_use ? kCyan : kLavender, Align::Center,
               in_use ? tr("The Wii controller you have chosen") : tr("Another Wii controller, to look at"));
    const auto lines = wrap(g, Font::Regular, ts(27), tr(wii_howto(guide_controller_)), lw - 90, 16);
    float ty = ny + 120;
    for (const std::string &l : lines)
    {
        g.text_mid(Font::Regular, ts(27), lx + 44, ty, kSoft, Align::Left, l);
        ty += 40;
    }
    {
        const auto beta = wrap(g, Font::Regular, ts(23),
                               tr("Wii motion controls are in beta: still being tuned. Your logs in the debug folder "
                                  "help."),
                               lw - 90, 3);
        float by = ly + lh - 40 - 32 * float(beta.size() - 1);
        for (const std::string &l : beta)
        {
            g.text_mid(Font::Regular, ts(23), lx + 44, by, kLavender, Align::Left, l);
            by += 32;
        }
    }
    draw_wii_controls(870, 120, 960, 850, wii, 1.0f);
    draw_prompts({{Glyph::DPad, "Other Wii Controllers"}, {Glyph::Circle, "Back"}}, {}, "");
}

} // namespace porpoise::ui

/* ---- the Wii Remote setup ------------------------------------------------------------------ */

namespace porpoise::ui
{
using namespace look;
using namespace porpoise::pad;

namespace
{
/* What suits a game best, from the first three letters of its disc ID. */
struct Advice
{
    const char *id;
    int controller;
    const char *note;
};
constexpr Advice kAdvice[] = {
    {"RSP", WiiRemote, "Wii Sports: the Remote. For Boxing, two controllers."},
    {"RZT", WiiRemote, "Wii Sports Resort: the Remote (some events want the Nunchuk)."},
    {"RMC", WiiSideways, "Mario Kart Wii: the Remote held sideways; tilt to steer."},
    {"SMN", WiiSideways, "New Super Mario Bros. Wii: the Remote held sideways."},
    {"SUK", WiiSideways, "Kirby's Return to Dream Land: the Remote held sideways."},
    {"R8P", WiiSideways, "Super Paper Mario: the Remote held sideways."},
    {"RMG", WiiRemoteNunchuk, "Super Mario Galaxy: the Remote with the Nunchuk."},
    {"SB4", WiiRemoteNunchuk, "Super Mario Galaxy 2: the Remote with the Nunchuk."},
    {"RZD", WiiRemoteNunchuk, "Twilight Princess: the Remote with the Nunchuk."},
    {"SOU", WiiRemoteNunchuk, "Skyward Sword: the Remote with the Nunchuk."},
    {"RSB", WiiGameCube, "Super Smash Bros. Brawl: the GameCube controller (or the Classic Controller)."},
    {"SX4", WiiClassic, "Xenoblade Chronicles: the Classic Controller."},
};
const Advice *advice_for(const Game *g)
{
    if (!g || g->id.size() < 3)
        return nullptr;
    for (const Advice &a : kAdvice)
        if (g->id.compare(0, 3, a.id) == 0)
            return &a;
    return nullptr;
}
/* The corner targets sit this far toward the corners (the screen's edge is 1). */
constexpr float kCorner = 0.8f;
constexpr float kDeg = 57.2957795f;

bool points(int controller)
{
    return controller != WiiSideways && controller != WiiClassic && controller != WiiGameCube;
}
} // namespace

App::Action App::start_game(Game *g, const std::string &state, bool resume_asked)
{
    /* Another console's image (found before this version, or read wrongly
     * once): an error, not a crash in the emulator. */
    if (g && foreign_disc(*g))
    {
        open_dialog(DialogKind::Info, tr("This isn't a GameCube or Wii game"),
                    trf("{file} is a disc image from another console, so Porpoise can't play it.",
                        {{"file", g->file}}),
                    "");
        return Action::None;
    }
    /* The game's own choice, else the global one. */
    Settings eff = *settings_;
    if (g)
        eff.load(game_settings_path(*g), true);
    /* Quick resume (beta): where the game was left, if it was; ask first. */
    if (g && state.empty() && eff.quick_resume && !resume_asked)
    {
        const std::string resume = porpoise::states::resume_path(Library::key_of(*g));
        struct stat st;
        if (stat(resume.c_str(), &st) == 0 && st.st_size > 0)
        {
            resume_game_ = g;
            open_dialog(DialogKind::Resume, tr("Pick up where you left off?"),
                        relative_time(static_cast<long long>(st.st_mtime), static_cast<long long>(std::time(nullptr))),
                        tr("Resume"));
            dialog_.no = tr("Start Over");
            dialog_.choice = 1; /* Resume first */
            const std::string picture = resume.substr(0, resume.size() - 6) + ".png"; /* resume.state -> resume.png */
            if (access(picture.c_str(), R_OK) == 0)
                dialog_.picture = g_->texture_file(picture);
            return Action::None;
        }
    }
    /* Real Wii Remotes (a DolphinBar) need no DualSense set-up. */
    if (g && g->platform == "Wii" && eff.wii_setup_ask && eff.wii_source == 0)
    {
        open_wii_setup(g, true, state);
        return Action::None;
    }
    /* Start Wii Discs in the Wii Menu with the Wii Menu only as its WAD in
     * the games: the core installs it first. */
    if (g && g->platform == "Wii" && g->kind.empty() && eff.wii_menu_boot && !wii_menu_installed())
        if (const Game *w = wii_menu_wad())
            wad_installs_.push_back(w->path);
    launch_ = g;
    launch_state_ = state;
    return Action::Launch;
}

/* The settings the setup changes: the game's (in game), else the global ones. */
Settings &App::ws_settings()
{
    return ws_in_game_ && menu_play_ ? *menu_play_ : *settings_;
}

void App::wii_setup_apply_pad()
{
    if (ws_in_game_)
    {
        menu_change_ = "wii_setup"; /* main hands the pad and the core the new set-up */
        return;
    }
    Settings eff = *settings_;
    if (ws_game_)
        eff.load(game_settings_path(*ws_game_), true);
    WiiConfig c = eff.wii_config(false);
    c.controller = ws_controller_;
    c.pointer = PointerGyro;
    porpoise::pad::set_wii(c);
}

/* A Wii value for every game (the global settings), and for the game being
 * set up when it has its own value of that key, so the global one shows. */
void App::ws_store(const char *key, int value)
{
    settings_->set(key, std::to_string(value));
    settings_->save(settings_path_);
    if (ws_in_game_ && menu_play_)
        menu_play_->set(key, std::to_string(value));
    if (ws_game_)
    {
        const std::string path = game_settings_path(*ws_game_);
        std::vector<std::string> keys = Settings::keys_in(path);
        if (std::find(keys.begin(), keys.end(), key) != keys.end())
        {
            Settings eff = *settings_;
            eff.load(path, true);
            eff.set(key, std::to_string(value));
            eff.save_keys(path, keys);
        }
    }
}

void App::open_wii_setup(Game *g, bool launch, const std::string &state)
{
    ws_game_ = g;
    ws_launch_ = launch;
    ws_state_ = state;
    ws_in_game_ = false;
    ws_recal_only_ = false;
    ws_return_ = screen_ == Screen::States ? Screen::Details : screen_;
    Settings eff = *settings_;
    if (g)
        eff.load(game_settings_path(*g), true);
    ws_controller_ = std::clamp(eff.wii_controller, 0, int(WiiControllerCount) - 1);
    ws_real_ = eff.wii_source == 1;
    ws_ask_ = eff.wii_setup_ask;
    ws_step_ = kWsController;
    ws_row_ = 0;
    ws_hold_from_ = -1;
    ws_note_.clear();
    ws_size_mode_ = 0;
    wii_setup_apply_pad();
    sfx(Sound::DetailsFlip);
    open_screen(Screen::WiiSetup);
}

void App::open_wii_setup_in_game(bool recalibrate_only)
{
    if (!menu_play_)
        return;
    ws_game_ = menu_game_;
    ws_launch_ = false;
    ws_in_game_ = true;
    ws_recal_only_ = recalibrate_only;
    ws_controller_ = std::clamp(menu_play_->wii_controller, 0, int(WiiControllerCount) - 1);
    ws_real_ = menu_play_->wii_source == 1;
    ws_step_ = recalibrate_only ? (points(ws_controller_) ? kWsCentre : kWsHold) : kWsController;
    ws_row_ = 0;
    ws_hold_from_ = -1;
    ws_note_.clear();
    ws_size_mode_ = 0;
    sfx(Sound::DetailsFlip);
}

void App::close_wii_setup()
{
    ws_hold_from_ = -1;
    if (ws_in_game_)
    {
        ws_in_game_ = false;
        menu_change_ = "wii_setup";
        return;
    }
    porpoise::pad::set_wii(WiiConfig{});
    if (ws_return_ == Screen::GameSettings)
    {
        build_game_settings();
        open_screen(Screen::GameSettings);
    }
    else
        open_screen(ws_return_ == Screen::WiiSetup ? Screen::Main : ws_return_);
}

App::Action App::update_wii_setup(bool up, bool down, bool left, bool right)
{
    if (ws_real_)
    {
        /* Real Wii Remotes: a page to read; back with Circle or Cross, or
         * Triangle for the DualSense's own setup (for when it plays the Wii
         * Remote again). */
        if (pressed(BtnTriangle))
        {
            sfx(Sound::DetailsFlip);
            ws_real_ = false;
            wii_setup_apply_pad();
        }
        else if (pressed(BtnCircle | BtnCross))
        {
            sfx(Sound::DetailsFlip);
            close_wii_setup();
        }
        return Action::None;
    }
    const Motion m = porpoise::pad::snapshot(0).motion;
    const float turning = std::sqrt(m.gyro[0] * m.gyro[0] + m.gyro[1] * m.gyro[1] + m.gyro[2] * m.gyro[2]);
    const bool pointing = points(ws_controller_);
    const bool advanced = settings_->wii_setup_advanced;
    Settings &t = ws_settings();
    auto note = [&](const char *text) {
        ws_note_ = tr(text);
        ws_note_time_ = time_;
    };
    auto finish = [&]() -> Action {
        sfx(Sound::LaunchGame);
        const bool launch = ws_launch_ && ws_game_ && !ws_in_game_;
        close_wii_setup();
        if (launch)
        {
            porpoise::pad::set_wii(WiiConfig{});
            launch_ = ws_game_;
            launch_state_ = ws_state_;
            return Action::Launch;
        }
        return ws_in_game_ ? Action::None : Action::SettingsChanged;
    };
    /* After the corners (or the size and distance): try it. */
    auto after_measure = [&]() { ws_step_ = kWsTry; };

    if (pressed(BtnCircle))
    {
        sfx(Sound::DetailsFlip);
        ws_hold_from_ = -1;
        const bool first = ws_step_ == kWsController || (ws_recal_only_ && (ws_step_ == kWsCentre || ws_step_ == kWsHold));
        if (first)
            close_wii_setup();
        else if (ws_step_ == kWsTry || ws_step_ == kWsTopLeft || ws_step_ == kWsBottomRight)
            ws_step_ = kWsCentre;
        else if (ws_step_ == kWsCentre)
            ws_step_ = advanced && !ws_recal_only_ ? kWsScreen : kWsHold;
        else if (ws_step_ == kWsScreen)
            ws_step_ = kWsHold;
        else if (ws_step_ == kWsTune)
            ws_step_ = kWsTry;
        else
            ws_step_ = kWsController;
        ws_row_ = 0;
        return Action::None;
    }

    switch (ws_step_)
    {
    case kWsController:
    {
        /* Rows: the Wii controller; Simple or Advanced; for a game, whether
         * this page comes up before it starts. */
        const int rows = ws_game_ && !ws_in_game_ ? 3 : 2;
        if (up && ws_row_ > 0)
            --ws_row_, sfx(Sound::MenuScroll);
        if (down && ws_row_ + 1 < rows)
            ++ws_row_, sfx(Sound::MenuScroll);
        if ((left || right) && ws_row_ == 0)
        {
            ws_controller_ = (ws_controller_ + (left ? WiiControllerCount - 1 : 1)) % WiiControllerCount;
            if (!ws_in_game_)
                wii_setup_apply_pad();
            sfx(Sound::MovingTab);
        }
        if (((left || right) && ws_row_ == 1) || pressed(BtnSquare))
        {
            settings_->wii_setup_advanced = !settings_->wii_setup_advanced;
            settings_->save(settings_path_);
            sfx(Sound::MovingTab);
        }
        if ((left || right) && ws_row_ == 2 && ws_game_)
        {
            /* This game's own choice (its settings show it too). */
            const std::string path = game_settings_path(*ws_game_);
            Settings eff = *settings_;
            eff.load(path, true);
            eff.wii_setup_ask = !eff.wii_setup_ask;
            std::vector<std::string> keys = Settings::keys_in(path);
            if (std::find(keys.begin(), keys.end(), "wii_setup_ask") == keys.end())
                keys.push_back("wii_setup_ask");
            mkdir((data_dir_ + "/game-settings").c_str(), 0777);
            eff.save_keys(path, keys);
            ws_ask_ = eff.wii_setup_ask;
            sfx(Sound::MovingTab);
        }
        if (ws_launch_ && pressed(BtnTriangle))
            return finish(); /* play now, as set before */
        if (pressed(BtnCross))
        {
            /* Keep the choice: for this game, or for every game from Settings. */
            t.wii_controller = ws_controller_;
            if (ws_game_)
            {
                const std::string path = game_settings_path(*ws_game_);
                Settings eff = *settings_;
                eff.load(path, true);
                eff.wii_controller = ws_controller_;
                std::vector<std::string> keys = Settings::keys_in(path);
                if (std::find(keys.begin(), keys.end(), "wii_controller") == keys.end())
                    keys.push_back("wii_controller");
                mkdir((data_dir_ + "/game-settings").c_str(), 0777);
                eff.save_keys(path, keys);
            }
            else
            {
                settings_->wii_controller = ws_controller_;
                settings_->save(settings_path_);
            }
            wii_setup_apply_pad();
            if (ws_controller_ == WiiGameCube)
                return finish(); /* nothing to hold, point or measure */
            ws_step_ = kWsHold;
            ws_row_ = 0;
            sfx(Sound::MenuScroll);
            return ws_game_ || ws_in_game_ ? Action::None : Action::SettingsChanged;
        }
        break;
    }
    case kWsHold:
        if (pressed(BtnCross))
        {
            ws_step_ = advanced && pointing && !ws_recal_only_ ? kWsScreen : kWsCentre;
            ws_row_ = 0;
            ws_hold_from_ = -1;
            sfx(Sound::MenuScroll);
        }
        break;
    case kWsScreen:
    {
        /* Advanced: how to size the pointer - the corners, or the screen's size
         * and distance. */
        constexpr int kRows = 4;
        if (up && ws_row_ > 0)
            --ws_row_, sfx(Sound::MenuScroll);
        if (down && ws_row_ + 1 < kRows)
            ++ws_row_, sfx(Sound::MenuScroll);
        const int dir = left ? -1 : right ? 1 : 0;
        if (dir)
        {
            if (ws_row_ == 0)
                ws_size_mode_ = 1 - ws_size_mode_;
            else if (ws_row_ == 1)
                ws_store("wii_size", std::clamp(settings_->wii_size + dir, 10, 150));
            else if (ws_row_ == 2)
                ws_store("wii_distance", std::clamp(settings_->wii_distance + dir * 5, 5, 250));
            sfx(Sound::MovingTab);
        }
        if (pressed(BtnCross))
        {
            if (ws_size_mode_ == 1)
            {
                /* A 16:9 screen: width 0.872 of the diagonal, height 0.490. */
                const float d = float(settings_->wii_distance) * 1.2f; /* tenths of a foot -> inches */
                const float hx = std::atan(float(settings_->wii_size) * 0.8716f * 0.5f / d);
                const float hy = std::atan(float(settings_->wii_size) * 0.4903f * 0.5f / d);
                ws_store("wii_screen_x", int(std::lround(hx * kDeg * 10)));
                ws_store("wii_screen_y", int(std::lround(hy * kDeg * 10)));
                ws_store("wii_speed", 0);
                wii_setup_apply_pad();
            }
            ws_step_ = kWsCentre;
            ws_hold_from_ = -1;
            sfx(Sound::MenuScroll);
        }
        break;
    }
    case kWsCentre:
        /* Hold Cross a moment, still: that hold, pointing at the middle, is level. */
        if ((raw_held_ & BtnCross) && turning < 0.6f)
        {
            if (ws_hold_from_ < 0)
                ws_hold_from_ = time_;
            if (time_ - ws_hold_from_ >= 0.5)
            {
                porpoise::pad::centre_now(0);
                ws_hold_from_ = -1;
                sfx(Sound::LaunchGame);
                if (!pointing)
                    ws_step_ = kWsTry;
                else if (advanced && ws_size_mode_ == 1 && !ws_recal_only_)
                    after_measure();
                else
                    ws_step_ = kWsTopLeft;
            }
        }
        else
            ws_hold_from_ = -1;
        break;
    case kWsTopLeft:
        if (pressed(BtnCross))
        {
            if (!m.centred || turning > 1.0f)
                note("Hold it still a moment, then press Cross.");
            else
            {
                ws_tl_[0] = m.rel_yaw;
                ws_tl_[1] = m.rel_pitch;
                ws_step_ = kWsBottomRight;
                sfx(Sound::MenuScroll);
            }
        }
        break;
    case kWsBottomRight:
        if (pressed(BtnCross))
        {
            if (!m.centred || turning > 1.0f)
            {
                note("Hold it still a moment, then press Cross.");
                break;
            }
            const float span_x = m.rel_yaw - ws_tl_[0], span_y = ws_tl_[1] - m.rel_pitch;
            const float half_x = span_x / (2 * kCorner), half_y = span_y / (2 * kCorner);
            const bool sane = half_x * kDeg > 3 && half_x * kDeg < 75 && half_y * kDeg > 2 && half_y * kDeg < 60 &&
                              half_y / half_x > 0.25f && half_y / half_x < 1.3f;
            if (!sane)
            {
                note("That didn't look like a screen. Let's try again: center first.");
                ws_step_ = kWsCentre;
                break;
            }
            /* The middle is half way between the corners. */
            porpoise::pad::shift_centre(0, (m.rel_yaw + ws_tl_[0]) * 0.5f, (m.rel_pitch + ws_tl_[1]) * 0.5f);
            ws_store("wii_screen_x", int(std::lround(half_x * kDeg * 10)));
            ws_store("wii_screen_y", int(std::lround(half_y * kDeg * 10)));
            ws_store("wii_speed", 0);
            wii_setup_apply_pad();
            after_measure();
            sfx(Sound::LaunchGame);
            return ws_in_game_ ? Action::None : Action::SettingsChanged;
        }
        break;
    case kWsTry:
        if (pressed(BtnSquare) && pointing)
        {
            ws_step_ = kWsCentre;
            sfx(Sound::MenuScroll);
        }
        else if (pressed(BtnTriangle) && advanced)
        {
            ws_step_ = kWsTune;
            ws_row_ = 0;
            ws_preset_slot_ = std::max(0, settings_->wii_preset - 1);
            sfx(Sound::MenuScroll);
        }
        else if (pressed(BtnCross))
            return finish();
        break;
    default: /* kWsTune */
    {
        /* Advanced: smoothing, reach, grip; keep it all as a named preset. */
        constexpr int kRows = 7;
        if (up && ws_row_ > 0)
            --ws_row_, sfx(Sound::MenuScroll);
        if (down && ws_row_ + 1 < kRows)
            ++ws_row_, sfx(Sound::MenuScroll);
        const int dir = left ? -1 : right ? 1 : 0;
        const int names = int(Settings::wii_preset_names().size());
        if (dir)
        {
            switch (ws_row_)
            {
            case 0: ws_store("wii_smooth", std::clamp(t.wii_smooth + dir, 0, 3)); break;
            case 1: ws_store("wii_reach", std::clamp(t.wii_reach + dir * 5, 70, 150)); break;
            case 2: ws_store("wii_grip", (t.wii_grip + dir + GripCount) % GripCount); break;
            case 3: ws_preset_slot_ = (ws_preset_slot_ + dir + Settings::kWiiPresets) % Settings::kWiiPresets; break;
            case 4: ws_preset_name_ = (ws_preset_name_ + dir + names) % names; break;
            default: break;
            }
            if (ws_row_ <= 2)
                wii_setup_apply_pad();
            sfx(Sound::MovingTab);
        }
        if (pressed(BtnCross))
        {
            if (ws_row_ == 5)
            {
                /* Everything as set now, under the name. */
                Settings snapshot = t;
                settings_->wii_presets[ws_preset_slot_] = {};
                snapshot.save_wii_preset(ws_preset_slot_, ws_preset_name_);
                settings_->wii_presets[ws_preset_slot_] = snapshot.wii_presets[ws_preset_slot_];
                settings_->wii_preset = ws_preset_slot_ + 1;
                settings_->save(settings_path_);
                note("Saved.");
                sfx(Sound::LaunchGame);
            }
            else if (ws_row_ == 6)
                return finish();
        }
        break;
    }
    }
    return Action::None;
}

void App::draw_wii_setup(double time)
{
    if (ws_real_)
    {
        draw_real_remote_setup(time);
        return;
    }
    Gfx &g = *g_;
    g.panel(0, 0, 1920, 1080, rgba(0x02040C, 0.93f), 1, 0);
    const bool pointing = points(ws_controller_);
    const bool advanced = settings_->wii_setup_advanced;
    const Settings &t = ws_in_game_ && menu_play_ ? *menu_play_ : *settings_;
    const Motion m = porpoise::pad::snapshot(0).motion;
    const float pulse = settings_->reduced_motion ? 1.0f : 0.8f + 0.2f * std::sin(float(time) * 4.0f);

    /* The title, Beta / Alpha, the game, and where we are. */
    const bool corners = ws_step_ == kWsTopLeft || ws_step_ == kWsBottomRight;
    if (!corners)
    {
        const std::string title = ws_recal_only_ ? tr("Recalibrate the Pointer") : tr("Wii Remote Setup");
        g.text_mid(Font::Bold, ts(40), 90, 70, kWhite, Align::Left, title);
        const bool alpha = ws_controller_ == WiiTwoControllers;
        const std::string tag = alpha ? tr("Alpha") : tr("Beta");
        const float bw = g.measure(Font::Bold, ts(19), tag) + 26;
        const float bx = 90 + g.measure(Font::Bold, ts(40), title) + 18;
        g.panel(bx, 70 - 16, bw, 32, alpha ? rgba(0xFF8A5C, 0.92f) : rgba(0xFFC85C, 0.9f), 0.8f, 16, rgba(0xFFE7B0), 1.4f);
        g.text_mid(Font::Bold, ts(19), bx + bw * 0.5f, 70, rgba(0x2A1A00), Align::Center, tag);
        std::string sub = ws_game_ ? ws_game_->title : std::string();
        if (!ws_recal_only_)
            sub += (sub.empty() ? "" : "   \xE2\x80\xA2   ") + (advanced ? tr("Advanced") : tr("Simple"));
        g.text_mid(Font::Regular, ts(24), 90, 112, kLavender, Align::Left, fit(g, Font::Regular, ts(24), sub, 1100));
        /* The steps. */
        std::vector<int> steps;
        if (!ws_recal_only_)
            steps = {kWsController, kWsHold};
        if (advanced && pointing && !ws_recal_only_)
            steps.push_back(kWsScreen);
        steps.push_back(kWsCentre);
        if (pointing && !(advanced && ws_size_mode_ == 1 && !ws_recal_only_))
            steps.push_back(kWsTopLeft);
        steps.push_back(kWsTry);
        if (advanced && !ws_recal_only_)
            steps.push_back(kWsTune);
        const int now = ws_step_ == kWsBottomRight ? kWsTopLeft : ws_step_;
        int at = 0;
        for (std::size_t i = 0; i < steps.size(); ++i)
            if (steps[i] == now)
                at = int(i);
        for (std::size_t i = 0; i < steps.size(); ++i)
        {
            const float cx = 1830 - float(steps.size() - 1 - i) * 44, cy = 74;
            const bool done = int(i) < at, cur = int(i) == at;
            g.panel(cx - 14, cy - 14, 28, 28, cur ? kCyan : done ? rgba(0x6BE3A8) : rgba(0x3D4F9E, 0.8f), 0.9f, 14,
                    cur ? kWhite : rgba(0x8BD9FF, 0.6f), 1.4f);
            g.text_mid(Font::Bold, ts(16), cx, cy, rgba(0x0A1236), Align::Center, std::to_string(i + 1));
        }
    }

    auto big_line = [&](const std::string &text, float y, Color c) {
        const auto lines = wrap(g, Font::SemiBold, ts(34), text, 1300, 3);
        for (const std::string &l : lines)
        {
            g.text_mid(Font::SemiBold, ts(34), 960, y, c, Align::Center, l);
            y += 48;
        }
    };
    auto target = [&](float x, float y, float fill) {
        g.blob(x, y, 220, 220, rgba(0x5CD3FF, 0.35f * pulse));
        g.panel(x - 54, y - 54, 108, 108, rgba(0xFFFFFF, 0.0f), 0, 54, rgba(0x8BD9FF, 0.95f), 3.0f);
        g.panel(x - 30, y - 30, 60, 60, rgba(0xFFFFFF, 0.0f), 0, 30, rgba(0xFFFFFF, 0.8f), 2.0f);
        g.panel(x - 9, y - 9, 18, 18, rgba(0xFFFFFF), 1, 9);
        if (fill > 0)
        {
            const float r = 54 * std::min(1.0f, fill);
            g.panel(x - r, y - r, 2 * r, 2 * r, rgba(0x6BE3A8, 0.55f), 0.9f, r);
        }
    };
    /* A left-hand panel with a heading and paragraphs. */
    const float lx = 90, ly = 160, lw = 750, lh = 780;
    float ty = 0;
    auto left_panel = [&](const std::string &heading) {
        g.panel(lx, ly, lw, lh, rgba(0x0F1F63, 0.62f), 0.75f, kR, rgba(0x4C6FD8, 0.9f), 1.8f, 0, 0.12f);
        g.text_mid(Font::Bold, ts(34), lx + 44, ly + 56, kWhite, Align::Left, heading);
        ty = ly + 120;
    };
    auto para = [&](const std::string &text, Color c) {
        for (const std::string &l : wrap(g, Font::Regular, ts(26), text, lw - 90, 10))
        {
            g.text_mid(Font::Regular, ts(26), lx + 44, ty, c, Align::Left, l);
            ty += 38;
        }
        ty += 14;
    };
    /* A list of rows (advanced pages): label left, value right, focus glow. */
    auto row = [&](int i, const std::string &label, const std::string &value, float x, float y, float w) {
        const bool on = i == ws_row_;
        if (on)
            g.panel(x, y - 28, w, 56, rgba(0x1D45B8, 0.88f), 0.7f, kR, kIcy, 2.2f, 8, 0.18f);
        g.text_mid(Font::SemiBold, ts(27), x + 28, y, on ? kWhite : kSoft, Align::Left, label);
        if (!value.empty())
        {
            g.text_mid(Font::Bold, ts(27), x + w - 28, y, on ? kWhite : kLavender, Align::Right, value);
            if (on && value[0] != '[')
            {
                const float vw = g.measure(Font::Bold, ts(27), value);
                g.glyph(Glyph::Arrow, x + w - 28 - vw - 22, y, 16, kCyan, -kPi * 0.5f);
            }
        }
    };

    WiiConfig cfg = t.wii_config(false);
    cfg.controller = ws_controller_;
    switch (ws_step_)
    {
    case kWsController:
    {
        left_panel(tr("Which Wii controller?"));
        const float rx = lx + 30, rw = lw - 60;
        float ry = ly + 128;
        row(0, tr("Wii Controller"), tr(wii_controller_name(ws_controller_)), rx, ry, rw);
        ry += 62;
        row(1, tr("Setup"), advanced ? tr("Advanced") : tr("Simple"), rx, ry, rw);
        ry += 62;
        if (ws_game_ && !ws_in_game_)
        {
            row(2, tr("Before This Game"), ws_ask_ ? tr("Show This Setup") : tr("Don't Show"), rx, ry, rw);
            ry += 62;
        }
        ty = ry + 10;
        if (const Advice *a = advice_for(ws_game_))
        {
            const bool match = a->controller == ws_controller_;
            para((match ? tr("Recommended. ") : tr("Recommended: ") + tr(wii_controller_name(a->controller)) + ". ") +
                     tr(a->note),
                 match ? rgba(0x6BE3A8) : rgba(0xFFC85C));
        }
        if (ws_controller_ == WiiTwoControllers)
            para(tr("Alpha: still rough. Two DualSenses, one for each hand."), rgba(0xFF8A5C));
        if (ws_row_ == 1)
            para(advanced ? tr("Advanced adds your screen's size and distance, smoothing, reach and presets.")
                          : tr("Simple: center, two corners, and play."),
                 kLavender);
        else if (ws_row_ == 2)
            para(ws_ask_ ? tr("This page comes up each time this game starts.")
                         : tr("This game starts straight away. Its settings (Square on it, then Settings) can bring "
                              "this page back."),
                 kLavender);
        para(tr(wii_howto(ws_controller_)), kSoft);
        draw_wii_controls(870, 160, 960, 780, cfg, 1.0f);
        std::vector<std::pair<Glyph, std::string>> prompts = {{Glyph::DPad, "Change"}, {Glyph::Cross, "Next"}};
        if (ws_launch_)
            prompts.push_back({Glyph::Triangle, "Play Now"});
        prompts.push_back({Glyph::Circle, "Back"});
        draw_prompts(prompts, {}, "");
        break;
    }
    case kWsHold:
    {
        left_panel(tr("Pick it up like this"));
        para(tr(pose_words(expected_pose(cfg, false))) + ".", kSoft);
        if (m.valid && pointing)
            para(tr("Porpoise sees: ") + tr(pose_words(m.pose)) + ".", rgba(0x6BE3A8));
        if (ws_controller_ == WiiTwoControllers)
        {
            const bool second = porpoise::pad::connected(1);
            para(second ? tr("Second controller: on.")
                        : tr("Second controller: not found. Sign in a second user and turn their controller on."),
                 second ? rgba(0x6BE3A8) : rgba(0xFFC85C));
        }
        para(tr("Porpoise reads how you hold it, so tilts, swings and the pointer come out the right way."),
             with_alpha(kLavender, 0.85f));
        para(tr("Then press Cross."), kLavender);
        const porpoise::pad::Motion m2 = porpoise::pad::snapshot(1).motion;
        draw_wii_controls(870, 160, 960, 780, cfg, 1.0f, m.valid ? m.pose : -1, m2.valid ? m2.pose : -1);
        draw_prompts({{Glyph::Cross, "Next"}, {Glyph::Circle, "Back"}}, {}, "");
        break;
    }
    case kWsScreen:
    {
        left_panel(tr("Your screen"));
        para(tr("The pointer is sized to your screen and how far you sit. Pointing at two corners measures both "
                "at once; or give the size and the distance."),
             kSoft);
        float ry = ty + 30;
        const float rx = lx + 30, rw = lw - 60;
        row(0, tr("Size the pointer by"), ws_size_mode_ ? tr("Size and Distance") : tr("Pointing at Corners"), rx, ry,
            rw);
        ry += 64;
        if (ws_size_mode_)
        {
            row(1, tr("Screen Size"), std::to_string(settings_->wii_size) + "\"", rx, ry, rw);
            ry += 64;
            char d[48];
            std::snprintf(d, sizeof d, "%.1f ft (%.1f m)", settings_->wii_distance / 10.0,
                          settings_->wii_distance * 0.03048);
            row(2, tr("Distance to it"), d, rx, ry, rw);
            ry += 64;
        }
        row(3, ws_size_mode_ ? tr("Next: center") : tr("Next: center, then the corners"), "", rx, ry, rw);
        draw_wii_controls(870, 160, 960, 780, cfg, 1.0f, m.valid ? m.pose : -1);
        draw_prompts({{Glyph::DPad, "Choose"}, {Glyph::Cross, "Next"}, {Glyph::Circle, "Back"}}, {}, "");
        break;
    }
    case kWsCentre:
    {
        const float fill = ws_hold_from_ >= 0 ? float((time_ - ws_hold_from_) / 0.5) : 0.0f;
        target(960, 540, fill);
        big_line(tr("Hold it the way you'll play and point at the dot."), 760, kWhite);
        big_line(tr("Then hold Cross, keeping still."), 830, kLavender);
        if (ws_controller_ == WiiTwoControllers)
            big_line(tr("Hold the second controller as you'll play, too."), 900, kLavender);
        draw_prompts({{Glyph::Cross, "Hold to Center"}, {Glyph::Circle, ws_recal_only_ ? "Close" : "Back"}}, {}, "");
        break;
    }
    case kWsTopLeft:
    case kWsBottomRight:
    {
        const bool tl = ws_step_ == kWsTopLeft;
        const float tx = 960 + (tl ? -kCorner : kCorner) * 960, ty2 = 540 + (tl ? -kCorner : kCorner) * 540;
        target(tx, ty2, 0);
        big_line(tl ? tr("Now point at the dot in the top-left corner") : tr("And the dot in the bottom-right corner"),
                 470, kWhite);
        big_line(tr("and press Cross."), 540, kLavender);
        big_line(tr("This measures your screen from where you sit, so the pointer is where you point."), 640,
                 with_alpha(kLavender, 0.8f));
        draw_prompts({{Glyph::Cross, "This is the corner"}, {Glyph::Circle, "Back"}}, {}, "");
        break;
    }
    case kWsTry:
    {
        if (pointing)
        {
            /* The screen's edge, and the pointer, live. */
            g.panel(24, 24, 1872, 1032, rgba(0xFFFFFF, 0.0f), 0, kR, rgba(0x3D4F9E, 0.9f), 2.0f);
            big_line(tr("Try it: the pointer should be right where you point."), 470, kWhite);
            if (t.wii_screen_x > 0 && t.wii_speed == 0)
                big_line(trf("Your screen: {x}\xC2\xB0 to the sides, {y}\xC2\xB0 up and down.",
                             {{"x", std::to_string(t.wii_screen_x / 10)}, {"y", std::to_string(t.wii_screen_y / 10)}}),
                         540, kLavender);
            big_line(tr("If it wanders while you play, hold R1 a moment to center it."), 610,
                     with_alpha(kLavender, 0.8f));
            if (m.centred)
            {
                const float px = 960 + std::clamp(m.aim_x, -1.05f, 1.05f) * 960;
                const float py = 540 + std::clamp(m.aim_y, -1.05f, 1.05f) * 540;
                g.blob(px, py, 90, 90, rgba(0xFFC85C, 0.45f));
                g.panel(px - 20, py - 20, 40, 40, rgba(0xFFFFFF, 0.0f), 0, 20, rgba(0xFFC85C), 3.5f);
                g.panel(px - 4, py - 4, 8, 8, rgba(0xFFC85C), 1, 4);
            }
        }
        else
        {
            big_line(tr("All set."), 500, kWhite);
            big_line(tr("Tilt and flick the controller: the game feels it."), 570, kLavender);
        }
        std::vector<std::pair<Glyph, std::string>> prompts = {
            {Glyph::Cross, ws_launch_ ? "Play" : ws_in_game_ ? "Back to the Game" : "Done"}};
        if (pointing)
            prompts.push_back({Glyph::Square, "Measure Again"});
        if (advanced && !ws_recal_only_)
            prompts.push_back({Glyph::Triangle, "Fine-Tune"});
        prompts.push_back({Glyph::Circle, "Back"});
        draw_prompts(prompts, {}, "");
        break;
    }
    default: /* kWsTune */
    {
        left_panel(tr("Fine-Tune"));
        para(tr("Changes show on the pointer at once. Keep the whole set-up as a preset to switch to it later "
                "(Settings > Wii Remote, or the pause menu)."),
             kSoft);
        static const char *const kSmooth[] = {"Off", "Light", "Medium", "Strong"};
        static const char *const kGrips[] = {"Auto", "Both Hands", "Upright, trigger to the TV", "Upright, facing you"};
        const auto &names = Settings::wii_preset_names();
        float ry = ty + 20;
        const float rx = lx + 30, rw = lw - 60;
        row(0, tr("Smoothing"), tr(kSmooth[std::clamp(t.wii_smooth, 0, 3)]), rx, ry, rw);
        ry += 62;
        row(1, tr("Reach"), std::to_string(t.wii_reach) + "%", rx, ry, rw);
        ry += 62;
        row(2, tr("Grip"), tr(kGrips[std::clamp(t.wii_grip, 0, 3)]), rx, ry, rw);
        ry += 62;
        const Settings::WiiPreset &slot = settings_->wii_presets[ws_preset_slot_];
        row(3, tr("Preset"),
            trf("{n}: {name}", {{"n", std::to_string(ws_preset_slot_ + 1)},
                                {"name", slot.used ? tr(names[std::size_t(slot.name)]) : tr("empty")}}),
            rx, ry, rw);
        ry += 62;
        row(4, tr("Name"), tr(names[std::size_t(std::clamp(ws_preset_name_, 0, int(names.size()) - 1))]), rx, ry, rw);
        ry += 62;
        row(5, tr("Save as This Preset"), "", rx, ry, rw);
        ry += 62;
        row(6, ws_launch_ ? tr("Play") : tr("Done"), "", rx, ry, rw);
        /* The pointer, live, so the changes show. */
        if (pointing)
        {
            g.text_mid(Font::SemiBold, ts(30), 1350, 520, with_alpha(kLavender, 0.85f), Align::Center,
                       tr("Move the controller: the pointer shows your changes."));
        }
        if (m.centred && pointing)
        {
            const float px = 960 + std::clamp(m.aim_x, -1.05f, 1.05f) * 960;
            const float py = 540 + std::clamp(m.aim_y, -1.05f, 1.05f) * 540;
            g.blob(px, py, 90, 90, rgba(0xFFC85C, 0.45f));
            g.panel(px - 20, py - 20, 40, 40, rgba(0xFFFFFF, 0.0f), 0, 20, rgba(0xFFC85C), 3.5f);
        }
        draw_prompts({{Glyph::DPad, "Change"}, {Glyph::Cross, "Choose"}, {Glyph::Circle, "Back"}}, {}, "");
        break;
    }
    }
    /* A short note (a corner that didn't take, a preset saved). */
    if (!ws_note_.empty() && time_ - ws_note_time_ < 3.5)
        big_line(ws_note_, 960, rgba(0xFFC85C));
}

} // namespace porpoise::ui

/* ---- Customize buttons: a Wii controller's ------------------------------------------------ */

namespace porpoise::ui
{
using namespace look;
using namespace porpoise::pad;

namespace
{
/* The four Wii controllers the player can set buttons for, in tab order after
 * GameCube (Settings::wii_buttons, porpoise::pad::wii_button_set). */
constexpr int kWiiSetController[4] = {WiiRemoteNunchuk, WiiRemote, WiiSideways, WiiClassic};
constexpr const char *kMapKindNames[5] = {"GameCube", "Remote + Nunchuk", "Remote", "Remote Sideways",
                                          "Classic Controller"};
/* The tabs' short names, across the top of the list. */
constexpr const char *kMapKindTabs[5] = {"GameCube", "Remote + Nunchuk", "Remote", "Sideways", "Classic"};

/* Porpoise's own layout for a set, held face up: the rows of the list. */
WiiLayout base_layout(int set)
{
    WiiConfig config;
    config.active = true;
    config.controller = kWiiSetController[set];
    config.menu = true; /* Porpoise's buttons, not the player's */
    return wii_layout(config, false, PoseFlat);
}
} // namespace

bool App::switch_map_kind()
{
    if (!pressed(BtnL1 | BtnR1))
        return false;
    map_kind_ = (map_kind_ + (pressed(BtnL1) ? 4 : 1)) % 5;
    map_row_ = map_kind_ > 0 ? 0 : kSelectRow;
    map_capture_ = false;
    map_note_.clear();
    sfx(Sound::MovingTab);
    return true;
}

/* The controllers, as tabs across the top of the list panel (L1 / R1). */
void App::draw_map_tabs()
{
    Gfx &g = *g_;
    const float px = 950, pw = 880, cy = 136 + 34;
    float size = ts(22), total = 0;
    float widths[5];
    for (int pass = 0; pass < 2; ++pass)
    {
        total = 0;
        for (int i = 0; i < 5; ++i)
        {
            widths[i] = g.measure(Font::SemiBold, size, tr(kMapKindTabs[i])) + 30;
            total += widths[i];
        }
        if (total <= pw - 40)
            break;
        size *= (pw - 40) / total; /* a long language: smaller */
    }
    float x = px + (pw - total) * 0.5f;
    g.panel(x - 4, cy - 22, total + 8, 44, rgba(0x07102E, 0.55f), 0.8f, 22, rgba(0x3D4F9E, 0.8f), 1.2f);
    for (int i = 0; i < 5; ++i)
    {
        const bool on = i == map_kind_;
        if (on)
            g.panel(x, cy - 18, widths[i], 36, rgba(0x1D45B8, 0.95f), 0.8f, 18, kIcy, 1.8f, 6, 0.15f);
        g.text_mid(Font::SemiBold, size, x + widths[i] * 0.5f, cy, on ? kWhite : kSoft, Align::Center,
                   tr(kMapKindTabs[i]));
        x += widths[i];
    }
}

App::Action App::update_wii_mapping(bool up, bool down, bool left, bool right)
{
    /* Row 0 picks the buttons (Porpoise's, Mine 1, Mine 2); then each
     * binding; then Start Over. Porpoise's own are shown but never change. */
    const int set = map_kind_ - 1;
    const WiiLayout base = base_layout(set);
    const int rows = base.count + 2;
    const bool global = map_target_ == settings_;
    const Action changed = global ? Action::SettingsChanged : Action::None;
    auto (*tables)[CtlCount] = settings_->wii_slot(map_wii_slot_);
    int *perm = tables ? tables[set] : nullptr;
    int bind = map_row_ - 1; /* the binding on this row, if it is one */
    if (map_capture_)
    {
        const std::uint32_t mask = 0xFFFFu;
        if (time_ - map_capture_start_ > kCaptureSeconds)
        {
            map_capture_ = false;
            map_note_ = tr("Nothing pressed; the button is unchanged.");
            map_note_time_ = time_;
            return Action::None;
        }
        if (!map_armed_)
        {
            map_armed_ = (raw_held_ & mask) == 0;
            if (map_armed_)
                map_capture_start_ = time_;
            return Action::None;
        }
        const std::uint32_t fresh = (raw_held_ & ~raw_prev_) & mask;
        if (fresh && perm && bind >= 0 && bind < base.count)
            for (int c = 0; c < CtlCount; ++c)
                if (fresh & control_bit(c))
                {
                    /* The binding moves to the pressed control; whatever was
                     * there takes its old place. */
                    const int from = base.binds[bind].control;
                    int other = 0;
                    for (int e = 0; e < CtlCount; ++e)
                        if (perm[e] == c)
                            other = e;
                    std::swap(perm[from], perm[other]);
                    use_wii_slot(map_wii_slot_);
                    map_note_ = trf("{button} is now on {control}.",
                                    {{"button", tr(wii_input_name(base.binds[bind].input))},
                                     {"control", control_name(c)}});
                    map_note_time_ = time_;
                    map_capture_ = false;
                    sfx(Sound::LaunchGame);
                    return changed;
                }
        return Action::None;
    }
    if (switch_map_kind())
        return Action::None;
    if (up && map_row_ > 0)
    {
        --map_row_;
        sfx(Sound::MenuScroll);
    }
    if (down && map_row_ + 1 < rows)
    {
        ++map_row_;
        sfx(Sound::MenuScroll);
    }
    if (map_row_ == 0 && (left || right))
    {
        map_wii_slot_ = (map_wii_slot_ + (left ? Settings::kWiiButtonSlots - 1 : 1)) % Settings::kWiiButtonSlots;
        sfx(Sound::MovingTab);
        tables = settings_->wii_slot(map_wii_slot_);
        perm = tables ? tables[set] : nullptr;
    }
    bind = map_row_ - 1;
    if (pressed(BtnCircle))
    {
        map_kind_ = 0;
        close_mapping();
        return Action::None;
    }
    if (pressed(BtnCross))
    {
        if (map_row_ == 0)
        {
            use_wii_slot(map_wii_slot_);
            sfx(Sound::LaunchGame);
            return changed;
        }
        if (!perm)
        {
            map_note_ = tr("Porpoise's buttons stay as they are: pick Mine 1 or Mine 2 to make your own.");
            map_note_time_ = time_;
            sfx(Sound::DetailsFlip);
            return Action::None;
        }
        if (bind < base.count)
        {
            map_capture_ = true;
            map_armed_ = false;
            map_capture_start_ = time_;
            sfx(Sound::DetailsFlip);
            return Action::None;
        }
        for (int c = 0; c < CtlCount; ++c)
            perm[c] = c;
        use_wii_slot(map_wii_slot_);
        map_note_ = trf("{controller} is back to Porpoise's layout.", {{"controller", tr(kMapKindNames[map_kind_])}});
        map_note_time_ = time_;
        sfx(Sound::LaunchGame);
        return changed;
    }
    return Action::None;
}

/* The Wii buttons in a slot go to whatever opened the screen (every game, or
 * one), and are saved. */
void App::use_wii_slot(int slot)
{
    /* In a game played with GameCube buttons, the Wii buttons are for every
     * Wii game: this game would never use them. */
    Settings *const target = map_target_;
    const bool in_game = map_in_game_;
    const bool for_all = in_game && !porpoise::pad::wii().active;
    if (for_all)
    {
        map_target_ = settings_;
        map_in_game_ = false;
    }
    const bool changed = map_target_->wii_buttons_use != slot;
    map_target_->wii_buttons_use = slot;
    save_mapping(changed, "wii_buttons_use");
    const std::string name = tr(slot == 0 ? "Porpoise's Set" : slot == 1 ? "Mine 1" : "Mine 2");
    map_note_ = map_game_ && map_target_ != settings_ ? trf("Wii buttons: {name}, for this game.", {{"name", name}})
                                                      : trf("Wii buttons: {name}.", {{"name", name}});
    map_note_time_ = time_;
    map_target_ = target;
    map_in_game_ = in_game;
    if (for_all && target)
    {
        /* The game's copies show it too (In Use), with nothing waiting. */
        target->wii_buttons_use = slot;
        menu_draft_.wii_buttons_use = menu_base_.wii_buttons_use = slot;
    }
}

void App::draw_wii_mapping(double time)
{
    Gfx &g = *g_;
    const int set = map_kind_ - 1;
    const WiiLayout base = base_layout(set);
    static const int kSame[CtlCount] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    auto (*tables)[CtlCount] = settings_->wii_slot(map_wii_slot_);
    const int *perm = tables ? tables[set] : kSame;
    WiiLayout mine = base;
    for (int i = 0; i < mine.count; ++i)
        mine.binds[i].control = perm[mine.binds[i].control];
    if (map_in_game_)
        g.panel(0, 0, 1920, 1080, rgba(0x02040C, 0.78f), 1, 0);

    /* Left: the DualSense as this Wii controller, with the player's buttons. */
    const float lx = 90, ly = 136, lw = 830, lh = 800;
    g.panel(lx, ly, lw, lh, rgba(0x0A1236, 0.55f), 0.85f, kR, rgba(0x3D4F9E, 0.9f), 1.6f, 0, 0.10f);
    g.text_mid(Font::Bold, ts(46), lx + 46, ly + 64, kWhite, Align::Left, tr(kMapKindNames[map_kind_]));
    g.text_mid(Font::Regular, ts(26), lx + 46, ly + 112, kLavender, Align::Left,
               fit(g, Font::Regular, ts(26), tr("Every Wii game played with this controller"), lw - 92));
    draw_wii_pad(mine, PoseFlat, lx + 35, ly + 170, 760, 1.0f);
    (void)time;

    /* Right: each Wii input and the DualSense control it's on. */
    const float px = 950, py = 136, pw = 880, ph = 800;
    g.panel(px, py, pw, ph, rgba(0x0F1F63, 0.62f), 0.75f, kR, rgba(0x4C6FD8, 0.9f), 1.8f, 0, 0.12f);
    const int rows = base.count + 1;
    const float row_x = px + 22, row_w = pw - 44;
    {
        /* Which buttons: Porpoise's, Mine 1, Mine 2 (and which are in use). */
        const bool on = map_row_ == 0;
        const float sy = py + 66, sh = 58;
        if (on)
            g.panel(row_x, sy, row_w, sh, rgba(0x1D45B8, 0.88f), 0.7f, kR, kIcy, 2.2f, 8, 0.18f);
        else
            g.panel(row_x, sy, row_w, sh, rgba(0x07102E, 0.45f), 0.8f, kR, rgba(0x3D4F9E, 0.8f), 1.2f);
        const int use = map_target_ ? map_target_->wii_buttons_use : 1;
        const std::string name = tr(map_wii_slot_ == 0 ? "Porpoise's Set" : map_wii_slot_ == 1 ? "Mine 1" : "Mine 2");
        g.text_mid(Font::SemiBold, ts(26), row_x + 30, sy + sh * 0.5f, on ? kWhite : kSoft, Align::Left,
                   fit(g, Font::SemiBold, ts(26), tr("Buttons"), row_w * 0.3f));
        const float vx = row_x + row_w * 0.62f;
        g.glyph(Glyph::Arrow, vx - 150, sy + sh * 0.5f, 14, on ? kWhite : kSoft, -kPi * 0.5f);
        g.glyph(Glyph::Arrow, vx + 150, sy + sh * 0.5f, 14, on ? kWhite : kSoft, kPi * 0.5f);
        g.text_mid(Font::Bold, ts(28), vx, sy + sh * 0.5f, kWhite, Align::Center,
                   fit(g, Font::Bold, ts(28), name, 270));
        const bool in_use = use == map_wii_slot_;
        std::string tag = map_wii_slot_ == 0 ? tr("Locked") : "";
        if (in_use)
            tag = tag.empty() ? tr("In Use") : tr("In Use") + " \xE2\x80\xA2 " + tag;
        g.text_mid(Font::SemiBold, ts(21), row_x + row_w - 26, sy + sh * 0.5f, in_use ? rgba(0x7CF0A6) : kLavender,
                   Align::Right, fit(g, Font::SemiBold, ts(21), tag, row_w * 0.22f));
    }
    const float row_h = std::min(44.0f, (ph - 200) / float(rows));
    float y = py + 136;
    for (int i = 0; i < rows; ++i)
    {
        const bool on = i + 1 == map_row_;
        const float cy = y + row_h * 0.5f;
        if (i == base.count)
        {
            g.panel(row_x + 20, y + 4, row_w - 40, 1.5f, rgba(0x3D4F9E, 0.8f), 1, 0);
            y += 12;
        }
        const float ry = i == base.count ? y : y;
        const float rcy = i == base.count ? ry + row_h * 0.5f : cy;
        if (on)
            g.panel(row_x, ry + 2, row_w, row_h - 4, rgba(0x1D45B8, 0.88f), 0.7f, kR, kIcy, 2.2f, 8, 0.18f);
        const float right = row_x + row_w - 22;
        if (i < base.count)
        {
            const int input = base.binds[i].input;
            const float h = 28, chx = row_x + 44;
            const std::string chip = wii_input_chip(input);
            const float w = chip.empty() ? h : std::max(h, g.measure(Font::Bold, h * 0.5f, chip) + h * 0.7f);
            g.panel(chx - w * 0.5f, rcy - h * 0.5f, w, h, rgba(wii_input_colour(input)), 0.8f, h * 0.5f,
                    rgba(0xFFFFFF, 0.6f), 1.2f);
            if (chip.empty())
                g.glyph(Glyph::Arrow, chx, rcy, h * 0.5f, rgba(0x0A1236), wii_input_arrow(input));
            else
                g.text_mid(Font::Bold, h * 0.5f, chx, rcy, rgba(0x0A1236), Align::Center, chip);
            g.text_mid(Font::SemiBold, ts(25), row_x + 96, rcy, on ? kWhite : kSoft, Align::Left,
                       tr(wii_input_name(input)));
            if (on && map_capture_)
                g.text_mid(Font::Bold, ts(24), right, rcy, kCyan, Align::Right, tr("Press a button\xE2\x80\xA6"));
            else
                draw_control(perm[base.binds[i].control], right, rcy, on, std::min(50.0f, row_h + 6));
        }
        else
            g.text_mid(Font::SemiBold, ts(26), row_x + 34, rcy, on ? kWhite : kSoft, Align::Left,
                       tr("Start Over from Porpoise's Layout"));
        y += row_h;
    }
    const double since = time_ - map_note_time_;
    if (!map_note_.empty() && since < 4.0)
    {
        const float a = since < 3.0 ? 1.0f : float(4.0 - since);
        g.text_mid(Font::SemiBold, ts(24), px + pw * 0.5f, py + ph - 32, with_alpha(kCyan, a), Align::Center,
                   fit(g, Font::SemiBold, ts(24), map_note_, pw - 60));
    }
    draw_map_tabs();
    const int bind = map_row_ - 1;
    if (map_capture_ && bind >= 0 && bind < base.count)
    {
        const int left = int(std::ceil(kCaptureSeconds - (time_ - map_capture_start_)));
        draw_prompts({}, {}, trf("Press the DualSense button for {button} ({n})",
                                 {{"button", tr(wii_input_name(base.binds[bind].input))},
                                  {"n", std::to_string(std::max(1, left))}}));
    }
    else if (map_row_ > 0 && map_wii_slot_ == 0)
        draw_prompts({{Glyph::DPad, "Browse"}, {Glyph::Circle, "Back"}}, {{kKeyL1R1, "Controller"}}, "");
    else
        draw_prompts({{Glyph::DPad, "Browse"},
                      {Glyph::Cross, map_row_ == 0 ? "Use" : bind < base.count ? "Change" : "Start Over"},
                      {Glyph::Circle, "Back"}},
                     {{kKeyL1R1, "Controller"}}, "");
}
} // namespace porpoise::ui
