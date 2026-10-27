"""Raster helpers: the extracted mascot, vector icons, wordmark and rounded surfaces."""

from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageTk

from . import theme

ASSETS = Path(__file__).resolve().parent.parent / "assets"
MASCOT_PATH = ASSETS / "mascot.png"
MASCOT_WHITE_PATH = ASSETS / "mascot_white.png"
LOGO_PATH = ASSETS.parent / "logo.png"

_photo_cache: dict = {}


def _photo(image: Image.Image):
    return ImageTk.PhotoImage(image)


def app_icon_photo(size: int = 64):
    key = ("app-icon", int(size))
    if key not in _photo_cache:
        im = Image.open(LOGO_PATH).convert("RGBA")
        im = im.resize((max(16, int(size)), max(16, int(size))), Image.LANCZOS)
        _photo_cache[key] = _photo(im)
    return _photo_cache[key]


# --------------------------------------------------------------------------- #
# Mascot & wordmark
# --------------------------------------------------------------------------- #
def mascot_image(size: int, color: str | None = None) -> Image.Image:
    source = MASCOT_WHITE_PATH if color == "white" else MASCOT_PATH
    im = Image.open(source).convert("RGBA")
    im = im.resize((max(8, int(size)), max(8, int(size))), Image.LANCZOS)
    if color and color not in ("white", None):
        rgb = Image.new("RGBA", im.size, color)
        im = Image.composite(rgb, Image.new("RGBA", im.size, (0, 0, 0, 0)), im.getchannel("A"))
    return im


def mascot_photo(size: int, color: str | None = None):
    key = ("mascot", int(size), color)
    if key not in _photo_cache:
        _photo_cache[key] = _photo(mascot_image(size, color))
    return _photo_cache[key]


def _tracked_text(text: str, font: ImageFont.FreeTypeFont, fill, tracking: int):
    widths = [font.getlength(ch) for ch in text]
    width = int(sum(widths) + tracking * (len(text) - 1))
    bbox = font.getbbox(text)
    height = bbox[3] - bbox[1]
    im = Image.new("RGBA", (max(1, width + 8), max(1, height + 20)), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    x = 4
    y = 6 - bbox[1]
    for ch, w in zip(text, widths):
        d.text((x, y), ch, font=font, fill=fill)
        x += w + tracking
    return im.crop(im.getbbox() or (0, 0, im.width, im.height))


def wordmark_image(height: int = 24, color: str = theme.INK) -> Image.Image:
    path = theme.pil_font_path("extrabold")
    scale = 4
    font = ImageFont.truetype(str(path), int(height * 1.35 * scale))
    text = "TAKOYO"
    tracking = int(-height * 0.035 * scale)
    im = _tracked_text(text, font, color, tracking)
    target_h = int(height)
    ratio = target_h / im.height
    im = im.resize((max(1, int(im.width * ratio)), target_h), Image.LANCZOS)
    return im


def wordmark_photo(height: int = 24, color: str = theme.INK):
    key = ("wordmark", int(height), color)
    if key not in _photo_cache:
        _photo_cache[key] = _photo(wordmark_image(height, color))
    return _photo_cache[key]


def mascot_alpha(size: int) -> Image.Image:
    im = Image.open(MASCOT_PATH).convert("RGBA")
    return im.resize((size, size), Image.LANCZOS).getchannel("A")


# --------------------------------------------------------------------------- #
# Rounded surfaces
# --------------------------------------------------------------------------- #
def rounded_image(
    w: int,
    h: int,
    radius: int = 16,
    fill: str = theme.WHITE,
    outline: str | None = None,
    thickness: float = 0,
    bg: str | None = None,
) -> Image.Image:
    w, h = max(2, int(w)), max(2, int(h))
    scale = 3 if max(w, h) < 700 else 2
    W, H = w * scale, h * scale
    r = max(0, int(radius * scale))
    im = Image.new("RGBA", (W, H), bg or (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    inset = max(0, int(round(thickness * scale / 2)))
    box = (inset, inset, W - 1 - inset, H - 1 - inset)
    d.rounded_rectangle(
        box,
        radius=max(0, r - inset),
        fill=fill,
        outline=outline,
        width=max(1, int(round(thickness * scale))) if outline else 0,
    )
    if scale > 1:
        im = im.resize((w, h), Image.LANCZOS)
    return im


def rounded_photo(
    w: int,
    h: int,
    radius: int = 16,
    fill: str = theme.WHITE,
    outline: str | None = None,
    thickness: float = 0,
    bg: str | None = None,
):
    return _photo(rounded_image(w, h, radius, fill, outline, thickness, bg))


def round_corners(im: Image.Image, radius: int) -> Image.Image:
    im = im.convert("RGBA")
    mask = Image.new("L", im.size, 0)
    d = ImageDraw.Draw(mask)
    d.rounded_rectangle((0, 0, im.width - 1, im.height - 1), radius=radius, fill=255)
    im.putalpha(mask)
    return im


def art_photo(path: str | Path, w: int, h: int, radius: int = 12):
    key = ("art", str(path), int(w), int(h), int(radius))
    try:
        mtime = Path(path).stat().st_mtime
    except OSError:
        mtime = 0
    key = key + (round(mtime, 1),)
    if key in _photo_cache:
        return _photo_cache[key]
    try:
        im = Image.open(path).convert("RGB")
    except Exception:
        im = Image.new("RGB", (w, h), theme.SEARCH_BG)
    # cover crop
    target = max(1, w), max(1, h)
    src_ratio = im.width / im.height
    dst_ratio = target[0] / target[1]
    if src_ratio > dst_ratio:
        new_h = im.height
        new_w = int(new_h * dst_ratio)
        left = (im.width - new_w) // 2
        im = im.crop((left, 0, left + new_w, new_h))
    else:
        new_w = im.width
        new_h = int(new_w / dst_ratio)
        top = (im.height - new_h) // 2
        im = im.crop((0, top, new_w, top + new_h))
    im = im.resize((target[0] * 2, target[1] * 2), Image.LANCZOS)
    im = round_corners(im, radius * 2)
    im = im.resize(target, Image.LANCZOS)
    photo = _photo(im)
    _photo_cache[key] = photo
    return photo


def clear_cache():
    _photo_cache.clear()


# --------------------------------------------------------------------------- #
# Icons
# --------------------------------------------------------------------------- #
def _line(d, pts, color, w):
    d.line(pts, fill=color, width=w, joint="curve")
    for p in (pts[0], pts[-1]):
        r = w / 2
        d.ellipse((p[0] - r, p[1] - r, p[0] + r, p[1] + r), fill=color)


def _icon_image(name: str, size: int, color: str) -> Image.Image:
    ss = 4
    S = max(8, int(size)) * ss
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    w = max(2, int(S * 0.068))
    thin = max(2, int(S * 0.052))
    c = color

    def P(x, y):
        return (x * S, y * S)

    def box(x0, y0, x1, y1):
        return (*P(x0, y0), *P(x1, y1))

    if name == "sidebar":
        d.rounded_rectangle(box(0.13, 0.16, 0.87, 0.84), radius=int(S * 0.16), outline=c, width=w)
        d.line([P(0.40, 0.16), P(0.40, 0.84)], fill=c, width=w)
    elif name == "search":
        d.ellipse(box(0.14, 0.13, 0.65, 0.64), outline=c, width=w)
        _line(d, [P(0.60, 0.60), P(0.85, 0.85)], c, w)
    elif name == "plus":
        d.ellipse(box(0.13, 0.13, 0.87, 0.87), outline=c, width=thin)
        _line(d, [P(0.29, 0.50), P(0.71, 0.50)], c, thin)
        _line(d, [P(0.50, 0.29), P(0.50, 0.71)], c, thin)
    elif name == "plus-fill":
        d.ellipse(box(0.10, 0.10, 0.90, 0.90), fill=c)
        _line(d, [P(0.30, 0.50), P(0.70, 0.50)], theme.WHITE, w)
        _line(d, [P(0.50, 0.30), P(0.50, 0.70)], theme.WHITE, w)
    elif name == "paperclip":
        _line(
            d,
            [P(0.30, 0.64), P(0.30, 0.40), P(0.53, 0.17), P(0.73, 0.37), P(0.44, 0.66), P(0.33, 0.55), P(0.52, 0.36)],
            c,
            thin,
        )
    elif name == "mic":
        d.rounded_rectangle(box(0.38, 0.10, 0.62, 0.56), radius=int(S * 0.12), outline=c, width=w)
        d.arc(box(0.25, 0.32, 0.75, 0.70), start=0, end=180, fill=c, width=w)
        _line(d, [P(0.50, 0.70), P(0.50, 0.85)], c, w)
        _line(d, [P(0.35, 0.86), P(0.65, 0.86)], c, w)
    elif name == "stop":
        d.rounded_rectangle(box(0.24, 0.24, 0.76, 0.76), radius=int(S * 0.04), outline=c, width=int(S * 0.13))
    elif name == "arrow-down":
        _line(d, [P(0.50, 0.16), P(0.50, 0.80)], c, w)
        _line(d, [P(0.28, 0.58), P(0.50, 0.80), P(0.72, 0.58)], c, w)
    elif name == "arrow-up":
        _line(d, [P(0.50, 0.84), P(0.50, 0.20)], c, w)
        _line(d, [P(0.28, 0.42), P(0.50, 0.20), P(0.72, 0.42)], c, w)
    elif name == "arrow-left":
        _line(d, [P(0.84, 0.50), P(0.20, 0.50)], c, w)
        _line(d, [P(0.42, 0.28), P(0.20, 0.50), P(0.42, 0.72)], c, w)
    elif name == "gear":
        d.ellipse(box(0.24, 0.24, 0.76, 0.76), outline=c, width=thin)
        d.ellipse(box(0.42, 0.42, 0.58, 0.58), outline=c, width=thin)
        for i in range(8):
            a = math.pi * 2 * i / 8
            p1 = (0.5 + 0.30 * math.cos(a), 0.5 + 0.30 * math.sin(a))
            p2 = (0.5 + 0.43 * math.cos(a), 0.5 + 0.43 * math.sin(a))
            _line(d, [P(*p1), P(*p2)], c, w)
    elif name == "close":
        _line(d, [P(0.24, 0.24), P(0.76, 0.76)], c, w)
        _line(d, [P(0.76, 0.24), P(0.24, 0.76)], c, w)
    elif name == "file":
        d.line([P(0.28, 0.10), P(0.60, 0.10), P(0.76, 0.26), P(0.76, 0.90), P(0.28, 0.90), P(0.28, 0.10)], fill=c, width=thin, joint="curve")
        d.line([P(0.60, 0.10), P(0.60, 0.26), P(0.76, 0.26)], fill=c, width=thin)
        d.line([P(0.38, 0.46), P(0.66, 0.46)], fill=c, width=thin)
        d.line([P(0.38, 0.62), P(0.66, 0.62)], fill=c, width=thin)
    elif name == "folder":
        pts = [P(0.13, 0.30), P(0.36, 0.30), P(0.44, 0.40), P(0.87, 0.40), P(0.87, 0.82), P(0.13, 0.82)]
        d.line(pts + [pts[0]], fill=c, width=thin, joint="curve")
    elif name == "trash":
        _line(d, [P(0.20, 0.28), P(0.80, 0.28)], c, thin)
        d.arc(box(0.38, 0.14, 0.62, 0.32), start=180, end=360, fill=c, width=thin)
        d.line([P(0.26, 0.30), P(0.31, 0.86), P(0.69, 0.86), P(0.74, 0.30)], fill=c, width=thin, joint="curve")
        d.line([P(0.43, 0.42), P(0.45, 0.74)], fill=c, width=thin)
        d.line([P(0.57, 0.42), P(0.55, 0.74)], fill=c, width=thin)
    elif name == "copy":
        d.rounded_rectangle(box(0.31, 0.31, 0.83, 0.83), radius=int(S * 0.08), outline=c, width=thin)
        d.rounded_rectangle(box(0.17, 0.17, 0.69, 0.69), radius=int(S * 0.08), outline=c, width=thin)
    elif name == "check":
        _line(d, [P(0.18, 0.52), P(0.42, 0.76), P(0.82, 0.26)], c, w)
    elif name == "external":
        d.line([P(0.20, 0.34), P(0.20, 0.82), P(0.68, 0.82), P(0.68, 0.66)], fill=c, width=thin)
        _line(d, [P(0.46, 0.54), P(0.82, 0.18)], c, thin)
        _line(d, [P(0.58, 0.18), P(0.82, 0.18), P(0.82, 0.42)], c, thin)
    elif name == "sparkle":
        d.polygon(
            [
                P(0.50, 0.08), P(0.59, 0.41), P(0.92, 0.50), P(0.59, 0.59),
                P(0.50, 0.92), P(0.41, 0.59), P(0.08, 0.50), P(0.41, 0.41),
            ],
            fill=c,
        )
    elif name == "image":
        d.rounded_rectangle(box(0.12, 0.16, 0.88, 0.84), radius=int(S * 0.10), outline=c, width=thin)
        d.ellipse(box(0.27, 0.29, 0.43, 0.45), fill=c)
        d.line([P(0.16, 0.74), P(0.38, 0.50), P(0.55, 0.66), P(0.68, 0.54), P(0.84, 0.72)], fill=c, width=thin, joint="curve")
    elif name == "more":
        for x in (0.25, 0.50, 0.75):
            d.ellipse(box(x - 0.055, 0.445, x + 0.055, 0.555), fill=c)
    elif name == "chat":
        d.rounded_rectangle(box(0.12, 0.14, 0.88, 0.70), radius=int(S * 0.16), outline=c, width=thin)
        d.line([P(0.32, 0.70), P(0.24, 0.86), P(0.48, 0.70)], fill=c, width=thin, joint="curve")
    elif name == "history":
        d.ellipse(box(0.13, 0.13, 0.87, 0.87), outline=c, width=thin)
        _line(d, [P(0.50, 0.28), P(0.50, 0.52), P(0.68, 0.62)], c, thin)
    elif name == "chevron-down":
        _line(d, [P(0.26, 0.40), P(0.50, 0.64), P(0.74, 0.40)], c, w)
    elif name == "chevron-right":
        _line(d, [P(0.40, 0.24), P(0.64, 0.50), P(0.40, 0.76)], c, w)
    elif name == "download":
        _line(d, [P(0.50, 0.14), P(0.50, 0.64)], c, w)
        _line(d, [P(0.30, 0.46), P(0.50, 0.66), P(0.70, 0.46)], c, w)
        d.line([P(0.18, 0.72), P(0.18, 0.86), P(0.82, 0.86), P(0.82, 0.72)], fill=c, width=w)
    elif name == "pencil":
        d.line([P(0.20, 0.82), P(0.28, 0.58), P(0.68, 0.18), P(0.84, 0.34), P(0.44, 0.74), P(0.20, 0.82)], fill=c, width=thin, joint="curve")
        _line(d, [P(0.28, 0.58), P(0.44, 0.74)], c, thin)
    elif name == "refresh":
        d.arc(box(0.18, 0.18, 0.82, 0.82), start=30, end=300, fill=c, width=thin)
        _line(d, [P(0.78, 0.16), P(0.86, 0.40), P(0.62, 0.40)], c, thin)
    elif name == "code":
        _line(d, [P(0.38, 0.26), P(0.16, 0.50), P(0.38, 0.74)], c, thin)
        _line(d, [P(0.62, 0.26), P(0.84, 0.50), P(0.62, 0.74)], c, thin)
    elif name == "brain":
        d.arc(box(0.16, 0.18, 0.62, 0.64), start=60, end=300, fill=c, width=thin)
        d.arc(box(0.38, 0.18, 0.84, 0.64), start=240, end=120, fill=c, width=thin)
        d.line([P(0.23, 0.70), P(0.23, 0.82), P(0.77, 0.82), P(0.77, 0.70)], fill=c, width=thin, joint="curve")
        d.line([P(0.50, 0.24), P(0.50, 0.70)], fill=c, width=thin)
    else:
        d.ellipse(box(0.2, 0.2, 0.8, 0.8), outline=c, width=thin)

    im = im.resize((max(8, int(size)), max(8, int(size))), Image.LANCZOS)
    return im


def icon_photo(name: str, size: int = 20, color: str = theme.INK):
    key = ("icon", name, int(size), color)
    if key not in _photo_cache:
        _photo_cache[key] = _photo(_icon_image(name, size, color))
    return _photo_cache[key]
