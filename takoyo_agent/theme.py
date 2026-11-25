"""Design tokens for Takoyo.

The interface follows the Takoyo concept sheets: pure white ground, ink-black
line work, soft grey controls, Poppins type and an octopus mascot.
"""

from __future__ import annotations

import ctypes
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
FONT_DIR = ASSETS / "fonts"

# --------------------------------------------------------------------------- #
# Palette
# --------------------------------------------------------------------------- #
WHITE = "#FFFFFF"
INK = "#0B0B0D"
INK_SOFT = "#3A3B3E"
MUTED = "#8B8B8F"
FAINT = "#B9B9BD"
LINE = "#111113"
LINE_SOFT = "#E7E7E9"
HOVER = "#F4F4F5"
ACTIVE = "#EDEDEF"
SEARCH_BG = "#DEDEDF"
CHIP = "#F2F2F3"
DANGER = "#D9433A"
SUCCESS = "#0F9D58"
BLUE = "#1C55A5"
CODE_RED = "#E2453C"
CODE_GREEN = "#12A15A"
CODE_BLUE = "#1B4FA0"
CODE_MUTED = "#8B8B8F"
ART_SKY = "#BFE7FB"
ART_HILL = "#8FA31E"

# Radii
R_CARD = 18
R_INPUT = 16
R_BUBBLE = 15
R_CHIP = 10

# --------------------------------------------------------------------------- #
# Fonts
# --------------------------------------------------------------------------- #
FONTS_LOADED = False
FONT_FAMILY = "Century Gothic"
FONT_MEDIUM = "Century Gothic"
FONT_SEMIBOLD = "Century Gothic"
FONT_BOLD = "Century Gothic"
MONO_FAMILY = "Consolas"

_FONT_FILES = {
    "Poppins-Regular.ttf": "Poppins",
    "Poppins-Medium.ttf": "Poppins Medium",
    "Poppins-SemiBold.ttf": "Poppins SemiBold",
    "Poppins-Bold.ttf": "Poppins SemiBold",
    "Poppins-ExtraBold.ttf": "Poppins ExtraBold",
}


def enable_dpi_awareness() -> None:
    """Render at true pixel sizes on scaled Windows displays."""
    if sys.platform != "win32":
        return
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(1)
        return
    except Exception:
        pass
    try:
        ctypes.windll.user32.SetProcessDPIAware()
    except Exception:
        pass


def load_fonts() -> bool:
    """Register the bundled Poppins family for this process only."""
    global FONTS_LOADED, FONT_FAMILY, FONT_MEDIUM, FONT_SEMIBOLD, FONT_BOLD, MONO_FAMILY
    if FONTS_LOADED:
        return True
    if not FONT_DIR.exists():
        return False

    loaded = False
    for filename in _FONT_FILES:
        path = FONT_DIR / filename
        if not path.exists():
            continue
        try:
            if sys.platform == "win32":
                if ctypes.windll.gdi32.AddFontResourceExW(str(path.resolve()), 0x10, 0):
                    loaded = True
            else:
                loaded = True  # Tk falls back gracefully elsewhere
        except Exception:
            pass

    if loaded:
        FONTS_LOADED = True
        FONT_FAMILY = "Poppins"
        FONT_MEDIUM = "Poppins Medium"
        FONT_SEMIBOLD = "Poppins SemiBold"
        FONT_BOLD = "Poppins ExtraBold"

    MONO_FAMILY = "Consolas"
    return FONTS_LOADED


def _family_for(weight: str) -> str:
    weight = (weight or "regular").lower()
    if weight in ("semibold", "semi", "600"):
        return FONT_SEMIBOLD
    if weight in ("medium", "500"):
        return FONT_MEDIUM
    if weight in ("bold", "extrabold", "700", "800", "black"):
        return FONT_BOLD
    return FONT_FAMILY


def font(size: int, weight: str = "regular") -> tuple:
    """Return a Tk font tuple using pixel sizes for pixel-accurate layout."""
    return (_family_for(weight), -int(size))


def mono(size: int, weight: str = "regular") -> tuple:
    return (MONO_FAMILY, -int(size), "bold" if weight == "bold" else "normal")


def pil_font_path(weight: str = "regular") -> Path:
    name = {
        "regular": "Poppins-Regular.ttf",
        "medium": "Poppins-Medium.ttf",
        "semibold": "Poppins-SemiBold.ttf",
        "bold": "Poppins-ExtraBold.ttf",
        "extrabold": "Poppins-ExtraBold.ttf",
    }.get((weight or "regular").lower(), "Poppins-Regular.ttf")
    path = FONT_DIR / name
    return path if path.exists() else FONT_DIR / "Poppins-Regular.ttf"


def measure(text: str, size: int = 14, weight: str = "regular") -> int:
    """Approximate text width in pixels without needing a Tk root."""
    try:
        from PIL import ImageFont

        f = ImageFont.truetype(str(pil_font_path(weight)), int(size))
        return int(f.getlength(text))
    except Exception:
        return int(len(text) * size * 0.6)
