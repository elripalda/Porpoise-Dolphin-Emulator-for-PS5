/* Porpoise UI - the notices for real Wii Remotes (a DolphinBar): "Wii
 * Controller Port n connected / disconnected", in the menus and over a game.
 * What they say comes from porpoise_dolphinbar.cpp (push_notice): only
 * confirmed changes, in order, about three seconds each, up to four at once.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <algorithm>
#include <string>

#include "porpoise_dolphinbar.hpp"
#include "ui_app.hpp"
#include "ui_app_common.hpp"
#include "ui_i18n.hpp"

namespace porpoise::ui
{
using namespace look;

void draw_wiimote_notices(Gfx &g)
{
    const auto list = porpoise::dolphinbar::notices();
    float y = 112;
    for (const auto &n : list)
    {
        const double age = n.age, end = porpoise::dolphinbar::kNoticeSeconds;
        const float a = float(std::clamp(std::min(age / 0.2, (end - age) / 0.4), 0.0, 1.0));
        if (a <= 0)
            continue;
        const std::string port = std::to_string(n.port);
        std::string text;
        if (!n.attachment)
            text = trf(n.connected ? "Wii Controller Port {n} connected" : "Wii Controller Port {n} disconnected",
                       {{"n", port}});
        else if (!n.what.empty())
            text = trf(n.connected ? "Wii Controller Port {n}: {what} plugged in" : "Wii Controller Port {n}: {what} pulled out",
                       {{"n", port}, {"what", tr(n.what)}});
        else
            text = trf(n.connected ? "Wii Controller Port {n}: attachment plugged in"
                                   : "Wii Controller Port {n}: attachment pulled out",
                       {{"n", port}});
        const float w = std::max(420.0f, g.measure(Font::Bold, 28, text) + 110);
        const float x = (1920 - w) * 0.5f, slide = (1 - a) * -12;
        const Color edge = n.connected ? rgba(0x7CF0B4, 0.95f * a) : rgba(0xFFD27A, 0.95f * a);
        g.panel(x, y + slide, w, 62, rgba(0x0A1236, 0.86f * a), 0.9f * a, 31, edge, 2.0f, 10 * a);
        g.panel(x + 26, y + slide + 23, 16, 16, edge, 1, 8);
        g.text_mid(Font::Bold, 28, x + 60, y + slide + 31, rgba(0xF4F7FF, a), Align::Left, text);
        y += 74;
    }
}
} // namespace porpoise::ui
