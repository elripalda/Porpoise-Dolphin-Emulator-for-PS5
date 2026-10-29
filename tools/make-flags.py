#!/usr/bin/env python3
# Porpoise - draws assets/ui/flags.png, the flags beside the languages in
# Settings > Interface > Language: seventeen 96x64 cells in a row, in the
# setting's numbering after System (porpoise::ui::Language): English (US),
# Spanish (Spain), French, Portuguese (Portugal), Italian, Japanese, Spanish
# (Latin America: Mexico's flag), Portuguese (Brazil), German, Dutch, Polish,
# Russian, Chinese (Simplified), Chinese (Traditional), Korean, Turkish and
# Swedish.
# Simplified for a 40-pixel flag on a TV. The two Chinese scripts are read in
# several places, so they get a plain tile with the script's own character
# (简 / 繁, drawn from Noto Sans SC / TC) instead of a flag.
# Needs Pillow; the Noto fonts are looked for in $NOTO_DIR (default
# ~/google/fonts/ofl).  SPDX-License-Identifier: GPL-3.0-or-later
import math
import os
import sys

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "assets", "ui", "flags.png")
W, H, S = 96, 64, 4  # cell, supersampling


def star(d, cx, cy, r, fill):
    pts = []
    for i in range(10):
        a = -math.pi / 2 + i * math.pi / 5
        rr = r if i % 2 == 0 else r * 0.4
        pts.append((cx + math.cos(a) * rr, cy + math.sin(a) * rr))
    d.polygon(pts, fill=fill)


def us(d, w, h):
    for i in range(13):
        d.rectangle([0, i * h / 13, w, (i + 1) * h / 13], fill=(178, 34, 52) if i % 2 == 0 else (255, 255, 255))
    cw, ch = w * 0.42, h * 7 / 13
    d.rectangle([0, 0, cw, ch], fill=(60, 59, 110))
    for row in range(5):
        for col in range(6 if row % 2 == 0 else 5):
            x = cw * (col + (0.5 if row % 2 == 0 else 1.0)) / 6
            y = ch * (row + 0.75) / 5.5
            star(d, x, y, w * 0.018, (255, 255, 255))


def es(d, w, h):
    d.rectangle([0, 0, w, h], fill=(170, 21, 27))
    d.rectangle([0, h * 0.25, w, h * 0.75], fill=(241, 191, 0))
    # A plain mark where the arms sit.
    d.rounded_rectangle([w * 0.24, h * 0.36, w * 0.34, h * 0.62], radius=w * 0.02, fill=(170, 21, 27))


def fr(d, w, h):
    for i, c in enumerate([(0, 85, 164), (255, 255, 255), (239, 65, 53)]):
        d.rectangle([i * w / 3, 0, (i + 1) * w / 3, h], fill=c)


def pt(d, w, h):
    d.rectangle([0, 0, w * 0.4, h], fill=(0, 102, 0))
    d.rectangle([w * 0.4, 0, w, h], fill=(255, 0, 0))
    cx, cy, r = w * 0.4, h * 0.5, h * 0.22
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(255, 221, 0), width=int(S * 3))
    d.rounded_rectangle([cx - r * 0.5, cy - r * 0.6, cx + r * 0.5, cy + r * 0.6], radius=r * 0.2,
                        fill=(255, 255, 255), outline=(255, 0, 0), width=int(S * 1.5))


def it(d, w, h):
    for i, c in enumerate([(0, 146, 70), (255, 255, 255), (206, 43, 55)]):
        d.rectangle([i * w / 3, 0, (i + 1) * w / 3, h], fill=c)


def ja(d, w, h):
    d.rectangle([0, 0, w, h], fill=(255, 255, 255))
    r = h * 0.3
    d.ellipse([w / 2 - r, h / 2 - r, w / 2 + r, h / 2 + r], fill=(188, 0, 45))


def mx(d, w, h):
    for i, c in enumerate([(0, 104, 71), (255, 255, 255), (206, 17, 38)]):
        d.rectangle([i * w / 3, 0, (i + 1) * w / 3, h], fill=c)
    # A plain emblem: the eagle on its cactus, as a brown and green mark.
    cx, cy = w / 2, h / 2
    d.ellipse([cx - w * 0.075, cy - h * 0.2, cx + w * 0.075, cy + h * 0.06], fill=(140, 90, 40))
    d.rounded_rectangle([cx - w * 0.1, cy + h * 0.08, cx + w * 0.1, cy + h * 0.17], radius=w * 0.02,
                        fill=(0, 104, 71))


def br(d, w, h):
    d.rectangle([0, 0, w, h], fill=(0, 151, 57))
    m = w * 0.085
    d.polygon([(m, h / 2), (w / 2, h * 0.11), (w - m, h / 2), (w / 2, h * 0.89)], fill=(254, 221, 0))
    r = h * 0.25
    d.ellipse([w / 2 - r, h / 2 - r, w / 2 + r, h / 2 + r], fill=(1, 33, 105))
    # The white band across the globe.
    band = Image.new("L", (int(w), int(h)), 0)
    bd = ImageDraw.Draw(band)
    bd.arc([w / 2 - r * 2.2, h / 2 - r * 0.55, w / 2 + r * 2.6, h / 2 + r * 3.6], 200, 290, fill=255,
           width=int(r * 0.2))
    globe = Image.new("L", (int(w), int(h)), 0)
    ImageDraw.Draw(globe).ellipse([w / 2 - r, h / 2 - r, w / 2 + r, h / 2 + r], fill=255)
    from PIL import ImageChops
    d._image.paste((255, 255, 255, 255), (0, 0), ImageChops.multiply(band, globe))


def de(d, w, h):
    for i, c in enumerate([(0, 0, 0), (221, 0, 0), (255, 206, 0)]):
        d.rectangle([0, i * h / 3, w, (i + 1) * h / 3], fill=c)


def nl(d, w, h):
    for i, c in enumerate([(174, 28, 40), (255, 255, 255), (33, 70, 139)]):
        d.rectangle([0, i * h / 3, w, (i + 1) * h / 3], fill=c)


def pl(d, w, h):
    d.rectangle([0, 0, w, h / 2], fill=(255, 255, 255))
    d.rectangle([0, h / 2, w, h], fill=(220, 20, 60))


def ru(d, w, h):
    for i, c in enumerate([(255, 255, 255), (0, 57, 166), (213, 43, 30)]):
        d.rectangle([0, i * h / 3, w, (i + 1) * h / 3], fill=c)


NOTO = os.environ.get("NOTO_DIR", os.path.expanduser("~/google/fonts/ofl"))


def script_tile(char, font_file):
    def draw(d, w, h):
        from PIL import ImageFont
        d.rectangle([0, 0, w, h], fill=(64, 78, 104))
        font = ImageFont.truetype(os.path.join(NOTO, font_file), int(h * 0.62))
        try:
            font.set_variation_by_axes([700])
        except Exception:
            pass
        d.text((w / 2, h / 2), char, font=font, fill=(255, 255, 255), anchor="mm")
    return draw


zh_hans = script_tile("\u7b80", "notosanssc/NotoSansSC[wght].ttf")
zh_hant = script_tile("\u7e41", "notosanstc/NotoSansTC[wght].ttf")


def ko(d, w, h):
    d.rectangle([0, 0, w, h], fill=(255, 255, 255))
    cx, cy, r = w / 2, h / 2, h * 0.25
    # The taegeuk: red above, blue below, with the two small circles.
    d.pieslice([cx - r, cy - r, cx + r, cy + r], 180 + 33.7, 360 + 33.7, fill=(205, 46, 58))
    d.pieslice([cx - r, cy - r, cx + r, cy + r], 33.7, 180 + 33.7, fill=(0, 71, 160))
    a = math.radians(33.7)
    for k, col in ((-1, (205, 46, 58)), (1, (0, 71, 160))):
        sx, sy = cx + k * math.cos(a) * r / 2, cy + k * math.sin(a) * r / 2
        d.ellipse([sx - r / 2, sy - r / 2, sx + r / 2, sy + r / 2], fill=col)
    # The four trigrams, as plain bars on the diagonals.
    for ang, broken in ((180 + 33.7, (0, 0, 0)), (-33.7, (1, 0, 1)), (33.7, (1, 1, 1)), (180 - 33.7, (0, 1, 0))):
        t = math.radians(ang)
        ux, uy = math.cos(t), math.sin(t)
        px, py = -uy, ux
        for i, split in enumerate(broken):
            dist = r * 1.45 + i * r * 0.28
            bx, by = cx + ux * dist, cy + uy * dist
            half, thick = r * 0.5, r * 0.09
            pieces = [(-half, -half * 0.12), (half * 0.12, half)] if split else [(-half, half)]
            for a0, a1 in pieces:
                pts = [(bx + px * a0 + ux * -thick, by + py * a0 + uy * -thick),
                       (bx + px * a1 + ux * -thick, by + py * a1 + uy * -thick),
                       (bx + px * a1 + ux * thick, by + py * a1 + uy * thick),
                       (bx + px * a0 + ux * thick, by + py * a0 + uy * thick)]
                d.polygon(pts, fill=(0, 0, 0))


def tr(d, w, h):
    red = (227, 10, 23)
    d.rectangle([0, 0, w, h], fill=red)
    cx, cy, r = w * 0.36, h / 2, h * 0.25
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(255, 255, 255))
    r2 = r * 0.8
    c2 = cx + r * 0.25
    d.ellipse([c2 - r2, cy - r2, c2 + r2, cy + r2], fill=red)
    star(d, cx + r * 1.15, cy, r * 0.55, (255, 255, 255))


def sv(d, w, h):
    blue = (0, 106, 167)
    yellow = (254, 204, 0)
    d.rectangle([0, 0, w, h], fill=blue)
    d.rectangle([w * 0.3, 0, w * 0.42, h], fill=yellow)
    d.rectangle([0, h * 0.42, w, h * 0.58], fill=yellow)


FLAGS = [us, es, fr, pt, it, ja, mx, br, de, nl, pl, ru, zh_hans, zh_hant, ko, tr, sv]
sheet = Image.new("RGBA", (W * len(FLAGS), H), (0, 0, 0, 0))
for i, fn in enumerate(FLAGS):
    big = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    flag = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 255))
    fn(ImageDraw.Draw(flag), W * S, H * S)
    mask = Image.new("L", (W * S, H * S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, W * S - 1, H * S - 1], radius=8 * S, fill=255)
    big.paste(flag, (0, 0), mask)
    # A thin light edge, so white flags show on the glass.
    ImageDraw.Draw(big).rounded_rectangle([S, S, W * S - S - 1, H * S - S - 1], radius=8 * S,
                                          outline=(255, 255, 255, 110), width=S * 2)
    sheet.alpha_composite(big.resize((W, H), Image.LANCZOS), (i * W, 0))
sheet.save(OUT, optimize=True)
print("wrote", OUT)
