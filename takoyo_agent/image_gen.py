"""Takoyo Art — unlimited on-device image generation, plus optional DALL·E 3."""

from __future__ import annotations

import base64
import hashlib
import json
import math
import random
import time
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

from .config import IMAGES_DIR, Config, ensure_dirs

USER_AGENT = "Takoyo/1.0"


class ImageError(RuntimeError):
    pass


# --------------------------------------------------------------------------- #
# Palettes
# --------------------------------------------------------------------------- #
PALETTES: dict[str, dict] = {
    "day": {
        "sky": ["#B9E3F8", "#DEF4FE", "#F6FDFF"],
        "sun": "#FFF6C9",
        "glow": "#FFF9DC",
        "cloud": "#FFFFFF",
        "hills": ["#CFE69B", "#A9C64A", "#7E9A12"],
        "accent": "#5E7A0C",
        "treeline": "#6E8A11",
    },
    "sunset": {
        "sky": ["#FFB48A", "#FFD9B0", "#FFF1DC"],
        "sun": "#FFE39A",
        "glow": "#FFD7A0",
        "cloud": "#FFF3E8",
        "hills": ["#D6B36B", "#A97B45", "#6E4B33"],
        "accent": "#5A3B27",
        "treeline": "#543A2A",
    },
    "night": {
        "sky": ["#0B1735", "#1B2D55", "#4A5D82"],
        "sun": "#F2EFCF",
        "glow": "#DCE6FF",
        "cloud": "#C9D6EE",
        "hills": ["#2A3D55", "#1A2A3C", "#0C1724"],
        "accent": "#070F19",
        "treeline": "#0A1520",
    },
    "ocean": {
        "sky": ["#8FD6F7", "#C8EDFD", "#EFFBFF"],
        "sun": "#FFF2C2",
        "glow": "#FFF7D6",
        "cloud": "#FFFFFF",
        "hills": ["#7FD3E8", "#3A9FC7", "#186C92"],
        "accent": "#0E5270",
        "treeline": "#0F5A6B",
    },
    "forest": {
        "sky": ["#BFE9E0", "#E3F7EE", "#F8FEF9"],
        "sun": "#FFF4C0",
        "glow": "#FFF9D9",
        "cloud": "#FFFFFF",
        "hills": ["#B7D98A", "#6FA34B", "#2F5E31"],
        "accent": "#224527",
        "treeline": "#1F4223",
    },
    "desert": {
        "sky": ["#F8C99C", "#FDE3BC", "#FFF6E2"],
        "sun": "#FFF0B8",
        "glow": "#FFE8B7",
        "cloud": "#FFF8EC",
        "hills": ["#EBC97D", "#D8A44E", "#B97B33"],
        "accent": "#9A6125",
        "treeline": "#8C5B2A",
    },
    "winter": {
        "sky": ["#BFD9EE", "#E2F0FA", "#F9FDFF"],
        "sun": "#FFFFFF",
        "glow": "#EAF6FF",
        "cloud": "#FFFFFF",
        "hills": ["#EFF7FC", "#CFE4F2", "#9FBED4"],
        "accent": "#7E9FB6",
        "treeline": "#587A72",
    },
    "lavender": {
        "sky": ["#D7C6FF", "#EEDFFF", "#FBF7FF"],
        "sun": "#FFE9B8",
        "glow": "#FFEFC9",
        "cloud": "#FFFFFF",
        "hills": ["#D8C5F5", "#B18FE4", "#7953B8"],
        "accent": "#5F3F97",
        "treeline": "#5D3E8C",
    },
    "space": {
        "sky": ["#080B22", "#171A3C", "#2C2450"],
        "sun": "#F2F0FF",
        "glow": "#9DA8FF",
        "cloud": "#C9C8F2",
        "hills": ["#1B1C3B", "#101128", "#070813"],
        "accent": "#05060E",
        "treeline": "#060711",
    },
    "mono": {
        "sky": ["#D8D8D8", "#EFEFEF", "#FAFAFA"],
        "sun": "#FFFFFF",
        "glow": "#FFFFFF",
        "cloud": "#FFFFFF",
        "hills": ["#C9C9C9", "#8F8F8F", "#3A3A3A"],
        "accent": "#161616",
        "treeline": "#202020",
    },
}

_KNOWN_PALETTES = {
    "night": ("night", ("night", "midnight", "dark", "moon", "star", "starry")),
    "sunset": ("sunset", ("sunset", "sunrise", "dusk", "dawn", "golden", "orange")),
    "ocean": ("ocean", ("ocean", "sea", "beach", "wave", "coast", "water", "island")),
    "forest": ("forest", ("forest", "jungle", "woods", "tree", "pine", "green")),
    "desert": ("desert", ("desert", "dune", "sand", "sahara")),
    "winter": ("winter", ("winter", "snow", "ice", "arctic", "frost")),
    "lavender": ("lavender", ("purple", "lavender", "pink", "violet", "magenta")),
    "space": ("space", ("space", "galaxy", "nebula", "planet", "cosmo", "universe")),
    "mono": ("mono", ("monochrome", "black and white", "grayscale", "greyscale")),
}


def _choose_palette(prompt: str) -> tuple[dict, str, set[str]]:
    text = (prompt or "").lower()
    features: set[str] = set()
    for feature in ("ocean", "sea", "beach", "wave", "water", "mountain", "alps", "valley", "forest", "tree", "pine", "jungle", "desert", "dune", "snow", "winter", "ice", "city", "skyline", "building", "space", "galaxy", "planet", "star", "cloud", "sun", "moon", "octopus", "takoyo", "bird", "boat", "sail", "flower", "field"):
        if feature in text:
            features.add(feature)
    palette_key = "day"
    style = "A clean, calm landscape with a soft gradient sky, rounded clouds and rolling hills."
    for key, (name, words) in _KNOWN_PALETTES.items():
        if any(word in text for word in words):
            palette_key = key
            break
    if "space" in features:
        palette_key = "space"
    if "snow" in features or "winter" in features:
        palette_key = "winter"
    if palette_key == "day" and "sunset" in text:
        palette_key = "sunset"
    if palette_key == "day" and "ocean" in features:
        palette_key = "ocean"
    return PALETTES[palette_key], palette_key, features


# --------------------------------------------------------------------------- #
# Drawing helpers
# --------------------------------------------------------------------------- #
def _hex(value: str) -> tuple[int, int, int]:
    value = value.lstrip("#")
    return tuple(int(value[i : i + 2], 16) for i in (0, 2, 4))


def _mix(a, b, t: float) -> tuple[int, int, int]:
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def _gradient(w: int, h: int, stops: list[str]) -> Image.Image:
    colors = [_hex(c) for c in stops]
    column = Image.new("RGB", (1, h))
    pixels = []
    segments = len(colors) - 1
    for y in range(h):
        pos = y / max(1, h - 1) * segments
        idx = min(segments - 1, int(pos))
        t = pos - idx
        pixels.append(_mix(colors[idx], colors[idx + 1], t))
    column.putdata(pixels)
    return column.resize((w, h), Image.BILINEAR)


def _radial_glow(size: int, color: str, power: float = 2.0) -> Image.Image:
    size = max(8, int(size))
    layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    rgb = _hex(color)
    steps = 46
    for i in range(steps, 0, -1):
        t = i / steps
        alpha = int(150 * ((1 - t) ** power))
        radius = int(size * 0.5 * t)
        d.ellipse(
            (size // 2 - radius, size // 2 - radius, size // 2 + radius, size // 2 + radius),
            fill=(*rgb, alpha),
        )
    return layer.filter(ImageFilter.GaussianBlur(max(2, size // 40)))


def _smooth_hill(w: int, h: int, base: float, amp: float, rng: random.Random, freq: float, bottom: int) -> list[tuple[int, int]]:
    phase1 = rng.uniform(0, math.tau)
    phase2 = rng.uniform(0, math.tau)
    tilt = rng.uniform(-0.03, 0.03)
    points = []
    step = max(6, w // 90)
    for x in range(0, w + step, step):
        u = x / w
        y = (
            base
            + amp * math.sin(u * math.tau * freq + phase1)
            + amp * 0.55 * math.sin(u * math.tau * freq * 2.2 + phase2)
            + tilt * (u - 0.5) * h
        )
        points.append((x, int(y)))
    points.append((w, bottom))
    points.append((0, bottom))
    return points


def _cloud_layer(w: int, h: int, rng: random.Random, color: str, count: int = 3) -> Image.Image:
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    rgb = _hex(color)
    layouts = [
        (rng.uniform(0.48, 0.64), rng.uniform(0.16, 0.24), rng.uniform(0.19, 0.26)),
        (rng.uniform(0.16, 0.28), rng.uniform(0.13, 0.20), rng.uniform(0.10, 0.14)),
        (rng.uniform(0.78, 0.89), rng.uniform(0.18, 0.26), rng.uniform(0.08, 0.12)),
        (rng.uniform(0.30, 0.42), rng.uniform(0.30, 0.38), rng.uniform(0.06, 0.09)),
    ]
    for i, (cx, cy, scale) in enumerate(layouts[: max(1, count)]):
        cw = w * scale
        ch = cw * 0.42
        x = w * cx
        y = h * cy
        puffs = [
            (0.0, 0.1, 0.62, 0.52),
            (-0.26, 0.08, 0.46, 0.44),
            (0.28, 0.10, 0.44, 0.40),
            (-0.12, -0.15, 0.44, 0.46),
            (0.14, -0.10, 0.36, 0.38),
        ]
        for px, py, pw, ph in puffs:
            box = (
                int(x + (px - pw / 2) * cw),
                int(y + (py - ph / 2) * ch),
                int(x + (px + pw / 2) * cw),
                int(y + (py + ph / 2) * ch),
            )
            d.ellipse(box, fill=(*rgb, 255))
        d.rounded_rectangle(
            (int(x - cw * 0.44), int(y + ch * 0.10), int(x + cw * 0.44), int(y + ch * 0.36)),
            radius=int(ch * 0.16),
            fill=(*rgb, 255),
        )
    return layer.filter(ImageFilter.GaussianBlur(max(1, w // 900)))


def _tree(d: ImageDraw.ImageDraw, x: int, y: int, size: int, color: str, kind: str = "pine"):
    rgb = _hex(color)
    if kind == "pine":
        d.rectangle((x - size * 0.045, y - size * 0.10, x + size * 0.045, y), fill=(*rgb, 255))
        for i in range(3):
            top = y - size * (0.28 + i * 0.22)
            half = size * (0.18 + i * 0.055)
            d.polygon([(x, top), (x - half, top + size * 0.30), (x + half, top + size * 0.30)], fill=(*rgb, 255))
    else:
        d.rectangle((x - size * 0.03, y - size * 0.34, x + size * 0.03, y), fill=(*rgb, 255))
        for dx, dy, r in ((0, -0.52, 0.24), (-0.16, -0.40, 0.17), (0.16, -0.42, 0.18)):
            d.ellipse((x + (dx - r) * size, y + (dy - r) * size, x + (dx + r) * size, y + (dy + r) * size), fill=(*rgb, 255))


def _mountain_layer(w: int, h: int, base_y: float, depth: float, color: str, rng: random.Random, snow: bool = False) -> Image.Image:
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    rgb = _hex(color)
    peaks = []
    count = rng.randint(5, 8)
    for i in range(count + 1):
        x = int(w * i / count + rng.randint(-w // 40, w // 40))
        peak = h * (base_y - rng.uniform(0.02, depth))
        peaks.append((x, int(peak)))
    points = [(0, h)]
    for i, (x, y) in enumerate(peaks):
        if i == 0:
            points.append((0, y))
        points.append((x, y))
        if i < len(peaks) - 1:
            mid_x = (x + peaks[i + 1][0]) // 2
            points.append((mid_x, int((y + peaks[i + 1][1]) / 2 + h * rng.uniform(0.01, 0.05))))
    points.append((w, peaks[-1][1]))
    points.append((w, h))
    d.polygon(points, fill=(*rgb, 255))
    if snow:
        for x, y in peaks:
            cap_w = w * rng.uniform(0.035, 0.06)
            cap_h = h * rng.uniform(0.035, 0.065)
            d.polygon(
                [(x, y), (x - cap_w, y + cap_h * 1.6), (x - cap_w * 0.3, y + cap_h * 1.2), (x + cap_w * 0.4, y + cap_h * 1.7), (x + cap_w, y + cap_h * 1.5)],
                fill=(250, 252, 255, 235),
            )
    return layer


def _window_light(d: ImageDraw.ImageDraw, box, color: tuple[int, int, int, int]):
    x0, y0, x1, y1 = box
    count = max(1, int((x1 - x0) / max(4, (y1 - y0) * 0.4)))
    for i in range(count):
        wx = x0 + 3 + i * max(5, int((x1 - x0) / count))
        for j in range(max(1, int((y1 - y0) / 12))):
            wy = y0 + 5 + j * 11
            if (i + j) % 3 != 0:
                d.rectangle((wx, wy, wx + 2, wy + 3), fill=color)


def _city_layer(w: int, h: int, rng: random.Random, palette: dict) -> Image.Image:
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    base = int(h * 0.74)
    x = -20
    dark = _hex(palette["hills"][2])
    lit = (255, 228, 150, 220)
    while x < w:
        bw = int(w * rng.uniform(0.035, 0.085))
        bh = int(h * rng.uniform(0.08, 0.28))
        top = base - bh
        d.rectangle((x, top, x + bw, base), fill=(*dark, 255))
        _window_light(d, (x + 4, top + 8, x + bw - 4, base - 4), lit)
        if rng.random() < 0.25:
            d.rectangle((x + bw * 0.45, top - h * 0.03, x + bw * 0.55, top), fill=(*dark, 255))
        x += bw + int(w * rng.uniform(0.006, 0.02))
    return layer


def _scatter_stars(layer: Image.Image, rng: random.Random, count: int, w: int, h: int):
    d = ImageDraw.Draw(layer)
    for _ in range(count):
        x = rng.uniform(0, w)
        y = rng.uniform(0, h * 0.62)
        r = rng.uniform(0.6, 2.0) * (w / 1400)
        alpha = int(rng.uniform(100, 235))
        d.ellipse((x - r, y - r, x + r, y + r), fill=(255, 255, 255, alpha))
    for _ in range(max(2, count // 45)):
        x = rng.uniform(0.05, 0.95) * w
        y = rng.uniform(0.04, 0.55) * h
        size = w * rng.uniform(0.003, 0.007)
        d.line((x - size * 2, y, x + size * 2, y), fill=(255, 255, 255, 160), width=max(1, int(w / 900)))
        d.line((x, y - size * 2, x, y + size * 2), fill=(255, 255, 255, 160), width=max(1, int(w / 900)))


def _nebula(w: int, h: int, rng: random.Random) -> Image.Image:
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    colors = [(130, 90, 255), (255, 100, 180), (80, 170, 255)]
    for i, color in enumerate(colors):
        cx = rng.uniform(0.2, 0.8) * w
        cy = rng.uniform(0.15, 0.5) * h
        rw = rng.uniform(0.2, 0.4) * w
        rh = rw * rng.uniform(0.3, 0.6)
        d.ellipse((cx - rw / 2, cy - rh / 2, cx + rw / 2, cy + rh / 2), fill=(*color, 46))
    return layer.filter(ImageFilter.GaussianBlur(max(8, w // 35)))


def _birds(layer: Image.Image, w: int, h: int, rng: random.Random, color: str):
    d = ImageDraw.Draw(layer)
    rgb = _hex(color)
    for _ in range(rng.randint(2, 4)):
        x = rng.uniform(0.2, 0.8) * w
        y = rng.uniform(0.24, 0.42) * h
        s = w * rng.uniform(0.006, 0.011)
        d.arc((x - s, y - s, x, y + s * 0.3), 200, 340, fill=(*rgb, 190), width=max(1, int(w / 800)))
        d.arc((x, y - s, x + s, y + s * 0.3), 200, 340, fill=(*rgb, 190), width=max(1, int(w / 800)))


def _ocean(w: int, h: int, palette: dict, rng: random.Random, sun_x: float, sun_y: float) -> Image.Image:
    layer = Image.new("RGB", (w, h))
    horizon = int(h * 0.56)
    sea = _gradient(w, h - horizon, [palette["hills"][2], palette["hills"][1], palette["hills"][0]])
    layer.paste(Image.new("RGB", (w, horizon), _hex(palette["sky"][1])), (0, 0))
    layer.paste(sea, (0, horizon))
    d = ImageDraw.Draw(layer, "RGBA")
    for i in range(60):
        y = horizon + int((h - horizon) * (i / 60) ** 1.4)
        width = w * rng.uniform(0.03, 0.2) * (0.5 + i / 60)
        x = rng.uniform(0, w - width)
        d.line((x, y, x + width, y), fill=(255, 255, 255, int(22 + 40 * (i / 60))), width=max(1, int(w / 1100)))
    # sun path
    for i in range(26):
        t = i / 26
        y = horizon + int((h - horizon) * t ** 1.3)
        width = w * (0.02 + 0.12 * t)
        x = sun_x * w
        d.rounded_rectangle(
            (x - width / 2, y - 2, x + width / 2, y + 2),
            radius=3,
            fill=(*_hex(palette["sun"]), int(120 * (1 - t) + 20)),
        )
    return layer


def _grain(im: Image.Image, strength: int = 10) -> Image.Image:
    noise = Image.effect_noise(im.size, strength).convert("L")
    noise_rgb = Image.merge("RGB", (noise, noise, noise))
    return Image.blend(im.convert("RGB"), noise_rgb, 0.018)


def _vignette(im: Image.Image) -> Image.Image:
    mask = Image.radial_gradient("L").resize(im.size, Image.BILINEAR)
    mask = mask.point(lambda v: 255 - int(v * 0.38))
    dark = Image.new("RGB", im.size, (0, 0, 0))
    return Image.composite(im.convert("RGB"), dark, mask)


def render_art(prompt: str, size: tuple[int, int] = (1024, 1280), seed: int | None = None) -> tuple[Image.Image, int, str]:
    w, h = max(512, int(size[0])), max(512, int(size[1]))
    seed = int(seed if seed is not None else int(hashlib.sha256((prompt or "takoyo").lower().encode()).hexdigest()[:8], 16))
    rng = random.Random(seed)
    palette, palette_key, features = _choose_palette(prompt)

    # supersample for smooth silhouettes
    scale = 2
    W, H = w * scale, h * scale
    im = _gradient(W, H, palette["sky"]).convert("RGBA")

    night = palette_key in ("night", "space")
    if palette_key == "space":
        im.alpha_composite(_nebula(W, H, rng))
    if night or "star" in features:
        stars = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        _scatter_stars(stars, rng, 300 if palette_key == "space" else 130, W, H)
        im.alpha_composite(stars)

    # sun / moon
    sun_x = rng.uniform(0.62, 0.8)
    sun_y = rng.uniform(0.18, 0.28)
    glow_size = int(W * 0.62)
    glow = _radial_glow(glow_size, palette["glow"])
    im.alpha_composite(glow, (int(sun_x * W - glow_size / 2), int(sun_y * H - glow_size / 2)))
    disc_r = W * (0.028 if palette_key != "space" else 0.034)
    d = ImageDraw.Draw(im, "RGBA")
    d.ellipse(
        (sun_x * W - disc_r, sun_y * H - disc_r, sun_x * W + disc_r, sun_y * H + disc_r),
        fill=(*_hex(palette["sun"]), 235),
    )

    if "planet" in features:
        planet_r = W * 0.06
        px, py = W * 0.24, H * 0.30
        d.ellipse((px - planet_r, py - planet_r, px + planet_r, py + planet_r), fill=(210, 225, 255, 235))
        d.ellipse((px - planet_r * 0.55, py - planet_r * 0.8, px + planet_r * 0.9, py + planet_r * 0.5), fill=(255, 255, 255, 35))
        d.arc((px - planet_r * 1.7, py - planet_r * 0.35, px + planet_r * 1.7, py + planet_r * 0.45), 200, 340, fill=(230, 235, 255, 160), width=max(2, int(W / 450)))

    # clouds
    cloud_count = 0 if "clear" in prompt.lower() else 3
    if palette_key in ("space",):
        cloud_count = 1 if "cloud" in features else 0
    if cloud_count and not (night and palette_key != "night"):
        im.alpha_composite(_cloud_layer(W, H, rng, palette["cloud"], cloud_count))
    elif night and "cloud" in features:
        im.alpha_composite(_cloud_layer(W, H, rng, palette["cloud"], 2))

    ocean = "ocean" in features or "sea" in features or "beach" in features or "coast" in features
    if ocean:
        sea = _ocean(W, H, palette, rng, sun_x, sun_y)
        mask = Image.new("L", (W, H), 0)
        ImageDraw.Draw(mask).rectangle((0, int(H * 0.56), W, H), fill=255)
        im.paste(sea, (0, 0), mask)
        # gentle dunes at the very bottom
        hill = _smooth_hill(W, H, H * 0.86, H * 0.035, rng, 1.2, H)
        d.polygon(hill, fill=(*_hex(palette["hills"][2]), 255))
    elif palette_key == "space":
        ridge = _smooth_hill(W, H, H * 0.84, H * 0.045, rng, 1.4, H)
        d.polygon(ridge, fill=(*_hex(palette["hills"][2]), 255))
    else:
        if "mountain" in features or "alps" in features or "valley" in features:
            im.alpha_composite(_mountain_layer(W, H, 0.68, 0.20, palette["hills"][2], rng, snow=palette_key == "winter"))
        back = _smooth_hill(W, H, H * 0.72, H * 0.052, rng, 1.15, H)
        d.polygon(back, fill=(*_hex(palette["hills"][0]), 255))
        mid = _smooth_hill(W, H, H * 0.80, H * 0.045, rng, 1.45, H)
        d.polygon(mid, fill=(*_hex(palette["hills"][1]), 255))
        front = _smooth_hill(W, H, H * 0.89, H * 0.038, rng, 1.8, H)
        d.polygon(front, fill=(*_hex(palette["hills"][2]), 255))

        if palette_key in ("forest", "winter") or "forest" in features or "tree" in features or "pine" in features:
            tree_color = palette["treeline"]
            tree_count = rng.randint(10, 22)
            for _ in range(tree_count):
                tx = rng.uniform(0.03, 0.97) * W
                base_y = H * rng.uniform(0.78, 0.9)
                tsize = H * rng.uniform(0.035, 0.075)
                _tree(d, int(tx), int(base_y), int(tsize), tree_color, "pine" if rng.random() < 0.7 else "round")

    if "city" in features or "skyline" in features or "building" in features:
        im.alpha_composite(_city_layer(W, H, rng, palette))

    if "bird" in features or (palette_key == "day" and rng.random() < 0.55):
        _birds(im, W, H, rng, palette.get("accent", "#4A4A4A"))

    if "octopus" in features or "takoyo" in features:
        try:
            from . import visuals

            mascot = visuals.mascot_image(int(W * 0.13))
            mx = int(W * 0.52)
            my = int(H * 0.83)
            shadow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
            ImageDraw.Draw(shadow).ellipse(
                (mx - W * 0.075, my - H * 0.012, mx + W * 0.075, my + H * 0.012),
                fill=(0, 0, 0, 55),
            )
            im.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(W // 90)))
            im.alpha_composite(mascot, (mx - mascot.width // 2, my - mascot.height))
        except Exception:
            pass

    im = im.convert("RGB").resize((w, h), Image.LANCZOS)
    im = _grain(im)
    im = _vignette(im)
    return im, seed, palette_key


# --------------------------------------------------------------------------- #
# Generator
# --------------------------------------------------------------------------- #
class ImageGenerator:
    def __init__(self, config: Config | None = None):
        ensure_dirs()
        self.config = config or Config()
        IMAGES_DIR.mkdir(parents=True, exist_ok=True)

    def generate(
        self,
        prompt: str,
        provider: str | None = None,
        size: str | None = None,
        seed: int | None = None,
    ) -> dict:
        prompt = (prompt or "").strip()
        if not prompt:
            raise ImageError("Tell Takoyo what to paint first.")
        provider = (provider or self.config.get("image_provider", "takoyo")).lower()
        size = size or self.config.get("image_size", "1024x1280")
        if provider == "openai":
            return self._generate_dalle(prompt, size)
        return self._generate_local(prompt, size, seed)

    def _generate_local(self, prompt: str, size: str, seed: int | None) -> dict:
        try:
            dims = [int(part) for part in size.lower().replace(" ", "").split("x")]
            width, height = (dims + [1024, 1280])[:2]
        except Exception:
            width, height = 1024, 1280
        width = max(512, min(2048, width))
        height = max(512, min(2048, height))
        started = time.time()
        image, used_seed, style = render_art(prompt, (width, height), seed)
        stamp = time.strftime("%Y%m%d-%H%M%S")
        path = IMAGES_DIR / f"takoyo-{stamp}-{used_seed & 0xFFFF:04x}.png"
        image.save(path, "PNG", optimize=True)
        meta = {
            "path": str(path),
            "prompt": prompt,
            "provider": "takoyo",
            "model": "Takoyo Art",
            "width": image.width,
            "height": image.height,
            "seed": used_seed,
            "style": style,
            "created": time.time(),
            "seconds": round(time.time() - started, 2),
        }
        self._write_meta(path, meta)
        return meta

    def _generate_dalle(self, prompt: str, size: str) -> dict:
        import urllib.error
        import urllib.request

        key = self.config.api_key("openai")
        if not key:
            raise ImageError("DALL·E needs an OpenAI API key. Add one in Settings, or use Takoyo Art (built in).")
        allowed = {"1024x1024", "1024x1792", "1792x1024"}
        if size not in allowed:
            size = "1024x1792"
        payload = {
            "model": "dall-e-3",
            "prompt": prompt[:3900],
            "n": 1,
            "size": size,
            "quality": "standard",
            "response_format": "b64_json",
        }
        request = urllib.request.Request(
            self.config.base_url("openai") + "/images/generations",
            data=json.dumps(payload).encode("utf-8"),
            headers={
                "Content-Type": "application/json",
                "Authorization": f"Bearer {key}",
                "User-Agent": USER_AGENT,
            },
            method="POST",
        )
        try:
            with urllib.request.urlopen(request, timeout=180) as response:
                data = json.loads(response.read().decode("utf-8"))
        except urllib.error.HTTPError as exc:
            body = exc.read().decode("utf-8", "replace")
            try:
                message = json.loads(body)["error"]["message"]
            except Exception:
                message = body[:300]
            raise ImageError(f"DALL·E error: {message}") from exc
        except Exception as exc:
            raise ImageError(f"Could not reach OpenAI Images: {exc}") from exc
        items = data.get("data") or []
        if not items or not items[0].get("b64_json"):
            raise ImageError("OpenAI did not return an image. Try another prompt.")
        raw = base64.b64decode(items[0]["b64_json"])
        stamp = time.strftime("%Y%m%d-%H%M%S")
        path = IMAGES_DIR / f"dalle-{stamp}-{int(time.time()) % 1000:03d}.png"
        path.write_bytes(raw)
        try:
            with Image.open(path) as im:
                width, height = im.size
        except Exception:
            width, height = 1024, 1792
        meta = {
            "path": str(path),
            "prompt": prompt,
            "provider": "openai",
            "model": "DALL·E 3",
            "width": width,
            "height": height,
            "seed": None,
            "style": "dalle",
            "created": time.time(),
        }
        self._write_meta(path, meta)
        return meta

    def _write_meta(self, path: Path, meta: dict) -> None:
        try:
            path.with_suffix(".json").write_text(json.dumps(meta, indent=1), "utf-8")
        except Exception:
            pass

    def list_images(self, limit: int = 120) -> list[dict]:
        items: list[dict] = []
        for path in IMAGES_DIR.glob("*.png"):
            meta_path = path.with_suffix(".json")
            meta = {}
            if meta_path.exists():
                try:
                    meta = json.loads(meta_path.read_text("utf-8"))
                except Exception:
                    meta = {}
            meta.setdefault("path", str(path))
            meta.setdefault("prompt", path.stem)
            meta.setdefault("created", path.stat().st_mtime)
            meta.setdefault("width", 0)
            meta.setdefault("height", 0)
            meta["filename"] = path.name
            items.append(meta)
        items.sort(key=lambda m: m.get("created", 0), reverse=True)
        return items[:limit]

    def latest(self) -> dict | None:
        items = self.list_images(limit=1)
        return items[0] if items else None

    def delete(self, path: str) -> None:
        p = Path(path)
        try:
            if p.parent.resolve() != IMAGES_DIR.resolve():
                return
        except Exception:
            return
        p.unlink(missing_ok=True)
        p.with_suffix(".json").unlink(missing_ok=True)
