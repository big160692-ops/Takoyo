"""Small easing helpers and a cancellable tkinter tween runner."""

from __future__ import annotations

import time
from typing import Callable


def ease_out_cubic(t: float) -> float:
    t = max(0.0, min(1.0, t))
    return 1.0 - (1.0 - t) ** 3


def ease_in_out_cubic(t: float) -> float:
    t = max(0.0, min(1.0, t))
    if t < 0.5:
        return 4.0 * t * t * t
    return 1.0 - (-2.0 * t + 2.0) ** 3 / 2.0


def lerp(start: float, end: float, t: float) -> float:
    return start + (end - start) * t


def lerp_color(first: str, second: str, t: float) -> str:
    t = max(0.0, min(1.0, t))
    a = tuple(int(first[i : i + 2], 16) for i in (1, 3, 5))
    b = tuple(int(second[i : i + 2], 16) for i in (1, 3, 5))
    return "#%02X%02X%02X" % tuple(round(x + (y - x) * t) for x, y in zip(a, b))


class Tween:
    """Runs ``update(t)`` from 0 to 1 with ease-out; ``done`` fires at the end."""

    def __init__(
        self,
        widget,
        duration: int,
        update: Callable[[float], None],
        done: Callable[[], None] | None = None,
        interval: int = 16,
        ease: Callable[[float], float] = ease_out_cubic,
    ):
        self.widget = widget
        self.duration = max(1, int(duration))
        self.update = update
        self.done = done
        self.interval = max(1, int(interval))
        self.ease = ease
        self._job = None
        self._cancelled = False
        self._started = time.perf_counter()
        self._step()

    def _step(self):
        if self._cancelled:
            return
        try:
            if not self.widget.winfo_exists():
                self._job = None
                return
            elapsed = (time.perf_counter() - self._started) * 1000.0
            t = min(1.0, elapsed / self.duration)
            self.update(self.ease(t))
            if t < 1.0:
                self._job = self.widget.after(self.interval, self._step)
            else:
                self._job = None
                if self.done is not None:
                    self.done()
        except Exception:
            self._job = None

    def cancel(self):
        self._cancelled = True
        if self._job is not None:
            try:
                self.widget.after_cancel(self._job)
            except Exception:
                pass
            self._job = None
