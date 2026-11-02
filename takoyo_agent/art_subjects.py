"""Subject-aware flat illustrations for Takoyo Art.

The landscape painter in :mod:`takoyo_agent.image_gen` only ever draws skies,
clouds and hills.  This module adds a small cast of procedural subjects and
colour parsing so prompts like "a red car" come back as something recognisable
instead of an unrelated mountain scene.
"""

from __future__ import annotations

import math
import random
import re

from PIL import Image, ImageDraw, ImageFilter

COLOR_WORDS: dict[str, tuple[int, int, int]] = {
    "red": (219, 68, 68),
    "crimson": (190, 40, 70),
    "scarlet": (232, 60, 45),
    "orange": (240, 140, 50),
    "amber": (240, 180, 60),
    "yellow": (245, 205, 70),
    "gold": (225, 175, 55),
    "green": (75, 165, 95),
    "emerald": (45, 155, 110),
    "lime": (150, 200, 60),
    "teal": (45, 155, 155),
    "cyan": (60, 180, 205),
    "blue": (65, 125, 215),
    "navy": (40, 70, 130),
    "purple": (130, 85, 195),
    "violet": (145, 95, 210),
    "lavender": (175, 155, 220),
    "pink": (238, 125, 170),
    "magenta": (210, 70, 165),
    "rose": (225, 90, 125),
    "brown": (145, 100, 65),
    "black": (45, 45, 50),
    "white": (245, 245, 245),
    "gray": (130, 132, 140),
    "grey": (130, 132, 140),
    "silver": (185, 190, 200),
}

_SUBJECTS: list[tuple[str, tuple[str, ...]]] = [
    ("octopus", ("octopus", "takoyo")),
    ("rocket", ("rocket", "spaceship", "spacecraft")),
    ("car", ("car", "automobile", "sedan")),
    ("robot", ("robot", "android", "droid")),
    ("cat", ("cat", "kitten")),
    ("dog", ("dog", "puppy")),
    ("house", ("house", "cottage", "cabin")),
    ("flower", ("flower", "rose", "tulip", "sunflower", "daisy", "blossom")),
    ("boat", ("boat", "sailboat", "yacht")),
    ("fish", ("fish", "goldfish")),
    ("bird", ("bird", "parrot", "owl", "sparrow")),
    ("cake", ("cake", "cupcake")),
    ("cup", ("coffee", "cup", "mug", "tea")),
    ("balloon", ("balloon",)),
    ("heart", ("heart",)),
    ("star", ("star",)),
    ("moon", ("moon", "crescent")),
    ("sun", ("sun",)),
    ("tree", ("tree", "oak", "palm")),
]

DEFAULT_COLORS: dict[str, tuple[int, int, int]] = {
    "car": (210, 65, 65),
    "cat": (236, 170, 95),
    "dog": (170, 120, 80),
    "house": (235, 220, 200),
    "flower": (232, 110, 150),
    "boat": (85, 140, 200),
    "rocket": (225, 225, 235),
    "robot": (125, 155, 195),
    "fish": (245, 145, 70),
    "bird": (85, 150, 215),
    "cake": (245, 200, 125),
    "cup": (235, 235, 240),
    "balloon": (225, 75, 90),
    "heart": (222, 60, 90),
    "star": (245, 200, 70),
    "moon": (250, 245, 215),
    "sun": (250, 200, 65),
    "tree": (80, 150, 95),
    "octopus": (232, 125, 155),
}

_SCENE_RE = re.compile(
    r"\b(?:landscapes?|mountains?|alps|valley|hills?|forest|jungle|woods|desert|dunes?|snow|winter|ice|"
    r"glacier|arctic|ocean|sea|seaside|beach|coast|waves?|island|lake|river|city|skyline|buildings?|"
    r"space|galaxy|nebula|planet|starry|night sky|sunset|sunrise|dusk|dawn|horizon|countryside|meadow|"
    r"fields?|rainforest|savanna|sand)\b",
    re.IGNORECASE,
)

_LEAD_VERB = re.compile(
    r"^\s*(?:(?:please|hey|hi|ok|okay)\s+)?"
    r"(?:(?:can|could|would|will)\s+you\s+|i\s+(?:want|need|would\s+like)\s+(?:you\s+to\s+)?|let'?s\s+)?"
    r"(?:generate|create|make|draw|paint|render|illustrate|sketch|design|show|give)\b",
    re.IGNORECASE,
)
_IMAGE_NOUN = re.compile(
    r"^\s*(?:me\s+)?(?:a|an|the|some)?\s*(?:quick\s+|simple\s+|nice\s+|beautiful\s+)?"
    r"(?:images?|pictures?|photos?|photographs?|drawings?|illustrations?|artwork|art|posters?|"
    r"wallpapers?|renders?|sketch(?:es)?|paintings?|logos?|icons?)\b",
    re.IGNORECASE,
)
_CONNECTOR = re.compile(r"^\s*(?:of|with|showing|that\s+shows?|featuring|about|for)\b", re.IGNORECASE)
_TRAILING = re.compile(r"[\s.!?,;:]+$")
_COLOR_RE = re.compile(
    r"\b(" + "|".join(sorted(map(re.escape, COLOR_WORDS), key=len, reverse=True)) + r")\b",
    re.IGNORECASE,
)


def clean_prompt(prompt: str) -> str:
    text = (prompt or "").strip()
    for _ in range(5):
        before = text
        text = _LEAD_VERB.sub("", text, count=1).strip()
        text = _IMAGE_NOUN.sub("", text, count=1).strip()
        text = _CONNECTOR.sub("", text, count=1).strip()
        if text == before:
            break
    text = re.sub(r"\bplease\b", "", text, flags=re.IGNORECASE)
    return _TRAILING.sub("", re.sub(r"\s+", " ", text)).strip()


def _first_subject(text: str) -> str | None:
    best: str | None = None
    best_pos: int | None = None
    for name, words in _SUBJECTS:
        for word in words:
            match = re.search(r"\b" + re.escape(word) + r"(?:s|es)?\b", text)
            if match and (best_pos is None or match.start() < best_pos):
                best, best_pos = name, match.start()
    return best


def analyze(prompt: str) -> dict:
    text = clean_prompt(prompt)
    lowered = text.lower()
    color_match = _COLOR_RE.search(lowered)
    subject = _first_subject(lowered)
    return {
        "text": text,
        "subject": subject,
        "color": COLOR_WORDS[color_match.group(1).lower()] if color_match else None,
        "scene": bool(_SCENE_RE.search(lowered)),
    }


def default_color(subject: str | None) -> tuple[int, int, int]:
    return DEFAULT_COLORS.get(subject or "", (110, 150, 190))


# --------------------------------------------------------------------------- #
# Small helpers
# --------------------------------------------------------------------------- #
def _shade(color: tuple[int, int, int], factor: float) -> tuple[int, int, int]:
    return tuple(max(0, min(255, int(c * factor))) for c in color)


def _tint(color: tuple[int, int, int], amount: float) -> tuple[int, int, int]:
    return tuple(int(c + (255 - c) * amount) for c in color)


def _mix(a: tuple[int, int, int], b: tuple[int, int, int], t: float) -> tuple[int, int, int]:
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def _ink(color: tuple[int, int, int]) -> tuple[int, int, int]:
    return _shade(color, 0.34) if sum(color) < 660 else _shade(color, 0.42)


def _hex(color: tuple[int, int, int]) -> str:
    return "#%02X%02X%02X" % color


def _gradient(w: int, h: int, top: tuple[int, int, int], bottom: tuple[int, int, int]) -> Image.Image:
    column = Image.new("RGB", (1, h))
    column.putdata([_mix(top, bottom, y / max(1, h - 1)) for y in range(h)])
    return column.resize((w, h), Image.BILINEAR)


def _glow(layer: Image.Image, cx: float, cy: float, radius: float, color: tuple[int, int, int],
          alpha: int = 120, power: float = 1.8) -> None:
    radius = max(8.0, radius)
    blob = Image.new("RGBA", layer.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(blob)
    steps = 34
    for i in range(steps, 0, -1):
        t = i / steps
        r = radius * t
        d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(*color, int(alpha * ((1 - t) ** power))))
    layer.alpha_composite(blob.filter(ImageFilter.GaussianBlur(max(2, int(radius * 0.07)))))


def _dot(d: ImageDraw.ImageDraw, cx: float, cy: float, r: float, color: tuple[int, int, int],
         alpha: int = 255, outline: tuple[int, int, int] | None = None, width: int = 0) -> None:
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(*color, alpha),
              outline=(*tuple(outline[:3]), 255) if outline else None, width=width)


def _shadow(d: ImageDraw.ImageDraw, cx: float, base_y: float, s: float, alpha: int = 60) -> None:
    d.ellipse((cx - s * 0.46, base_y - s * 0.025, cx + s * 0.46, base_y + s * 0.075),
              fill=(20, 22, 28, alpha))


# --------------------------------------------------------------------------- #
# Subjects
# --------------------------------------------------------------------------- #
def _car(d, cx, base_y, s, base, rng):
    ink = _ink(base)
    light = _tint(base, 0.45)
    _shadow(d, cx, base_y, s * 1.04)
    cabin = (cx - s * 0.30, base_y - s * 0.44, cx + s * 0.24, base_y - s * 0.22)
    body = (cx - s * 0.50, base_y - s * 0.26, cx + s * 0.50, base_y - s * 0.075)
    d.rounded_rectangle(cabin, radius=s * 0.07, fill=(*base, 255), outline=(*ink, 255),
                        width=max(2, int(s * 0.012)))
    d.rounded_rectangle(body, radius=s * 0.08, fill=(*base, 255), outline=(*ink, 255),
                        width=max(2, int(s * 0.012)))
    glass = (188, 220, 240, 255)
    d.rounded_rectangle((cx - s * 0.265, base_y - s * 0.405, cx - s * 0.035, base_y - s * 0.245),
                        radius=s * 0.03, fill=glass, outline=(*ink, 255),
                        width=max(1, int(s * 0.008)))
    d.rounded_rectangle((cx - s * 0.005, base_y - s * 0.405, cx + s * 0.205, base_y - s * 0.245),
                        radius=s * 0.03, fill=glass, outline=(*ink, 255),
                        width=max(1, int(s * 0.008)))
    for wx in (cx - s * 0.295, cx + s * 0.295):
        r = s * 0.105
        wy = base_y - s * 0.055
        _dot(d, wx, wy, r, (38, 40, 46))
        _dot(d, wx, wy, r * 0.46, (205, 208, 218))
    d.ellipse((cx + s * 0.415, base_y - s * 0.205, cx + s * 0.475, base_y - s * 0.15),
              fill=(255, 236, 150, 255), outline=(*ink, 255), width=max(1, int(s * 0.006)))
    d.rounded_rectangle((cx - s * 0.49, base_y - s * 0.205, cx - s * 0.445, base_y - s * 0.155),
                        radius=s * 0.008, fill=(235, 80, 80, 255))
    d.line((cx + s * 0.005, base_y - s * 0.245, cx + s * 0.005, base_y - s * 0.085),
           fill=(*ink, 255), width=max(1, int(s * 0.007)))
    d.rounded_rectangle((cx - s * 0.17, base_y - s * 0.19, cx - s * 0.11, base_y - s * 0.168),
                        radius=s * 0.008, fill=(*light, 255), outline=(*ink, 255),
                        width=max(1, int(s * 0.005)))


def _cat(d, cx, base_y, s, base, rng):
    ink = _ink(base)
    light = _tint(base, 0.5)
    _shadow(d, cx, base_y, s * 0.78)
    d.arc((cx + s * 0.10, base_y - s * 0.30, cx + s * 0.40, base_y + s * 0.01), 300, 115,
          fill=(*ink, 255), width=max(5, int(s * 0.03)))
    d.ellipse((cx - s * 0.19, base_y - s * 0.39, cx + s * 0.19, base_y - s * 0.01),
              fill=(*base, 255), outline=(*ink, 255), width=max(3, int(s * 0.012)))
    d.ellipse((cx - s * 0.20, base_y - s * 0.30, cx + s * 0.16, base_y - s * 0.08),
              fill=(*light, 255))
    hr = s * 0.17
    hy = base_y - s * 0.455
    for sign in (-1, 1):
        d.polygon([(cx + sign * hr * 0.82, hy - hr * 0.35), (cx + sign * hr * 0.42, hy - hr * 1.30),
                   (cx + sign * hr * 0.02, hy - hr * 0.85)], fill=(*base, 255), outline=(*ink, 255))
    d.ellipse((cx - hr, hy - hr, cx + hr, hy + hr), fill=(*base, 255), outline=(*ink, 255),
              width=max(3, int(s * 0.012)))
    for sign in (-1, 1):
        ex = cx + sign * hr * 0.36
        ey = hy - hr * 0.06
        d.ellipse((ex - hr * 0.13, ey - hr * 0.17, ex + hr * 0.13, ey + hr * 0.17), fill=(38, 42, 48, 255))
        d.ellipse((ex - hr * 0.05, ey - hr * 0.11, ex + hr * 0.03, ey - hr * 0.02), fill=(255, 255, 255, 255))
    d.polygon([(cx - hr * 0.07, hy + hr * 0.27), (cx + hr * 0.07, hy + hr * 0.27), (cx, hy + hr * 0.38)],
              fill=(230, 125, 135, 255))
    for sign in (-1, 1):
        for offset in (-0.12, 0.0, 0.12):
            d.line((cx + sign * hr * 0.45, hy + hr * (0.30 + offset * 0.4),
                    cx + sign * hr * 1.25, hy + hr * (0.20 + offset)),
                   fill=(*ink, 255), width=max(1, int(s * 0.006)))


def _dog(d, cx, base_y, s, base, rng):
    ink = _ink(base)
    light = _tint(base, 0.45)
    _shadow(d, cx, base_y, s * 0.82)
    d.arc((cx + s * 0.16, base_y - s * 0.40, cx + s * 0.40, base_y - s * 0.22), 300, 20,
          fill=(*ink, 255), width=max(6, int(s * 0.035)))
    d.ellipse((cx - s * 0.21, base_y - s * 0.36, cx + s * 0.18, base_y - s * 0.02),
              fill=(*base, 255), outline=(*ink, 255), width=max(3, int(s * 0.012)))
    d.ellipse((cx - s * 0.14, base_y - s * 0.28, cx + s * 0.18, base_y - s * 0.06),
              fill=(*light, 255))
    hr = s * 0.16
    hy = base_y - s * 0.48
    for sign in (-1, 1):
        d.ellipse((cx + sign * hr * 0.95 - hr * 0.34, hy - hr * 0.55,
                   cx + sign * hr * 0.95 + hr * 0.30, hy + hr * 0.75),
                  fill=(*_shade(base, 0.72), 255), outline=(*ink, 255), width=max(2, int(s * 0.008)))
    d.ellipse((cx - hr, hy - hr, cx + hr, hy + hr), fill=(*base, 255), outline=(*ink, 255),
              width=max(3, int(s * 0.012)))
    d.ellipse((cx - hr * 0.52, hy + hr * 0.06, cx + hr * 0.52, hy + hr * 0.86),
              fill=(*light, 255), outline=(*ink, 255), width=max(2, int(s * 0.007)))
    for sign in (-1, 1):
        ex = cx + sign * hr * 0.36
        d.ellipse((ex - hr * 0.11, hy - hr * 0.25, ex + hr * 0.11, hy + hr * 0.05), fill=(38, 42, 48, 255))
        d.ellipse((ex - hr * 0.04, hy - hr * 0.22, ex + hr * 0.03, hy - hr * 0.14), fill=(255, 255, 255, 255))
    d.ellipse((cx - hr * 0.16, hy + hr * 0.12, cx + hr * 0.16, hy + hr * 0.38), fill=(45, 38, 38, 255))
    d.arc((cx - hr * 0.22, hy + hr * 0.30, cx + hr * 0.22, hy + hr * 0.62), 15, 165,
          fill=(*ink, 255), width=max(2, int(s * 0.008)))


def _house(d, cx, base_y, s, base, rng):
    ink = _ink(base)
    roof = _shade(base, 0.58)
    roof_ink = _shade(roof, 0.6)
    _shadow(d, cx, base_y, s * 0.95)
    d.rectangle((cx - s * 0.30, base_y - s * 0.45, cx + s * 0.30, base_y - s * 0.01),
                fill=(*base, 255), outline=(*ink, 255), width=max(3, int(s * 0.012)))
    d.polygon([(cx - s * 0.39, base_y - s * 0.43), (cx, base_y - s * 0.68), (cx + s * 0.39, base_y - s * 0.43)],
              fill=(*roof, 255), outline=(*roof_ink, 255))
    d.rectangle((cx + s * 0.16, base_y - s * 0.66, cx + s * 0.25, base_y - s * 0.50),
                fill=(*roof, 255), outline=(*roof_ink, 255), width=max(2, int(s * 0.008)))
    d.rounded_rectangle((cx - s * 0.075, base_y - s * 0.22, cx + s * 0.075, base_y - s * 0.01),
                        radius=s * 0.02, fill=(*_shade(base, 0.5), 255), outline=(*ink, 255),
                        width=max(2, int(s * 0.008)))
    _dot(d, cx + s * 0.045, base_y - s * 0.11, s * 0.012, (235, 200, 120))
    for wx in (cx - s * 0.21, cx + s * 0.21):
        wy = base_y - s * 0.34
        d.rectangle((wx - s * 0.075, wy - s * 0.065, wx + s * 0.075, wy + s * 0.065),
                    fill=(255, 236, 170, 255), outline=(*ink, 255), width=max(2, int(s * 0.007)))
        d.line((wx, wy - s * 0.065, wx, wy + s * 0.065), fill=(*ink, 255), width=max(1, int(s * 0.006)))
        d.line((wx - s * 0.075, wy, wx + s * 0.075, wy), fill=(*ink, 255), width=max(1, int(s * 0.006)))
    for i in range(4):
        x = cx - s * 0.24 + i * s * 0.14
        d.ellipse((x, base_y - s * 0.02, x + s * 0.06, base_y + s * 0.04),
                  fill=(*_shade(base, 0.55), 255))


def _flower(d, cx, base_y, s, base, rng):
    ink = _ink(base)
    stem = (70, 140, 80)
    _shadow(d, cx, base_y, s * 0.5)
    d.line((cx, base_y - s * 0.06, cx, base_y - s * 0.52), fill=(*stem, 255),
           width=max(4, int(s * 0.028)))
    for sign in (-1, 1):
        d.polygon([(cx, base_y - s * 0.28), (cx + sign * s * 0.18, base_y - s * 0.36),
                   (cx + sign * s * 0.04, base_y - s * 0.40)], fill=(*stem, 255))
    cy = base_y - s * 0.60
    for i in range(8):
        angle = i * math.tau / 8 + rng.uniform(-0.05, 0.05)
        px = cx + math.cos(angle) * s * 0.12
        py = cy + math.sin(angle) * s * 0.12
        _dot(d, px, py, s * 0.095, base, 255, ink, max(2, int(s * 0.006)))
    _dot(d, cx, cy, s * 0.085, (240, 190, 80), 255, _shade((240, 190, 80), 0.6), max(2, int(s * 0.006)))
    for i in range(6):
        angle = i * math.tau / 6
        _dot(d, cx + math.cos(angle) * s * 0.035, cy + math.sin(angle) * s * 0.035,
             s * 0.012, (120, 80, 30))


def _boat(d, cx, base_y, s, base, rng):
    ink = _ink(base)
    sail_color = (248, 246, 236)
    _shadow(d, cx, base_y, s * 0.9, 45)
    for i in range(3):
        y = base_y + s * (0.005 + i * 0.028)
        width = s * (0.46 - i * 0.08)
        d.arc((cx - width, y - s * 0.03, cx + width, y + s * 0.05), 200, 340,
              fill=(90, 150, 200, 190), width=max(2, int(s * 0.009)))
    d.line((cx, base_y - s * 0.08, cx, base_y - s * 0.62), fill=(110, 80, 60, 255),
           width=max(3, int(s * 0.018)))
    d.polygon([(cx + s * 0.012, base_y - s * 0.60), (cx + s * 0.012, base_y - s * 0.12),
               (cx + s * 0.31, base_y - s * 0.12)], fill=(*sail_color, 255),
              outline=(*_shade(sail_color, 0.65), 255))
    d.polygon([(cx - s * 0.012, base_y - s * 0.52), (cx - s * 0.012, base_y - s * 0.16),
               (cx - s * 0.24, base_y - s * 0.16)], fill=(*_shade(sail_color, 0.94), 255),
              outline=(*_shade(sail_color, 0.65), 255))
    d.polygon([(cx - s * 0.40, base_y - s * 0.12), (cx + s * 0.40, base_y - s * 0.12),
               (cx + s * 0.29, base_y + s * 0.025), (cx - s * 0.29, base_y + s * 0.025)],
              fill=(*base, 255), outline=(*ink, 255))
    d.polygon([(cx, base_y - s * 0.62), (cx + s * 0.08, base_y - s * 0.57),
               (cx, base_y - s * 0.53)], fill=(220, 80, 80, 255))


def _rocket(d, cx, base_y, s, base, rng):
    ink = _ink(base)
    accent = (225, 80, 70)
    _shadow(d, cx, base_y, s * 0.6)
    d.polygon([(cx - s * 0.14, base_y - s * 0.54), (cx, base_y - s * 0.84),
               (cx + s * 0.14, base_y - s * 0.54)], fill=(*accent, 255), outline=(*_ink(accent), 255))
    d.rounded_rectangle((cx - s * 0.14, base_y - s * 0.56, cx + s * 0.14, base_y - s * 0.13),
                        radius=s * 0.10, fill=(*base, 255), outline=(*ink, 255),
                        width=max(3, int(s * 0.010)))
    for sign in (-1, 1):
        d.polygon([(cx + sign * s * 0.13, base_y - s * 0.30), (cx + sign * s * 0.28, base_y - s * 0.06),
                   (cx + sign * s * 0.13, base_y - s * 0.12)], fill=(*accent, 255),
                  outline=(*_ink(accent), 255))
    _dot(d, cx, base_y - s * 0.40, s * 0.068, (150, 205, 235), 255, (*_shade(base, 0.5), 255),
         max(2, int(s * 0.010)))
    _dot(d, cx, base_y - s * 0.40, s * 0.040, (60, 110, 160))
    for sign in (-1, 1):
        d.polygon([(cx + sign * s * 0.03, base_y - s * 0.12), (cx + sign * s * 0.10, base_y + s * 0.04),
                   (cx, base_y - s * 0.04)], fill=(*accent, 255))
    d.polygon([(cx - s * 0.05, base_y - s * 0.11), (cx + s * 0.05, base_y - s * 0.11),
               (cx, base_y + s * 0.09)], fill=(250, 190, 70, 255))
    d.polygon([(cx - s * 0.03, base_y - s * 0.10), (cx + s * 0.03, base_y - s * 0.10),
               (cx, base_y + s * 0.04)], fill=(250, 235, 150, 255))


def _robot(d, cx, base_y, s, base, rng):
    ink = _ink(base)
    accent = (80, 110, 150)
    _shadow(d, cx, base_y, s * 0.7)
    d.line((cx, base_y - s * 0.74, cx, base_y - s * 0.86), fill=(*ink, 255),
           width=max(3, int(s * 0.016)))
    _dot(d, cx, base_y - s * 0.88, s * 0.03, (235, 90, 90))
    d.rounded_rectangle((cx - s * 0.17, base_y - s * 0.74, cx + s * 0.17, base_y - s * 0.54),
                        radius=s * 0.05, fill=(*base, 255), outline=(*ink, 255),
                        width=max(3, int(s * 0.010)))
    for sign in (-1, 1):
        _dot(d, cx + sign * s * 0.07, base_y - s * 0.65, s * 0.028, (90, 220, 220), 255,
             (*ink, 255), max(2, int(s * 0.006)))
    d.line((cx - s * 0.05, base_y - s * 0.57, cx + s * 0.05, base_y - s * 0.57),
           fill=(*ink, 255), width=max(2, int(s * 0.008)))
    d.rounded_rectangle((cx - s * 0.22, base_y - s * 0.52, cx + s * 0.22, base_y - s * 0.20),
                        radius=s * 0.05, fill=(*_shade(base, 0.9), 255), outline=(*ink, 255),
                        width=max(3, int(s * 0.010)))
    for sign in (-1, 1):
        d.rounded_rectangle((cx + sign * s * 0.24 - s * 0.045, base_y - s * 0.50,
                             cx + sign * s * 0.24 + s * 0.045, base_y - s * 0.26),
                            radius=s * 0.03, fill=(*accent, 255), outline=(*ink, 255),
                            width=max(2, int(s * 0.007)))
        _dot(d, cx + sign * s * 0.24, base_y - s * 0.24, s * 0.035, accent, 255,
             (*ink, 255), max(2, int(s * 0.006)))
        d.rounded_rectangle((cx + sign * s * 0.10 - s * 0.035, base_y - s * 0.20,
                             cx + sign * s * 0.10 + s * 0.035, base_y - s * 0.03),
                            radius=s * 0.02, fill=(*accent, 255), outline=(*ink, 255),
                            width=max(2, int(s * 0.007)))
        d.ellipse((cx + sign * s * 0.10 - s * 0.06, base_y - s * 0.05,
                   cx + sign * s * 0.10 + s * 0.06, base_y + s * 0.015),
                  fill=(*accent, 255), outline=(*ink, 255), width=max(2, int(s * 0.007)))
    for i in range(3):
        _dot(d, cx - s * 0.10 + i * s * 0.10, base_y - s * 0.36, s * 0.022, (240, 190, 90), 255,
             (*ink, 255), max(1, int(s * 0.005)))


def _fish(d, cx, base_y, s, base, rng):
    ink = _ink(base)
    light = _tint(base, 0.45)
    for i, (bx, by, br) in enumerate(((cx - s * 0.22, base_y - s * 0.52, s * 0.030),
                                      (cx - s * 0.13, base_y - s * 0.60, s * 0.022),
                                      (cx - s * 0.20, base_y - s * 0.68, s * 0.016))):
        d.ellipse((bx - br, by - br, bx + br, by + br), outline=(150, 210, 235, 220),
                  width=max(2, int(s * 0.008)))
    d.polygon([(cx - s * 0.28, base_y - s * 0.30), (cx - s * 0.44, base_y - s * 0.40),
               (cx - s * 0.44, base_y - s * 0.20)], fill=(*_shade(base, 0.8), 255),
              outline=(*ink, 255))
    d.ellipse((cx - s * 0.30, base_y - s * 0.40, cx + s * 0.26, base_y - s * 0.18),
              fill=(*base, 255), outline=(*ink, 255), width=max(3, int(s * 0.010)))
    d.polygon([(cx - s * 0.10, base_y - s * 0.38), (cx + s * 0.02, base_y - s * 0.49),
               (cx + s * 0.10, base_y - s * 0.37)], fill=(*_shade(base, 0.85), 255),
              outline=(*ink, 255))
    d.ellipse((cx - s * 0.10, base_y - s * 0.33, cx + s * 0.08, base_y - s * 0.23),
              fill=(*light, 180))
    _dot(d, cx + s * 0.14, base_y - s * 0.33, s * 0.030, (250, 250, 250), 255,
         (*ink, 255), max(2, int(s * 0.006)))
    _dot(d, cx + s * 0.155, base_y - s * 0.33, s * 0.014, (35, 40, 48))
    d.arc((cx + s * 0.11, base_y - s * 0.28, cx + s * 0.20, base_y - s * 0.22), 20, 160,
          fill=(*ink, 255), width=max(1, int(s * 0.006)))


def _bird(d, cx, base_y, s, base, rng):
    ink = _ink(base)
    light = _tint(base, 0.5)
    _shadow(d, cx, base_y, s * 0.45, 45)
    d.polygon([(cx - s * 0.24, base_y - s * 0.20), (cx - s * 0.42, base_y - s * 0.27),
               (cx - s * 0.40, base_y - s * 0.15)], fill=(*_shade(base, 0.8), 255),
              outline=(*ink, 255))
    d.ellipse((cx - s * 0.25, base_y - s * 0.32, cx + s * 0.18, base_y - s * 0.10),
              fill=(*base, 255), outline=(*ink, 255), width=max(3, int(s * 0.010)))
    d.ellipse((cx - s * 0.12, base_y - s * 0.30, cx + s * 0.10, base_y - s * 0.14),
              fill=(*light, 255))
    _dot(d, cx + s * 0.18, base_y - s * 0.38, s * 0.10, base, 255, ink, max(3, int(s * 0.009)))
    d.polygon([(cx + s * 0.27, base_y - s * 0.38), (cx + s * 0.36, base_y - s * 0.35),
               (cx + s * 0.27, base_y - s * 0.33)], fill=(245, 170, 60, 255))
    _dot(d, cx + s * 0.21, base_y - s * 0.40, s * 0.019, (35, 40, 48))
    _dot(d, cx + s * 0.212, base_y - s * 0.405, s * 0.008, (255, 255, 255))
    for sign in (-1, 1):
        d.line((cx + sign * s * 0.06, base_y - s * 0.10, cx + sign * s * 0.06, base_y - s * 0.01),
               fill=(*ink, 255), width=max(2, int(s * 0.008)))


def _cake(d, cx, base_y, s, base, rng):
    ink = _ink(base)
    frosting = (250, 240, 248)
    _shadow(d, cx, base_y, s * 0.9)
    d.ellipse((cx - s * 0.42, base_y - s * 0.03, cx + s * 0.42, base_y + s * 0.045),
              fill=(235, 235, 240, 255), outline=(*ink, 255))
    d.rounded_rectangle((cx - s * 0.30, base_y - s * 0.24, cx + s * 0.30, base_y - s * 0.01),
                        radius=s * 0.03, fill=(*base, 255), outline=(*ink, 255),
                        width=max(3, int(s * 0.010)))
    d.rounded_rectangle((cx - s * 0.24, base_y - s * 0.44, cx + s * 0.24, base_y - s * 0.23),
                        radius=s * 0.03, fill=(*_tint(base, 0.18), 255), outline=(*ink, 255),
                        width=max(3, int(s * 0.010)))
    for i in range(7):
        px = cx - s * 0.21 + i * s * 0.07
        _dot(d, px, base_y - s * 0.235, s * 0.035, frosting)
    for i in range(7):
        px = cx - s * 0.27 + i * s * 0.09
        _dot(d, px, base_y - s * 0.015, s * 0.034, frosting)
    d.rectangle((cx - s * 0.015, base_y - s * 0.56, cx + s * 0.015, base_y - s * 0.43),
                fill=(120, 180, 220, 255), outline=(*_ink((120, 180, 220)), 255))
    d.ellipse((cx - s * 0.022, base_y - s * 0.60, cx + s * 0.022, base_y - s * 0.54),
              fill=(250, 200, 70, 255), outline=(240, 150, 50, 255))
    for i in range(8):
        px = cx - s * 0.24 + rng.uniform(0, 0.48) * s
        py = base_y - s * rng.uniform(0.10, 0.22)
        d.line((px, py, px + s * 0.025, py - s * 0.012),
               fill=(rng.randint(120, 250), rng.randint(80, 220), rng.randint(90, 240), 255),
               width=max(2, int(s * 0.008)))


def _cup(d, cx, base_y, s, base, rng):
    ink = _ink(base)
    coffee = (110, 72, 45)
    _shadow(d, cx, base_y, s * 0.75, 50)
    d.ellipse((cx - s * 0.36, base_y - s * 0.035, cx + s * 0.36, base_y + s * 0.035),
              fill=(235, 235, 240, 255), outline=(*ink, 255))
    d.arc((cx + s * 0.10, base_y - s * 0.36, cx + s * 0.40, base_y - s * 0.14), 280, 100,
          fill=(*ink, 255), width=max(5, int(s * 0.025)))
    d.polygon([(cx - s * 0.25, base_y - s * 0.44), (cx + s * 0.25, base_y - s * 0.44),
               (cx + s * 0.19, base_y - s * 0.04), (cx - s * 0.19, base_y - s * 0.04)],
              fill=(*base, 255), outline=(*ink, 255))
    d.ellipse((cx - s * 0.25, base_y - s * 0.48, cx + s * 0.25, base_y - s * 0.40),
              fill=(*coffee, 255), outline=(*ink, 255), width=max(2, int(s * 0.008)))
    d.ellipse((cx - s * 0.17, base_y - s * 0.465, cx + s * 0.10, base_y - s * 0.43),
              fill=(*_tint(coffee, 0.30), 200))
    for i in range(3):
        x = cx - s * 0.12 + i * s * 0.12
        d.arc((x - s * 0.055, base_y - s * 0.72, x + s * 0.055, base_y - s * 0.56), 110, 250,
              fill=(255, 255, 255, 130), width=max(3, int(s * 0.012)))


def _balloon(d, cx, base_y, s, base, rng):
    ink = _ink(base)
    light = _tint(base, 0.55)
    d.arc((cx - s * 0.10, base_y - s * 0.18, cx + s * 0.16, base_y + s * 0.04), 20, 160,
          fill=(*ink, 255), width=max(2, int(s * 0.006)))
    d.polygon([(cx - s * 0.035, base_y - s * 0.18), (cx + s * 0.035, base_y - s * 0.18),
               (cx, base_y - s * 0.25)], fill=(*_shade(base, 0.8), 255), outline=(*ink, 255))
    d.ellipse((cx - s * 0.27, base_y - s * 0.76, cx + s * 0.27, base_y - s * 0.22),
              fill=(*base, 255), outline=(*ink, 255), width=max(3, int(s * 0.010)))
    d.ellipse((cx - s * 0.17, base_y - s * 0.68, cx - s * 0.02, base_y - s * 0.50),
              fill=(*light, 200))
    d.arc((cx - s * 0.27, base_y - s * 0.76, cx + s * 0.27, base_y - s * 0.22), 30, 120,
          fill=(*_tint(base, 0.7), 140), width=max(2, int(s * 0.010)))


def _heart(d, cx, base_y, s, base, rng):
    ink = _ink(base)
    _shadow(d, cx, base_y, s * 0.55)
    cy = base_y - s * 0.34
    points = []
    for i in range(72):
        t = i / 72 * math.tau
        x = 16 * math.sin(t) ** 3
        y = 13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t)
        points.append((cx + x * s / 34, cy - y * s / 34))
    d.polygon(points, fill=(*base, 255), outline=(*ink, 255))
    d.ellipse((cx - s * 0.15, cy - s * 0.14, cx - s * 0.02, cy - s * 0.02),
              fill=(*_tint(base, 0.6), 170))


def _star(d, cx, base_y, s, base, rng):
    ink = _shade(base, 0.55)
    cy = base_y - s * 0.40
    _glow(d._image, cx, cy, s * 0.46, base, 95)
    points = []
    for i in range(10):
        angle = -math.pi / 2 + i * math.pi / 5
        radius = s * 0.31 if i % 2 == 0 else s * 0.135
        points.append((cx + math.cos(angle) * radius, cy + math.sin(angle) * radius))
    d.polygon(points, fill=(*base, 255), outline=(*ink, 255))
    _dot(d, cx - s * 0.03, cy - s * 0.03, s * 0.035, _tint(base, 0.6), 150)


def _moon(d, cx, base_y, s, base, rng):
    cy = base_y - s * 0.46
    _glow(d._image, cx, cy, s * 0.50, _tint(base, 0.2), 80)
    mask = Image.new("L", d._image.size, 0)
    md = ImageDraw.Draw(mask)
    r = s * 0.29
    md.ellipse((cx - r, cy - r, cx + r, cy + r), fill=255)
    md.ellipse((cx - r * 0.35, cy - r * 1.18, cx + r * 1.75, cy + r * 0.92), fill=0)
    d._image.paste((*base, 255), (0, 0), mask)


def _sun(d, cx, base_y, s, base, rng):
    cy = base_y - s * 0.42
    _glow(d._image, cx, cy, s * 0.56, base, 100)
    for i in range(12):
        angle = i * math.tau / 12
        inner = s * 0.23
        outer = s * 0.33
        d.line((cx + math.cos(angle) * inner, cy + math.sin(angle) * inner,
                cx + math.cos(angle) * outer, cy + math.sin(angle) * outer),
               fill=(*_tint(base, 0.15), 220), width=max(3, int(s * 0.022)))
    _dot(d, cx, cy, s * 0.20, base)
    d.ellipse((cx - s * 0.12, cy - s * 0.14, cx + s * 0.02, cy - s * 0.02),
              fill=(*_tint(base, 0.55), 140))


def _tree(d, cx, base_y, s, base, rng):
    trunk = (120, 85, 60)
    ink = _ink(base)
    _shadow(d, cx, base_y, s * 0.5)
    d.rounded_rectangle((cx - s * 0.045, base_y - s * 0.36, cx + s * 0.045, base_y - s * 0.02),
                        radius=s * 0.015, fill=(*trunk, 255), outline=(*_shade(trunk, 0.6), 255))
    for px, py, pr in ((cx - s * 0.10, base_y - s * 0.38, s * 0.16),
                       (cx + s * 0.10, base_y - s * 0.40, s * 0.15),
                       (cx, base_y - s * 0.52, s * 0.18),
                       (cx - s * 0.02, base_y - s * 0.32, s * 0.15)):
        _dot(d, px, py, pr, base, 255, ink, max(2, int(s * 0.007)))


def _octopus(d, cx, base_y, s, base, rng):
    ink = _ink(base)
    light = _tint(base, 0.55)
    _shadow(d, cx, base_y, s * 0.7)
    for i, sign in enumerate((-1, -1, 1, 1, -1, 1)):
        spread = (0.06 + 0.05 * (i % 3)) * s
        box = (cx + sign * s * 0.05 - s * 0.16, base_y - s * 0.38,
               cx + sign * s * 0.05 + s * 0.16, base_y + s * 0.02 + spread * 0.4)
        d.arc(box, 20 if sign > 0 else 160, 160 if sign > 0 else 300,
              fill=(*base, 255), width=max(5, int(s * 0.032)))
    d.ellipse((cx - s * 0.27, base_y - s * 0.66, cx + s * 0.27, base_y - s * 0.16),
              fill=(*base, 255), outline=(*ink, 255), width=max(3, int(s * 0.012)))
    d.ellipse((cx - s * 0.16, base_y - s * 0.60, cx + s * 0.02, base_y - s * 0.42),
              fill=(*light, 140))
    for sign in (-1, 1):
        _dot(d, cx + sign * s * 0.10, base_y - s * 0.42, s * 0.052, (255, 255, 255))
        _dot(d, cx + sign * s * 0.105, base_y - s * 0.415, s * 0.026, (35, 40, 48))
        _dot(d, cx + sign * s * 0.095, base_y - s * 0.43, s * 0.009, (255, 255, 255))
    d.arc((cx - s * 0.08, base_y - s * 0.36, cx + s * 0.08, base_y - s * 0.26), 20, 160,
          fill=(*ink, 255), width=max(2, int(s * 0.008)))


_DRAWERS = {
    "car": _car,
    "cat": _cat,
    "dog": _dog,
    "house": _house,
    "flower": _flower,
    "boat": _boat,
    "rocket": _rocket,
    "robot": _robot,
    "fish": _fish,
    "bird": _bird,
    "cake": _cake,
    "cup": _cup,
    "balloon": _balloon,
    "heart": _heart,
    "star": _star,
    "moon": _moon,
    "sun": _sun,
    "tree": _tree,
    "octopus": _octopus,
}


class _LayerDraw:
    """ImageDraw wrapper that also exposes the layer for mask operations."""

    def __init__(self, layer: Image.Image):
        self._image = layer
        self._draw = ImageDraw.Draw(layer)

    def __getattr__(self, name):
        return getattr(self._draw, name)


def _draw_subject(layer: Image.Image, subject: str, cx: float, base_y: float, size: float,
                  base: tuple[int, int, int], rng: random.Random) -> None:
    drawer = _DRAWERS.get(subject)
    if not drawer:
        return
    drawer(_LayerDraw(layer), cx, base_y, size, base, rng)


# --------------------------------------------------------------------------- #
# Compositions
# --------------------------------------------------------------------------- #
def _stars(w: int, h: int, rng: random.Random, count: int, alpha: int = 200) -> Image.Image:
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    for _ in range(count):
        x = rng.uniform(0, w)
        y = rng.uniform(0, h * 0.65)
        r = rng.uniform(0.8, 2.4) * (w / 1400)
        d.ellipse((x - r, y - r, x + r, y + r), fill=(255, 255, 255, alpha))
    return layer


def studio(w: int, h: int, rng: random.Random, subject: str, base: tuple[int, int, int],
           palette: dict, palette_key: str) -> Image.Image:
    night = subject in ("moon", "star") or palette_key in ("night", "space")
    if night:
        top, bottom = (13, 19, 42), (38, 50, 86)
        glow_color, glow_alpha = (120, 140, 255), 80
    else:
        top = _mix(base, (255, 255, 255), 0.84)
        bottom = _mix(base, (255, 255, 255), 0.97)
        glow_color, glow_alpha = (255, 255, 255), 120
    im = _gradient(w, h, top, bottom).convert("RGBA")

    light_layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    _glow(light_layer, w * 0.5, h * 0.48, w * 0.44, glow_color, glow_alpha)
    im.alpha_composite(light_layer)

    if night:
        im.alpha_composite(_stars(w, h, rng, 220))

    stage = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    sd = ImageDraw.Draw(stage)
    sd.ellipse((w * 0.14, h * 0.76, w * 0.86, h * 0.85),
               fill=(255, 255, 255, 60) if not night else (100, 120, 200, 45))
    im.alpha_composite(stage.filter(ImageFilter.GaussianBlur(max(3, w // 80))))

    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    _draw_subject(layer, subject, w * 0.5, h * 0.80, w * 0.58, base, rng)
    im.alpha_composite(layer)

    accents = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ad = ImageDraw.Draw(accents)
    for _ in range(rng.randint(5, 9)):
        ax = rng.uniform(0.08, 0.92) * w
        ay = rng.uniform(0.12, 0.70) * h
        ar = rng.uniform(0.006, 0.018) * w
        ad.ellipse((ax - ar, ay - ar, ax + ar, ay + ar),
                   fill=(*_tint(base, 0.55), rng.randint(70, 140)))
    im.alpha_composite(accents)
    return im


def overlay(im: Image.Image, subject: str, base: tuple[int, int, int], rng: random.Random) -> None:
    w, h = im.size
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    if subject in ("moon", "star", "sun"):
        size, cx, base_y = w * 0.13, w * 0.22, h * 0.32
    elif subject == "bird":
        size, cx, base_y = w * 0.11, w * 0.72, h * 0.44
    elif subject == "boat":
        size, cx, base_y = w * 0.32, w * 0.48, h * 0.72
    elif subject == "fish":
        size, cx, base_y = w * 0.18, w * 0.58, h * 0.76
    else:
        size, cx, base_y = w * 0.34, w * 0.5, h * 0.92
    _draw_subject(layer, subject, cx, base_y, size, base, rng)
    im.alpha_composite(layer)


def abstract(w: int, h: int, rng: random.Random, base: tuple[int, int, int],
             palette: dict) -> Image.Image:
    top = _mix(base, (255, 255, 255), 0.58)
    bottom = _mix(base, (255, 255, 255), 0.93)
    im = _gradient(w, h, top, bottom).convert("RGBA")
    blob = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    _glow(blob, rng.uniform(0.3, 0.7) * w, rng.uniform(0.25, 0.55) * h,
          w * rng.uniform(0.28, 0.5), _tint(base, 0.35), 120)
    im.alpha_composite(blob)

    accent = _shade(base, 0.62)
    light = _tint(base, 0.5)
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    for _ in range(rng.randint(6, 10)):
        kind = rng.choice(("ellipse", "rrect", "triangle", "ring"))
        cx = rng.uniform(0.1, 0.9) * w
        cy = rng.uniform(0.12, 0.88) * h
        size = rng.uniform(0.08, 0.24) * w
        color = rng.choice((base, accent, light, tuple(_hex_to_rgb(palette["accent"]))))
        alpha = rng.randint(38, 92)
        if kind == "ellipse":
            d.ellipse((cx - size, cy - size, cx + size, cy + size), fill=(*color, alpha))
        elif kind == "rrect":
            d.rounded_rectangle((cx - size, cy - size * 0.55, cx + size, cy + size * 0.55),
                                radius=size * 0.25, fill=(*color, alpha))
        elif kind == "triangle":
            d.polygon([(cx, cy - size * 0.8), (cx - size * 0.85, cy + size * 0.65),
                       (cx + size * 0.85, cy + size * 0.65)], fill=(*color, alpha))
        else:
            d.ellipse((cx - size, cy - size, cx + size, cy + size), outline=(*color, min(255, alpha + 60)),
                      width=max(3, int(size * 0.16)))
    im.alpha_composite(layer)
    return im


def _hex_to_rgb(value: str) -> tuple[int, int, int]:
    value = value.lstrip("#")
    return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4))
