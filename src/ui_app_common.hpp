/* Porpoise UI - what the launcher's screens share: sizes, colours, the room's
 * perspective, glass blocks and text helpers.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Design: Ruben's Porpoise UI direction (sapphire glass blocks in a grid room,
 * cover flow library, controller-first). Layout is in 1920x1080 design pixels.
 * Every rounded shape uses the same corner radius (kR), and every label that
 * sits inside a shape is centred on its capital letters (Gfx::text_mid). */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "ui_gfx.hpp"

namespace porpoise::ui::look
{
constexpr const char *kVersion = "4.0";
constexpr int kBuild = 0; /* a release named "... (build N)" with a higher N is newer */
constexpr int kVersionMajor = 4, kVersionMinor = 0, kVersionPatch = 0;
/* 0 for a final release; its betas (1, 2...) come before it, and its alphas
 * before those (-1000 + n: alpha 1 is -999), as porpoise::update reads tags. */
constexpr int kVersionBeta = 0; /* 4.0 (0: the release) */
/* A test build's tag: shown by the logo, and in a notice at the first start
 * of each such build. Release for neither. */
enum class Channel
{
    Release,
    Beta,
    Alpha,
};
constexpr Channel kChannel = Channel::Release;
/* This build, for the notice: shown again when any of these change. */
constexpr int kNoticeId = (((((kVersionMajor * 10 + kVersionMinor) * 10 + kVersionPatch) * 100 + kVersionBeta) * 100 +
                            kBuild) * 4 + int(kChannel));

/* What a settings row does when pressed, besides changing a value. */
enum RowAction
{
    kRowNone = 0,
    kRowAddFolder,
    kRowRemoveFolder,
    kRowRescan,
    kRowResetAll,
    kRowResetGame,
    kRowMapping,
    kRowWiiGuide,
    kRowWiiSetup,
    kRowRecommended,
    kRowUseSetup,
    kRowUpdate,
    kRowPickVersion, /* About > Choose a version: left / right pick, Cross installs */
    kRowSendReport,  /* About > Report a bug: the logs into a report folder (and onto a USB drive) */
    kRowSetupCheck,  /* Games > Check my setup: what Porpoise can see, and what to do about it */
    kRowMoveData,    /* Games > Move Porpoise's folder to a drive (folder: the place's index) */
    kRowAccount,     /* Games > RetroAchievements: the account panel */
    kRowImportSaves, /* Games > Saves from a USB drive (beta) */
    kRowDeveloperOff,
    kRowReinitialize, /* Interface > Reinitialize Porpoise: settings wiped, the first start again */
    kRowCoversAgain,  /* Games > Download covers again (every game), or a game's own in its settings */
    kRowDiagnostic,   /* About > Diagnostic test: what Porpoise can see and do, saved with the logs */
    kRowForwarder,    /* a game's settings > Home screen: make (or remake) its home screen tile */
    kRowTileArt,      /* a game's settings > Home screen: the tile art editor */
    kRowShots,        /* Screenshots: the gallery */
    kRowAddShare,     /* Games > Add a network share: the share panel */
    kRowNandImport,   /* Console > Wii System Data: the player's own BootMii NAND backup into Dolphin's */
    kRowEditShare,    /* Games > a network share (folder: its index): the share panel */
    kRowOnlineDns,    /* Online > DNS Server for Online Play: the address panel */
    kRowOnlineHub,    /* Online > Online Hub (ui_app_online.cpp) */
    kRowGcMenu,       /* Console > GameCube Menu: start it, with a game or none (ui_app_system.cpp) */
    kRowWiiMenu,      /* Console > Wii Menu: start it, with a game or none */
    kRowChannels,     /* Console > Wii Channels: the player's WADs into the Wii Menu */
    kRowSysUpdate,    /* Console > Wii System Update: the Wii's system software from Nintendo's servers */
    kRowAddCode,      /* a game's Cheats and Patches > Add a Code (ui_app_code.cpp) */
    kRowAllSettings,  /* 4.0: Show All Settings (Settings and a game's settings) */
    kRowControllers,  /* Controllers & Accessories: the test and diagnosis screen (folder: which device) */
};

/* Prompts with a keycap instead of a face-button glyph (draw_prompts). */
constexpr Glyph kKeyL2R2 = Glyph(32);
constexpr Glyph kKeyL1R1 = Glyph(33);

constexpr float kPi = 3.14159265f;
inline float kR = 14.0f;         /* the one corner radius: the theme's (apply_look) */
constexpr float kCx = 960.0f;    /* cover flow centre */
constexpr float kCy = 470.0f;
constexpr float kTileW = 320.0f; /* box art: 5 wide for 7 high */
constexpr float kTileH = 448.0f;
constexpr float kFocal = 1500.0f;
constexpr float kBarY = 38.0f, kBarH = 58.0f; /* top bar */
constexpr float kBarCy = kBarY + kBarH * 0.5f;
constexpr float kPromptY = 1010.0f;

/* Memory cards */
constexpr int kMcCols = 4, kMcRows = 3;
constexpr float kMcTile = 146.0f, kMcGap = 24.0f;
constexpr float kMcW = 740.0f, kMcH = 650.0f, kMcY = 136.0f;
constexpr float kMcX[2] = {190.0f, 990.0f};

inline const Color kWhite = rgba(0xF4F7FF);
inline const Color kLavender = rgba(0xA9A6FF);
inline const Color kSoft = rgba(0xC9D3FF);
inline const Color kCyan = rgba(0x5CD3FF);
inline const Color kIcy = rgba(0x9DEBFF);
inline const Color kTileFill = rgba(0x1E46C8, 0.55f);
inline const Color kEdge = rgba(0x4C8DFF, 0.95f);
inline const Color kClear = rgba(0x000000, 0.0f);
inline const Color kGlassBody = rgba(0x1C48D8, 0.50f);
inline const Color kDanger = rgba(0xFF7A9A);

/* The Revolution look's own palette: white glass on a pale, lined room, gray
 * rims, blue for what is chosen. */
namespace rev
{
inline const Color kRoom = rgba(0xF2F4F7);
inline const Color kLine = rgba(0xDCE1E7);
inline const Color kTile = rgba(0xFFFFFF);
inline const Color kRim = rgba(0xC6CCD4);
inline const Color kBlue = rgba(0x3DB8EC);
inline const Color kBlueSoft = rgba(0xA9DFF6);
inline const Color kInk = rgba(0x3B434D);
inline const Color kInkSoft = rgba(0x7A838E);
inline const Color kDigits = rgba(0x98A1AB);
inline const Color kBar = rgba(0xE7EAEE);
inline const Color kGameCube = rgba(0x6E63D9);
inline const Color kWii = rgba(0x39A9DF);
constexpr float kTileR = 26;
} // namespace rev

inline Color with_alpha(Color c, float a)
{
    c.a *= a;
    return c;
}

inline float smooth(float current, float target, double dt, float rate)
{
    const float k = 1.0f - std::exp(-float(dt) * rate);
    return current + (target - current) * k;
}

inline float ease_out(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

/* A point of a block in the room: (lx, ly) on its face from its centre,
 * lz into its thickness, the block turned by yaw about its vertical axis and
 * brought z0 toward the viewer about its own centre (negative z0: closer).
 * Perspective converges on the room's vanishing point on the horizon, so a
 * block's thickness shows on the side that faces the middle of the room -
 * like real boxes on a shelf. */
constexpr float kVpX = 960.0f, kVpY = 560.0f;
inline Corner project(float cx, float cy, float lx, float ly, float yaw, float lz = 0.0f, float z0 = 0.0f)
{
    const float k0 = kFocal / (kFocal + z0);
    const float c = std::cos(yaw), s = std::sin(yaw);
    const float x = (lx * c - lz * s) * k0;
    const float z = (lx * s + lz * c) * k0;
    const float k = kFocal / (kFocal + z);
    return Corner{kVpX + (cx + x - kVpX) * k, kVpY + (cy + ly * k0 - kVpY) * k, 1.0f / (k * k0)};
}

inline void rect_at(float cx, float cy, float hw, float hh, float yaw, float lz, float z0, Corner out[4])
{
    out[0] = project(cx, cy, -hw, -hh, yaw, lz, z0);
    out[1] = project(cx, cy, hw, -hh, yaw, lz, z0);
    out[2] = project(cx, cy, hw, hh, yaw, lz, z0);
    out[3] = project(cx, cy, -hw, hh, yaw, lz, z0);
}

/* A point in the room, for things turned every way (a cube): turned by yaw
 * (about the vertical), pitch (about the horizontal) and roll (in the
 * screen's plane), then seen in the room's perspective about (cx, cy). */
struct V3
{
    float x, y, z;
};
inline V3 turn3(V3 p, float yaw, float pitch, float roll = 0)
{
    const float cr = std::cos(roll), sr = std::sin(roll);
    p = V3{p.x * cr - p.y * sr, p.x * sr + p.y * cr, p.z};
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    p = V3{p.x, p.y * cp - p.z * sp, p.y * sp + p.z * cp};
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    return V3{p.x * cy - p.z * sy, p.y, p.x * sy + p.z * cy};
}
inline Corner seen3(float cx, float cy, V3 p)
{
    const float k = kFocal / (kFocal + p.z);
    return Corner{kVpX + (cx + p.x - kVpX) * k, kVpY + (cy + p.y - kVpY) * k, 1.0f / k};
}
/* A face (corners clockwise as seen from outside) faces the viewer. */
inline bool facing_you(const Corner q[4])
{
    float area = 0;
    for (int i = 0; i < 4; ++i)
        area += q[i].x * q[(i + 1) % 4].y - q[(i + 1) % 4].x * q[i].y;
    return area > 1.0f;
}

/* A glass block: its thickness as stacked slices, far side first, then the
 * face toward the viewer. from_behind: the block has turned past 90 degrees,
 * so its back (at lz = depth) is the face that shows. margin is room around
 * the face for its glow. */
inline void glass_block(Gfx &g, float cx, float cy, float w, float h, float depth, float yaw, float z0, float margin,
                        Glass face, bool from_behind = false)
{
    constexpr int kSlices = 8;
    Glass side = face;
    side.face = 1;
    side.glow = 0;
    side.rim_w = 0;
    side.tint = Color{face.tint.r * 0.55f, face.tint.g * 0.60f, face.tint.b * 0.80f,
                      std::min(1.0f, face.tint.a * 1.6f)};
    Corner c[4];
    for (int k = 0; k < kSlices; ++k)
    {
        const int i = from_behind ? k : kSlices - k; /* far to near */
        const float f = float(i) / kSlices;
        side.light = 0.55f + 0.55f * (from_behind ? f : 1.0f - f);
        side.phase = face.phase + f * 0.08f;
        rect_at(cx, cy, w * 0.5f, h * 0.5f, yaw, depth * f, z0, c);
        g.glass(c, w, h, 0, side);
    }
    rect_at(cx, cy, w * 0.5f + margin, h * 0.5f + margin, yaw, from_behind ? depth : 0.0f, z0, c);
    g.glass(c, w, h, margin, face);
}

/* Gloss and the room's reflection over a picture set in glass. */
inline void gloss_over(Gfx &g, const Corner c[4], float w, float h, float radius, float phase, float fade = 1)
{
    Glass gloss;
    gloss.face = 2;
    gloss.radius = radius;
    gloss.rim_w = 0;
    gloss.tint = kWhite;
    gloss.rim = kClear;
    gloss.phase = phase;
    gloss.fade = fade;
    g.glass(c, w, h, 0, gloss);
}

/* uv rectangle that fills a w x h shape with a texture, cropping the excess. */
inline void cover_uv(const Texture *t, float w, float h, float uv[4])
{
    uv[0] = 0;
    uv[1] = 0;
    uv[2] = 1;
    uv[3] = 1;
    if (!t || t->height <= 0 || h <= 0)
        return;
    const float ta = float(t->width) / float(t->height), aa = w / h;
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
}

inline std::string human_size(std::uint64_t bytes)
{
    char buf[32];
    if (bytes >= (1ull << 30))
        std::snprintf(buf, sizeof buf, "%.1f GB", double(bytes) / double(1ull << 30));
    else
        std::snprintf(buf, sizeof buf, "%.0f MB", double(bytes) / double(1ull << 20));
    return buf;
}

/* Pixel art stays crisp: enlarge by whole pixels before the GPU smooths it. */
inline std::vector<std::uint8_t> enlarge(const std::vector<std::uint8_t> &src, int w, int h, int k)
{
    std::vector<std::uint8_t> out(std::size_t(w * k) * std::size_t(h * k) * 4);
    for (int y = 0; y < h * k; ++y)
        for (int x = 0; x < w * k; ++x)
        {
            const std::uint8_t *p = &src[(std::size_t(y / k) * std::size_t(w) + std::size_t(x / k)) * 4];
            std::uint8_t *q = &out[(std::size_t(y) * std::size_t(w * k) + std::size_t(x)) * 4];
            q[0] = p[0];
            q[1] = p[1];
            q[2] = p[2];
            q[3] = p[3];
        }
    return out;
}

/* Shortens s with an ellipsis until it fits max_w. */
inline std::string fit(Gfx &g, Font f, float size, std::string s, float max_w)
{
    if (g.measure(f, size, s) <= max_w)
        return s;
    /* The longest start (whole UTF-8 sequences) that fits with the ellipsis:
     * found by halving, as it's asked for many titles every frame. */
    std::vector<std::size_t> cuts; /* where a character starts */
    for (std::size_t i = 1; i < s.size(); ++i)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80)
            cuts.push_back(i);
    std::size_t lo = 0, hi = cuts.size(); /* cuts[lo-1] fits (0: nothing kept); hi: the first that doesn't */
    while (lo < hi)
    {
        const std::size_t mid = (lo + hi + 1) / 2;
        if (g.measure(f, size, s.substr(0, cuts[mid - 1]) + "\xE2\x80\xA6") <= max_w)
            lo = mid;
        else
            hi = mid - 1;
    }
    return (lo == 0 ? std::string() : s.substr(0, cuts[lo - 1])) + "\xE2\x80\xA6";
}

/* Chinese and Japanese have no spaces: a line may end after any of their
 * characters (Korean keeps its spaces and wraps like English). */
inline bool breaks_anywhere(unsigned cp)
{
    return (cp >= 0x2E80 && cp <= 0xA4CF && !(cp >= 0x3130 && cp <= 0x318F)) ||
           (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFFEF);
}

/* Punctuation and small kana that never start a line. */
inline bool keeps_with_previous(unsigned cp)
{
    static constexpr unsigned kKeep[] = {
        ',',    '.',    ':',    ';',    '!',    '?',    ')',    ']',    0x2026, 0x3001, 0x3002,
        0x300D, 0x300F, 0x3011, 0x30FB, 0x30FC, 0xFF01, 0xFF09, 0xFF0C, 0xFF0E, 0xFF1A, 0xFF1B,
        0xFF1F, 0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063, 0x3083, 0x3085, 0x3087, 0x30A1,
        0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30C3, 0x30E3, 0x30E5, 0x30E7};
    for (unsigned k : kKeep)
        if (k == cp)
            return true;
    return false;
}

/* Breaks text into lines no wider than max_w; the last kept line ends in an
 * ellipsis when there was more. */
inline std::vector<std::string> wrap(Gfx &g, Font f, float size, const std::string &text,
                                     float max_w, std::size_t max_lines = 99)
{
    /* Pieces a line may break between; glued pieces join without a space. */
    struct Piece
    {
        std::string text;
        bool glued = false;
        bool newline = false;
    };
    std::vector<Piece> pieces;
    bool space = true;      /* a space (or the start) came before the next piece */
    bool open_word = false; /* the last piece is a word still being read */
    for (std::size_t i = 0; i < text.size();)
    {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const std::size_t len = c < 0x80           ? 1
                                : (c >> 5) == 0x6  ? 2
                                : (c >> 4) == 0xE  ? 3
                                : (c >> 3) == 0x1E ? 4
                                                   : 1;
        unsigned cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
        for (std::size_t k = 1; k < len && i + k < text.size(); ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
        const std::string ch = text.substr(i, len);
        i += len;
        if (c == ' ' || c == '\n')
        {
            if (c == '\n')
                pieces.push_back({"", false, true});
            space = true;
            open_word = false;
            continue;
        }
        const bool anywhere = breaks_anywhere(cp);
        if (!pieces.empty() && !space && !pieces.back().newline && keeps_with_previous(cp))
            pieces.back().text += ch; /* never at the start of a line */
        else if (open_word && !anywhere)
            pieces.back().text += ch;
        else
            pieces.push_back({ch, !space && !pieces.empty() && !pieces.back().newline, false});
        open_word = !anywhere;
        space = false;
    }

    std::vector<std::string> lines;
    std::string line;
    bool more = false;
    for (const Piece &p : pieces)
    {
        if (p.newline)
        {
            if (!line.empty())
                lines.push_back(line);
            line.clear();
        }
        else
        {
            const std::string trial = line.empty() ? p.text : line + (p.glued ? "" : " ") + p.text;
            if (!line.empty() && g.measure(f, size, trial) > max_w)
            {
                lines.push_back(line);
                line = p.text;
            }
            else
                line = trial;
        }
        if (lines.size() > max_lines)
        {
            more = true;
            break;
        }
    }
    if (!line.empty() && lines.size() <= max_lines)
        lines.push_back(line);
    if (lines.size() > max_lines)
    {
        lines.resize(max_lines);
        more = true;
    }
    if (more && !lines.empty())
        lines.back() = fit(g, f, size, lines.back() + " \xE2\x80\xA6", max_w);
    return lines;
}
} // namespace porpoise::ui::look
