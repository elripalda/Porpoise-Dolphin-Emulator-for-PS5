/* Porpoise UI - Star Cube: its own home. A big glass cube floats on black,
 * a section on each edge of its face (Games at the top, the Calendar on the
 * right, Memory Cards at the bottom, Settings on the left). The stick turns
 * it toward an edge, Cross goes in, Circle comes back out to it. Small cubes
 * drift and tumble around it.
 *
 * The pages behind it: Games (the library as spinning discs or covers on a
 * glass page, each game with its own page), the Calendar (the date and time,
 * the month, what was played lately), Memory Cards (each save a little glass
 * cube) and Settings (the usual settings, in the theme's glass). */

#include "ui_app.hpp"
#include "porpoise_pad.hpp"
#include "ui_app_common.hpp"
#include "ui_i18n.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace porpoise::ui
{

using namespace look;
using namespace porpoise::pad;

namespace
{
constexpr float kHomeCx = 960.0f, kHomeCy = 480.0f, kHomeSize = 500.0f;
constexpr int kGridCols = 6, kGridRows = 2;
constexpr float kPageX = 90.0f, kPageY = 132.0f, kPageW = 1740.0f, kPageH = 660.0f;
constexpr float kInfoY = 812.0f, kInfoH = 128.0f;

/* How the cube leans to show each edge: the chosen edge turns away, so the
 * side beyond it comes into view. Games, Calendar, Memory Cards, Settings. */
constexpr float kLeanYaw[4] = {0.0f, -0.24f, 0.0f, 0.24f};
constexpr float kLeanPitch[4] = {0.24f, 0.0f, -0.24f, 0.0f};

float hash1(float n)
{
    const float s = std::sin(n * 12.9898f) * 43758.5453f;
    return s - std::floor(s);
}

float sstep(float a, float b, float x)
{
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* 4.0: a color around the hue circle (0..1), for the cube's oil-on-water
 * sheen: the GameCube menu's cube shows green, blue and red in its gloss. */
Color sheen(float hue, float a)
{
    hue -= std::floor(hue);
    const float r = std::clamp(std::fabs(hue * 6.0f - 3.0f) - 1.0f, 0.0f, 1.0f);
    const float g = std::clamp(2.0f - std::fabs(hue * 6.0f - 2.0f), 0.0f, 1.0f);
    const float b = std::clamp(2.0f - std::fabs(hue * 6.0f - 4.0f), 0.0f, 1.0f);
    return Color{0.35f + 0.65f * r, 0.35f + 0.65f * g, 0.35f + 0.65f * b, a};
}

/* 4.0: each section's picture, made of little squares that fly together on
 * the cube's face when its edge is chosen (as the GameCube menu builds its
 * icons out of cubes): a disc, the calendar, a memory card, a gear. */
const char *const kIcons[4][9] = {
    {"..XXXXX..", ".XXXXXXX.", "XXXXXXXXX", "XXXX.XXXX", "XXX...XXX", "XXXX.XXXX", "XXXXXXXXX", ".XXXXXXX.",
     "..XXXXX.."},
    {".X.....X.", "XXXXXXXXX", "X.......X", "X.X.X.X.X", "X.......X", "X.X.X.X.X", "X.......X", "X.X.X...X",
     "XXXXXXXXX"},
    {".XXXXXX..", ".X....XX.", ".X.....X.", ".X.XXX.X.", ".X.XXX.X.", ".X.....X.", ".X.XXX.X.", ".X.....X.",
     ".XXXXXXX."},
    {"...X.X...", ".X.XXX.X.", "..XXXXX..", "XXXX.XXXX", ".XX...XX.", "XXXX.XXXX", "..XXXXX..", ".X.XXX.X.",
     "...X.X..."},
};

const char *const kMonths[12] = {"January", "February", "March",     "April",   "May",      "June",
                                 "July",    "August",   "September", "October", "November", "December"};
} // namespace

/* ---- state -------------------------------------------------------------------------------- */

int App::sc_face_of_tab() const
{
    return tab_ == Tab::MemoryCards ? 2 : tab_ == Tab::Settings ? 3 : 0;
}

void App::sc_open(int face, bool zoom)
{
    sc_home_ = false;
    sc_face_ = face;
    sc_calendar_ = face == 1;
    const Tab want = face == 2 ? Tab::MemoryCards : face == 3 ? Tab::Settings : Tab::Library;
    if (face != 1)
        set_tab(int(want), +1);
    if (zoom)
    {
        tab_anim_ = 0;
        screen_anim_ = settings_->reduced_motion ? 0.0f : 1.0f;
    }
    else
    {
        tab_anim_ = settings_->reduced_motion ? 0.0f : 1.0f;
        tab_dir_ = +1;
    }
    sc_recent_ = 0;
    sc_month_ = 0;
    sc_page_time_ = 0; /* its pieces arrive one after another */
}

void App::sc_go_home()
{
    sc_home_ = true;
    sc_face_ = sc_calendar_ ? 1 : sc_face_of_tab();
    open_screen(Screen::Main);
}

/* Each frame, whatever shows: the cube's lean and zoom, the grid's rows. */
void App::sc_tick(double dt)
{
    if (!starcube())
        return;
    const bool calm = settings_->reduced_motion;
    const float target = sc_home_ ? 0.0f : 1.0f;
    if (calm)
        sc_zoom_ = target;
    else if (sc_zoom_ < target)
        sc_zoom_ = std::min(target, sc_zoom_ + float(dt) * 2.4f);
    else
        sc_zoom_ = std::max(target, sc_zoom_ - float(dt) * 2.4f);
    /* The cube turns on a spring: it swings a touch past the edge and settles. */
    const float step = float(std::min(dt, 0.05));
    if (calm)
    {
        sc_yaw_ = kLeanYaw[sc_face_];
        sc_pitch_ = kLeanPitch[sc_face_];
        sc_vyaw_ = sc_vpitch_ = 0;
    }
    else
    {
        constexpr float kStiff = 70.0f, kDamp = 10.5f;
        sc_vyaw_ += (kStiff * (kLeanYaw[sc_face_] - sc_yaw_) - kDamp * sc_vyaw_) * step;
        sc_vpitch_ += (kStiff * (kLeanPitch[sc_face_] - sc_pitch_) - kDamp * sc_vpitch_) * step;
        sc_yaw_ += sc_vyaw_ * step;
        sc_pitch_ += sc_vpitch_ * step;
    }
    sc_pulse_ = calm ? 0.0f : std::max(0.0f, sc_pulse_ - step * 2.5f);
    sc_page_time_ += step;
    for (int i = 0; i < 4; ++i)
        sc_glow_[i] = calm ? (i == sc_face_ ? 1.0f : 0.0f) : smooth(sc_glow_[i], i == sc_face_ ? 1.0f : 0.0f, dt, 8.0f);
    /* The games page keeps the chosen row in view. */
    const int row = selected_ / kGridCols;
    if (row < sc_first_row_)
        sc_first_row_ = row;
    if (row > sc_first_row_ + kGridRows - 1)
        sc_first_row_ = row - (kGridRows - 1);
    sc_rows_ = calm ? float(sc_first_row_) : smooth(sc_rows_, float(sc_first_row_), dt, 12.0f);
}

/* The games played lately, most recent first. */
std::vector<int> App::sc_recent_games() const
{
    std::vector<int> out;
    const auto &games = lib_->games();
    for (int i = 0; i < lib_->shown(); ++i)
        if (games[std::size_t(i)].last_played > 0)
            out.push_back(i);
    std::sort(out.begin(), out.end(), [&](int a, int b) {
        return games[std::size_t(a)].last_played > games[std::size_t(b)].last_played;
    });
    if (out.size() > 4)
        out.resize(4);
    return out;
}

void App::sc_open_details()
{
    auto &games = lib_->games();
    open_screen(Screen::Details);
    sfx(Sound::DetailsFlip);
    flip_anim_ = 1.0f;
    box_back_ = false;
    box_yaw_ = 0;
    details_row_ = 0;
    details_custom_ = !Settings::keys_in(game_settings_path(games[std::size_t(selected_)])).empty();
    count_states(&games[std::size_t(selected_)]);
    details_shots_ = count_shots(games[std::size_t(selected_)]);
    lib_->set_selected(Library::key_of(games[std::size_t(selected_)]));
}

/* ---- input -------------------------------------------------------------------------------- */

/* What Star Cube does with the controls; handled false lets the usual
 * screens (a game's page, Memory Cards, Settings) take them. */
App::Action App::update_starcube(bool left, bool right, bool up, bool down, bool &handled)
{
    handled = true;
    auto &games = lib_->games();
    if (sc_home_)
    {
        int to = -1;
        if (up)
            to = 0;
        else if (right)
            to = 1;
        else if (down)
            to = 2;
        else if (left)
            to = 3;
        if (to >= 0 && to != sc_face_)
        {
            sc_face_ = to;
            sc_pulse_ = 1.0f;
            sfx(Sound::MenuScroll);
        }
        if (pressed(BtnCross))
        {
            sc_open(sc_face_, true);
            sfx(Sound::DetailsFlip);
        }
        return Action::None;
    }
    if (screen_ != Screen::Main)
    {
        handled = false;
        return Action::None;
    }
    /* L1 / R1: round the cube's edges without going back out. */
    if (l1_tap_ || pressed(BtnR1))
    {
        const int face = sc_calendar_ ? 1 : sc_face_of_tab();
        sc_open((face + (pressed(BtnR1) ? 1 : 3)) % 4, false);
        tab_dir_ = pressed(BtnR1) ? +1 : -1;
        sfx(Sound::MovingTab);
        return Action::None;
    }
    const bool at_top = sc_calendar_ || tab_ == Tab::Library || tab_ == Tab::MemoryCards ||
                        (tab_ == Tab::Settings && on_rail_);
    if (pressed(BtnCircle) && at_top)
    {
        sc_go_home();
        sfx(Sound::DetailsFlip);
        return Action::None;
    }
    if (sc_calendar_)
    {
        const std::vector<int> recent = sc_recent_games();
        const int before = sc_recent_;
        if (up && sc_recent_ > 0)
            --sc_recent_;
        if (down && sc_recent_ + 1 < int(recent.size()))
            ++sc_recent_;
        if (sc_recent_ != before)
            sfx(Sound::MenuScroll);
        if (left || pressed(BtnL2))
        {
            --sc_month_;
            sc_page_time_ = 0.15f;
            sfx(Sound::MovingTab);
        }
        if (right || pressed(BtnR2))
        {
            ++sc_month_;
            sc_page_time_ = 0.15f;
            sfx(Sound::MovingTab);
        }
        if (pressed(BtnTriangle) && sc_month_ != 0)
        {
            sc_month_ = 0; /* back to today */
            sfx(Sound::MovingTab);
        }
        if (pressed(BtnCross) && sc_recent_ < int(recent.size()))
        {
            /* Straight to that game's page. */
            selected_ = recent[std::size_t(sc_recent_)];
            sc_calendar_ = false;
            sc_face_ = 0;
            set_tab(int(Tab::Library), -1);
            tab_anim_ = 0;
            sc_open_details();
        }
        return Action::None;
    }
    if (tab_ != Tab::Library)
    {
        handled = false;
        return Action::None;
    }

    /* The games page: a grid. */
    const int shown = lib_->shown();
    if (pressed(BtnTriangle) && !games.empty())
    {
        sfx(Sound::MenuScroll);
        screen_ = Screen::Sort;
        sort_row_ = 0;
        return Action::None;
    }
    if (shown == 0)
        return Action::None;
    selected_ = std::clamp(selected_, 0, shown - 1);
    int sel = selected_;
    const int col = sel % kGridCols, row = sel / kGridCols, last_row = (shown - 1) / kGridCols;
    if (left && col > 0)
        --sel;
    if (right && col < kGridCols - 1 && sel + 1 < shown)
        ++sel;
    if (up && row > 0)
        sel -= kGridCols;
    if (down && row < last_row)
        sel = std::min(shown - 1, sel + kGridCols);
    if (sel != selected_)
    {
        selected_ = sel;
        lift_ = 0.4f;
        sfx(Sound::GameRow);
        lib_->set_selected(Library::key_of(games[std::size_t(selected_)]));
    }
    if (pressed(BtnCross) || pressed(BtnSquare))
        sc_open_details();
    if (pressed(BtnOptions))
    {
        lib_->toggle_favourite(games[std::size_t(selected_)]);
        sfx(games[std::size_t(selected_)].favourite ? Sound::LaunchGame : Sound::MovingTab);
    }
    return Action::None;
}

/* ---- drawing: pieces ---------------------------------------------------------------------- */

/* Small cubes drifting past and tumbling, far behind everything. */
void App::draw_sc_drift(double time, float alpha, float rush)
{
    const float t = settings_->reduced_motion || settings_->still_background ? 0.0f : float(time);
    for (int i = 0; i < 16; ++i)
    {
        const float f = float(i);
        const float depth = 0.35f + 0.65f * hash1(f + 3.1f);
        const float size = 22.0f + 58.0f * depth;
        const float speed = 10.0f + 26.0f * depth;
        const float span = 2240.0f;
        float x = std::fmod(hash1(f + 0.7f) * span - t * speed, span);
        if (x < 0)
            x += span;
        x -= 160.0f;
        const float y = 120.0f + hash1(f + 9.3f) * 820.0f + std::sin(t * 0.4f + f) * 18.0f;
        /* Going in, they rush past outward, the near ones fastest. */
        const float k = 1.0f + rush * (1.2f + 2.0f * depth);
        x = kHomeCx + (x - kHomeCx) * k;
        const float yy = kHomeCy + (y - kHomeCy) * k;
        const float a = alpha * (0.22f + 0.38f * depth);
        draw_glass_cube(x, yy, size * (1.0f + rush * depth), t * (0.2f + 0.3f * hash1(f + 1.0f)) + f, t * (0.15f + 0.25f * hash1(f + 2.0f)) + f * 0.5f,
                        t * 0.1f * (hash1(f + 4.0f) - 0.5f), rgba(0x2A3FD8, 0.35f), rgba(0x8FA0FF, 0.6f), nullptr, a, false);
    }
}

/* The page's heading where the tabs would be: a little cube, the section's
 * name in dot-matrix letters, the clock. */
void App::draw_sc_header(const std::string &title, double time)
{
    Gfx &g = *g_;
    const float t = settings_->reduced_motion ? 0.6f : float(time);
    draw_glass_cube(88, kBarCy, 44, 0.7f + t * 0.6f, 0.55f + 0.1f * std::sin(t * 0.5f), 0.15f, rgba(0x4A50F0, 0.55f),
                    rgba(0xB4A8FF), nullptr, 1.0f, true);
    g.text_mid(Font::ExtraBold, ts(42), 140, kBarCy, kWhite, Align::Left, title);
    g.text_mid(Font::SemiBold, ts(27), 1866, kBarCy, kWhite, Align::Right, clock_text());
    g.panel(60, kBarCy + 46, 1800, 1.6f, rgba(0x6D6AF0, 0.35f), 1, 0);
}

/* A page of glass the content sits on. */
void App::draw_sc_page(float x, float y, float w, float h)
{
    Gfx &g = *g_;
    g.panel(x, y, w, h, rgba(0x14207A, 0.42f), 0.7f, kR, rgba(0x6F7CFF, 0.75f), 1.6f, 0, 0.14f);
}

/* ---- the home cube ------------------------------------------------------------------------ */

void App::draw_sc_home(double time, float zoom)
{
    Gfx &g = *g_;
    const bool calm = settings_->reduced_motion;
    const float t = calm ? 0.0f : float(time);
    const float out = sstep(0.0f, 1.0f, zoom);
    const float alpha = 1.0f - sstep(0.15f, 0.85f, zoom);
    if (alpha <= 0.002f)
        return;
    draw_sc_drift(time, alpha, out);

    /* Coming in (the entrance): it flies in from far off, spinning. */
    const float arrive = calm ? 1.0f : sstep(0.0f, 1.0f, intro_fade_);
    const float pulse = sc_pulse_ * sc_pulse_;
    const float size = kHomeSize * (1.0f + out * 2.6f) * (0.45f + 0.55f * arrive) * (1.0f + 0.035f * pulse);
    const float cx = kHomeCx, cy = kHomeCy + (calm ? 0.0f : std::sin(t * 0.8f) * 9.0f) * (1.0f - out);
    /* A slow turn about its lean, always back to the chosen edge; going in, it
     * turns further toward it. */
    const float yaw = sc_yaw_ * (1.0f + out * 1.2f) - 0.05f + (calm ? 0.0f : std::sin(t * 0.33f) * 0.05f) +
                      (1.0f - arrive) * 2.4f;
    const float pitch = sc_pitch_ * (1.0f + out * 1.2f) + 0.06f + (calm ? 0.0f : std::sin(t * 0.27f + 1.0f) * 0.04f) +
                        (1.0f - arrive) * 0.8f;
    const float roll = calm ? 0.0f : std::sin(t * 0.21f) * 0.025f;
    const float h = size * 0.5f;

    /* Its light on the floor below, and a glow behind it. */
    g.blob(cx, cy + h * 1.25f, size * 1.4f, size * 0.20f, rgba(0x5E4BFF, 0.18f * alpha));
    g.blob(cx, cy, size * 1.8f, size * 1.8f, rgba(0x3B2E9E, 0.12f * alpha));

    /* 4.0: after the GameCube's own menu, a glass slab seen nearly face on: a
     * thick rounded bezel around a dark, recessed face washed in teal, blue and
     * purple, light glinting off its edges, the four sides' names on the bezel. */
    const V3 faces[6][4] = {
        {{-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h}}, /* front */
        {{h, -h, h}, {-h, -h, h}, {-h, h, h}, {h, h, h}},     /* back */
        {{-h, -h, h}, {-h, -h, -h}, {-h, h, -h}, {-h, h, h}}, /* left */
        {{h, -h, -h}, {h, -h, h}, {h, h, h}, {h, h, -h}},     /* right */
        {{-h, -h, h}, {h, -h, h}, {h, -h, -h}, {-h, -h, -h}}, /* top */
        {{-h, h, -h}, {h, h, -h}, {h, h, h}, {-h, h, h}},     /* bottom */
    };
    const float light[6] = {1.0f, 0.45f, 0.75f, 0.75f, 1.15f, 0.55f};
    auto corners = [&](int f, Corner q[4]) {
        for (int i = 0; i < 4; ++i)
            q[i] = seen3(cx, cy, turn3(faces[f][i], yaw, pitch, roll));
    };
    /* A point on the front face (x, y across it, -h..h), lifted off it by z. */
    auto on_face = [&](float x, float y, float z) { return seen3(cx, cy, turn3(V3{x, y, -h - z}, yaw, pitch, roll)); };
    auto face_quad = [&](float x0, float y0, float x1, float y1, float z, Corner q[4]) {
        q[0] = on_face(x0, y0, z);
        q[1] = on_face(x1, y0, z);
        q[2] = on_face(x1, y1, z);
        q[3] = on_face(x0, y1, z);
    };
    const float glint = t * 0.03f + yaw * 0.35f + pitch * 0.25f;
    auto face_glass = [&](int f, float fade, bool inside) {
        Glass glass;
        const float l = light[f] * (inside ? 0.5f : 1.0f);
        glass.tint = Color{0.07f * l, 0.09f * l, 0.24f * l, inside ? 0.22f : 0.60f};
        /* Teal through blue to purple, never pink. */
        glass.rim = inside ? rgba(0x4A5CD8, 0.35f)
                           : sheen(0.63f + 0.15f * std::sin(float(f) * 1.7f + glint * 3.0f), 0.92f);
        glass.radius = size * 0.09f;
        glass.rim_w = inside ? 1.4f : 3.0f;
        glass.phase = 0.3f + float(f) * 0.11f + yaw * 0.2f;
        glass.fade = fade;
        return glass;
    };
    /* The far sides first, faint through the glass; then its depth, the near
     * sides, which read as the slab's beveled edge. */
    for (int f = 0; f < 6; ++f)
    {
        Corner q[4];
        corners(f, q);
        if (facing_you(q))
            continue;
        const Corner r[4] = {q[1], q[0], q[3], q[2]};
        g.glass(r, size, size, 0, face_glass(f, alpha, true));
    }
    for (int f = 1; f < 6; ++f)
    {
        Corner q[4];
        corners(f, q);
        if (facing_you(q))
            g.glass(q, size, size, 0, face_glass(f, alpha, false));
    }
    /* The front: the bezel's glass, then the recessed face inside it. While it
     * arrives spinning, its front is drawn only while it faces you. */
    {
        Corner q[4];
        corners(0, q);
        if (!facing_you(q))
            return;
        g.glass(q, size, size, 0, face_glass(0, alpha, false));
    }
    const float bezel = size * 0.135f, inner = h - bezel;
    {
        Corner q[4];
        face_quad(-inner, -inner, inner, inner, 0.5f, q);
        g.quad3d(nullptr, q, inner * 2, inner * 2, rgba(0x090722, 0.74f * alpha), size * 0.055f, false, false);
        /* The face's colors, bouncing off each other as it turns. */
        const struct
        {
            float x, y, r;
            float hue;
            float a;
        } wash[4] = {{-0.45f, -0.40f, 0.80f, 0.47f, 0.50f},
                     {0.40f, 0.42f, 0.72f, 0.76f, 0.32f},
                     {0.30f, -0.35f, 0.55f, 0.60f, 0.26f},
                     {-0.35f, 0.38f, 0.52f, 0.68f, 0.22f}};
        for (const auto &w : wash)
        {
            const float drift = calm ? 0.0f : std::sin(t * 0.35f + w.hue * 9.0f) * 0.08f;
            const Corner c = on_face((w.x + drift) * inner, (w.y - drift) * inner, 0.8f);
            Color col = sheen(w.hue + 0.06f * std::sin(glint * 4.0f), w.a * alpha);
            col.r *= 0.45f, col.g *= 0.62f, col.b *= 0.80f;
            g.blob(c.x, c.y, size * w.r, size * w.r, col);
        }
    }
    /* Glare: a bright line along the bezel's top and left edges, and a glint
     * that runs along the top. */
    {
        Corner q[4];
        face_quad(-h * 0.80f, -h * 0.965f, h * 0.80f, -h * 0.935f, 1.2f, q);
        g.quad3d(nullptr, q, h * 1.6f, h * 0.03f, rgba(0xFFFFFF, 0.30f * alpha), h * 0.015f, false, false);
        face_quad(-h * 0.965f, -h * 0.80f, -h * 0.935f, h * 0.60f, 1.2f, q);
        g.quad3d(nullptr, q, h * 0.03f, h * 1.4f, rgba(0xFFFFFF, 0.16f * alpha), h * 0.015f, false, false);
        const float run = calm ? 0.2f : std::fmod(t * 0.18f, 1.6f) - 0.3f;
        const Corner c = on_face((-0.85f + run * 1.7f) * h, -h * 0.95f, 1.4f);
        g.blob(c.x, c.y, size * 0.22f, size * 0.05f, rgba(0xFFFFFF, 0.22f * alpha));
        const Corner c2 = on_face(-h * 0.78f, -h * 0.78f, 1.4f);
        g.blob(c2.x, c2.y, size * 0.30f, size * 0.30f, rgba(0xD8E8FF, 0.07f * alpha));
    }

    /* The sides' names, on the bezel, each along its edge with its letters'
     * tops toward the outside, in the GameCube menu's own words. */
    const std::string names[4] = {tr("Game Play"), tr("Calendar"), tr("Memory Card"), tr("Options")};
    const float inset = bezel * 0.5f;
    struct Edge
    {
        float cx, cy, ux, uy; /* the label's middle on the face, the way it reads */
    };
    const Edge edges[4] = {{0, -h + inset, 1, 0}, {h - inset, 0, 0, 1}, {0, h - inset, 1, 0}, {-h + inset, 0, 0, -1}};
    for (int e = 0; e < 4; ++e)
    {
        const Edge &ed = edges[e];
        const float vx = -ed.uy, vy = ed.ux; /* down, for the letters */
        const float glow = sc_glow_[e];
        float px = std::min(ts(48) * (size / kHomeSize), bezel * 0.78f);
        const float room = size * 0.80f;
        const float w = g.measure(Font::ExtraBold, px, names[e]);
        if (w > room)
            px *= room / w;
        auto map_at = [&](float lift, float dx, float dy) {
            return [&, lift, dx, dy](float x, float y) {
                return on_face(ed.cx + ed.ux * x + vx * y + dx, ed.cy + ed.uy * x + vy * y + dy, lift);
            };
        };
        const float k = 1.0f + 0.06f * glow + 0.08f * glow * pulse;
        /* A dark edge under the letters, as the GameCube's have. */
        g.text_mapped(Font::ExtraBold, px * k, names[e], rgba(0x05041A, 0.75f * alpha), Align::Center,
                      map_at(1.6f, size * 0.004f, size * 0.004f));
        const Color on = rgba(0xFFFFFF, alpha), off = rgba(0xD6D2F4, 0.82f * alpha);
        const Color c{off.r + (on.r - off.r) * glow, off.g + (on.g - off.g) * glow, off.b + (on.b - off.b) * glow,
                      off.a + (on.a - off.a) * glow};
        g.text_mapped(Font::ExtraBold, px * k, names[e], c, Align::Center, map_at(2.0f, 0, 0));
    }

    /* 4.0: the chosen side's picture, its squares flying together on the
     * face, each in the sheen's colors. */
    {
        const float build = calm ? 1.0f : sstep(0.0f, 1.0f, sc_glow_[sc_face_]);
        const float cell = size * 0.040f, span = cell * 9.0f;
        for (int row = 0; row < 9; ++row)
            for (int col = 0; col < 9; ++col)
            {
                if (kIcons[sc_face_][row][col] != 'X')
                    continue;
                const float fx = -span * 0.5f + (float(col) + 0.5f) * cell;
                const float fy = -span * 0.5f + (float(row) + 0.5f) * cell;
                /* From scattered to in place, the outer squares last. */
                const float d = std::sqrt(fx * fx + fy * fy) / span;
                const float k = std::clamp((build - d * 0.45f) / 0.55f, 0.0f, 1.0f);
                const float ease = k * k * (3.0f - 2.0f * k);
                const float seed = hash1(float(row * 9 + col) + float(sc_face_) * 31.0f);
                const float sx = fx + (1.0f - ease) * (seed - 0.5f) * size * 1.4f;
                const float sy = fy + (1.0f - ease) * (hash1(seed * 7.0f) - 0.5f) * size * 1.4f;
                const float half = cell * 0.42f * (0.4f + 0.6f * ease);
                Corner q[4];
                const float pts[4][2] = {{-half, -half}, {half, -half}, {half, half}, {-half, half}};
                for (int i = 0; i < 4; ++i)
                {
                    const V3 p{sx + pts[i][0], sy + pts[i][1], -h - 2.0f};
                    q[i] = seen3(cx, cy, turn3(p, yaw, pitch, roll));
                }
                /* Pearl white, with only a hint of the sheen's colors. */
                const float hue = 0.45f + (float(col) + float(row)) / 16.0f * 0.45f + t * 0.04f;
                const Color tint = sheen(hue, 1.0f);
                const Color pearl{0.88f + 0.12f * (tint.r - 0.88f) * 2.5f, 0.86f + 0.14f * (tint.g - 0.86f) * 2.5f,
                                  1.0f, (0.25f + 0.70f * ease) * alpha * (1.0f - out)};
                g.quad3d(nullptr, q, half * 2, half * 2, pearl, half * 0.25f, false, false);
            }
    }

    if (out > 0.02f)
        return;
    /* What the chosen edge holds, under the cube. */
    std::string sub;
    if (sc_face_ == 0)
        sub = lib_->games().empty() ? tr("No games yet") : library_count();
    else if (sc_face_ == 1)
        sub = short_date() + "   " + clock_text();
    else if (sc_face_ == 3)
        sub = build_label();
    /* The caption under it slides in with each turn. */
    const float cap = 1.0f - pulse;
    g.text_mid(Font::ExtraBold, ts(50), 960 + 30.0f * pulse, 890, with_alpha(kWhite, 0.3f + 0.7f * cap), Align::Center,
               names[sc_face_]);
    if (!sub.empty())
        g.text_mid(Font::SemiBold, ts(27), 960 + 18.0f * pulse, 944, with_alpha(kLavender, 0.3f + 0.7f * cap),
                   Align::Center, sub);
    g.text_mid(Font::SemiBold, ts(27), 1866, kBarCy, kWhite, Align::Right, clock_text());
    draw_prompts({{Glyph::DPad, "Choose"}, {Glyph::Cross, "Confirm"}}, {}, "");
}

/* ---- games -------------------------------------------------------------------------------- */

void App::draw_sc_games(double time)
{
    Gfx &g = *g_;
    auto &games = lib_->games();
    const int shown = lib_->shown();
    draw_sc_page(kPageX, kPageY, kPageW, kPageH);
    if (games.empty())
    {
        draw_mark(960, 330, 150, kCyan);
        g.text(Font::Bold, ts(40), 960, 410, kWhite, Align::Center, tr("Add Games"));
        g.text(Font::Regular, ts(26), 960, 470, kSoft, Align::Center,
               tr("Copy .iso, .rvz or .ciso files with PS5 Upload into"));
        g.text(Font::SemiBold, ts(26), 960, 510, kIcy, Align::Center, data_dir_ + "/games");
        g.text(Font::Regular, ts(26), 960, 550, kSoft, Align::Center,
               tr("or add your own folder in Settings, under Games."));
        draw_prompts({{Glyph::Circle, "Back"}}, {}, "");
        return;
    }
    const bool discs = settings_->sc_games == 0;
    release_far_art(kGridCols * (kGridRows + 1));
    const float spin = settings_->reduced_motion ? 0.0f : float(time);
    const float cell_w = (kPageW - 40) / kGridCols, cell_h = (kPageH - 20) / kGridRows;
    const float x0 = kPageX + 20 + cell_w * 0.5f, y0 = kPageY + 10 + cell_h * 0.5f;
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < shown; ++i)
        {
            const bool focused = i == selected_;
            if ((pass == 1) != focused)
                continue;
            const float r = float(i / kGridCols) - sc_rows_;
            if (r < -0.9f || r > kGridRows - 0.1f)
                continue;
            const float edge = std::clamp(std::min(r + 0.9f, kGridRows - 0.1f - r) / 0.6f, 0.0f, 1.0f);
            /* Opening the page, the discs arrive one after another. */
            const float order = float((i / kGridCols - sc_first_row_) * kGridCols + i % kGridCols);
            const float in = settings_->reduced_motion ? 1.0f : sstep(0.0f, 1.0f, (sc_page_time_ - 0.03f * order) / 0.35f);
            if (in <= 0.0f)
                continue;
            const float cx = x0 + float(i % kGridCols) * cell_w,
                        cy = y0 + r * cell_h - (focused ? 14.0f * lift_ : 0.0f) + 40.0f * (1.0f - in);
            Game *game = &games[std::size_t(i)];
            if (discs)
            {
                const float d = focused ? 236.0f : 200.0f;
                draw_disc(game, cx, cy - 16, d * (0.7f + 0.3f * in),
                          spin * (focused ? 1.2f : 0.25f) + float(i) * 0.7f + (1.0f - in) * 3.0f, 0, edge * in, focused);
            }
            else
                draw_tile(game, cx, cy - 6, focused ? 190.0f : 168.0f, focused ? 266.0f : 236.0f, (1.0f - in) * 1.4f,
                          edge * in, focused, false);
            if (game->favourite)
                g.glyph(Glyph::Star, cx + 84, cy - 120, 30, rgba(0xFFD45C, edge));
            if (focused)
                g.glyph(Glyph::Arrow, cx, cy + cell_h * 0.5f - 16, 26, rgba(0xB8F0FF, edge), kPi);
        }

    /* The chosen game, along the bottom. */
    Game &sel = games[std::size_t(selected_)];
    g.panel(kPageX, kInfoY, kPageW, kInfoH, rgba(0x0B0F3A, 0.72f), 0.85f, kR, rgba(0x6F7CFF, 0.85f), 1.8f, 0, 0.12f);
    const float icy = kInfoY + kInfoH * 0.5f;
    if (Texture *cover = cover_of(sel))
        g.image(cover, kPageX + 22, icy - 48, 70, 96, kWhite, 6);
    g.text_mid(Font::Bold, ts(36), kPageX + 116, icy - 19, kWhite, Align::Left,
               fit(g, Font::Bold, ts(36), sel.title, kPageW - 400));
    g.text_mid(Font::Regular, ts(26), kPageX + 116, icy + 23, kLavender, Align::Left, game_meta(sel));
    char pos[24];
    std::snprintf(pos, sizeof pos, "%02d / %02d", selected_ + 1, shown);
    g.text_mid(Font::ExtraBold, ts(30), kPageX + kPageW - 32, icy, kSoft, Align::Right, pos);
    draw_prompts({{Glyph::DPad, "Browse"}, {Glyph::Cross, "Open"}, {Glyph::Circle, "Back"}},
                 {{Glyph::Options, "Favorite"}, {Glyph::Triangle, "Sort & Filter"}}, "");
}

/* A game's own page: its disc (or box) turning on the left, what you can
 * do with it on the right. */
void App::draw_sc_details(double time)
{
    Gfx &g = *g_;
    auto &games = lib_->games();
    if (games.empty())
        return;
    Game &game = games[std::size_t(std::clamp(selected_, 0, int(games.size()) - 1))];
    draw_sc_page(kPageX, kPageY, kPageW, 808);
    const float spin = settings_->reduced_motion ? 0.0f : float(time) * 0.5f;
    const float in = 1.0f - flip_anim_;
    if (settings_->sc_games == 0)
        draw_disc(&game, 500, 520, 470 * (0.85f + 0.15f * in), spin, 0.10f, 1.0f, false);
    else
        draw_tile(&game, 500, 520, 340, 476, (1.0f - in) * 1.2f + std::sin(float(time) * 0.6f) * 0.08f, 1.0f, true,
                  false);

    const float rx = 900, rw = 840;
    g.text_mid(Font::Bold, ts(50), rx, 222, kWhite, Align::Left, fit(g, Font::Bold, ts(50), game.title, rw));
    g.text_mid(Font::Regular, ts(26), rx, 280, kLavender, Align::Left, game_meta(game));
    if (game.favourite)
        g.glyph(Glyph::Star, rx + rw - 10, 222, 36, rgba(0xFFD45C));
    const std::string actions[5] = {title_case(tr("Play")), title_case(tr("Save States")),
                                    title_case(tr("Game Settings")), title_case(tr("Save Data")),
                                    title_case(tr("Screenshots"))};
    const float t = settings_->reduced_motion ? 0.0f : float(time);
    for (int i = 0; i < (details_shots_ > 0 ? 5 : 4); ++i)
    {
        const bool on = details_row_ == i;
        const float y = 360 + float(i) * 112, h = 88;
        g.panel(rx, y, rw, h, on ? rgba(0x3A3FE8, 0.85f) : rgba(0x101860, 0.55f), 0.65f, kR,
                on ? rgba(0xC8F2FF) : rgba(0x5560D8, 0.7f), on ? 2.2f : 1.4f, on ? 8.0f : 0.0f, 0.25f);
        draw_glass_cube(rx + 48, y + h * 0.5f, on ? 34.0f : 26.0f, on ? t * 1.3f : 0.6f, -0.5f, 0.2f,
                        on ? rgba(0x7FD0FF, 0.7f) : rgba(0x3A44C8, 0.5f), on ? rgba(0xE6FAFF) : rgba(0x8C94FF, 0.7f),
                        nullptr, 1.0f, on);
        g.text_mid(on ? Font::Bold : Font::SemiBold, ts(32), rx + 96, y + h * 0.5f, on ? kWhite : kSoft, Align::Left,
                   actions[i]);
        if (i == 2 && details_custom_)
            g.text_mid(Font::SemiBold, ts(22), rx + rw - 30, y + h * 0.5f, kCyan, Align::Right, tr("Custom"));
        if (i == 4)
            g.text_mid(Font::SemiBold, ts(22), rx + rw - 30, y + h * 0.5f, kCyan, Align::Right,
                       plural(details_shots_, "1 screenshot", "{n} screenshots"));
    }
    if (screen_ != Screen::States)
    {
        std::vector<std::pair<Glyph, std::string>> right = {{kKeyL2R2, "Other Games"}, {Glyph::Options, "Favorite"}};
        if (details_achievements(game).valid())
            right.insert(right.begin() + 1, {Glyph::Square, "Achievements"});
        draw_prompts({{Glyph::DPad, "Browse"}, {Glyph::Cross, "Confirm"}, {Glyph::Circle, "Back"}}, right, "");
    }
}

/* ---- calendar ----------------------------------------------------------------------------- */

void App::draw_sc_calendar(double time)
{
    Gfx &g = *g_;
    (void)time;
    auto &games = lib_->games();
    const std::time_t now = std::time(nullptr);
    std::tm today{};
    localtime_r(&now, &today);
    const Language lang = language();
    const bool cjk = lang == Language::Japanese || lang == Language::Korean ||
                     lang == Language::ChineseSimplified || lang == Language::ChineseTraditional;

    /* Left: the time, the date and what was played lately. */
    const float lx = kPageX, lw = 600;
    draw_sc_page(lx, kPageY, lw, kPageH);
    g.text_mid(Font::ExtraBold, ts(96), lx + lw * 0.5f, kPageY + 108, kWhite, Align::Center, clock_text());
    g.text_mid(Font::SemiBold, ts(32), lx + lw * 0.5f, kPageY + 190, kLavender, Align::Center,
               short_date() + "  " + std::to_string(today.tm_year + 1900));
    g.panel(lx + 40, kPageY + 238, lw - 80, 1.6f, rgba(0x6D6AF0, 0.4f), 1, 0);
    g.text_mid(Font::ExtraBold, ts(30), lx + 40, kPageY + 278, kSoft, Align::Left, tr("Recently Played"));
    const std::vector<int> recent = sc_recent_games();
    sc_recent_ = std::clamp(sc_recent_, 0, std::max(0, int(recent.size()) - 1));
    if (recent.empty())
        g.text(Font::Regular, ts(26), lx + 40, kPageY + 320, kLavender, Align::Left, tr("Games you play show up here."));
    for (int k = 0; k < int(recent.size()); ++k)
    {
        Game &game = games[std::size_t(recent[std::size_t(k)])];
        const bool on = k == sc_recent_;
        const float y = kPageY + 312 + float(k) * 82, h = 72;
        if (on)
            g.panel(lx + 24, y, lw - 48, h, rgba(0x3A3FE8, 0.85f), 0.65f, kR, rgba(0xC8F2FF), 2.0f, 6, 0.25f);
        if (Texture *cover = cover_of(game))
            g.image(cover, lx + 40, y + 8, 42, 56, kWhite, 4);
        g.text_mid(on ? Font::Bold : Font::SemiBold, ts(26), lx + 100, y + 25, on ? kWhite : kSoft, Align::Left,
                   fit(g, Font::Bold, ts(26), game.title, lw - 150));
        g.text_mid(Font::Regular, ts(21), lx + 100, y + 52, kLavender, Align::Left,
                   relative_time(game.last_played, (long long)now));
    }

    /* Right: the month. */
    const float mx = kPageX + lw + 30, mw = kPageW - lw - 30;
    draw_sc_page(mx, kPageY, mw, kPageH);
    std::tm first = today;
    first.tm_mday = 1;
    first.tm_mon += sc_month_;
    first.tm_hour = 12;
    first.tm_isdst = -1;
    std::mktime(&first); /* normalises the month and year, and finds the weekday */
    const int year = first.tm_year + 1900, month = first.tm_mon;
    std::string heading;
    if (lang == Language::Korean)
        heading = std::to_string(year) + "\xEB\x85\x84 " + std::to_string(month + 1) + "\xEC\x9B\x94";
    else if (cjk)
        heading = std::to_string(year) + "\xE5\xB9\xB4" + std::to_string(month + 1) + "\xE6\x9C\x88";
    else
        heading = tr(kMonths[month]) + " " + std::to_string(year);
    const float hy = kPageY + 62;
    g.text_mid(Font::ExtraBold, ts(44), mx + mw * 0.5f, hy, kWhite, Align::Center, heading);
    g.glyph(Glyph::Arrow, mx + 60, hy, 28, kSoft, kPi * 1.5f);
    g.glyph(Glyph::Arrow, mx + mw - 60, hy, 28, kSoft, kPi * 0.5f);

    /* Sunday first, or Monday where weeks start then. */
    const bool monday = !(lang == Language::English || cjk || lang == Language::PortugueseBrazil ||
                          lang == Language::SpanishLatinAmerica);
    static const char *const kDays[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const float gx = mx + 30, gw = mw - 60, cw = gw / 7;
    for (int c = 0; c < 7; ++c)
    {
        const int wd = monday ? (c + 1) % 7 : c;
        g.text_mid(Font::SemiBold, ts(24), gx + cw * (float(c) + 0.5f), kPageY + 130, wd == 0 ? kLavender : kSoft,
                   Align::Center, tr(kDays[wd]));
    }
    std::tm probe = first;
    probe.tm_mon += 1;
    probe.tm_mday = 0;
    std::mktime(&probe);
    const int days = probe.tm_mday;
    const int lead = monday ? (first.tm_wday + 6) % 7 : first.tm_wday;
    /* The games last played on each day of this month. */
    std::vector<int> played(std::size_t(days + 1), -1);
    for (int i = 0; i < lib_->shown(); ++i)
    {
        const Game &game = games[std::size_t(i)];
        if (game.last_played <= 0)
            continue;
        const std::time_t when = std::time_t(game.last_played);
        std::tm tm{};
        localtime_r(&when, &tm);
        if (tm.tm_year + 1900 == year && tm.tm_mon == month)
        {
            int &slot = played[std::size_t(tm.tm_mday)];
            if (slot < 0 || games[std::size_t(slot)].last_played < game.last_played)
                slot = i;
        }
    }
    const float top = kPageY + 160, ch = (kPageH - 180) / 6;
    for (int d = 1; d <= days; ++d)
    {
        const int cell = lead + d - 1;
        const float x = gx + cw * float(cell % 7), y = top + ch * float(cell / 7);
        const bool is_today = sc_month_ == 0 && d == today.tm_mday;
        const float in = settings_->reduced_motion ? 1.0f : sstep(0.0f, 1.0f, (sc_page_time_ - 0.012f * float(d)) / 0.3f);
        g.set_layer(layer_dx_, layer_dy_ + 18.0f * (1.0f - in), layer_fade_ * in);
        if (is_today)
            g.panel(x + 4, y + 4, cw - 8, ch - 8, rgba(0x3A3FE8, 0.85f), 0.65f, kR * 0.6f, rgba(0xC8F2FF), 2.0f, 8,
                    0.25f);
        else
            g.panel(x + 4, y + 4, cw - 8, ch - 8, rgba(0x0E1450, 0.45f), 1, kR * 0.6f, rgba(0x3E46B8, 0.45f), 1.0f);
        g.text_mid(is_today ? Font::ExtraBold : Font::SemiBold, ts(28), x + 20, y + 30, is_today ? kWhite : kSoft,
                   Align::Left, std::to_string(d));
        const int game = played[std::size_t(d)];
        if (game >= 0)
        {
            if (Texture *cover = cover_of(games[std::size_t(game)]))
                g.image(cover, x + cw - 50, y + 10, 36, ch - 20, kWhite, 3);
            else
                draw_glass_cube(x + cw - 32, y + ch * 0.5f, 22, 0.5f, -0.5f, 0, rgba(0x7FD0FF, 0.7f), kIcy, nullptr,
                                1.0f, false);
        }
    }

    g.set_layer(layer_dx_, layer_dy_, layer_fade_);
    /* The chosen recent game, along the bottom. */
    g.panel(kPageX, kInfoY, kPageW, kInfoH, rgba(0x0B0F3A, 0.72f), 0.85f, kR, rgba(0x6F7CFF, 0.85f), 1.8f, 0, 0.12f);
    const float icy = kInfoY + kInfoH * 0.5f;
    if (!recent.empty())
    {
        Game &game = games[std::size_t(recent[std::size_t(sc_recent_)])];
        if (Texture *cover = cover_of(game))
            g.image(cover, kPageX + 22, icy - 48, 70, 96, kWhite, 6);
        g.text_mid(Font::Bold, ts(36), kPageX + 116, icy - 19, kWhite, Align::Left,
                   fit(g, Font::Bold, ts(36), game.title, kPageW - 200));
        g.text_mid(Font::Regular, ts(26), kPageX + 116, icy + 23, kLavender, Align::Left, game_meta(game));
    }
    else
        g.text_mid(Font::SemiBold, ts(30), kPageX + 40, icy, kLavender, Align::Left, short_date());
    std::vector<std::pair<Glyph, std::string>> right = {{kKeyL2R2, "Month"}};
    if (sc_month_ != 0)
        right.push_back({Glyph::Triangle, "Today"});
    if (recent.empty())
        draw_prompts({{Glyph::Circle, "Back"}}, right, "");
    else
        draw_prompts({{Glyph::DPad, "Browse"}, {Glyph::Cross, "Open"}, {Glyph::Circle, "Back"}}, right, "");
}

/* ---- the whole screen --------------------------------------------------------------------- */

void App::draw_starcube(double time)
{
    Gfx &g = *g_;
    g.set_layer();
    g.set_tone(false);
    g.background();
    g.set_intro(intro_fade_, intro_dy_);
    const float zoom = sc_zoom_;
    if (sc_home_ || zoom < 0.999f)
        draw_sc_home(time, zoom);
    const float page = sstep(0.35f, 1.0f, zoom);
    if (page > 0.002f)
    {
        const std::string names[4] = {tr("Games"), tr("Calendar"), tr("Memory Cards"), tr("Settings")};
        g.set_layer(0, 0, page);
        draw_sc_header(names[sc_calendar_ ? 1 : sc_face_of_tab()], time);
        float dx = 0, dy = 0, fade = page;
        if (tab_anim_ > 0)
        {
            const float p = ease_out(1.0f - tab_anim_);
            dx = float(tab_dir_) * 150.0f * (1.0f - p);
            fade *= 0.2f + 0.8f * p;
        }
        if (screen_anim_ > 0)
        {
            const float p = ease_out(1.0f - screen_anim_);
            dy = 22.0f * (1.0f - p);
            fade *= 0.25f + 0.75f * p;
        }
        /* Zooming in: the page grows out of the cube. */
        const float grow = 1.0f - page;
        dy += grow * 40.0f;
        g.set_layer(dx, dy, fade);
        layer_dx_ = dx;
        layer_dy_ = dy;
        layer_fade_ = fade;
        if (screen_ == Screen::Details || screen_ == Screen::States)
        {
            draw_sc_details(time);
            if (screen_ == Screen::States)
                draw_states();
        }
        else if (screen_ == Screen::Browse)
            draw_browser();
        else if (screen_ == Screen::TileArt)
            draw_tile_art(time);
        else if (screen_ == Screen::Shots)
            draw_shots(time);
        else if (screen_ == Screen::Controllers)
            draw_controllers(time);
        else if (screen_ == Screen::GameSettings)
            draw_settings();
        else if (screen_ == Screen::Mapping)
            draw_mapping(time);
        else if (screen_ == Screen::WiiGuide)
            draw_wii_guide(time);
        else if (screen_ == Screen::WiiSetup)
            draw_wii_setup(time);
        else if (sc_calendar_)
            draw_sc_calendar(time);
        else
            switch (tab_)
            {
            case Tab::Library:
                draw_sc_games(time);
                if (screen_ == Screen::Sort)
                    draw_sort();
                break;
            case Tab::MemoryCards:
                draw_memory_cards(time);
                break;
            case Tab::Settings:
                draw_settings();
                break;
            }
    }
    g.set_layer();
    draw_dialog();
    draw_update_overlay(time);
    g.set_intro(1, 0);
    draw_theme_overlay();
}

#ifdef PORPOISE_HOST_PREVIEW
/* For the preview: a face of the cube, or its page. */
void App::preview_starcube(int face, bool page)
{
    if (page)
        sc_open(face, true);
    else
    {
        sc_go_home();
        sc_face_ = face;
    }
}

#endif

} // namespace porpoise::ui
