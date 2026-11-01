/* Porpoise UI - the launcher screens.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The library, a game's details, memory cards, dialogs and the launch screen.
 * Settings and the folder browser are in ui_app_settings.cpp; the look they
 * share is in ui_app_common.hpp. */
#include "porpoise_states.hpp"
#include "ui_widescreen.hpp"
#include "porpoise_dolphinbar.hpp"
#include "porpoise_ns2gc.hpp"
#include "porpoise_usbhid.hpp"
#include "ui_app.hpp"

#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "porpoise_pad.hpp"
#include "ui_app_common.hpp"
#include "ui_i18n.hpp"

namespace porpoise::ui
{
using namespace look;
using namespace porpoise::pad;

void App::init(Gfx *gfx, Library *library, Settings *settings, const std::string &settings_path,
               const std::string &options_path, const std::string &saves_dir)
{
    g_ = gfx;
    lib_ = library;
    settings_ = settings;
    settings_path_ = settings_path;
    options_path_ = options_path;
    saves_dir_ = saves_dir;
    data_dir_ = settings_path.substr(0, settings_path.rfind('/'));
    mc_wii_ = settings_->ui_theme == 1; /* Revolution opens on the Wii saves */
    build_settings();
    /* Return to the game played last. */
    const std::string key = lib_->selected_id();
    for (int i = 0; i < lib_->shown(); ++i)
        if (Library::key_of(lib_->games()[std::size_t(i)]) == key)
            selected_ = i;
    scroll_ = float(selected_);
}

void App::forget_textures()
{
    /* The device is gone and its textures with it: only drop the pointers. */
    ach_tex_.clear();
    sysmenu_game_.cover = sysmenu_game_.disc = sysmenu_game_.back = sysmenu_game_.spine = nullptr;
    sysmenu_game_.cover_tried = sysmenu_game_.disc_tried = sysmenu_game_.back_tried = sysmenu_game_.spine_tried = false;
    for (Game &g : lib_->games())
    {
        g.cover = nullptr;
        g.cover_tried = false;
        g.disc = nullptr;
        g.disc_tried = false;
        g.back = nullptr;
        g.back_tried = false;
        g.spine = nullptr;
        g.spine_tried = false;
    }
    for (Card *c : {&card_a_, &card_b_})
        for (Save &s : c->saves)
        {
            s.icon_tex = nullptr;
            s.banner_tex = nullptr;
        }
    for (WiiSave &s : wii_saves_)
        s.icon_tex = s.banner_tex = nullptr;
    hands_[0] = hands_[1] = nullptr;
    hand_tried_ = false;
    forget_banners();
    logo_ = nullptr;
    logo_tried_ = false;
    pad_art_ = nullptr;
    pad_art_tried_ = false;
    lines_art_ = nullptr;
    lines_art_tried_ = false;
    qr_ = nullptr;
    qr_tried_ = false;
    ripalda_ = nullptr;
    mark_tried_ = false;
    flags_ = nullptr;
    flags_tried_ = false;
    for (Texture *&t : menu_slot_tex_)
        t = nullptr;
}

void App::free_card_textures()
{
    for (Card *c : {&card_a_, &card_b_})
        for (Save &s : c->saves)
        {
            if (s.icon_tex)
                g_->free_texture(s.icon_tex);
            if (s.banner_tex)
                g_->free_texture(s.banner_tex);
            s.icon_tex = nullptr;
            s.banner_tex = nullptr;
        }
}

/* A game's picture, decoded off the render thread: nullptr while it decodes
 * (picture_loading() says so) or when there is none. */
Texture *App::picture(Texture *&tex, bool &tried, std::string &wait, std::string (Library::*where)(const Game &) const,
                      const Game &g)
{
    if (tex)
        return tex;
    if (!tried)
    {
        tried = true;
        wait = (lib_->*where)(g);
    }
    if (wait.empty())
        return nullptr;
    bool pending = false;
    tex = g_->texture_file_async(wait, &pending);
    if (!pending)
        wait.clear();
    return tex;
}

Texture *App::cover_of(Game &g)
{
    const bool had = g.cover != nullptr;
    Texture *t = picture(g.cover, g.cover_tried, g.cover_wait, &Library::cover_path, g);
    if (t && !had)
        g.cover_at = time_; /* it fades in */
    return t;
}

void App::launch_cover_now()
{
    /* After the display changed hands for the game (the launch screen's
     * textures went with the old device): the game's cover again at once, not
     * through the loader, which can't finish while Dolphin prepares its
     * graphics - the tile showed empty then. */
    if (!launch_ || launch_->cover)
        return;
    launch_->cover_tried = true;
    launch_->cover_wait.clear();
    const std::string path = lib_->cover_path(*launch_);
    if (!path.empty())
        launch_->cover = g_->texture_file(path);
    launch_->cover_at = -10.0; /* no fade: it was already showing */
}

Texture *App::disc_of(Game &g)
{
    return picture(g.disc, g.disc_tried, g.disc_wait, &Library::disc_path, g);
}

Texture *App::back_of(Game &g)
{
    return picture(g.back, g.back_tried, g.back_wait, &Library::back_path, g);
}

Texture *App::spine_of(Game &g)
{
    return picture(g.spine, g.spine_tried, g.spine_wait, &Library::spine_path, g);
}

Texture *App::save_icon(Save &s)
{
    if (!s.icon_tex && s.icon.size() == 32u * 32u * 4u)
    {
        const auto big = enlarge(s.icon, 32, 32, 3);
        s.icon_tex = g_->texture_rgba(big.data(), 96, 96);
    }
    return s.icon_tex;
}

Texture *App::save_banner(Save &s)
{
    if (!s.banner_tex && s.banner.size() == 96u * 32u * 4u)
    {
        const auto big = enlarge(s.banner, 96, 32, 3);
        s.banner_tex = g_->texture_rgba(big.data(), 288, 96);
    }
    return s.banner_tex;
}

/* ---- memory cards model ------------------------------------------------------------------ */

void App::scan_memory_cards()
{
    save_scan_.reset(); /* read now: a scan still running is stale */
    free_card_textures();
    load_cards(saves_dir_, card_a_, card_b_);
    finish_card_scan();
}

struct App::SaveScan
{
    std::atomic<bool> done{false};
    std::string dir;
    Card a, b;
    std::vector<WiiSave> wii;
};

namespace
{
void *save_scan_work(void *opaque)
{
    auto *handed = static_cast<std::shared_ptr<App::SaveScan> *>(opaque);
    const std::shared_ptr<App::SaveScan> scan = *handed;
    delete handed;
    load_cards(scan->dir, scan->a, scan->b);
    load_wii_saves(scan->dir, scan->wii);
    scan->done.store(true, std::memory_order_release);
    return nullptr;
}
} // namespace

void App::start_save_scan()
{
    auto scan = std::make_shared<SaveScan>();
    scan->dir = saves_dir_;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1u << 20);
    pthread_t thread;
    auto *handed = new std::shared_ptr<SaveScan>(scan);
    const bool started = pthread_create(&thread, &attr, save_scan_work, handed) == 0;
    pthread_attr_destroy(&attr);
    if (!started)
    {
        delete handed;
        save_scan_.reset();
        cards_scanned_ = wii_scanned_ = false; /* read on the spot instead */
        return;
    }
    pthread_detach(thread);
    save_scan_ = scan;
}

void App::take_save_scan()
{
    if (!save_scan_ || !save_scan_->done.load(std::memory_order_acquire))
        return;
    const std::shared_ptr<SaveScan> scan = std::move(save_scan_);
    save_scan_.reset();
    free_card_textures();
    card_a_ = std::move(scan->a);
    card_b_ = std::move(scan->b);
    finish_card_scan();
    free_wii_textures();
    wii_saves_ = std::move(scan->wii);
    finish_wii_scan();
}

void App::ensure_saves(bool wii)
{
    take_save_scan();
    if (save_scan_)
        return; /* still reading: the tab shows what it has */
    if (wii ? !wii_scanned_ : !cards_scanned_)
    {
        if (wii)
            scan_wii_saves();
        else
            scan_memory_cards();
    }
}

void App::finish_card_scan()
{
    cards_scanned_ = true;
    Card *cards[2] = {&card_a_, &card_b_};
    for (int i = 0; i < 2; ++i)
        mc_sel_[i] = std::clamp(mc_sel_[i], 0, std::max(0, int(cards[i]->saves.size()) - 1));
    if (!mc_focus_code_.empty())
    {
        bool found = false;
        for (int i = 0; i < 2 && !found; ++i)
            for (std::size_t j = 0; j < cards[i]->saves.size(); ++j)
                if (cards[i]->saves[j].game_code.compare(0, 4, mc_focus_code_, 0, 4) == 0)
                {
                    mc_card_ = i;
                    mc_sel_[i] = int(j);
                    found = true;
                    break;
                }
        mc_focus_code_.clear();
    }
    mc_lift_ = 0;
}

void App::move_memcard(int dx, int dy)
{
    Card *cards[2] = {&card_a_, &card_b_};
    const int cur = mc_card_;
    const int n = int(cards[cur]->saves.size());
    int &sel = mc_sel_[cur];
    const int col = sel % kMcCols, row = sel / kMcCols;
    const int moved_from = sel;
    if (dx != 0)
    {
        const int other = 1 - cur;
        const bool cross = dx > 0 ? (col == kMcCols - 1 || sel + 1 >= n) && cur == 0
                                  : col == 0 && cur == 1;
        if (cross)
        {
            /* Into the other card, on the same visible row. */
            const int vis_row = row - int(mc_scroll_[cur]);
            const int n_other = int(cards[other]->saves.size());
            const int target_row = int(mc_scroll_[other]) + vis_row;
            const int idx = target_row * kMcCols + (dx > 0 ? 0 : kMcCols - 1);
            mc_sel_[other] = std::clamp(idx, 0, std::max(0, n_other - 1));
            mc_card_ = other;
            mc_lift_ = 0.4f;
            sfx(Sound::MenuScroll);
            return;
        }
        if (dx > 0 && col < kMcCols - 1 && sel + 1 < n)
            ++sel;
        if (dx < 0 && col > 0)
            --sel;
    }
    if (dy != 0)
    {
        const int next = sel + dy * kMcCols;
        if (next >= 0 && next < n)
            sel = next;
        else if (dy > 0 && n > 0 && row < (n - 1) / kMcCols)
            sel = n - 1; /* the last, shorter row */
    }
    if (sel != moved_from)
    {
        mc_lift_ = 0.4f;
        sfx(Sound::MenuScroll);
    }
}

/* ---- input ------------------------------------------------------------------------------- */

bool App::nav(std::uint32_t bit, float &timer, double dt)
{
    if (!(held_ & bit))
    {
        timer = 0;
        return false;
    }
    if (!(prev_ & bit))
    {
        timer = 0.38f;
        return true;
    }
    timer -= float(dt);
    if (timer <= 0)
    {
        timer = 0.085f;
        return true;
    }
    return false;
}

/* The menu Wii Remote's buttons as the DualSense's (Porpoise's own menus only):
 * the D-pad navigates, A confirms (Cross), B goes back (Circle), + and - are
 * Triangle and Square (each screen's extra actions), 1 and 2 are L1 and R1
 * (the tabs). HOME does nothing here: it never reaches the console's own
 * menu, which only the PS button opens. */
std::uint32_t wii_menu_bits(std::uint16_t b)
{
    namespace db = porpoise::dolphinbar;
    using namespace porpoise::pad;
    std::uint32_t out = 0;
    if (db::menu_sideways())
    {
        /* Held sideways, the D-pad on the left: its Up points left. 1
         * confirms and 2 goes back; A Sort & Filter; + and - the tabs.
         * B is a shift for what the sideways Remote lacks: B + 1 Options
         * (Favorite, Done), B + 2 R2, B + - L2; B alone, let go without
         * another, Square (Details, Apply) for that frame. */
        static bool b_was = false, b_chord = false;
        const bool b_now = b & db::RbB;
        if (b & db::RbUp) out |= BtnLeft;
        if (b & db::RbDown) out |= BtnRight;
        if (b & db::RbLeft) out |= BtnDown;
        if (b & db::RbRight) out |= BtnUp;
        if (b & db::RbA) out |= BtnTriangle;
        if (b_now)
        {
            if (!b_was)
                b_chord = false;
            if (b & db::RbOne) out |= BtnOptions;
            if (b & db::RbTwo) out |= BtnR2;
            if (b & db::RbMinus) out |= BtnL2;
            if (b & (db::RbOne | db::RbTwo | db::RbMinus | db::RbPlus | db::RbA | db::RbUp | db::RbDown |
                     db::RbLeft | db::RbRight))
                b_chord = true;
        }
        else
        {
            if (b_was && !b_chord)
                out |= BtnSquare;
            if (b & db::RbOne) out |= BtnCross;
            if (b & db::RbTwo) out |= BtnCircle;
            if (b & db::RbMinus) out |= BtnL1;
        }
        if (b & db::RbPlus) out |= BtnR1;
        b_was = b_now;
        return out;
    }
    if (b & db::RbUp) out |= BtnUp;
    if (b & db::RbDown) out |= BtnDown;
    if (b & db::RbLeft) out |= BtnLeft;
    if (b & db::RbRight) out |= BtnRight;
    if (b & db::RbA) out |= BtnCross;   /* confirm */
    if (b & db::RbB) out |= BtnCircle;  /* back */
    if (b & db::RbPlus) out |= BtnR1;   /* the next tab or page */
    if (b & db::RbMinus) out |= BtnL1;  /* the one before */
    if (b & db::RbOne) out |= BtnTriangle; /* Sort & Filter */
    if (b & db::RbTwo) out |= BtnR2;    /* R2's things: GameCube or Wii saves, the next page */
    /* HOME: nothing */
    const db::MenuNunchuk nc = db::menu_nunchuk();
    if (nc.present)
    {
        if (nc.c) out |= BtnOptions; /* Favorite */
        if (nc.z) out |= BtnSquare;  /* Details */
        if (nc.x < -0.55f) out |= BtnLeft;
        if (nc.x > 0.55f) out |= BtnRight;
        if (nc.y > 0.55f) out |= BtnUp;
        if (nc.y < -0.55f) out |= BtnDown;
    }
    return out;
}
std::uint32_t gc_menu_bits()
{
    namespace gc = porpoise::ns2gc;
    using namespace porpoise::pad;
    const gc::Pad p = gc::pad();
    if (!p.connected)
        return 0;
    static const struct
    {
        std::uint32_t gc, pad;
    } kMap[] = {{gc::GcA, BtnCross},    {gc::GcB, BtnCircle}, {gc::GcX, BtnSquare},  {gc::GcY, BtnTriangle},
                {gc::GcStart, BtnOptions}, {gc::GcZ, BtnR1},  {gc::GcZL, BtnL1},     {gc::GcL, BtnL2},
                {gc::GcR, BtnR2},       {gc::GcUp, BtnUp},    {gc::GcDown, BtnDown}, {gc::GcLeft, BtnLeft},
                {gc::GcRight, BtnRight}};
    std::uint32_t out = 0;
    for (const auto &m : kMap)
        if (p.buttons & m.gc)
            out |= m.pad;
    /* the triggers pressed most of the way: as their click */
    if (p.lt > 0x6000) out |= BtnL2;
    if (p.rt > 0x6000) out |= BtnR2;
    /* the control stick (libretro's way, y + down) */
    constexpr int kPush = int(0.55f * 32767);
    if (p.lx < -kPush) out |= BtnLeft;
    if (p.lx > kPush) out |= BtnRight;
    if (p.ly < -kPush) out |= BtnUp;
    if (p.ly > kPush) out |= BtnDown;
    return out;
}

void App::follow_last_controller(std::uint32_t pad_held, std::uint32_t wii_held, bool remote_there,
                                 std::uint32_t gc_held)
{
    bool wii = g_->wii_buttons(), gc = g_->gc_buttons();
    if (wii_held & ~wii_prev_)
        wii = true, gc = false;
    if (gc_held & ~gc_prev_)
        gc = true, wii = false;
    if (pad_held & ~raw_held_) /* raw_held_ is still the last frame's */
        wii = gc = false;
    if (!remote_there)
        wii = false; /* the Wii Remote is gone */
    if (!porpoise::ns2gc::connected())
        gc = false;
    wii_prev_ = wii_held;
    gc_prev_ = gc_held;
    g_->set_wii_buttons(wii);
    g_->set_gc_buttons(gc);
    g_->set_wii_sideways(wii && porpoise::dolphinbar::menu_sideways());
}

App::Action App::update(const Input &in, double dt)
{
    time_ += dt;
    prev_ = held_;
    held_ = in.held;
    /* The menu Wii Remote beside the DualSense (not on the controller test,
     * where its buttons are what's being tested). */
    if (screen_ != Screen::Controllers)
    {
        const std::uint32_t wii = wii_menu_bits(porpoise::dolphinbar::menu_buttons());
        /* the Switch 2's GameCube controller too; its buttons count as the
         * DualSense's for the prompts */
        const std::uint32_t gc = gc_menu_bits();
        follow_last_controller(in.held, wii, porpoise::dolphinbar::menu_remote() >= 0, gc);
        held_ |= wii | gc;
    }
    else
    {
        /* The test screen: the Switch 2's GameCube controller works it too,
         * except on its own page, where its buttons are what's tested. */
        const std::uint32_t gc = ctl_sel_ == 7 ? 0 : gc_menu_bits();
        follow_last_controller(in.held, 0, porpoise::dolphinbar::menu_remote() >= 0, gc);
        held_ |= gc;
    }
    raw_prev_ = raw_held_;
    raw_held_ = in.held;
    stick_x_ = in.stick_x;
    stick_y_ = in.stick_y;
    if (in.stick_x < -0.55f) held_ |= BtnLeft;
    if (in.stick_x > 0.55f) held_ |= BtnRight;
    if (in.stick_y < -0.55f) held_ |= BtnUp;
    if (in.stick_y > 0.55f) held_ |= BtnDown;

    /* L1 alone changes tab as it's let go; L1 + Square is the account panel. */
    if (pressed(BtnL1))
    {
        l1_armed_ = true;
        l1_chord_ = false;
    }
    l1_tap_ = false;
    if (!(held_ & BtnL1))
    {
        l1_tap_ = l1_armed_ && !l1_chord_;
        l1_armed_ = false;
    }

    const bool left = nav(BtnLeft, rep_left_, dt), right = nav(BtnRight, rep_right_, dt);
    const bool up = nav(BtnUp, rep_up_, dt), down = nav(BtnDown, rep_down_, dt);
    auto &games = lib_->games();
    Action action = Action::None;
    home_pad_sync(); /* the Revolution look's pointer */
    /* The DolphinBar in the menus: the menu Remote (light), or everything for
     * Controllers & Accessories (full); never while a game has it
     * (porpoise_dolphinbar.hpp). And the setting for the Remotes' speakers. */
    porpoise::dolphinbar::monitor(true);
    /* Full only on the test screen itself: the Settings section shows what
     * the light set-up knows too, and switching the Remote over (its
     * MotionPlus on and off, other reports) each time the section came into
     * view held up the menu Remote's input (a PS5 test). */
    porpoise::dolphinbar::set_full_detail(screen_ == Screen::Controllers);
    porpoise::usbhid::set_speaker_enabled(settings_->real_wiimote_speaker);
    if (tab_ == Tab::Settings && screen_ == Screen::Main)
        refresh_controller_rows();
    poll_texture_packs(); /* Settings > Textures: the packs' counts, when counted */
    if (update_nand_job())
        return Action::None; /* a Wii's system data being imported: the menus wait */
    {
        Action out = Action::None;
        if (update_sys_update(out) || out != Action::None)
            return out; /* Wii System Update's list being asked for: the menus wait */
    }
    pump_banners();  /* Wii discs' tiles and banners, as they come */
    g_->set_theme_fonts(fonts_for(*settings_)); /* a theme's own letters, once they're made */
    sc_tick(dt);

    const bool calm = settings_->reduced_motion;
    const float rate = calm ? 40.0f : 14.0f;
    scroll_ = smooth(scroll_, float(selected_), dt, rate);
    lift_ = smooth(lift_, 1.0f, dt, 10.0f);
    mc_lift_ = smooth(mc_lift_, 1.0f, dt, 10.0f);
    dock_lift_ = smooth(dock_lift_, 1.0f, dt, 10.0f);
    dock_glide_ = calm ? float(dock_sel_) : smooth(dock_glide_, float(dock_sel_), dt, 14.0f);
    dock_open_ = calm ? (dock_focus_ ? 1.0f : 0.0f) : smooth(dock_open_, dock_focus_ ? 1.0f : 0.0f, dt, 10.0f);
    tab_anim_ = calm ? 0.0f : std::max(0.0f, tab_anim_ - float(dt) * 3.6f);
    screen_anim_ = calm ? 0.0f : std::max(0.0f, screen_anim_ - float(dt) * 4.5f);
    dialog_.anim = calm ? 1.0f : std::min(1.0f, dialog_.anim + float(dt) * 6.0f);
    flip_anim_ = calm ? 0.0f : std::max(0.0f, flip_anim_ - float(dt) * 2.4f);
    swipe_anim_ = calm ? 0.0f : std::max(0.0f, swipe_anim_ - float(dt) * 3.4f);
    zoom_anim_ = calm ? 0.0f : std::max(0.0f, zoom_anim_ - float(dt) * 1.9f);
    right_x_ = std::fabs(in.right_x) > 0.15f ? in.right_x : 0.0f;
    {
        const float target = (box_back_ ? kPi : 0.0f) + right_x_ * 0.9f;
        box_yaw_ = calm ? target : smooth(box_yaw_, target, dt, 7.0f);
    }
    if (pill_x_ >= 0 && tab_w_[0] > 0)
    {
        const float target = tab_x_[int(tab_)], width = tab_w_[int(tab_)];
        pill_x_ = calm ? target : smooth(pill_x_, target, dt, 16.0f);
        pill_w_ = calm ? width : smooth(pill_w_, width, dt, 16.0f);
    }

    if (updating())
    {
        /* The update holds the screen; when it's in, Cross closes Porpoise,
         * as it does by itself a few seconds later. */
        if (update_phase_ == 4 && (pressed(BtnCross) || time_ - update_done_time_ > 8.0))
            return Action::Quit;
        return Action::None;
    }
    if (update_failed_)
    {
        update_failed_ = false;
        open_dialog(DialogKind::Info, tr("The update didn't finish"), tr(update_error_), "");
    }
    if (testing_notice_ && !dialog_.open && !acct_.open && !share_.open && !dns_.open && !hub_.open && !sys_.open && !code_.open &&
        screen_ == Screen::Main &&
        wizard_step_ < 0 && !welcome_after_dialog_)
    {
        /* A test build's notice, once the start's own messages are seen. */
        testing_notice_ = false;
        const bool alpha = kChannel == Channel::Alpha;
        open_dialog(DialogKind::Testing, alpha ? tr("This is an alpha build") : tr("This is a beta build"),
                    (alpha ? tr("Porpoise is still being worked on in this build: expect problems, and even crashes.")
                           : tr("This build is still being tested: some things may not work as they should.")) +
                        "\n" +
                        tr("If something goes wrong, go to Settings > About > Report a Bug to save the logs, then "
                           "share them in the RIPALDA Discord's bug reports with the game and what happened. "
                           "Thanks for testing!"),
                    "");
        /* Seen: not again until the next test build. */
        settings_->testing_notice = kNoticeId;
        settings_->save(settings_path_);
    }
    if (dialog_.open)
        return update_dialog(left, right);
    if (acct_.open)
        return update_account(up, down, left, right);
    if (share_.open)
        return update_share(up, down, left, right);
    if (dns_.open)
        return update_dns(up, down, left, right);
    if (sys_.open)
        return update_system(up, down);
    if (code_.open)
        return update_code(up, down, left, right);
    if (hub_.open)
        return update_hub(up, down, left, right);
    if (screen_ == Screen::Achievements)
        return update_achievements_screen(up, down);
    if (screen_ == Screen::Welcome)
        return update_welcome(left, right, dt);
    if (screen_ == Screen::Browse)
        return update_browser(up, down);
    if (screen_ == Screen::TileArt)
        return update_tile_art(dt);
    if (screen_ == Screen::GameSettings)
        return update_settings(up, down, left, right);
    if (screen_ == Screen::Mapping)
        return update_mapping(up, down, left, right);
    if (screen_ == Screen::WiiGuide)
        return update_wii_guide(left, right);
    if (screen_ == Screen::WiiSetup)
        return update_wii_setup(up, down, left, right);
    if (screen_ == Screen::States)
        return update_states(left, right);
    if (screen_ == Screen::Shots)
        return update_shots(left, right, up, down);
    if (screen_ == Screen::Controllers)
        return update_controllers(up, down, left, right);
    if (screen_ == Screen::Sort)
    {
        /* Sort & filter: the order, which games, covers and info now. Changes
         * show at once behind the panel. */
        if (up || down)
        {
            sort_row_ = (sort_row_ + (up ? 2 : 1)) % 3;
            sfx(Sound::MenuScroll);
        }
        const int step = left ? -1 : right ? +1 : 0;
        if (step != 0 && sort_row_ < 2)
        {
            const std::string key = lib_->shown() > 0 ? Library::key_of(games[std::size_t(selected_)]) : "";
            if (sort_row_ == 0)
                lib_->sort(Library::Sort((int(lib_->sort_order()) + 4 + step) % 4));
            else
                lib_->set_show(Library::Show((int(lib_->show()) + 4 + step) % 4));
            keep_selection(key);
            lib_->save();
            sfx(Sound::MenuScroll);
        }
        if (pressed(BtnCross) && sort_row_ == 2)
        {
            sfx(Sound::LaunchGame);
            screen_ = Screen::Main;
            return Action::FetchCovers;
        }
        if (pressed(BtnCross) && sort_row_ < 2)
        {
            /* Cross on a choice: the next one, as Right does. */
            const std::string key = lib_->shown() > 0 ? Library::key_of(games[std::size_t(selected_)]) : "";
            if (sort_row_ == 0)
                lib_->sort(Library::Sort((int(lib_->sort_order()) + 1) % 4));
            else
                lib_->set_show(Library::Show((int(lib_->show()) + 1) % 4));
            keep_selection(key);
            lib_->save();
            sfx(Sound::MenuScroll);
        }
        if (pressed(BtnCircle | BtnTriangle))
            screen_ = Screen::Main;
        return action;
    }
    if ((held_ & BtnL1) && pressed(BtnSquare) && ra_state_)
    {
        /* L1 + Square: RetroAchievements (the tab stays where it was). */
        l1_chord_ = true;
        open_account();
        return Action::None;
    }
    if (screen_ == Screen::Details && pressed(BtnSquare) && lib_->shown() > 0)
    {
        /* Square on Details: the game's achievements, as last kept. */
        const Game &game = games[std::size_t(std::clamp(selected_, 0, lib_->shown() - 1))];
        if (details_achievements(game).valid())
        {
            open_achievements(game);
            return Action::None;
        }
    }
    if (starcube())
    {
        bool handled = false;
        const Action a = update_starcube(left, right, up, down, handled);
        if (handled)
            return a;
    }
    if (screen_ == Screen::Details && revolution())
        return update_rev_details(left, right, up, down, dt);
    if (screen_ == Screen::Details)
    {
        /* L2 / R2: the previous / next game, without leaving Details. Held,
         * they keep going, a little slower than the D-pad so each swipe shows. */
        auto trigger = [&](std::uint32_t bit, float &timer) {
            if (!(held_ & bit))
            {
                timer = 0;
                return false;
            }
            if (!(prev_ & bit))
            {
                timer = 0.42f;
                return true;
            }
            timer -= float(dt);
            if (timer <= 0)
            {
                timer = 0.26f;
                return true;
            }
            return false;
        };
        const bool prev_game = trigger(BtnL2, rep_l2_), next_game = trigger(BtnR2, rep_r2_);
        if ((prev_game || next_game) && !games.empty())
        {
            const int dir = next_game ? +1 : -1;
            const int to = selected_ + dir;
            if (to >= 0 && to < lib_->shown())
            {
                swipe_from_ = selected_;
                selected_ = to;
                swipe_dir_ = dir;
                swipe_anim_ = calm ? 0.0f : 1.0f;
                flip_anim_ = 0;
                box_back_ = false;
                box_yaw_ = calm ? 0.0f : float(dir) * 1.25f; /* it swings in and settles */
                details_custom_ = !Settings::keys_in(game_settings_path(games[std::size_t(selected_)])).empty();
                count_states(&games[std::size_t(selected_)]);
                details_shots_ = count_shots(games[std::size_t(selected_)]);
                details_row_ = std::min(details_row_, details_shots_ > 0 ? 4 : 3);
                lib_->set_selected(Library::key_of(games[std::size_t(selected_)]));
                sfx(Sound::GameRow);
            }
        }
        if (up && details_row_ > 0)
        {
            --details_row_;
            sfx(Sound::MenuScroll);
        }
        if (down && details_row_ < (details_shots_ > 0 ? 4 : 3))
        {
            ++details_row_;
            sfx(Sound::MenuScroll);
        }
        if (pressed(BtnCircle))
        {
            open_screen(Screen::Main);
            sfx(Sound::DetailsFlip);
        }
        if (pressed(BtnTriangle))
        {
            box_back_ = !box_back_; /* the back of the box */
            sfx(Sound::DetailsFlip);
        }
        if (pressed(BtnOptions) && !games.empty())
        {
            lib_->toggle_favourite(games[std::size_t(selected_)]);
            sfx(games[std::size_t(selected_)].favourite ? Sound::LaunchGame : Sound::MovingTab);
        }
        if (pressed(BtnCross) && !games.empty())
        {
            if (details_row_ == 0)
            {
                action = start_game(&games[selected_], "");
                sfx(Sound::LaunchGame);
            }
            else if (details_row_ == 1)
                open_states(&games[std::size_t(selected_)]);
            else if (details_row_ == 2)
            {
                open_game_settings(games[std::size_t(selected_)]);
                sfx(Sound::MenuScroll);
            }
            else if (details_row_ == 4)
                open_shots(Library::key_of(games[std::size_t(selected_)]), Screen::Details);
            else
            {
                /* Straight to this game's save, when there is one. */
                open_screen(Screen::Main);
                mc_focus_code_ = games[selected_].id;
                set_tab(int(Tab::MemoryCards), +1);
            }
        }
        return action;
    }

    if (l1_tap_)
        set_tab((int(tab_) + 2) % 3, -1);
    if (pressed(BtnR1))
        set_tab((int(tab_) + 1) % 3, +1);

    if (tab_ == Tab::Library && (!games.empty() || revolution()))
    {
        /* Only the games Sort & filter shows (the first shown() of the list). */
        const int shown = lib_->shown();
        selected_ = std::clamp(selected_, 0, std::max(0, shown - 1));
        bool play = false, details = false, fav = false;
        if (revolution() && settings_->ui_layout == 0)
            update_home(left, right, up, down, play, details, fav, dt);
        else
        {
            if (dock_focus_ && !dock_shown())
                dock_focus_ = false;
            if (dock_focus_)
            {
                /* Recently Played: left and right along it, Cross plays,
                 * Square shows the game's details, up or Circle goes back. */
                const std::vector<int> recent = recent_games();
                dock_sel_ = std::clamp(dock_sel_, 0, int(recent.size()) - 1);
                if ((left && dock_sel_ > 0) || (right && dock_sel_ + 1 < int(recent.size())))
                {
                    dock_sel_ += left ? -1 : 1;
                    dock_lift_ = 0.4f;
                    sfx(Sound::GameRow);
                }
                if (up || pressed(BtnCircle))
                {
                    dock_focus_ = false;
                    sfx(Sound::MenuScroll);
                }
                else if (pressed(BtnCross))
                {
                    action = start_game(&games[std::size_t(recent[std::size_t(dock_sel_)])], "");
                    sfx(Sound::LaunchGame);
                }
                else if (pressed(BtnSquare) && recent[std::size_t(dock_sel_)] < shown)
                {
                    selected_ = recent[std::size_t(dock_sel_)];
                    scroll_ = float(selected_);
                    dock_focus_ = false;
                    details = true;
                }
            }
            else
            {
                if (update_view_nav(left, right, up, down, dt))
                {
                    lift_ = 0.4f;
                    sfx(Sound::GameRow);
                }
                else if (down && dock_shown())
                {
                    dock_focus_ = true;
                    dock_sel_ = 0;
                    dock_lift_ = 0.4f;
                    sfx(Sound::MenuScroll);
                }
                play = pressed(BtnCross);
                details = pressed(BtnSquare);
                fav = pressed(BtnOptions);
            }
        }
        if (play && shown > 0)
        {
            action = start_game(&games[selected_], "");
            sfx(Sound::LaunchGame);
        }
        if (fav && shown > 0)
        {
            lib_->toggle_favourite(games[std::size_t(selected_)]);
            sfx(games[std::size_t(selected_)].favourite ? Sound::LaunchGame : Sound::MovingTab);
        }
        if (details && shown > 0)
        {
            open_screen(Screen::Details);
            sfx(Sound::DetailsFlip);
            flip_anim_ = 1.0f;
            box_back_ = false;
            box_yaw_ = calm ? 0.0f : -2.0f * kPi; /* one full turn on the way in */
            details_row_ = 0;
            details_custom_ = !Settings::keys_in(game_settings_path(games[std::size_t(selected_)])).empty();
            count_states(&games[std::size_t(selected_)]);
            details_shots_ = count_shots(games[std::size_t(selected_)]);
            if (revolution())
            {
                rd_focus_ = 0;
                rd_info_ = false;
                opened_tile(games[std::size_t(selected_)]);
                start_zoom(+1);
            }
        }
        if (pressed(BtnTriangle))
        {
            sfx(Sound::MenuScroll);
            screen_ = Screen::Sort;
            sort_row_ = 0;
        }
        if (shown > 0)
            lib_->set_selected(Library::key_of(games[selected_]));
    }
    else if (tab_ == Tab::Settings)
        action = update_settings(up, down, left, right);
    else if (tab_ == Tab::MemoryCards && mc_view() == 2)
    {
        /* By game: every save in one list. */
        if (cards_scanned_ || wii_scanned_)
            update_saves_by_game(up, down);
        if (pressed(BtnCircle))
            set_tab(int(Tab::Library), -1);
    }
    else if (tab_ == Tab::MemoryCards)
    {
        /* L2 / R2: the GameCube memory cards or the Wii saves. */
        if (pressed(BtnL2 | BtnR2))
        {
            mc_wii_ = !mc_wii_;
            sfx(Sound::MovingTab);
            tab_anim_ = 0.6f;
            tab_dir_ = pressed(BtnR2) ? +1 : -1;
        }
        if (mc_wii_)
            update_wii_saves(left, right, up, down);
        else if (cards_scanned_ && mc_view() == 4)
            update_card_classic(left, right, up, down);
        else if (cards_scanned_ && mc_view() == 1)
        {
            /* Blocks: up and down the card's saves, left and right the other card. */
            const int n = int((mc_card_ == 0 ? card_a_ : card_b_).saves.size());
            int &sel = mc_sel_[mc_card_];
            const int before = sel;
            if (up && sel > 0)
                --sel;
            if (down && sel + 1 < n)
                ++sel;
            if (left || right)
            {
                mc_card_ = 1 - mc_card_;
                tab_anim_ = 0.5f;
                tab_dir_ = right ? +1 : -1;
                sfx(Sound::MovingTab);
            }
            else if (sel != before)
                sfx(Sound::MenuScroll);
            if (pressed(BtnTriangle))
                ask_delete_save();
            if (pressed(BtnSquare))
                ask_copy_save();
            if (pressed(BtnOptions))
                export_save_to_usb();
        }
        else if (cards_scanned_)
        {
            if (left)
                move_memcard(-1, 0);
            if (right)
                move_memcard(+1, 0);
            if (up)
                move_memcard(0, -1);
            if (down)
                move_memcard(0, +1);
            if (pressed(BtnTriangle))
                ask_delete_save();
            if (pressed(BtnSquare))
                ask_copy_save();
            if (pressed(BtnOptions))
                export_save_to_usb();
        }
        if (pressed(BtnCircle))
            set_tab(int(Tab::Library), -1);
    }
    return action;
}

void App::set_tab(int tab, int dir)
{
    if (tab == int(tab_))
        return;
    if (tab_ == Tab::Settings && screen_ == Screen::Main)
    {
        /* Leaving Settings with changes not applied: Apply or Discard first. */
        leave_tab_ = tab;
        leave_dir_ = dir;
        if (ask_before_leaving())
            return;
        leave_tab_ = -1;
    }
    tab_ = Tab(tab);
    sfx(Sound::MovingTab);
    tab_dir_ = dir;
    tab_anim_ = 1.0f;
    if (tab_ == Tab::MemoryCards)
        start_save_scan(); /* fresh from disk each visit, read on a worker thread */
    if (tab_ == Tab::Settings)
    {
        shots_total_ = -1; /* looked at again each visit */
        bios_looked_ = false;
        on_rail_ = true;
        build_settings(); /* game counts and folders may have changed */
    }
}

void App::open_screen(Screen s)
{
    screen_ = s;
    screen_anim_ = 1.0f;
}

/* ---- dialogs ------------------------------------------------------------------------------ */

void App::open_dialog(DialogKind kind, const std::string &title, const std::string &message, const std::string &yes,
                      bool danger)
{
    close_dialog(); /* a picture left by the last one */
    dialog_.open = true;
    dialog_.kind = kind;
    dialog_.no.clear();
    dialog_.checks.clear();
    dialog_.title = title;
    dialog_.message = message;
    dialog_.yes = yes;
    dialog_.danger = danger;
    dialog_.choice = 0; /* Cancel first: nothing happens by accident */
    dialog_.anim = 0;
    sfx(Sound::DetailsFlip);
}

/* A round mark: a tick on green, or "!" on amber for something to do. The
 * tick is two bars, since the fonts have no tick character. */
void App::draw_check_mark(float cx, float cy, float size, bool ok)
{
    Gfx &g = *g_;
    const float r = size * 0.5f;
    g.panel(cx - r, cy - r, size, size, ok ? rgba(0x2FBF71) : rgba(0xF2A93B), 1.0f, r);
    if (!ok)
    {
        g.text_mid(Font::ExtraBold, size * 0.72f, cx, cy + size * 0.02f, rgba(0x2A1600), Align::Center, "!");
        return;
    }
    auto bar = [&](float x0, float y0, float x1, float y1) {
        const float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
        const float t = size * 0.075f, nx = -dy / len * t, ny = dx / len * t;
        const Corner c[4] = {{x0 + nx, y0 + ny, 1}, {x1 + nx, y1 + ny, 1}, {x1 - nx, y1 - ny, 1}, {x0 - nx, y0 - ny, 1}};
        g.quad3d(nullptr, c, len, t * 2, kWhite, t, false, false);
    };
    bar(cx - size * 0.22f, cy + size * 0.01f, cx - size * 0.06f, cy + size * 0.17f);
    bar(cx - size * 0.06f, cy + size * 0.17f, cx + size * 0.23f, cy - size * 0.15f);
}

void App::offer_found_folder(const std::string &path, const std::string &place, bool handmade)
{
    move_target_ = path;
    open_dialog(DialogKind::UseFolder, trf("Porpoise's folder is on {place}", {{"place", tr(place)}}),
                handmade ? trf("There's a folder for Porpoise there ({path}). Use it to keep everything there: "
                               "games, saves, covers and texture packs. Porpoise closes; open it again.",
                               {{"path", path}})
                         : tr("Porpoise found its folder there, with settings and saves. Use it to pick up where "
                              "you left off. Porpoise closes; open it again."),
                tr("Use It"));
    dialog_.no = tr("Start Fresh");
    dialog_.choice = 1; /* Use it first */
    welcome_after_dialog_ = true;
}

void App::show_sandbox_notice(const std::string &title, const std::string &message)
{
    open_dialog(DialogKind::SandboxNotice, title, message, tr("OK"));
    dialog_.no = tr("Don't Show Again");
    dialog_.choice = 1; /* OK first */
}

void App::offer_jailbreak_retry()
{
    open_dialog(DialogKind::JailbreakClosed, tr("Porpoise stayed in the sandbox"),
                tr("Last time, Porpoise closed while your jailbreak was freeing it from the app sandbox, so this "
                   "time it didn't ask. It can't see /data or USB drives, but games in /app0/porpoise/games work. "
                   "Stay in the Sandbox to always start this way (change it in Settings > Storage), or try again."),
                tr("Stay in the Sandbox"));
    dialog_.no = tr("Try Again");
    dialog_.choice = 1;
}

void App::close_dialog()
{
    dialog_.open = false;
    if (dialog_.picture)
    {
        g_->free_texture(dialog_.picture);
        dialog_.picture = nullptr;
    }
}

App::Action App::update_dialog(bool left, bool right)
{
    if (!dialog_.yes.empty())
    {
        if (left && dialog_.choice != 0)
        {
            dialog_.choice = 0;
            sfx(Sound::MenuScroll);
        }
        if (right && dialog_.choice != 1)
        {
            dialog_.choice = 1;
            sfx(Sound::MenuScroll);
        }
    }
    if (pressed(BtnCircle))
    {
        if (dialog_.kind == DialogKind::ApplyChanges)
        {
            leave_tab_ = -1; /* keep editing */
            leave_game_settings_ = false;
        }
        if (dialog_.kind == DialogKind::Wizard && wizard_step_ >= 0)
        {
            /* Circle: what is set stays; on to the next question. */
            close_dialog();
            wizard_next();
            if (wizard_step_ < 0)
            {
                rescan_after_apply_ = wizard_rescan_;
                return Action::SettingsChanged;
            }
            return Action::None;
        }
        close_dialog();
        if (welcome_after_dialog_)
        {
            welcome_after_dialog_ = false;
            start_welcome();
        }
        return Action::None;
    }
    if (pressed(BtnCross))
    {
        const DialogKind kind = dialog_.kind;
        const int choice = dialog_.choice;
        close_dialog();
        if (kind == DialogKind::UseFolder && welcome_after_dialog_)
        {
            welcome_after_dialog_ = false;
            if (choice != 1)
            {
                start_welcome(); /* Start fresh */
                return Action::None;
            }
        }
        if (kind == DialogKind::Wizard && wizard_step_ >= 0)
        {
            /* The answer is kept at once (the first start's own settings). */
            const bool yes = choice == 1;
            switch (wizard_step_)
            {
            case 0:
                settings_->stay_sandboxed = !yes;
                break;
            case 1:
                wizard_rescan_ |= settings_->auto_search != yes;
                settings_->auto_search = yes;
                break;
            case 2:
                settings_->download_covers = settings_->download_info = yes;
                break;
            case 3:
                settings_->button_layout = yes ? porpoise::pad::LayoutPlayStation : porpoise::pad::LayoutGameCube;
                break;
            }
            settings_->save(settings_path_);
            const bool rescan = wizard_rescan_;
            wizard_next();
            if (wizard_step_ < 0)
            {
                rescan_after_apply_ = rescan;
                return Action::SettingsChanged; /* main: stay in the sandbox, buttons, covers, the search */
            }
            return Action::None;
        }
        if (kind == DialogKind::ApplyChanges)
        {
            const Action a = choice == 1 ? apply_pending() : Action::None;
            if (choice != 1)
                discard_pending();
            finish_leaving();
            return a;
        }
        if (kind == DialogKind::RestartPorpoise)
            return choice == 1 ? Action::Restart : Action::None;
        if (kind == DialogKind::JailbreakClosed)
        {
            if (choice == 1)
            {
                settings_->stay_sandboxed = true;
                build_settings();
                return Action::StayInSandbox;
            }
            return Action::RetryJailbreak;
        }
        if (kind == DialogKind::SandboxNotice)
        {
            if (choice == 0)
            {
                /* Don't show again: back on from Settings > Games. */
                settings_->sandbox_notice = false;
                settings_->save(settings_path_);
                build_settings();
            }
            return Action::None;
        }
        if (kind == DialogKind::Resume && resume_game_)
        {
            /* Resume, or Start Over: the saved spot goes and the game boots afresh. */
            Game *g = resume_game_;
            resume_game_ = nullptr;
            if (choice != 1)
            {
                const std::string resume = porpoise::states::resume_path(Library::key_of(*g));
                std::remove(resume.c_str());
                std::remove((resume.substr(0, resume.size() - 6) + ".png").c_str());
            }
            return start_game(g, "", true);
        }
        if (!dialog_.yes.empty() && choice == 1)
            return confirm_dialog(kind);
    }
    return Action::None;
}

App::Action App::confirm_dialog(DialogKind kind)
{
    switch (kind)
    {
    case DialogKind::ResetAll:
        settings_->reset();
        draft_ = base_ = *settings_; /* changes not applied go too */
        settings_->save(settings_path_);
        settings_->write_core_options(options_path_);
        apply_language(settings_->ui_language, data_dir_ + "/lang");
        build_settings();
        return Action::SettingsChanged;
    case DialogKind::MoveData:
        if (move_pick_ >= 0 && move_pick_ < int(move_places_.size()))
        {
            move_target_ = move_places_[std::size_t(move_pick_)].second;
            return Action::MoveData;
        }
        return Action::None;
    case DialogKind::UseFolder:
        return move_target_.empty() ? Action::None : Action::UseFolder;
    case DialogKind::NandImport:
        start_nand_import();
        return Action::None;
    case DialogKind::NandRemove:
        remove_nand();
        return Action::None;
    case DialogKind::SysUpdate:
        start_sys_update();
        return Action::None;
    case DialogKind::CoversAgain:
        covers_again_.clear();
        for (const Game &g : lib_->games())
            if (!g.id.empty() && std::find(covers_again_.begin(), covers_again_.end(), g.id) == covers_again_.end())
                covers_again_.push_back(g.id);
        return covers_again_.empty() ? Action::None : Action::CoversAgain;
    case DialogKind::Reinitialize:
    {
        /* Settings wiped (games' own too); games, folders, saves and states
         * stay. Then Porpoise starts again as it did the first time. */
        settings_->reset();
        draft_ = base_ = *settings_;
        settings_->setup_checked = false;
        if (DIR *d = opendir((data_dir_ + "/game-settings").c_str()))
        {
            while (dirent *e = readdir(d))
            {
                const std::string n = e->d_name;
                if (n.size() > 4 && n.compare(n.size() - 4, 4, ".ini") == 0)
                    std::remove((data_dir_ + "/game-settings/" + n).c_str());
            }
            closedir(d);
        }
        settings_->save(settings_path_);
        settings_->write_core_options(options_path_);
        apply_language(settings_->ui_language, data_dir_ + "/lang");
        return Action::Reinitialize;
    }
    case DialogKind::ResetGame:
        if (game_for_)
        {
            game_keys_.clear();
            std::remove(game_settings_path(*game_for_).c_str());
            game_ = *settings_;
            game_base_ = game_;
            game_keys_base_.clear();
            build_game_settings();
        }
        return Action::None;
    case DialogKind::InstallUpdate:
        update_phase_ = 2;
        update_done_ = 0;
        update_total_ = latest_size_;
        return Action::InstallUpdate;
    case DialogKind::InstallVersion:
        update_phase_ = 2;
        update_done_ = 0;
        update_total_ = version_pick_ >= 0 && version_pick_ < int(version_sizes_.size())
                            ? version_sizes_[std::size_t(version_pick_)]
                            : 0;
        return Action::InstallVersion;
    case DialogKind::CopyGame:
        start_game_copy(browse_copy_name_);
        return Action::None;
    case DialogKind::DeleteShot:
        delete_shot();
        return Action::None;
    case DialogKind::DeleteState:
        if (states_game_)
        {
            porpoise::states::remove(Library::key_of(*states_game_), states_sel_);
            load_slots(states_game_);
            count_states(states_game_);
        }
        return Action::None;
    case DialogKind::DeleteWiiSave:
        if (wii_sel_ >= 0 && wii_sel_ < int(wii_saves_.size()))
        {
            if (!delete_wii_save(wii_saves_[std::size_t(wii_sel_)]))
                open_dialog(DialogKind::Info, tr("Could not delete the save"),
                            trf("Its folder could not be removed: {path}",
                                {{"path", wii_saves_[std::size_t(wii_sel_)].data_dir}}),
                            "");
            wii_scanned_ = false;
            save_scan_.reset(); /* a scan already running read the old state */
        }
        return Action::None;
    case DialogKind::DeleteSave:
        if (Save *s = focused_save())
        {
            const std::string path = s->path;
            if (std::remove(path.c_str()) != 0)
            {
                open_dialog(DialogKind::Info, tr("Could not delete the save"),
                            trf("The file could not be removed: {path}", {{"path", path}}), "");
                return Action::None;
            }
            cards_scanned_ = false;
            save_scan_.reset(); /* a scan already running read the old state */
        }
        return Action::None;
    case DialogKind::CopySave:
        if (Save *s = focused_save())
        {
            const std::string target = copy_target(*s);
            const std::string dir = target.substr(0, target.rfind('/'));
            /* The other card's folder, and its parents. */
            for (std::size_t i = 1; i <= dir.size(); ++i)
                if (i == dir.size() || dir[i] == '/')
                    mkdir(dir.substr(0, i).c_str(), 0777);
            bool ok = false;
            if (std::FILE *in = std::fopen(s->path.c_str(), "rb"))
            {
                const std::string part = target + ".part";
                if (std::FILE *out = std::fopen(part.c_str(), "wb"))
                {
                    char buf[16384];
                    std::size_t n;
                    ok = true;
                    while ((n = std::fread(buf, 1, sizeof buf, in)) > 0)
                        ok &= std::fwrite(buf, 1, n, out) == n;
                    std::fclose(out);
                    ok = ok && std::rename(part.c_str(), target.c_str()) == 0;
                }
                std::fclose(in);
            }
            if (!ok)
                open_dialog(DialogKind::Info, tr("Could not copy the save"),
                            trf("Writing {path} failed.", {{"path", target}}), "");
            cards_scanned_ = false;
            save_scan_.reset(); /* a scan already running read the old state */
        }
        return Action::None;
    default:
        return Action::None;
    }
}

void App::draw_dialog()
{
    if (screen_ == Screen::Achievements)
        draw_achievements_screen(); /* over whichever look's library */
    draw_account(); /* under any dialog */
    draw_share();
    draw_hub();
    draw_dns();
    draw_system();
    draw_code();
    draw_nand_job();
    draw_sys_update();
    if (!dialog_.open)
        return;
    Gfx &g = *g_;
    const float t = ease_out(dialog_.anim);
    g.set_layer();
    g.panel(0, 0, 1920, 1080, rgba(0x02040C, 0.62f * t), 1, 0);
    g.set_layer(0, 24.0f * (1.0f - t), t);
    const float w = 820;
    /* Most dialogs are a line or two; a few (the sandbox help) run longer. The
     * box grows with the text, so allow enough lines for those and still fit. */
    const auto lines = dialog_.message.empty() ? std::vector<std::string>{}
                                               : wrap(g, Font::Regular, ts(28), dialog_.message, w - 112, 12);
    /* A checklist: each item's lines, beside its mark. */
    std::vector<std::vector<std::string>> items;
    float checks_h = 0;
    for (const auto &c : dialog_.checks)
    {
        items.push_back(wrap(g, Font::Regular, ts(27), c.second, w - 112 - 62, 4));
        checks_h += float(items.back().size()) * 36 + 22;
    }
    const float pic_h = dialog_.picture && dialog_.picture->width > 0
                            ? std::min(300.0f, 420.0f * float(dialog_.picture->height) / float(dialog_.picture->width))
                            : 0.0f;
    const float pic_w = pic_h > 0 ? pic_h * float(dialog_.picture->width) / float(dialog_.picture->height) : 0.0f;
    const float h = 248 + float(lines.size()) * 38 + (pic_h > 0 ? pic_h + 24 : 0.0f) + checks_h -
                    (lines.empty() && checks_h > 0 ? 20.0f : 0.0f);
    const float x = 960 - w * 0.5f, y = 540 - h * 0.5f - 20;
    Glass face;
    face.tint = rgba(0x16348F, 0.92f);
    face.rim = dialog_.danger ? kDanger : rgba(0x8BD9FF);
    face.radius = kR;
    face.rim_w = 2.4f;
    face.glow = 14;
    face.phase = 0.4f;
    glass_block(g, 960, y + h * 0.5f, w, h, 18, 0, 0, 42, face);
    g.text_mid(Font::Bold, ts(38), x + 56, y + 64, kWhite, Align::Left, fit(g, Font::Bold, ts(38), dialog_.title, w - 112));
    float ly = y + 118;
    if (pic_h > 0)
    {
        /* Where the game was left. */
        g.panel(960 - pic_w * 0.5f - 4, ly - 14, pic_w + 8, pic_h + 8, rgba(0x07102E, 0.6f), 1, 12,
                rgba(0x8BD9FF, 0.7f), 1.6f);
        g.image(dialog_.picture, 960 - pic_w * 0.5f, ly - 10, pic_w, pic_h, {}, 10);
        ly += pic_h + 24;
    }
    for (const std::string &l : lines)
    {
        /* Under a picture, centred beneath it. */
        if (pic_h > 0)
            g.text_mid(Font::Regular, ts(28), 960, ly, kSoft, Align::Center, l);
        else
            g.text_mid(Font::Regular, ts(28), x + 56, ly, kSoft, Align::Left, l);
        ly += 38;
    }
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        draw_check_mark(x + 56 + 20, ly, 40, dialog_.checks[i].first);
        for (const std::string &l : items[i])
        {
            g.text_mid(Font::Regular, ts(27), x + 56 + 62, ly, dialog_.checks[i].first ? kSoft : kWhite, Align::Left,
                       l);
            ly += 36;
        }
        ly += 22;
    }
    /* Buttons: Cancel and the action, or just OK. */
    const float bh = 60, by = y + h - 56 - bh * 0.5f;
    auto button = [&](float bx, float bw, const std::string &label, bool on, bool danger) {
        g.panel(bx, by, bw, bh, on ? (danger ? rgba(0xB0305A, 0.92f) : rgba(0x1F63F0, 0.92f)) : rgba(0x07102E, 0.5f),
                0.7f, kR, on ? (danger ? kDanger : kIcy) : rgba(0x3D5AB0, 0.8f), on ? 2.2f : 1.4f, on ? 8 : 0,
                on ? 0.25f : 0.0f);
        g.text_mid(Font::Bold, ts(28), bx + bw * 0.5f, by + bh * 0.5f, on ? kWhite : kSoft, Align::Center,
                   title_case(label));
    };
    if (dialog_.yes.empty())
        button(x + w - 56 - 220, 220, tr("OK"), true, false);
    else
    {
        button(x + w - 56 - 220, 220, dialog_.yes, dialog_.choice == 1, dialog_.danger);
        button(x + w - 56 - 220 - 24 - 220, 220, dialog_.no.empty() ? tr("Cancel") : dialog_.no,
               dialog_.choice == 0, false);
    }
    g.set_layer();
    drawing_dialog_ = true;
    if (dialog_.yes.empty())
        draw_prompts({{Glyph::Cross, "OK"}}, {}, "");
    else
        draw_prompts({{Glyph::DPad, "Choose"}, {Glyph::Cross, "Confirm"}, {Glyph::Circle, "Cancel"}}, {}, "");
    drawing_dialog_ = false;
}

/* ---- memory card actions ------------------------------------------------------------------ */

Save *App::focused_save()
{
    Card &card = mc_card_ == 0 ? card_a_ : card_b_;
    const int sel = mc_sel_[mc_card_];
    return sel >= 0 && sel < int(card.saves.size()) ? &card.saves[std::size_t(sel)] : nullptr;
}

static bool is_gci(const std::string &path)
{
    return path.size() > 4 && (path.compare(path.size() - 4, 4, ".gci") == 0 || path.compare(path.size() - 4, 4, ".GCI") == 0);
}

/* The same file name in the other slot's folder (Card A <-> Card B). */
std::string App::copy_target(const Save &s) const
{
    const std::size_t slash = s.path.rfind('/');
    if (slash == std::string::npos)
        return "";
    const std::string dir = s.path.substr(0, slash), name = s.path.substr(slash + 1);
    const std::size_t up = dir.rfind('/');
    const std::string parent = up == std::string::npos ? "" : dir.substr(0, up);
    const std::string card = dir.substr(up == std::string::npos ? 0 : up + 1);
    std::string other;
    if (card == "Card A")
        other = "Card B";
    else if (card == "Card B")
        other = "Card A";
    else
        return "";
    return parent + "/" + other + "/" + name;
}

void App::ask_delete_save()
{
    Save *s = focused_save();
    if (!s)
        return;
    const std::string slot = mc_card_ == 0 ? "A" : "B";
    if (!is_gci(s->path))
    {
        open_dialog(DialogKind::Info, tr("This save is inside a card file"),
                    tr("Porpoise can delete saves that Dolphin keeps as files (its GCI folders), not ones inside a "
                       ".raw memory card image."),
                    "");
        return;
    }
    const std::string what = s->title.empty() ? s->file_name : s->title;
    open_dialog(DialogKind::DeleteSave, tr("Delete this save?"),
                trf("{save}. It is removed from Slot {slot} for good.",
                    {{"save", what + (s->detail.empty() ? "" : " - " + s->detail)}, {"slot", slot}}),
                tr("Delete"), true);
}

/* Beta: the focused GameCube save onto a USB drive. */
void App::export_save_to_usb()
{
    Save *s = focused_save();
    if (!s)
        return;
    const Card &card = mc_card_ == 0 ? card_a_ : card_b_;
    std::string where;
    if (usb_root().empty())
        open_dialog(DialogKind::Info, tr("No USB drive"), tr("Plug in a USB drive (exFAT) to copy saves to it."), "");
    else if (!card.folder)
        open_dialog(DialogKind::Info, tr("Can't copy from this card"),
                    tr("This card is one memory card file, not Dolphin's folder of saves, so its saves can't be "
                       "copied one by one."),
                    "");
    else if (export_gc_save(*s, where))
    {
        flash_note(trf("Copied to the USB drive: {path}", {{"path", where.substr(where.find("Porpoise Saves"))}}));
        sfx(Sound::LaunchGame);
    }
    else
        open_dialog(DialogKind::Info, tr("Could not copy the save"),
                    tr("Copying it to the USB drive failed. Is the drive full or read-only?"), "");
}

void App::ask_copy_save()
{
    Save *s = focused_save();
    if (!s)
        return;
    const std::string to = mc_card_ == 0 ? "B" : "A";
    const std::string target = is_gci(s->path) ? copy_target(*s) : "";
    if (target.empty())
    {
        open_dialog(DialogKind::Info, tr("This save can't be copied"),
                    tr("Porpoise copies saves that Dolphin keeps as files (its GCI folders)."), "");
        return;
    }
    const Card &other = mc_card_ == 0 ? card_b_ : card_a_;
    struct stat st;
    const bool exists = stat(target.c_str(), &st) == 0;
    if (!exists && other.present && s->blocks > other.free_blocks)
    {
        open_dialog(DialogKind::Info, tr("Not enough room"),
                    trf("Slot {slot} has {free} blocks open; this save needs {need}.",
                        {{"slot", to}, {"free", std::to_string(other.free_blocks)}, {"need", std::to_string(s->blocks)}}),
                    "");
        return;
    }
    const std::string what = s->title.empty() ? s->file_name : s->title;
    if (exists)
        open_dialog(DialogKind::CopySave, trf("Replace the save on Slot {slot}?", {{"slot", to}}),
                    trf("Slot {slot} already has {save}. Copying replaces it with this one.", {{"slot", to}, {"save", what}}),
                    tr("Replace"), true);
    else
        open_dialog(DialogKind::CopySave, trf("Copy to Slot {slot}?", {{"slot", to}}),
                    trf("{save} ({blocks}) is copied to Slot {slot}.",
                        {{"save", what}, {"blocks", plural(s->blocks, "1 block", "{n} blocks")}, {"slot", to}}),
                    tr("Copy"));
}

/* ---- drawing: shared ---------------------------------------------------------------------- */

std::string App::clock_text() const
{
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    int hour = tm.tm_hour % 12;
    if (hour == 0)
        hour = 12;
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d:%02d %s", hour, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
    return buf;
}

/* The Porpoise mark and name, centred on cy. Ruben's textured logo
 * (assets/brand/logo.png) replaces the tinted silhouette when it is there. */
void App::draw_brand(float cy)
{
    Gfx &g = *g_;
    if (!logo_tried_)
    {
        logo_tried_ = true;
        const std::string path = g.asset_dir() + "/brand/logo.png";
        if (access(path.c_str(), R_OK) == 0)
            logo_ = g.texture_file(path);
    }
    float x = 48;
    if (logo_ && logo_->height > 0)
    {
        const float h = 72, w = std::min(160.0f, h * float(logo_->width) / float(logo_->height));
        g.image(logo_, x, cy - h * 0.5f, w, h, kWhite);
        x += w + 16;
    }
    else
    {
        if (g.brand_mask())
            g.image(g.brand_mask(), x, cy - 32, 64, 64, kCyan);
        x += 82;
    }
    const float word = g.text_mid(Font::SemiBold, 30, x, cy, rgba(0xBFE9FF), Align::Left, "PORPOISE", 7.0f);
    if (kChannel != Channel::Release)
    {
        /* A test build: its tag after the wordmark. */
        const std::string tag = kChannel == Channel::Alpha ? "ALPHA" : "BETA";
        const float tx = x + word + 10, tw = g.measure(Font::Bold, 20, tag, 2.0f) + 22;
        g.panel(tx, cy - 16, tw, 32, rgba(0xFFB547, 0.95f), 1, 16);
        g.text_mid(Font::Bold, 20, tx + tw * 0.5f, cy, rgba(0x2A1A00), Align::Center, tag, 2.0f);
    }
}

void App::draw_curtain(float amount)
{
    if (amount <= 0.0f)
        return;
    Gfx &g = *g_;
    if (!logo_tried_)
    {
        logo_tried_ = true;
        const std::string path = g.asset_dir() + "/brand/logo.png";
        if (access(path.c_str(), R_OK) == 0)
            logo_ = g.texture_file(path);
    }
    const float a = std::min(1.0f, amount);
    g.panel(-20, -20, 1960, 1120, rgba(0x02040C, a), 1.0f, 0);
    /* As it lifts, the dolphin swells a little and goes first, with its glow
     * (the same as the boot's: no jump between them). */
    const float m = a * a;
    g.blob(960, 540, 900, 900, rgba(0x2F6BFF, 0.16f * m));
    draw_mark(960, 540, 220 * (1.0f + 0.18f * (1.0f - a)), with_alpha(kWhite, m));
}

bool App::draw_mark(float cx, float cy, float width, Color tint)
{
    Gfx &g = *g_;
    if (logo_ && logo_->width > 0)
    {
        /* Ruben's logo keeps its own colours; only the tint's fade applies. */
        const float h = width * float(logo_->height) / float(logo_->width);
        g.image(logo_, cx - width * 0.5f, cy - h * 0.5f, width, h, with_alpha(kWhite, tint.a));
        return true;
    }
    if (!g.brand_mask())
        return false;
    g.image(g.brand_mask(), cx - width * 0.5f, cy - width * 0.5f, width, width, tint);
    return true;
}

void App::draw_top_bar()
{
    Gfx &g = *g_;
    draw_brand(kBarCy);

    /* Section tabs, as wide as their names; the highlight glides to the chosen one. */
    const std::string names[3] = {tr("Library"), tr("Memory Cards"), tr("Settings")};
    float widths[3], bar_w = 12;
    for (int i = 0; i < 3; ++i)
    {
        widths[i] = std::max(170.0f, g.measure(Font::Bold, ts(27), names[i]) + 74);
        bar_w += widths[i];
    }
    const float bar_x = 960 - bar_w * 0.5f;
    const bool pills = th().pills;
    g.panel(bar_x, kBarY, bar_w, kBarH, rgba(0x0A1236, 0.70f), 0.9f, pills ? kBarH * 0.5f : kR, rgba(0x3D4F9E, 0.9f),
            1.6f);
    {
        float tx = bar_x + 6;
        for (int i = 0; i < 3; ++i)
        {
            tab_x_[i] = tx;
            tab_w_[i] = widths[i];
            tx += widths[i];
        }
    }
    if (pill_x_ < 0)
    {
        pill_x_ = tab_x_[int(tab_)];
        pill_w_ = tab_w_[int(tab_)];
    }
    g.panel(pill_x_, kBarY + 6, pill_w_, kBarH - 12, rgba(0x1F63F0), 0.62f, pills ? (kBarH - 12) * 0.5f : kR,
            rgba(0x7FD9FF), 1.6f, 6, 0.35f);
    for (int i = 0; i < 3; ++i)
    {
        const bool on = int(tab_) == i;
        g.text_mid(on ? Font::Bold : Font::SemiBold, ts(27), tab_x_[i] + widths[i] * 0.5f, kBarCy, on ? kWhite : kSoft,
                   Align::Center, names[i]);
    }
    /* L1 / R1 keycaps */
    auto keycap = [&](float cx, Glyph glyph, const char *label) {
        if (g.has_icons())
        {
            g.glyph(glyph, cx, kBarCy, 46, rgba(0xE8F0FF));
            return;
        }
        g.panel(cx - 30, kBarCy - 19, 60, 38, kClear, 1, kR, rgba(0xDDE6FF, 0.85f), 2.0f);
        g.text_mid(Font::Bold, 21, cx, kBarCy, kWhite, Align::Center, label);
    };
    keycap(bar_x - 66, Glyph::L1, "L1");
    keycap(bar_x + bar_w + 66, Glyph::R1, "R1");

    g.text_mid(Font::SemiBold, ts(27), 1866, kBarCy, kWhite, Align::Right, clock_text());
}

/* Two keycaps side by side (L2 R2, L1 R1) where a prompt's glyph would be;
 * returns their width. measure_only draws nothing. */
float App::draw_key_pair(float x, float cy, Glyph which, bool measure_only)
{
    Gfx &g = *g_;
    if (which == kKeyL2R2 && g.wii_buttons())
    {
        /* The Wii Remote's 2 does what L2 and R2 do (wii_menu_bits). */
        if (!measure_only)
            g.glyph(Glyph::R2, x + 22, cy, 44, kWhite);
        return 44;
    }
    const char *a = which == kKeyL1R1 ? "L1" : "L2";
    const char *b = which == kKeyL1R1 ? "R1" : "R2";
    if (g.has_icons())
    {
        /* The two buttons' own icons, side by side. */
        const Glyph ga = which == kKeyL1R1 ? Glyph::L1 : Glyph::L2;
        const Glyph gb = which == kKeyL1R1 ? Glyph::R1 : Glyph::R2;
        const float cell = 44, gap = 4;
        if (!measure_only)
        {
            g.glyph(ga, x + cell * 0.5f, cy, cell, kWhite);
            g.glyph(gb, x + cell * 1.5f + gap, cy, cell, kWhite);
        }
        return cell * 2 + gap;
    }
    const float kw = 50, kh = 34, gap = 6;
    if (!measure_only)
        for (int i = 0; i < 2; ++i)
        {
            const float kx = x + float(i) * (kw + gap);
            g.panel(kx, cy - kh * 0.5f, kw, kh, kClear, 1, kR * 0.8f, rgba(0xDDE6FF, 0.9f), 2.0f);
            g.text_mid(Font::Bold, 19, kx + kw * 0.5f, cy, kWhite, Align::Center, i == 0 ? a : b);
        }
    return kw * 2 + gap;
}

void App::draw_prompts(const std::vector<std::pair<Glyph, std::string>> &left_in,
                       const std::vector<std::pair<Glyph, std::string>> &right_in, const std::string &center)
{
    Gfx &g = *g_;
    if ((dialog_.open || acct_.open || share_.open || dns_.open || hub_.open || sys_.open || code_.open ||
         screen_ == Screen::Achievements) &&
        !drawing_dialog_)
        return; /* the dialog (or the account panel, the achievements page) brings its own */
    /* Every prompt is translated here, so callers write plain English. */
    std::vector<std::pair<Glyph, std::string>> left_tr, right_tr;
    for (const auto &p : left_in)
        left_tr.push_back({p.first, title_case(tr(p.second))});
    for (const auto &p : right_in)
        right_tr.push_back({p.first, title_case(tr(p.second))});
    const auto &left = left_tr;
    const auto &right = right_tr;
    /* The theme sets their size (smaller than 2.0's); Accessibility can make them larger. */
    const float sc = th().prompts * (settings_ && settings_->big_prompts ? 1.3f : 1.0f);
    const float y = kPromptY + (1.0f - sc) * 12.0f, size = ts(28) * sc, gs = 44 * sc;
    /* Star Cube: "X ··· Confirm", no rules between. */
    const bool dotted = starcube();
    const std::string leader = "\xC2\xB7\xC2\xB7\xC2\xB7";
    const float leader_w = dotted ? g.measure(Font::Bold, size, leader) + 14 * sc : 0.0f;
    float x = 58;
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        if (i > 0)
        {
            if (!dotted)
                g.panel(x - 2, y - 22 * sc, 1.6f, 44 * sc, rgba(0x5A68A8, 0.8f), 1, 0);
            x += 38 * sc;
        }
        if (int(left[i].first) >= int(kKeyL2R2))
            x += draw_key_pair(x, y, left[i].first) - 54 + 14;
        else
            g.glyph(left[i].first, x + gs * 0.45f, y, gs, kWhite);
        x += gs + 10 * sc;
        if (dotted)
        {
            g.text_mid(Font::Bold, size, x, y, kLavender, Align::Left, leader);
            x += leader_w;
        }
        x += g.text_mid(Font::SemiBold, size, x, y, kWhite, Align::Left, left[i].second) + 40 * sc;
    }
    float rx = 1862;
    for (std::size_t i = right.size(); i-- > 0;)
    {
        const float w = g.measure(Font::SemiBold, size, right[i].second) + leader_w;
        g.text_mid(Font::SemiBold, size, rx, y, kWhite, Align::Right, right[i].second);
        if (dotted)
            g.text_mid(Font::Bold, size, rx - w + leader_w - 14 * sc, y, kLavender, Align::Right, leader);
        float glyph_x = rx - w - gs * 0.8f;
        if (int(right[i].first) >= int(kKeyL2R2))
        {
            const float kw = draw_key_pair(0, y, right[i].first, true);
            glyph_x = rx - w - 14 - kw;
            draw_key_pair(glyph_x, y, right[i].first);
            glyph_x += 22; /* as if a glyph centred here */
        }
        else
            g.glyph(right[i].first, glyph_x, y, gs, kWhite);
        rx = glyph_x - gs * 0.5f - 38 * sc;
        if (i > 0)
        {
            if (!dotted)
                g.panel(rx, y - 22 * sc, 1.6f, 44 * sc, rgba(0x5A68A8, 0.8f), 1, 0);
            rx -= 38 * sc;
        }
    }
    if (!center.empty())
        g.text_mid(Font::SemiBold, ts(26) * sc, 960, y, kLavender, Align::Center, center, 2.0f);
}

/* One glass tile with its cover, in perspective, and its reflection. */
void App::draw_tile(Game *game, float cx, float cy, float w, float h, float yaw, float alpha, bool focused,
                    bool reflection)
{
    Gfx &g = *g_;
    const float m = 13.0f; /* glass frame margin */
    const float glow = focused ? 20.0f : 0.0f;
    const float ex = w * 0.5f + m + glow * 3, ey = h * 0.5f + m + glow * 3;
    const float inner_r = kR * 0.6f; /* the cover sits inside the frame */
    Texture *cover = game ? cover_of(*game) : nullptr;

    if (reflection)
    {
        /* The tile mirrored below its bottom edge, fading out. */
        const float base = cy + h * 0.5f + m;
        const Corner r[4] = {project(cx, base, -w * 0.5f, 0, yaw), project(cx, base, w * 0.5f, 0, yaw),
                             project(cx, base, w * 0.5f, h * 0.85f, yaw), project(cx, base, -w * 0.5f, h * 0.85f, yaw)};
        if (cover)
        {
            float uv[4] = {0, 0, 1, 1};
            const float ta = float(cover->width) / float(cover->height), aa = w / h;
            if (ta > aa)
            {
                const float k = aa / ta;
                uv[0] = 0.5f - k * 0.5f;
                uv[2] = 0.5f + k * 0.5f;
            }
            uv[1] = 0.15f;
            g.quad3d(cover, r, w, h * 0.85f, with_alpha(rgba(0xB8C8FF), 0.30f * alpha), inner_r, true, true, uv);
        }
        else
            g.quad3d(nullptr, r, w, h * 0.85f, with_alpha(rgba(0x2A4FD0), 0.25f * alpha), inner_r, true, true);
        return;
    }

    /* The block: a slab of blue glass with real thickness, the cover set into its face. */
    Glass face;
    face.tint = kGlassBody;
    face.rim = focused ? kIcy : kEdge;
    face.fade = alpha;
    face.radius = kR;
    face.rim_w = focused ? 3.2f : 2.2f;
    face.glow = glow;
    face.phase = cx / 1920.0f;
    const float depth = 34.0f * (w / kTileW);
    if (std::cos(yaw) < 0.0f && game)
    {
        /* Turned around: the back of the box shows, set into the far face. */
        glass_block(g, cx, cy, w + m * 2, h + m * 2, depth, yaw, 0, glow * 3, face, true);
        Corner b[4];
        rect_at(cx, cy, w * 0.5f, h * 0.5f, yaw, depth, 0, b);
        if (Texture *back = back_of(*game))
        {
            float uv[4];
            cover_uv(back, w, h, uv);
            std::swap(uv[0], uv[2]); /* seen from behind, the box's left is on the right */
            g.quad3d(back, b, w, h, with_alpha(kWhite, alpha), inner_r, false, false, uv);
        }
        else
        {
            g.quad3d(nullptr, b, w, h, with_alpha(rgba(0x13286F), alpha), inner_r, false, false);
            Texture *mark = logo_ ? logo_ : g.brand_mask();
            if (mark)
            {
                Corner lc[4];
                const float lw = w * 0.42f, lh = lw * float(mark->height) / float(std::max(1, mark->width));
                rect_at(cx, cy, lw * 0.5f, lh * 0.5f, yaw, depth, 0, lc);
                std::swap(lc[0], lc[1]);
                std::swap(lc[2], lc[3]);
                g.quad3d(mark, lc, lw, lh, with_alpha(logo_ ? kWhite : kCyan, 0.85f * alpha), 0, false, false);
            }
        }
        gloss_over(g, b, w, h, inner_r, cx / 1920.0f, alpha);
        (void)ex;
        (void)ey;
        return;
    }
    glass_block(g, cx, cy, w + m * 2, h + m * 2, depth, yaw, 0, glow * 3, face);
    (void)ex;
    (void)ey;

    const Corner c[4] = {project(cx, cy, -w * 0.5f, -h * 0.5f, yaw), project(cx, cy, w * 0.5f, -h * 0.5f, yaw),
                         project(cx, cy, w * 0.5f, h * 0.5f, yaw), project(cx, cy, -w * 0.5f, h * 0.5f, yaw)};
    if (cover)
    {
        float uv[4] = {0, 0, 1, 1};
        const float ta = float(cover->width) / float(cover->height), aa = w / h;
        if (ta > aa)
        {
            const float k = aa / ta;
            uv[0] = 0.5f - k * 0.5f;
            uv[2] = 0.5f + k * 0.5f;
        }
        else
        {
            const float k = ta / aa;
            uv[1] = 0.5f - k * 0.5f;
            uv[3] = 0.5f + k * 0.5f;
        }
        /* A cover that has just arrived fades in over the tile's own blue. */
        const float in = game ? std::clamp(float(time_ - game->cover_at) / 0.18f, 0.0f, 1.0f) : 1.0f;
        if (in < 1.0f)
            g.quad3d(nullptr, c, w, h, with_alpha(rgba(0x16328F), alpha), inner_r, false, false);
        g.quad3d(cover, c, w, h, with_alpha(kWhite, alpha * in), inner_r, false, false, uv);
    }
    else
    {
        /* No cover yet: a Porpoise tile with the game's title (or, while the
         * cover is still being read, just the blue). */
        g.quad3d(nullptr, c, w, h, with_alpha(rgba(0x16328F), alpha), inner_r, false, false);
        if (std::fabs(yaw) < 0.08f && game && !cover_loading(*game))
        {
            const float s = w / kTileW;
            draw_mark(cx, cy - h * 0.30f + 70 * s, 150 * s, with_alpha(rgba(0x6FD8FF), 0.9f * alpha));
            /* Title, wrapped to two lines. */
            const std::string &t = game->title;
            const float size = 34 * s, max_w = w - 40 * s;
            std::vector<std::string> lines;
            std::string line, word;
            for (std::size_t i = 0; i <= t.size(); ++i)
            {
                if (i == t.size() || t[i] == ' ')
                {
                    const std::string trial = line.empty() ? word : line + " " + word;
                    if (!line.empty() && g.measure(Font::ExtraBold, size, trial) > max_w)
                    {
                        lines.push_back(line);
                        line = word;
                    }
                    else
                        line = trial;
                    word.clear();
                }
                else
                    word += t[i];
            }
            if (!line.empty())
                lines.push_back(line);
            if (lines.size() > 3)
                lines.resize(3);
            float ty = cy + h * 0.06f;
            for (const std::string &l : lines)
            {
                g.text(Font::ExtraBold, size, cx, ty, with_alpha(kWhite, alpha), Align::Center, l);
                ty += size * 1.15f;
            }
        }
    }

    /* The glass over the art: gloss and the room's reflection. */
    Glass gloss;
    gloss.face = 2;
    gloss.radius = inner_r;
    gloss.rim_w = 0;
    gloss.tint = kWhite;
    gloss.rim = kClear;
    gloss.fade = alpha;
    gloss.phase = cx / 1920.0f;
    g.glass(c, w, h, 0, gloss);

    /* A favourite: a gold star on the top right corner. */
    if (game && game->favourite)
    {
        const Corner tr_corner = project(cx, cy, w * 0.5f - 30, -h * 0.5f + 30, yaw);
        const float s = w * 0.15f / tr_corner.w;
        g.blob(tr_corner.x, tr_corner.y, s * 1.9f, s * 1.9f, rgba(0x0A1236, 0.55f * alpha));
        g.glyph(Glyph::Star, tr_corner.x, tr_corner.y, s, rgba(0xFFD45C, alpha));
    }
}

/* ---- library ------------------------------------------------------------------------------ */

void App::draw_empty()
{
    Gfx &g = *g_;
    g.text(Font::Bold, ts(54), 90, 132, kWhite, Align::Left, tr("Your Games"));
    g.text(Font::SemiBold, ts(32), 92, 200, kLavender, Align::Left, tr("No games yet"));
    g.panel(560, 330, 800, 380, rgba(0x13256F, 0.55f), 0.6f, kR, rgba(0x5A8CFF, 0.9f), 2.0f, 0, 0.2f);
    draw_mark(960, 420, 150, kCyan);
    g.text(Font::Bold, ts(40), 960, 500, kWhite, Align::Center, tr("Add Games"));
    g.text(Font::Regular, ts(26), 960, 560, kSoft, Align::Center, tr("Copy .iso, .rvz or .ciso files with PS5 Upload into"));
    g.text(Font::SemiBold, ts(26), 960, 600, kIcy, Align::Center, data_dir_ + "/games");
    g.text(Font::Regular, ts(26), 960, 640, kSoft, Align::Center, tr("or add your own folder in Settings, under Games."));
}

void App::draw_library(double time)
{
    Gfx &g = *g_;
    auto &games = lib_->games();
    if (games.empty())
    {
        draw_empty();
        draw_prompts({}, {}, "");
        return;
    }
    const int shown = lib_->shown();
    g.text(Font::Bold, ts(54), 90, 132, kWhite, Align::Left, tr("Your Games"));
    const float cw = g.text(Font::SemiBold, ts(32), 92, 200, kLavender, Align::Left, library_count());
    const std::string note = library_note();
    if (!note.empty())
        g.text(Font::Regular, ts(26), 92 + cw + 24, 205, with_alpha(kIcy, 0.85f), Align::Left,
               "\xE2\x80\xA2  " + note);
    if (update_available())
    {
        /* A newer Porpoise is out: a small pill at the right. */
        const std::string text = trf("Porpoise {version} is out", {{"version", latest_version_}});
        const float tw = g.measure(Font::SemiBold, ts(24), text) + 44;
        g.panel(1866 - tw, 150, tw, 46, rgba(0x0E2A6E, 0.8f), 0.8f, 23, with_alpha(kCyan, 0.9f), 1.6f, 8);
        g.text_mid(Font::SemiBold, ts(24), 1866 - tw * 0.5f, 173, kWhite, Align::Center, text);
        g.text_mid(Font::Regular, ts(20), 1866 - tw * 0.5f, 214, kLavender, Align::Center,
                   tr("Settings > About"));
    }

    /* The games, in the view chosen in Settings > Interface. */
    const int view = std::clamp(settings_->lib_view, 0, 9);
    switch (view)
    {
    case 8: draw_spines(time); break;
    case 9: draw_spotlight(time); break;
    case 1: draw_wheel(time); break;
    case 2: draw_disc_flow(time); break;
    case 3: draw_shelf(time); break;
    case 4: draw_box_view(time); break;
    case 5: draw_list_view(time); break;
    case 6: draw_stack(time); break;
    case 7: draw_helix(time); break;
    default: draw_cover_flow(time); break;
    }
    release_far_art(view == 3 || view == 5 ? 20 : view == 9 ? 10 : 9);

    /* Side arrows. (No marker over the chosen cover: its glow says it, and a
     * triangle there read as the Triangle button.) */
    if (view != 3 && view != 5 && view != 9)
    {
        if (selected_ > 0)
            g.glyph(Glyph::Arrow, 34, kCy, 46, rgba(0x58B8FF), -kPi * 0.5f);
        if (selected_ + 1 < shown)
            g.glyph(Glyph::Arrow, 1886, kCy, 46, rgba(0x58B8FF), kPi * 0.5f);
    }

    if (shown == 0)
    {
        /* Sort & filter shows none: say so, and how to see them again. */
        g.text(Font::Bold, ts(40), 960, 520, kWhite, Align::Center,
               lib_->show() == Library::Show::Wii        ? tr("No Wii games here")
               : lib_->show() == Library::Show::Channels ? tr("No channels here")
                                                         : tr("No GameCube games here"));
        g.text(Font::Regular, ts(28), 960, 580, kSoft, Align::Center,
               tr("Sort & Filter (Triangle) can show all of your games."));
        draw_prompts({}, {{Glyph::Triangle, "Sort & Filter"}}, "");
        return;
    }

    /* Title and details (the shelf and the box show their own). */
    Game &sel = games[std::size_t(selected_)];
    if (view <= 2 || (view >= 6 && view <= 8))
    {
        const float ty = view == 0 || view == 6 || view == 8 ? 748.0f : 800.0f;
        g.text(Font::Bold, ts(46), kCx, ty, kWhite, Align::Center, sel.title);
        g.text(Font::SemiBold, ts(28), kCx, ty + 64, kLavender, Align::Center, game_meta(sel));
        if (sel.favourite)
        {
            const float tw = g.measure(Font::Bold, ts(46), sel.title);
            g.glyph(Glyph::Star, kCx - tw * 0.5f - 34, ty - 16, 34, rgba(0xFFD45C));
        }
    }

    if (dock_shown())
        draw_dock(time);
    if (dock_focus_)
    {
        draw_prompts({{Glyph::DPad, "Browse"}, {Glyph::Cross, "Play"}},
                     {{Glyph::Square, "Details"}, {Glyph::Circle, "Back"}}, "");
        return;
    }
    char pos[32];
    std::snprintf(pos, sizeof pos, "%02d / %02d", selected_ + 1, shown);
    /* L2 / R2: a letter at a time sorted by title, else a page (update_view_nav). */
    draw_prompts({{Glyph::DPad, "Browse"},
                  {kKeyL2R2, lib_->sort_order() == Library::Sort::Title ? "Letters" : "Pages"},
                  {Glyph::Cross, "Play"}},
                 {{Glyph::Options, sel.favourite ? "Unfavorite" : "Favorite"}, {Glyph::Square, "Details"},
                  {Glyph::Triangle, "Sort & Filter"}},
                 pos);
}

/* ---- recently played ----------------------------------------------------------------------- */

std::vector<int> App::recent_games()
{
    std::vector<int> out;
    auto &games = lib_->games();
    for (int i = 0; i < int(games.size()); ++i)
        if (games[std::size_t(i)].last_played > 0)
            out.push_back(i);
    const std::size_t n = std::min<std::size_t>(out.size(), 6);
    std::partial_sort(out.begin(), out.begin() + std::ptrdiff_t(n), out.end(), [&](int a, int b) {
        return games[std::size_t(a)].last_played > games[std::size_t(b)].last_played;
    });
    out.resize(n);
    return out;
}

bool App::dock_shown()
{
    if (!settings_ || settings_->recent_style == 0 || tab_ != Tab::Library || starcube() ||
        (revolution() && settings_->ui_layout == 0) || lib_->shown() == 0)
        return false;
    for (const Game &g : lib_->games())
        if (g.last_played > 0)
            return true;
    return false;
}

/* The chosen game's name under the row (Floating and Discs), where the
 * library's position count is while the row has the focus. */
void App::draw_dock_bubble(float cx, float top, const std::string &title)
{
    (void)cx;
    (void)top;
    Gfx &g = *g_;
    const float a = dock_open_;
    if (a < 0.02f)
        return;
    const std::string text = fit(g, Font::Bold, ts(24), title, 760);
    const float w = g.measure(Font::Bold, ts(24), text) + 48, h = 42;
    const float x = kCx - w * 0.5f, y = 999 + (1.0f - a) * 6.0f;
    g.panel(x, y, w, h, rgba(0x0B1848, 0.9f * a), 0.8f, h * 0.5f, with_alpha(kIcy, 0.7f * a), 1.4f, 6, 0.12f * a);
    g.text_mid(Font::Bold, ts(24), kCx, y + h * 0.5f, with_alpha(kWhite, a), Align::Center, text);
}

/* Floating: the boxes alone on a thin glass shelf; chosen, the one in focus
 * and its neighbors grow and rise, as a dock's icons do under the pointer. */
void App::draw_dock_floating(const std::vector<int> &recent)
{
    Gfx &g = *g_;
    auto &games = lib_->games();
    const int n = int(recent.size());
    /* Below the library's own lines (a view's info ends near y 890). */
    const float base = 58, gap = 14, shelf_y = 990;
    float scale[8] = {}, total = 0;
    for (int j = 0; j < n; ++j)
    {
        const float dist = std::fabs(float(j) - dock_glide_);
        scale[j] = 1.0f + 0.3f * dock_open_ * std::max(0.0f, 1.0f - dist / 2.2f);
        total += base * 5.0f / 7.0f * scale[j] + (j ? gap : 0.0f);
    }
    const float shelf_w = std::max(total, float(n) * (base * 5.0f / 7.0f + gap) - gap) + 56;
    g.panel(kCx - shelf_w * 0.5f, shelf_y - 30, shelf_w, 36, rgba(0x0F1F63, 0.35f + 0.25f * dock_open_), 0.75f, 18,
            rgba(0x8BD9FF, 0.35f + 0.4f * dock_open_), 1.4f, 0, 0.18f);
    float x = kCx - total * 0.5f;
    float focus_cx = kCx, focus_top = shelf_y;
    for (int j = 0; j < n; ++j)
    {
        Game &game = games[std::size_t(recent[std::size_t(j)])];
        const float bh = base * scale[j], bw = bh * 5.0f / 7.0f;
        const float top = shelf_y - 14 - bh;
        const bool on = dock_focus_ && j == dock_sel_;
        if (on)
        {
            focus_cx = x + bw * 0.5f;
            focus_top = top;
            g.blob(focus_cx, top + bh * 0.5f, bw * 1.8f, bh * 1.4f, rgba(0x5CD3FF, 0.22f * dock_open_));
        }
        g.blob(x + bw * 0.5f, shelf_y - 12, bw * 0.9f, 10, rgba(0x000000, 0.5f)); /* its shadow on the shelf */
        if (Texture *cover = cover_of(game))
        {
            float uv[4];
            cover_uv(cover, bw, bh, uv);
            g.image_part(cover, x, top, bw, bh, uv, with_alpha(kWhite, dock_focus_ && !on ? 0.8f : 1.0f), 6);
        }
        else
            g.panel(x, top, bw, bh, with_alpha(kTileFill, 0.7f), 0.7f, 6, with_alpha(kEdge, 0.6f), 1.2f);
        if (on) /* the light under the chosen one, as a dock marks a running app */
            g.panel(x + bw * 0.5f - 4, shelf_y - 8, 8, 8, kIcy, 1, 4);
        x += bw + gap;
    }
    if (dock_focus_)
        draw_dock_bubble(focus_cx, focus_top,
                         games[std::size_t(recent[std::size_t(std::clamp(dock_sel_, 0, n - 1))])].title);
}

/* Discs: the games' discs in a row on a glass strip; the chosen one lifts
 * and spins. */
void App::draw_dock_discs(const std::vector<int> &recent, double time)
{
    Gfx &g = *g_;
    auto &games = lib_->games();
    const int n = int(recent.size());
    const float d = 70, gap = 22, y = 949; /* the strip from y 902, below the library's own lines */
    const float w = float(n) * (d + gap) - gap + 64;
    g.panel(kCx - w * 0.5f, y - d * 0.5f - 12, w, d + 24, rgba(0x0F1F63, dock_focus_ ? 0.7f : 0.45f), 0.75f, kR,
            rgba(0x4C6FD8, dock_focus_ ? 0.9f : 0.5f), 1.4f, 0, 0.12f);
    const bool calm = settings_ && settings_->reduced_motion;
    float focus_cx = kCx, focus_top = y - d * 0.5f;
    for (int j = 0; j < n; ++j)
    {
        Game &game = games[std::size_t(recent[std::size_t(j)])];
        const bool on = dock_focus_ && j == dock_sel_;
        const float cx = kCx - w * 0.5f + 32 + d * 0.5f + float(j) * (d + gap);
        const float size = on ? d * (1.0f + 0.18f * dock_lift_) : d;
        const float cy = y - (on ? 8.0f * dock_lift_ : 0.0f);
        const float spin = calm ? 0.0f : on ? float(time) * 2.2f : float(j) * 1.3f;
        draw_disc(&game, cx, cy, size, spin, 0.0f, dock_focus_ && !on ? 0.75f : 1.0f, on);
        if (on)
        {
            focus_cx = cx;
            focus_top = cy - size * 0.5f;
        }
    }
    if (dock_focus_)
        draw_dock_bubble(focus_cx, focus_top,
                         games[std::size_t(recent[std::size_t(std::clamp(dock_sel_, 0, n - 1))])].title);
}

/* Recently Played along the bottom, in the look Settings > Interface picks:
 * a glass dock with "Recently Played" and the chosen name, then the boxes. */
void App::draw_dock(double time)
{
    Gfx &g = *g_;
    auto &games = lib_->games();
    const std::vector<int> recent = recent_games();
    const int n = int(recent.size());
    if (n == 0)
        return;
    if (settings_->recent_style == 2)
    {
        draw_dock_floating(recent);
        return;
    }
    if (settings_->recent_style == 3)
    {
        draw_dock_discs(recent, time);
        return;
    }
    const float ch = 74, cw = ch * 5.0f / 7.0f, gap = 18, label_w = 290, h = 96, y = 902;
    const float w = 32 + label_w + float(n) * (cw + gap) - gap + 32, x0 = kCx - w * 0.5f;
    g.panel(x0, y, w, h, rgba(0x0F1F63, dock_focus_ ? 0.80f : 0.55f), 0.75f, kR,
            rgba(0x4C6FD8, dock_focus_ ? 0.95f : 0.55f), 1.6f, 0, 0.12f);
    g.text(Font::SemiBold, ts(22), x0 + 32, y + 22, kLavender, Align::Left,
           fit(g, Font::SemiBold, ts(22), tr("Recently Played"), label_w - 20));
    const Game &first = games[std::size_t(recent[std::size_t(dock_focus_ ? std::clamp(dock_sel_, 0, n - 1) : 0)])];
    g.text(Font::Bold, ts(24), x0 + 32, y + 54, dock_focus_ ? kWhite : kSoft, Align::Left,
           fit(g, Font::Bold, ts(24), first.title, label_w - 20));
    for (int j = 0; j < n; ++j)
    {
        Game &game = games[std::size_t(recent[std::size_t(j)])];
        const bool on = dock_focus_ && j == dock_sel_;
        const float grow = on ? 1.0f + 0.22f * dock_lift_ : 1.0f;
        const float cx = x0 + 32 + label_w + float(j) * (cw + gap) + cw * 0.5f;
        const float cy = y + h * 0.5f - (on ? 10.0f : 0.0f);
        const float bw = cw * grow, bh = ch * grow;
        if (on)
            g.panel(cx - bw * 0.5f - 4, cy - bh * 0.5f - 4, bw + 8, bh + 8, kClear, 1, 8, kIcy, 2.4f, 10);
        if (Texture *cover = cover_of(game))
        {
            float uv[4];
            cover_uv(cover, bw, bh, uv);
            g.image_part(cover, cx - bw * 0.5f, cy - bh * 0.5f, bw, bh, uv, with_alpha(kWhite, on || !dock_focus_ ? 1.0f : 0.75f), 5);
        }
        else
            g.panel(cx - bw * 0.5f, cy - bh * 0.5f, bw, bh, with_alpha(kTileFill, 0.6f), 0.7f, 5, with_alpha(kEdge, 0.6f), 1.2f);
    }
}

std::string App::library_count() const
{
    const int shown = lib_->shown();
    switch (lib_->show())
    {
    case Library::Show::Wii: return plural((long long)shown, "1 Wii game", "{n} Wii games");
    case Library::Show::GameCube: return plural((long long)shown, "1 GameCube game", "{n} GameCube games");
    case Library::Show::Channels: return plural((long long)shown, "1 channel", "{n} channels");
    default: return plural((long long)shown, "1 game", "{n} games");
    }
}

std::string App::library_note() const
{
    if (!note_.empty())
        return note_;
    return time_ - flash_time_ < 4.0 ? flash_note_ : std::string();
}

void App::draw_sort()
{
    Gfx &g = *g_;
    g.panel(0, 0, 1920, 1080, rgba(0x02040C, 0.55f), 1, 0);
    const float w = 760, h = 440, x = 960 - w * 0.5f, y = 300;
    g.panel(x, y, w, h, rgba(0x13256F, 0.92f), 0.65f, kR, rgba(0x6FAEFF), 2.0f, 10, 0.25f);
    g.text_mid(Font::Bold, ts(36), x + 40, y + 62, kWhite, Align::Left, tr("Sort & Filter"));
    const std::string orders[4] = {tr("Title A-Z"), tr("Recently Played"), tr("Most Played"), tr("Favorites First")};
    const std::string shows[4] = {tr("All Games"), tr("GameCube"), tr("Wii"), tr("Channels")};
    const std::string names[3] = {tr("Sort By"), tr("Show"), tr("Get Covers and Info Now")};
    const std::string values[3] = {orders[int(lib_->sort_order())], shows[int(lib_->show())], ""};
    for (int i = 0; i < 3; ++i)
    {
        const float ry = y + 112 + i * 92, rh = 72, cy = ry + rh * 0.5f;
        const bool on = sort_row_ == i;
        g.panel(x + 40, ry, w - 80, rh, on ? rgba(0x1D3FA8, 0.9f) : rgba(0x0E1C55, 0.6f), 0.8f, kR,
                on ? kIcy : rgba(0x3D5AB0, 0.8f), on ? 2.4f : 1.4f, on ? 10 : 0);
        g.text_mid(Font::SemiBold, ts(30), x + 72, cy, on ? kWhite : kSoft, Align::Left, names[i]);
        if (!values[i].empty())
        {
            /* The choice between arrows, as the settings show theirs. */
            const float vx = x + w - 92;
            g.text_mid(Font::SemiBold, ts(28), vx, cy, on ? kCyan : kLavender, Align::Right, values[i]);
            if (on)
            {
                const float vw = g.measure(Font::SemiBold, ts(28), values[i]);
                g.glyph(Glyph::Arrow, vx - vw - 26, cy, 22, kIcy, -kPi * 0.5f);
                g.glyph(Glyph::Arrow, vx + 24, cy, 22, kIcy, kPi * 0.5f);
            }
        }
        else if (on)
            g.glyph(Glyph::Cross, x + w - 90, cy, 34, kWhite);
    }
    g.text(Font::Regular, ts(22), 960, y + h - 34, kLavender, Align::Center,
           sort_row_ == 2 ? tr("Downloads what's missing from GameTDB, and looks again for art it lacked.")
           : revolution() ? tr("Left and right change it; Circle is done.")
                          : tr("Changes show at once."));
    if (!revolution()) /* the home screen's bar has no room for the prompts */
        draw_prompts({{Glyph::DPad, "Change"}, {Glyph::Cross, sort_row_ == 2 ? "Get" : "Next"}},
                     {{Glyph::Circle, "Done"}}, "");
}

/* ---- details ------------------------------------------------------------------------------ */

void App::draw_details(double time)
{
    Gfx &g = *g_;
    auto &games = lib_->games();
    if (games.empty())
        return;
    Game &game = games[std::size_t(selected_)];
    /* The back of the box asked for now, so it's ready when Triangle turns
     * the box (loaded at the turn, a big back's upload made it stutter). */
    back_of(game);

    /* The box, large, in glass: 5 wide for 7 high. It flies in from the
     * library turning over once, and turns with the right stick. */
    const float cw = 486, ch = 680, ccx = 120 + cw * 0.5f, ccy = 150 + ch * 0.5f + 10;
    /* L2 / R2: the old box slides off one side as the new one swings in from
     * the other, and the panel follows a little behind. */
    const bool swiping = swipe_anim_ > 0 && swipe_from_ >= 0 && swipe_from_ < int(games.size());
    const float st = swiping ? ease_out(1.0f - swipe_anim_) : 1.0f;
    {
        const float p = ease_out(1.0f - flip_anim_);
        const float bx = kCx + (ccx - kCx) * p, by = kCy + (ccy - kCy) * p;
        const float bw = kTileW + (cw - kTileW) * p, bh = kTileH + (ch - kTileH) * p;
        g.set_layer(); /* the box is not part of the panel's fade */
        if (swiping)
        {
            Game &old = games[std::size_t(swipe_from_)];
            draw_tile(&old, ccx - float(swipe_dir_) * 760.0f * st, ccy, cw, ch, -float(swipe_dir_) * 0.9f * st,
                      1.0f - st, false, false);
        }
        draw_tile(&game, bx + (swiping ? float(swipe_dir_) * 760.0f * (1.0f - st) : 0.0f), by, bw, bh, box_yaw_,
                  swiping ? st : 1.0f, false, false);
        g.set_layer(layer_dx_ + (swiping ? float(swipe_dir_) * 120.0f * (1.0f - st) : 0.0f), layer_dy_,
                    layer_fade_ * (swiping ? 0.15f + 0.85f * st : 1.0f));
    }

    /* Information panel. */
    const float px = 690, py = 150, pw = 1140, ph = 790;
    g.panel(px, py, pw, ph, rgba(0x0F1F63, 0.62f), 0.75f, kR, rgba(0x4C6FD8, 0.9f), 1.8f, 0, 0.12f);

    /* The disc, turning slowly, at the panel's top right. */
    float text_w = pw - 104;
    float disc_bottom = 0; /* below it, text may take the panel's whole width */
    if (Texture *disc = disc_of(game))
    {
        const float d = 176, dcx = px + pw - 52 - d * 0.5f, dcy = py + 36 + d * 0.5f;
        disc_bottom = dcy + d * 0.5f + 14;
        const float angle = settings_->reduced_motion ? 0.0f : float(time) * 0.6f;
        Corner c[4];
        const float ca = std::cos(angle), sa = std::sin(angle), r = d * 0.5f;
        const float pts[4][2] = {{-r, -r}, {r, -r}, {r, r}, {-r, r}};
        for (int i = 0; i < 4; ++i)
            c[i] = Corner{dcx + pts[i][0] * ca - pts[i][1] * sa, dcy + pts[i][0] * sa + pts[i][1] * ca, 1};
        g.blob(dcx, dcy + 8, d * 1.2f, d * 1.2f, rgba(0x000000, 0.45f));
        g.quad3d(disc, c, d, d, kWhite, r, false, false);
        Corner gc[4];
        rect_at(dcx, dcy, r, r, 0, 0, 0, gc);
        gloss_over(g, gc, d, d, r, 0.7f);
        text_w = pw - 104 - d - 30;
    }

    const std::string title = game.db_title.empty() ? game.title : game.db_title;
    const auto title_lines = wrap(g, Font::Bold, ts(48), title, text_w, 2);
    float ty = py + 70;
    for (const std::string &l : title_lines)
    {
        g.text_mid(Font::Bold, ts(48), px + 52, ty, kWhite, Align::Left, l);
        ty += 56;
    }
    std::string sub = game.kind.empty() ? game.platform : tr(game.kind);
    if (!game.region.empty())
        sub += "   \xE2\x80\xA2   " + tr(game.region);
    if (!game.id.empty())
        sub += "   \xE2\x80\xA2   " + game.id;
    g.text_mid(Font::SemiBold, ts(26), px + 52, ty - 6, kCyan, Align::Left, sub);

    /* Facts: two columns of label and value. */
    std::vector<std::pair<std::string, std::string>> facts;
    if (!game.developer.empty()) facts.push_back({tr("Developer"), game.developer});
    if (!game.publisher.empty()) facts.push_back({tr("Publisher"), game.publisher});
    if (!game.released.empty()) facts.push_back({tr("Released"), game.released});
    if (!game.genre.empty()) facts.push_back({tr("Genre"), game.genre});
    if (game.players > 0) facts.push_back({tr("Players"), std::to_string(game.players)});
    if (!game.rating.empty()) facts.push_back({tr("Rating"), game.rating});
    facts.push_back({tr("Last Played"), relative_time(game.last_played, (long long)std::time(nullptr))});
    if (game.play_seconds > 0)
        facts.push_back({tr("Play Time"), play_time_text(game.play_seconds)});
    facts.push_back({tr("File"), game.format + "  \xE2\x80\xA2  " + human_size(game.bytes)});
    {
        /* A texture pack for it (Load/Textures/<ID> or its first three letters). */
        if (details_tex_for_ != game.id)
        {
            details_tex_for_ = game.id;
            details_tex_.clear();
            struct stat st{};
            for (const std::string &name : {game.id, game.id.substr(0, std::min<std::size_t>(3, game.id.size()))})
                if (details_tex_.empty() && !name.empty() &&
                    stat((saves_dir_ + "/User/Load/Textures/" + name).c_str(), &st) == 0 && S_ISDIR(st.st_mode))
                    details_tex_ = name;
        }
        if (!details_tex_.empty())
            facts.push_back({tr("Texture Pack"), settings_->custom_textures ? trf("On ({id})", {{"id", details_tex_}})
                                                                             : trf("Found ({id}), turned off",
                                                                                   {{"id", details_tex_}})});
    }
    if (const AchievementSet &ach = details_achievements(game); ach.valid())
        facts.push_back({tr("Achievements"), trf("{done} of {total}", {{"done", std::to_string(ach.unlocked)},
                                                                      {"total", std::to_string(ach.total)}})});
    if (!game.id.empty() && game.platform != "Wii")
    {
        /* How it plays in widescreen (ui_widescreen). */
        if (details_ws_for_ != game.id)
        {
            details_ws_for_ = game.id;
            details_ws_ = int(widescreen::kind_of(game.id, sys_dir_, false));
        }
        const auto kind = widescreen::Kind(details_ws_);
        facts.push_back({tr("Widescreen"), kind == widescreen::Kind::Patch    ? tr("16:9 Code")
                                           : kind == widescreen::Kind::Native ? tr("In the game's options")
                                                                              : tr("4:3 Only")});
    }
    /* Nine fit. When there are more (full game info, play time and a texture
     * pack), the least useful go first, so the texture pack and play time stay. */
    for (const char *drop : {"Rating", "Players", "Genre", "Publisher"})
    {
        if (facts.size() <= 9)
            break;
        const std::string label = tr(drop);
        for (auto it = facts.begin(); it != facts.end(); ++it)
            if (it->first == label)
            {
                facts.erase(it);
                break;
            }
    }
    if (facts.size() > 9)
        facts.resize(9);
    /* The back of the box (Triangle) gives the description the whole panel:
     * on the front, the facts leave it a few lines, often half a sentence. */
    const bool whole_text = box_back_ && !game.synopsis.empty();
    if (whole_text)
        facts.clear();
    const float fy0 = std::max(ty + 34, py + 236), col_w = (pw - 104) / 3.0f;
    for (std::size_t i = 0; i < facts.size(); ++i)
    {
        const float fx = px + 52 + float(i % 3) * col_w, fy = fy0 + float(i / 3) * 62;
        g.text_mid(Font::Regular, ts(22), fx, fy, kLavender, Align::Left, facts[i].first);
        g.text_mid(Font::SemiBold, ts(27), fx, fy + 28, kWhite, Align::Left,
                   fit(g, Font::SemiBold, ts(27), facts[i].second, col_w - 30));
    }
    /* The whole description runs the panel's width: below the disc, not
     * under it. */
    float dy = whole_text ? std::max(ty + 20, disc_bottom) : fy0 + float((facts.size() + 2) / 3) * 62 + 6;

    /* What the game is about. */
    const int action_count = details_shots_ > 0 ? 5 : 4; /* Screenshots, when it has some */
    const float actions_y = py + ph - float(action_count) * 66 - 24;
    if (whole_text)
    {
        /* All of it: the largest size it fits at (worked out once for the
         * game and the room, not every frame of the box turning). */
        const std::string key = game.path + "|" + std::to_string(int(actions_y - dy)) + "|" +
                                std::to_string(ts(25)) + "|" + std::to_string(game.synopsis.size()) + "|" +
                                std::to_string(settings_->ui_font) + "|" + std::to_string(settings_->ui_theme);
        if (key != whole_text_for_)
        {
            const float sizes[3] = {25, 23, 21};
            whole_text_size_ = sizes[2];
            whole_text_line_ = 30;
            for (float sz : sizes)
            {
                const float lh = sz * 1.36f;
                const std::size_t fits = std::size_t(std::max(0.0f, (actions_y - 16 - dy) / lh));
                const std::vector<std::string> all = wrap(g, Font::Regular, ts(sz), game.synopsis, pw - 104);
                whole_text_lines_ = wrap(g, Font::Regular, ts(sz), game.synopsis, pw - 104, fits);
                whole_text_size_ = sz;
                whole_text_line_ = lh;
                if (all.size() <= fits)
                    break;
            }
            whole_text_for_ = key;
        }
        for (const std::string &l : whole_text_lines_)
        {
            g.text_mid(Font::Regular, ts(whole_text_size_), px + 52, dy + 12, kSoft, Align::Left, l);
            dy += whole_text_line_;
        }
    }
    else if (const std::size_t room = std::size_t(std::max(0.0f, (actions_y - 16 - dy) / 34.0f));
             !game.synopsis.empty() && room > 0)
    {
        /* More than fits: a line less of it, and where the rest is. */
        const bool more = room >= 2 && wrap(g, Font::Regular, ts(25), game.synopsis, pw - 104).size() > room;
        for (const std::string &l : wrap(g, Font::Regular, ts(25), game.synopsis, pw - 104, more ? room - 1 : room))
        {
            g.text_mid(Font::Regular, ts(25), px + 52, dy + 12, kSoft, Align::Left, l);
            dy += 34;
        }
        if (more)
            g.text_mid(Font::SemiBold, ts(22), px + 52, dy + 12, kCyan, Align::Left,
                       tr("The whole description is on the back of the box (Triangle)."));
    }
    else if (game.synopsis.empty() && actions_y - 16 - dy >= 34)
        g.text_mid(Font::Regular, ts(24), px + 52, dy + 12, with_alpha(kLavender, 0.8f), Align::Left,
                   settings_->download_info ? tr("No description yet. It arrives with the game info from GameTDB.com.")
                                            : tr("Turn on Settings > Downloads > Download Game Info for a description."));

    const std::string actions[5] = {title_case(tr("Play")), title_case(tr("Save States")),
                                    title_case(tr("Game Settings")), title_case(tr("Save Data")),
                                    title_case(tr("Screenshots"))};
    for (int i = 0; i < action_count; ++i)
    {
        const float ry = actions_y + i * 66, rx = px + 52 + 40, rw = pw - 104 - 40, rh = 58;
        const bool on = details_row_ == i;
        g.panel(rx, ry, rw, rh, on ? rgba(0x153A9E, 0.9f) : rgba(0x0E1C55, 0.55f), 0.8f, kR,
                on ? kIcy : rgba(0x3D5AB0, 0.8f), on ? 2.6f : 1.4f, on ? 12 : 0, on ? 0.15f : 0.0f);
        g.text_mid(Font::SemiBold, ts(29), rx + 36, ry + rh * 0.5f, on ? kWhite : kSoft, Align::Left, actions[i]);
        if (i == 1 && details_states_ > 0)
            g.text_mid(Font::SemiBold, ts(22), rx + rw - 30, ry + rh * 0.5f, kCyan, Align::Right,
                       plural(details_states_, "1 saved", "{n} saved"));
        if (i == 4)
            g.text_mid(Font::SemiBold, ts(22), rx + rw - 30, ry + rh * 0.5f, kCyan, Align::Right,
                       plural(details_shots_, "1 screenshot", "{n} screenshots"));
        if (i == 2 && details_custom_)
            g.text_mid(Font::SemiBold, ts(22), rx + rw - 30, ry + rh * 0.5f, kCyan, Align::Right, tr("Custom"));
        else if (i == 2)
        {
            recommend::Pick pick;
            if (recommend::pick_for(game.id, pick))
                g.text_mid(Font::SemiBold, ts(22), rx + rw - 30, ry + rh * 0.5f, kCyan, Align::Right,
                           tr("Auto Boost"));
        }
        if (on)
            g.glyph(Glyph::Arrow, rx - 26, ry + rh * 0.5f, 32, kWhite, kPi * 0.5f);
    }
    g.set_layer(layer_dx_, layer_dy_, layer_fade_);
    if (screen_ != Screen::States) /* the save states bring their own */
    {
        std::vector<std::pair<Glyph, std::string>> right = {
            {kKeyL2R2, "Other Games"}, {Glyph::Triangle, box_back_ ? "Front of Box" : "Back of Box"}};
        if (details_achievements(game).valid())
            right.insert(right.begin() + 1, {Glyph::Square, "Achievements"});
        draw_prompts({{Glyph::Cross, "Confirm"}, {Glyph::Circle, "Back"}}, right,
                     games.size() > 1 ? "" : tr("Right stick: turn the box"));
    }
}

/* ---- memory cards ------------------------------------------------------------------------- */

/* One card, the way the GameCube shows it: the slot letter, the free blocks,
 * and a grid of save icons on glass. */
void App::draw_card(Card &card, int which, float x, float y, double time)
{
    Gfx &g = *g_;
    (void)time;
    const bool active = mc_card_ == which;
    g.panel(x, y, kMcW, kMcH, rgba(0x0F1F63, 0.62f), 0.75f, kR,
            active ? rgba(0x6FAEFF, 0.95f) : rgba(0x3D4F9E, 0.8f), active ? 2.2f : 1.6f, 0, 0.12f);

    /* Header: the slot letter, the card, its free blocks. */
    const float head_cy = y + 66;
    const float bx = x + 34, bs = 72;
    g.panel(bx, head_cy - bs * 0.5f, bs, bs, rgba(0x1F63F0), 0.62f, kR, rgba(0x7FD9FF), 1.8f, active ? 8 : 0,
            0.35f);
    g.text_mid(Font::ExtraBold, 44, bx + bs * 0.5f, head_cy, kWhite, Align::Center, card.slot);
    const int n = int(card.saves.size());
    std::string sub;
    if (saves_loading() && !cards_scanned_)
        sub = ""; /* still being read */
    else if (!card.present)
        sub = tr("No saves yet");
    else
        sub = plural(n, "1 save", "{n} saves");
    g.text_mid(Font::Bold, ts(32), bx + bs + 24, head_cy - 17, kWhite, Align::Left, trf("Slot {slot}", {{"slot", card.slot}}));
    g.text_mid(Font::Regular, ts(24), bx + bs + 24, head_cy + 21, kLavender, Align::Left, sub);

    const std::string free = card.present ? std::to_string(card.free_blocks) : std::to_string(card.total_blocks);
    const float num_w = g.measure(Font::Bold, ts(32), free);
    const float box_w = std::max(116.0f, num_w + 44), box_h = 54, box_x = x + kMcW - 34 - box_w;
    g.panel(box_x, head_cy - box_h * 0.5f, box_w, box_h, rgba(0x07102E, 0.6f), 1, kR, rgba(0x8BD9FF, 0.9f), 1.8f);
    g.text_mid(Font::Bold, ts(32), box_x + box_w * 0.5f, head_cy, kWhite, Align::Center, free);
    g.text_mid(Font::SemiBold, ts(26), box_x - 16, head_cy, kSoft, Align::Right, trc("memcard", "Open"));

    /* Grid, scrolled so the focused row shows. */
    const int rows_total = std::max(kMcRows, (n + kMcCols - 1) / kMcCols);
    const int sel = mc_sel_[which];
    int first = int(mc_scroll_[which]);
    if (sel / kMcCols < first)
        first = sel / kMcCols;
    if (sel / kMcCols > first + kMcRows - 1)
        first = sel / kMcCols - (kMcRows - 1);
    first = std::clamp(first, 0, rows_total - kMcRows);
    mc_scroll_[which] = float(first);

    const float grid_w = kMcCols * kMcTile + (kMcCols - 1) * kMcGap;
    const float gx = x + (kMcW - grid_w) * 0.5f, gy = y + 146;
    float focus_cx = -1, focus_top = 0;
    for (int pass = 0; pass < 2; ++pass) /* the focused tile last, above its neighbours */
        for (int r = 0; r < kMcRows; ++r)
            for (int c = 0; c < kMcCols; ++c)
            {
                const int idx = (first + r) * kMcCols + c;
                const bool focused = active && idx == sel;
                if ((pass == 1) != focused)
                    continue;
                const float tx = gx + c * (kMcTile + kMcGap), ty = gy + r * (kMcTile + kMcGap);
                if (idx >= n)
                {
                    /* An empty slot, pressed into the card. */
                    g.panel(tx, ty, kMcTile, kMcTile, rgba(0x050C26, 0.55f), 1.0f, kR,
                            focused ? kIcy : rgba(0x2A3C80, 0.65f), focused ? 2.4f : 1.4f);
                    if (focused)
                    {
                        focus_cx = tx + kMcTile * 0.5f;
                        focus_top = ty;
                    }
                    continue;
                }
                Save &s = card.saves[std::size_t(idx)];
                /* A small glass block; the focused one rises out of the card toward you. */
                const float bcx = tx + kMcTile * 0.5f, bcy = ty + kMcTile * 0.5f;
                const float z0 = focused ? -90.0f * mc_lift_ : 0.0f;
                Glass face;
                face.tint = kGlassBody;
                face.rim = focused ? kIcy : kEdge;
                face.radius = kR;
                face.rim_w = focused ? 3.0f : 2.0f;
                face.glow = focused ? 16.0f : 0.0f;
                face.phase = bcx / 1920.0f + bcy / 3000.0f;
                glass_block(g, bcx, bcy, kMcTile, kMcTile, 20.0f, 0, z0, face.glow * 3, face);
                Corner ic[4];
                rect_at(bcx, bcy, 48, 48, 0, 0, z0, ic);
                if (Texture *icon = save_icon(s))
                    g.quad3d(icon, ic, 96, 96, kWhite, 0, false, false);
                else if (g.brand_mask())
                {
                    rect_at(bcx, bcy, 36, 36, 0, 0, z0, ic);
                    g.quad3d(g.brand_mask(), ic, 72, 72, with_alpha(kCyan, 0.85f), 0, false, false);
                }
                Corner tc[4];
                rect_at(bcx, bcy, kMcTile * 0.5f, kMcTile * 0.5f, 0, 0, z0, tc);
                Glass gloss;
                gloss.face = 2;
                gloss.radius = kR;
                gloss.rim_w = 0;
                gloss.tint = kWhite;
                gloss.rim = kClear;
                gloss.phase = face.phase;
                g.glass(tc, kMcTile, kMcTile, 0, gloss);
                if (focused)
                {
                    focus_cx = (tc[0].x + tc[1].x) * 0.5f;
                    focus_top = tc[0].y;
                }
            }

    /* More saves above or below the visible rows. */
    if (first > 0)
        g.glyph(Glyph::Arrow, x + kMcW - 22, gy + 16, 18, rgba(0x58B8FF), 0);
    if (first + kMcRows < rows_total && (first + kMcRows) * kMcCols < n)
        g.glyph(Glyph::Arrow, x + kMcW - 22, gy + kMcRows * (kMcTile + kMcGap) - kMcGap - 16, 18,
                rgba(0x58B8FF), kPi);

    (void)focus_cx;
    (void)focus_top;
}

void App::draw_memory_cards(double time)
{
    Gfx &g = *g_;
    if (mc_view() == 2)
    {
        draw_saves_by_game(time);
        return;
    }
    if (mc_wii_)
    {
        draw_wii_saves(time);
        return;
    }
    if (mc_view() == 1)
    {
        draw_card_blocks(time);
        return;
    }
    if (mc_view() == 4)
    {
        draw_card_classic(time);
        return;
    }
    ensure_saves(false);
    if (mc_view() == 3)
    {
        draw_card_cubes(card_a_, 0, kMcX[0], kMcY, time);
        draw_card_cubes(card_b_, 1, kMcX[1], kMcY, time);
    }
    else
    {
        draw_card(card_a_, 0, kMcX[0], kMcY, time);
        draw_card(card_b_, 1, kMcX[1], kMcY, time);
    }

    /* Information bar: the focused save's banner and what it is. */
    const float ix = kMcX[0], iy = 812, iw = kMcX[1] + kMcW - kMcX[0], ih = 128, icy = iy + ih * 0.5f;
    g.panel(ix, iy, iw, ih, rgba(0x0A1236, 0.72f), 0.85f, kR, rgba(0x4C6FD8, 0.9f), 1.8f, 0, 0.12f);
    Card &card = mc_card_ == 0 ? card_a_ : card_b_;
    const int n = int(card.saves.size()), sel = mc_sel_[mc_card_];
    const float pic_x = ix + 18, pic_w = 288, pic_h = 96;
    const float text_x = pic_x + pic_w + 30;
    if (sel < n)
    {
        Save &s = card.saves[std::size_t(sel)];
        if (Texture *banner = save_banner(s))
        {
            g.image(banner, pic_x, icy - pic_h * 0.5f, pic_w, pic_h, kWhite, kR);
            g.panel(pic_x, icy - pic_h * 0.5f, pic_w, pic_h, kClear, 1, kR, rgba(0x7FB6FF, 0.8f), 1.6f);
        }
        else
        {
            g.panel(pic_x, icy - pic_h * 0.5f, pic_w, pic_h, kTileFill, 0.55f, kR, kEdge, 1.6f);
            if (Texture *icon = save_icon(s))
                g.image(icon, pic_x + pic_w * 0.5f - 40, icy - 40, 80, 80, kWhite);
        }
        const std::string blocks = plural(s.blocks, "1 block", "{n} blocks");
        const float right_x = ix + iw - 32;
        const float bw = g.measure(Font::Bold, ts(32), blocks);
        const float tw = right_x - std::max(bw, 260.0f) - 40 - text_x;
        const std::string title = s.title.empty() ? s.file_name : s.title;
        g.text_mid(Font::Bold, ts(36), text_x, icy - 19, kWhite, Align::Left, fit(g, Font::Bold, ts(36), title, tw));
        g.text_mid(Font::Regular, ts(26), text_x, icy + 23, kLavender, Align::Left,
                   fit(g, Font::Regular, ts(26), s.detail.empty() ? s.game_code : s.detail, tw));
        g.text_mid(Font::Bold, ts(32), right_x, icy - 19, kWhite, Align::Right, blocks);
        g.text_mid(Font::Regular, ts(26), right_x, icy + 23, kLavender, Align::Right, format_date(s.modified));
    }
    else
    {
        g.panel(pic_x, icy - pic_h * 0.5f, pic_w, pic_h, rgba(0x050C26, 0.55f), 1.0f, kR, rgba(0x2A3C80, 0.65f),
                1.4f);
        draw_mark(pic_x + pic_w * 0.5f, icy, 90, with_alpha(kCyan, 0.6f));
        g.text_mid(Font::Bold, ts(36), text_x, icy - 19, kWhite, Align::Left,
                   trf("Nothing saved in Slot {slot} yet", {{"slot", card.slot}}));
        g.text_mid(Font::Regular, ts(26), text_x, icy + 23, kLavender, Align::Left,
                   tr("Your saves appear here after you save in a game."));
    }

    char pos[48];
    if (n > 0)
        std::snprintf(pos, sizeof pos, "%s   %02d / %02d", trf("SLOT {slot}", {{"slot", card.slot}}).c_str(), sel + 1, n);
    else
        std::snprintf(pos, sizeof pos, "%s", trf("SLOT {slot}", {{"slot", card.slot}}).c_str());
    if (sel < n)
        draw_prompts({{Glyph::DPad, "Browse"}, {kKeyL2R2, "Wii Saves"}, {Glyph::Circle, "Back"}},
                     {{Glyph::Options, "To USB"}, {Glyph::Square, trf("Copy to {slot}", {{"slot", mc_card_ == 0 ? "B" : "A"}})}, {Glyph::Triangle, "Delete"}},
                     pos);
    else
        draw_prompts({{Glyph::DPad, "Browse"}, {kKeyL2R2, "Wii Saves"}, {Glyph::Circle, "Back"}}, {}, pos);
}

void App::release_covers()
{
    for (Game &game : lib_->games())
    {
        if (game.cover)
            g_->free_texture(game.cover);
        if (game.disc)
            g_->free_texture(game.disc);
        if (game.back)
            g_->free_texture(game.back);
        if (game.spine)
            g_->free_texture(game.spine);
        game.cover = game.disc = game.back = game.spine = nullptr;
        game.cover_tried = game.disc_tried = game.back_tried = game.spine_tried = false;
    }
}

/* A new theme starts from its own view: every game, and the GameCube cards
 * (Porpoise) or the Wii saves (Revolution). */
void App::apply_look()
{
    if (!settings_)
        return;
    g_->set_look(look_for(*settings_));
    g_->set_theme_fonts(fonts_for(*settings_));
    kR = th().radius;
    theme_seen_ = settings_->ui_theme;
}

/* Over everything: the theme's tube, terminal or tape, and a TV's frame. */
void App::draw_theme_overlay()
{
    Gfx &g = *g_;
    const Theme &t = th();
    g.set_tone(false);
    {
        Look full = g.look();
        full.content_scale = 1; /* the frame is the TV's, round everything */
        g.set_look(full);
    }
    if (t.overlay)
        g.fx(t.overlay, settings_->reduced_motion ? 0.6f : 1.0f);
    if (t.bezel)
    {
        /* The set's plastic, its curved glass's dark edge, and a glint. */
        const float inset = 26, b = 140, r = 64;
        g.panel(inset - b, inset - b, 1920 - inset * 2 + b * 2, 1080 - inset * 2 + b * 2, kClear, 1, r + b,
                rgba(0x15171B), b);
        g.panel(inset, inset, 1920 - inset * 2, 1080 - inset * 2, kClear, 1, r, rgba(0x000000, 0.55f), 10);
        g.panel(inset - 3, inset - 3, 1920 - inset * 2 + 6, 1080 - inset * 2 + 6, kClear, 1, r + 3,
                rgba(0x3A3E46, 0.9f), 2);
    }
}

void App::look_changed(int was)
{
    /* A new theme brings its own colours and font; both can be changed after. */
    settings_->ui_palette = 0;
    settings_->ui_font = 0;
    apply_look();
    if (starcube())
    {
        /* Into Star Cube from Settings: stay on its Settings page. */
        sc_home_ = false;
        sc_calendar_ = false;
        sc_face_ = sc_face_of_tab();
        sc_zoom_ = 1;
    }
    if ((was == 1) == (settings_->ui_theme == 1) && was >= 0)
        return; /* only Revolution brings its own library filter and saves view */
    /* Every game, GameCube and Wii, in every look (Revolution once showed
     * only Wii games when there were any). */
    const int want = int(Library::Show::All);
    mc_wii_ = settings_->ui_theme == 1;
    wii_sel_ = 0;
    wii_scroll_ = 0;
    const std::string key = lib_->shown() > 0 ? Library::key_of(lib_->games()[std::size_t(selected_)]) : "";
    lib_->set_show(Library::Show(want));
    keep_selection(key);
    lib_->save();
    settings_->save(settings_path_);
    home_pointing_ = false;
}

void App::keep_selection(const std::string &key)
{
    auto &games = lib_->games();
    selected_ = 0;
    for (int i = 0; i < lib_->shown(); ++i)
        if (Library::key_of(games[std::size_t(i)]) == key)
            selected_ = i;
    scroll_ = float(selected_);
    home_synced_ = false;
    if (lib_->shown() > 0)
        lib_->set_selected(Library::key_of(games[std::size_t(selected_)]));
}

void App::library_changed()
{
    details_ach_for_.clear();
    details_ws_for_.clear();
    auto &games = lib_->games();
    const std::string key = lib_->selected_id();
    selected_ = 0;
    for (int i = 0; i < lib_->shown(); ++i)
        if (Library::key_of(games[std::size_t(i)]) == key)
            selected_ = i;
    selected_ = std::clamp(selected_, 0, std::max(0, lib_->shown() - 1));
    scroll_ = float(selected_);
    home_synced_ = false;
    build_settings();
}

void App::cover_arrived(const std::string &id)
{
    for (Game &game : lib_->games())
        if (game.id == id)
        {
            if (game.cover)
                g_->free_texture(game.cover);
            if (game.disc)
                g_->free_texture(game.disc);
            if (game.back)
                g_->free_texture(game.back);
            if (game.spine)
                g_->free_texture(game.spine);
            game.cover = game.disc = game.back = game.spine = nullptr;
            game.cover_tried = game.disc_tried = game.back_tried = game.spine_tried = false;
        }
}

/* ---- frame ------------------------------------------------------------------------------- */

void App::draw(double time)
{
    Gfx &g = *g_;
    apply_look();
    /* The game the player is on: where its cover is drawn is kept, for the
     * glide into the launch screen. */
    if (!launch_ && lib_ && selected_ >= 0 && selected_ < lib_->shown())
        g.watch(lib_->games()[std::size_t(selected_)].cover);
    if (screen_ == Screen::Welcome)
    {
        draw_welcome(time);
        draw_dialog();
        draw_theme_overlay();
        return;
    }
    if (revolution())
    {
        draw_revolution(time);
        return;
    }
    if (starcube())
    {
        draw_starcube(time);
        return;
    }
    g.background();
    if (tab_ == Tab::Library && (screen_ == Screen::Main || screen_ == Screen::Sort) && settings_ &&
        settings_->lib_view == 9)
        draw_spotlight_backdrop();
    if (th().light)
        g.set_tone(true);
    intro_stage(0.0f);
    draw_top_bar();
    intro_stage(0.3f); /* then the page */

    /* Motion: a new tab slides in from the side it came from; a screen that
     * opens rises and fades in. */
    float dx = 0, dy = 0, fade = 1;
    if (tab_anim_ > 0)
    {
        const float p = ease_out(1.0f - tab_anim_);
        dx = float(tab_dir_) * 150.0f * (1.0f - p);
        fade = 0.2f + 0.8f * p;
    }
    if (screen_anim_ > 0)
    {
        const float p = ease_out(1.0f - screen_anim_);
        dy = 22.0f * (1.0f - p);
        fade *= 0.25f + 0.75f * p;
    }
    g.set_layer(dx, dy, fade);
    layer_dx_ = dx;
    layer_dy_ = dy;
    layer_fade_ = fade;
    if (screen_ == Screen::Details)
        draw_details(time);
    else if (screen_ == Screen::States)
    {
        draw_details(time);
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
    else
        switch (tab_)
        {
        case Tab::Library:
            draw_library(time);
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
    g.set_layer();
    draw_dialog();
    draw_update_overlay(time);
    g.set_intro(1, 0);
    draw_theme_overlay();
}

/* The Revolution look: its own home and opened tile, and the other screens
 * drawn light over its room. */
void App::draw_revolution(double time)
{
    Gfx &g = *g_;
    g.set_layer();
    g.set_tone(false);
    draw_room();
    g.set_intro(intro_fade_, intro_dy_);
    const bool home = home_showing();
    const bool opened = tab_ == Tab::Library && (screen_ == Screen::Details || screen_ == Screen::States);
    /* While a tile opens or closes, the zoom decides which side shows. */
    const bool zooming = zoom_anim_ > 0 && tab_ == Tab::Library;
    if (zooming ? zoom_shows_home() : home)
        draw_home(time);
    else if (opened || zooming)
        draw_rev_details(time);
    /* The full-screen pages (the setups, the guide, a folder) have headings of their own. */
    const bool full_page = screen_ == Screen::WiiSetup || screen_ == Screen::WiiGuide || screen_ == Screen::Mapping ||
                           screen_ == Screen::Browse || screen_ == Screen::GameSettings ||
                           screen_ == Screen::TileArt || screen_ == Screen::Shots || screen_ == Screen::Controllers;
    if (!full_page)
        draw_rev_top_bar(!home);
    draw_zoom(time);

    float dx = 0, dy = 0, fade = 1;
    if (tab_anim_ > 0)
    {
        const float p = ease_out(1.0f - tab_anim_);
        dx = float(tab_dir_) * 150.0f * (1.0f - p);
        fade = 0.2f + 0.8f * p;
    }
    if (screen_anim_ > 0 && !opened)
    {
        const float p = ease_out(1.0f - screen_anim_);
        dy = 22.0f * (1.0f - p);
        fade *= 0.25f + 0.75f * p;
    }
    g.set_tone(true);
    g.set_layer(dx, dy, fade);
    layer_dx_ = dx;
    layer_dy_ = dy;
    layer_fade_ = fade;
    if (screen_ == Screen::States)
        draw_states();
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
    else if (!opened)
        switch (tab_)
        {
        case Tab::Library:
            if (!home)
                draw_library(time); /* the cover flow, in white */
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
    g.set_layer();
    draw_dialog();
    draw_update_overlay(time);
    g.set_tone(false);
    draw_rev_pointer();
    g.set_intro(1, 0);
}

/* Downloading, installing, then done: a box over everything. */
void App::draw_update_overlay(double time)
{
    if (!updating())
        return;
    Gfx &g = *g_;
    g.panel(0, 0, 1920, 1080, rgba(0x02040C, 0.75f), 1, 0);
    const float w = 900, h = 330, x = 960 - w * 0.5f, y = 375;
    Glass face;
    face.tint = rgba(0x13308A, 0.92f);
    face.rim = rgba(0x8BD9FF);
    face.radius = kR;
    face.rim_w = 2.2f;
    face.glow = 12;
    face.phase = 0.4f;
    glass_block(g, 960, y + h * 0.5f, w, h, 22, 0, 0, 36, face);
    const bool done = update_phase_ == 4;
    g.text_mid(Font::Bold, ts(40), 960, y + 74, kWhite, Align::Center,
               done ? tr("Porpoise is updated")
                    : trf("Updating to Porpoise {version}", {{"version", latest_version_}}));
    if (done)
    {
        for (const std::string &l : wrap(g, Font::Regular, ts(26), tr("Press Cross to close Porpoise, then open it again "
                                                                      "from the home screen."), w - 120, 2))
        {
            g.text_mid(Font::Regular, ts(26), 960, y + 150, kSoft, Align::Center, l);
            break;
        }
        g.glyph(Glyph::Cross, 960 - 110, y + 250, 40, kWhite);
        g.text_mid(Font::Bold, ts(28), 960 - 80, y + 250, kWhite, Align::Left, tr("Close Porpoise"));
        return;
    }
    const bool installing = update_phase_ == 3, finishing = update_phase_ == 7;
    const float frac = finishing ? 1.0f : update_total_ ? float(update_done_) / float(update_total_) : 0.0f;
    const std::string what =
        finishing    ? tr("Finishing up\xE2\x80\xA6")
        : installing ? trf("Putting the new files in place: {done} of {total}",
                           {{"done", std::to_string(std::min(update_done_ + 1, update_total_))},
                            {"total", std::to_string(update_total_)}})
                     : trf("Downloading: {done} of {total} MB",
                           {{"done", std::to_string(update_done_ >> 20)}, {"total", std::to_string(update_total_ >> 20)}});
    g.text_mid(Font::Regular, ts(26), 960, y + 150, kSoft, Align::Center, what);
    const float bx = x + 70, bw = w - 140, by = y + 205;
    g.panel(bx, by, bw, 26, rgba(0x07102E, 0.7f), 1, 13, rgba(0x3D5AB0, 0.9f), 1.4f);
    g.panel(bx + 3, by + 3, std::max(20.0f, (bw - 6) * std::clamp(frac, 0.0f, 1.0f)), 20, rgba(0x5CD3FF), 0.8f, 10);
    if (installing || finishing)
    {
        /* A spinner: a big file (or the last steps) takes a while, and the
         * bar alone looked stuck. */
        const float sx = bx + bw + 30, sy = by + 13;
        for (int i = 0; i < 8; ++i)
        {
            const float a = float(i) / 8.0f * 2 * kPi + (settings_->reduced_motion ? 0.0f : float(time) * 5.0f);
            const float alpha = 0.25f + 0.75f * float(i) / 7.0f;
            g.panel(sx + std::cos(a) * 12 - 3, sy + std::sin(a) * 12 - 3, 6, 6, rgba(0x5CD3FF, alpha), 1, 3);
        }
    }
    g.text_mid(Font::Regular, ts(21), 960, y + 280, kLavender, Align::Center,
               tr("Keep Porpoise open until it's done."));
}

/* ---- launch ------------------------------------------------------------------------------- */

float App::launch_intro(double time) const
{
    if (settings_ && settings_->reduced_motion)
        return 1.0f;
    return std::clamp(float((time - launch_start_) / 0.5), 0.0f, 1.0f);
}

void App::begin_launch(Game *game)
{
    /* Where its cover was in the last frame: it glides from there. */
    launch_from_ok_ = game && game->cover && g_->watched(launch_from_) && launch_from_[2] > 8 && launch_from_[3] > 8;
    launch_ = game;
    launch_status_ = "Loading Dolphin";
    launch_progress_ = -1;
    launch_start_ = time_;
    if (game && !game->boot_menu) /* a menu's chosen game was marked when it was chosen */
        lib_->mark_played(*game);
}

void App::set_launch_status(const std::string &status, float progress)
{
    launch_status_ = status;
    launch_progress_ = progress;
}

void App::draw_launch(double time)
{
    apply_look();
    Gfx &g = *g_;
    /* The entrance (about half a second, from Play): the library fades under
     * the launch screen, the cover glides from where it was to the middle and
     * grows, the tile rises into place under it, then the words and the bar. */
    const float k = launch_intro(time);
    auto ease = [](float t) { return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); };
    const float bg = ease(std::min(1.0f, k * 1.6f));
    const float rise = ease(std::clamp((k - 0.15f) / 0.6f, 0.0f, 1.0f));
    const float words = std::clamp((k - 0.6f) / 0.4f, 0.0f, 1.0f);
    if (revolution())
    {
        g.set_tone(false);
        g.set_layer(0, 0, bg);
        draw_room();
        g.set_layer();
        g.set_tone(true); /* the rest drawn light */
    }
    else
        g.background(bg);
    g.set_layer(0, 0, words);
    draw_brand(kBarCy);
    g.text_mid(Font::SemiBold, ts(27), 1866, kBarCy, rgba(0x9FD8FF), Align::Right, tr("Launching"));
    g.set_layer();

    /* The tile: the game's cover when there is one, else the Porpoise mark. */
    const float size = 330, cx = 960, cy = 420;
    Texture *cover = launch_ ? cover_of(*launch_) : nullptr;
    Glass face;
    face.tint = rgba(0x1E50D8, 0.80f);
    face.rim = rgba(0x9FDFFF);
    face.radius = kR;
    face.rim_w = 3.0f;
    face.glow = 22;
    face.phase = 0.5f;
    g.set_layer(0, 60.0f * (1.0f - rise), rise);
    glass_block(g, cx, cy, size, size, 40.0f, 0, 0, 66, face);
    /* Its reflection on the floor. */
    g.blob(cx, cy + size * 0.5f + 40, size * 1.4f, 120, rgba(0x2F7BFF, 0.35f));
    g.set_layer();
    if (cover)
    {
        const float ch = size - 36, cw = ch * float(cover->width) / float(cover->height);
        float x = cx - cw * 0.5f, y = cy - ch * 0.5f, w = cw, h = ch;
        if (k < 1.0f && launch_from_ok_)
        {
            /* From where it was, lifted a little on the way (a shadow under it). */
            const float e = ease(k);
            const float lift = std::sin(k * kPi);
            x = launch_from_[0] + (x - launch_from_[0]) * e;
            y = launch_from_[1] + (y - launch_from_[1]) * e - 26.0f * lift;
            w = launch_from_[2] + (w - launch_from_[2]) * e;
            h = launch_from_[3] + (h - launch_from_[3]) * e;
            const float grow = 1.0f + 0.05f * lift;
            x -= w * (grow - 1.0f) * 0.5f;
            y -= h * (grow - 1.0f) * 0.5f;
            w *= grow;
            h *= grow;
            g.blob(x + w * 0.5f, y + h + 26.0f + 20.0f * lift, w * 1.2f, 70, rgba(0x000000, 0.35f * lift));
        }
        g.image(cover, x, y, w, h, kWhite, kR * 0.6f);
        Corner gc[4];
        rect_at(x + w * 0.5f, y + h * 0.5f, w * 0.5f, h * 0.5f, 0, 0, 0, gc);
        Glass gloss;
        gloss.face = 2;
        gloss.radius = kR * 0.6f;
        gloss.rim_w = 0;
        gloss.tint = kWhite;
        gloss.rim = kClear;
        gloss.phase = 0.5f;
        g.glass(gc, w, h, 0, gloss);
    }
    else if (!launch_ || launch_->cover_wait.empty())
    {
        /* no cover at all (not one still coming back after the game took the
         * screen: that showed the mark for a moment) */
        g.set_layer(0, 60.0f * (1.0f - rise), rise);
        draw_mark(cx, cy, 250, rgba(0x6EDCFF));
        g.set_layer();
    }

    g.set_layer(0, 16.0f * (1.0f - words), words);
    g.text(Font::SemiBold, ts(52), 960, 690, kWhite, Align::Center,
           launch_ ? trf("Starting {game}\xE2\x80\xA6", {{"game", launch_->title}}) : tr("Starting game\xE2\x80\xA6"));

    /* Progress: measured when known, otherwise a moving segment. */
    const float bx = 591, by = 800, bw = 738, bh = 24;
    g.panel(bx, by, bw, bh, rgba(0x08113A, 0.9f), 1, kR, rgba(0x3B5BB5, 0.95f), 2.0f);
    if (launch_progress_ >= 0)
        g.panel(bx + 3, by + 3, std::max(18.0f, (bw - 6) * std::min(launch_progress_, 1.0f)), bh - 6,
                rgba(0x5AD8FF), 0.85f, kR, rgba(0xBDF1FF, 0.6f), 0, 10);
    else
    {
        const float t = float(std::fmod(time - launch_start_, 1.6) / 1.6);
        const float seg = 220, sx = bx + 3 + (bw - 6 - seg) * (0.5f - 0.5f * std::cos(t * 2 * kPi));
        g.panel(sx, by + 3, seg, bh - 6, rgba(0x5AD8FF), 0.85f, kR, rgba(0xBDF1FF, 0.6f), 0, 10);
    }
    g.text(Font::SemiBold, ts(28), 960, 852, rgba(0xB6BDE8), Align::Center, tr(launch_status_));
    g.set_layer();
}
} // namespace porpoise::ui
