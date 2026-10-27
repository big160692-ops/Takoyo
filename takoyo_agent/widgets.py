"""Hand-built tkinter widgets that match the Takoyo sheets (rounded cards, ink lines)."""

from __future__ import annotations

import re
import tkinter as tk
import webbrowser
from typing import Callable

from . import theme, visuals


def _font(size: int, weight: str = "regular") -> tuple:
    return theme.font(size, weight)


# --------------------------------------------------------------------------- #
# Surfaces
# --------------------------------------------------------------------------- #
class RoundedCanvas(tk.Canvas):
    """A canvas with a crisp rounded surface and an embedded body frame."""

    def __init__(
        self,
        master,
        radius: int = theme.R_CARD,
        fill: str = theme.WHITE,
        outline: str | None = theme.LINE,
        thickness: float = 0,
        bg: str = theme.WHITE,
        padding: int = 6,
        **kwargs,
    ):
        super().__init__(master, bg=bg, highlightthickness=0, bd=0, **kwargs)
        self.radius = radius
        self.fill = fill
        self.outline = outline
        self.thickness = thickness
        self.padding = padding
        self.body = tk.Frame(self, bg=fill)
        self._bg_item = None
        self._win = None
        self._img = None
        self._size = (0, 0)
        self.bind("<Configure>", self._on_configure)

    def _on_configure(self, event):
        size = (event.width, event.height)
        if size == self._size or size[0] < 4 or size[1] < 4:
            return
        self._size = size
        self._render(*size)

    def _render(self, w, h):
        self._img = visuals.rounded_photo(w, h, self.radius, self.fill, self.outline, self.thickness, bg=None)
        if self._bg_item is None:
            self._bg_item = self.create_image(0, 0, anchor="nw", image=self._img)
            self.tag_lower(self._bg_item)
        else:
            self.itemconfigure(self._bg_item, image=self._img)
        pad = self.padding
        if self._win is None:
            self._win = self.create_window(pad, pad, anchor="nw", window=self.body)
        self.coords(self._win, pad, pad)
        self.itemconfigure(self._win, width=max(1, w - pad * 2), height=max(1, h - pad * 2))

    def set_surface(self, fill: str | None = None, outline: str | None = None, thickness: float | None = None):
        if fill is not None:
            self.fill = fill
            self.body.configure(bg=fill)
        if outline is not None:
            self.outline = outline
        if thickness is not None:
            self.thickness = thickness
        self._size = (0, 0)
        if self.winfo_width() > 4:
            self._render(self.winfo_width(), self.winfo_height())


class Card(tk.Frame):
    """Auto-sized rounded card. Content goes inside `.body`."""

    def __init__(
        self,
        master,
        radius: int = theme.R_CARD,
        fill: str = theme.WHITE,
        outline: str | None = theme.LINE,
        thickness: float = 0,
        bg: str = theme.WHITE,
        padding: int = 16,
        **kwargs,
    ):
        super().__init__(master, bg=bg, **kwargs)
        self.radius = radius
        self.fill = fill
        self.outline = outline
        self.thickness = thickness
        self.pad = padding
        self._bg_label = tk.Label(self, bd=0, bg=bg, highlightthickness=0)
        self._bg_label.place(x=0, y=0, relwidth=1, relheight=1)
        self.body = tk.Frame(self, bg=fill)
        self.body.pack(fill="both", expand=True, padx=padding, pady=padding)
        self._img = None
        self._size = (0, 0)
        self._pending = None
        self.bind("<Configure>", self._schedule, add="+")

    def _schedule(self, _event=None):
        if self._pending:
            try:
                self.after_cancel(self._pending)
            except Exception:
                pass
        self._pending = self.after(10, self._redraw)

    def _redraw(self):
        self._pending = None
        w, h = self.winfo_width(), self.winfo_height()
        if (w, h) == self._size or w < 4 or h < 4:
            return
        self._size = (w, h)
        self._img = visuals.rounded_photo(w, h, self.radius, self.fill, self.outline, self.thickness, bg=None)
        self._bg_label.configure(image=self._img)
        self._bg_label.lower()


class ScrollArea(tk.Frame):
    def __init__(self, master, bg: str = theme.WHITE, **kwargs):
        super().__init__(master, bg=bg, **kwargs)
        self.canvas = tk.Canvas(self, bg=bg, highlightthickness=0, bd=0)
        self.canvas.pack(fill="both", expand=True)
        self.inner = tk.Frame(self.canvas, bg=bg)
        self._win = self.canvas.create_window((0, 0), window=self.inner, anchor="nw")
        self.inner.bind("<Configure>", self._on_inner)
        self.canvas.bind("<Configure>", self._on_canvas)
        self.canvas.bind("<Enter>", self._bind_wheel)
        self.canvas.bind("<Leave>", self._unbind_wheel)
        self.inner.bind("<Enter>", self._bind_wheel)
        self._wheel_bound = False

    def _on_inner(self, _event=None):
        self.canvas.configure(scrollregion=self.canvas.bbox("all"))

    def _on_canvas(self, event):
        self.canvas.itemconfigure(self._win, width=event.width)

    def _bind_wheel(self, _event=None):
        if not self._wheel_bound:
            self.canvas.bind_all("<MouseWheel>", self._on_wheel)
            self.canvas.bind_all("<Button-4>", self._on_wheel)
            self.canvas.bind_all("<Button-5>", self._on_wheel)
            self._wheel_bound = True

    def _unbind_wheel(self, _event=None):
        if self._wheel_bound:
            try:
                self.canvas.unbind_all("<MouseWheel>")
                self.canvas.unbind_all("<Button-4>")
                self.canvas.unbind_all("<Button-5>")
            except Exception:
                pass
            self._wheel_bound = False

    def _on_wheel(self, event):
        if not self.winfo_exists():
            return
        if getattr(event, "num", None) == 4:
            step = -3
        elif getattr(event, "num", None) == 5:
            step = 3
        else:
            step = -int(event.delta / 120) * 3 if event.delta else 0
        if step:
            self.canvas.yview_scroll(step, "units")

    def scroll_to_top(self):
        self.canvas.yview_moveto(0)

    def scroll_to_bottom(self):
        self.update_idletasks()
        self.canvas.yview_moveto(1.0)

    def destroy(self):
        self._unbind_wheel()
        super().destroy()


# --------------------------------------------------------------------------- #
# Buttons
# --------------------------------------------------------------------------- #
class IconButton(tk.Canvas):
    def __init__(
        self,
        master,
        icon: str,
        size: int = 19,
        command: Callable | None = None,
        color: str = theme.MUTED,
        hover_color: str = theme.INK,
        bg: str = theme.WHITE,
        hover_bg: str | None = theme.HOVER,
        pad: int = 6,
        outline: bool = False,
        tooltip: str = "",
        width: int | None = None,
        height: int | None = None,
    ):
        self._cw = width or size + pad * 2
        self._ch = height or size + pad * 2
        super().__init__(master, width=self._cw, height=self._ch, bg=bg, highlightthickness=0, bd=0, cursor="hand2")
        self._icon = icon
        self._size = size
        self._command = command
        self._color = color
        self._hover_color = hover_color
        self._bg = bg
        self._hover_bg = hover_bg
        self._outline = outline
        self._enabled = True
        self._hover_item = None
        self._image = visuals.icon_photo(icon, size, color)
        self._icon_item = self.create_image(self._cw // 2, self._ch // 2, image=self._image)
        if outline:
            self.create_oval(2, 2, self._cw - 2, self._ch - 2, outline=color, width=1.6)
        self.bind("<Enter>", self._on_enter)
        self.bind("<Leave>", self._on_leave)
        self.bind("<Button-1>", self._on_click)
        if tooltip:
            Tooltip(self, tooltip)

    def _on_enter(self, _event=None):
        if not self._enabled:
            return
        if self._hover_bg and not self._hover_item:
            self._hover_item = self.create_oval(1, 1, self._cw - 1, self._ch - 1, fill=self._hover_bg, outline="")
            self.tag_lower(self._hover_item)
        self._image = visuals.icon_photo(self._icon, self._size, self._hover_color)
        self.itemconfigure(self._icon_item, image=self._image)

    def _on_leave(self, _event=None):
        if self._hover_item:
            self.delete(self._hover_item)
            self._hover_item = None
        self._image = visuals.icon_photo(self._icon, self._size, self._color)
        self.itemconfigure(self._icon_item, image=self._image)

    def _on_click(self, _event=None):
        if self._enabled and self._command:
            self._command()

    def set_enabled(self, enabled: bool):
        self._enabled = enabled
        self.configure(cursor="hand2" if enabled else "arrow")
        self._color = theme.MUTED if enabled else theme.FAINT
        self._image = visuals.icon_photo(self._icon, self._size, self._color)
        self.itemconfigure(self._icon_item, image=self._image)

    def set_icon(self, icon: str, color: str | None = None):
        self._icon = icon
        if color:
            self._color = color
        self._image = visuals.icon_photo(icon, self._size, self._color)
        self.itemconfigure(self._icon_item, image=self._image)


class TextButton(tk.Canvas):
    def __init__(
        self,
        master,
        text: str,
        command: Callable | None = None,
        variant: str = "primary",
        size: int = 12,
        width: int | None = None,
        height: int = 38,
        bg: str = theme.WHITE,
        icon: str | None = None,
    ):
        self.variant = variant
        self.text_value = text
        self._font = _font(size, "semibold")
        text_w = theme.measure(text, size, "semibold")
        self._cw = width or (text_w + (66 if icon else 44))
        self._ch = height
        super().__init__(master, width=self._cw, height=self._ch, bg=bg, highlightthickness=0, bd=0, cursor="hand2")
        self._command = command
        self._bg = bg
        self._img = None
        self._icon_name = icon
        self._icon_photo = visuals.icon_photo(icon, 15, self._fg()) if icon else None
        self._enabled = True
        self._render()
        if icon:
            self._icon_item = self.create_image(22, self._ch // 2, image=self._icon_photo, anchor="center")
            self._text_item = self.create_text((self._cw + 16) // 2, self._ch // 2, text=text, font=self._font, fill=self._fg())
        else:
            self._text_item = self.create_text(self._cw // 2, self._ch // 2, text=text, font=self._font, fill=self._fg())
        self.bind("<Enter>", self._on_enter)
        self.bind("<Leave>", self._on_leave)
        self.bind("<Button-1>", self._on_click)

    def _fg(self):
        if self.variant == "primary":
            return theme.WHITE
        if self.variant == "danger":
            return theme.DANGER
        return theme.INK if self.variant == "secondary" else theme.MUTED

    def _fill(self, hover=False):
        if self.variant == "primary":
            return "#2A2A2D" if hover else theme.INK
        if self.variant == "danger":
            return "#F8E9E8" if hover else theme.WHITE
        return theme.HOVER if hover else theme.WHITE

    def _render(self, hover=False):
        outline = theme.DANGER if self.variant == "danger" else (theme.LINE if self.variant == "secondary" else None)
        self._img = visuals.rounded_photo(self._cw, self._ch, 12, self._fill(hover), outline, 1.5 if outline else 0, bg=None)
        if self._img is not None:
            if getattr(self, "_bg_item", None) is None:
                self._bg_item = self.create_image(0, 0, anchor="nw", image=self._img)
                self.tag_lower(self._bg_item)
            else:
                self.itemconfigure(self._bg_item, image=self._img)

    def _on_enter(self, _event=None):
        if self._enabled:
            self._render(hover=True)

    def _on_leave(self, _event=None):
        self._render(hover=False)

    def _on_click(self, _event=None):
        if self._enabled and self._command:
            self._command()

    def set_enabled(self, enabled: bool):
        self._enabled = enabled
        self.configure(cursor="hand2" if enabled else "arrow")
        color = self._fg() if enabled else theme.FAINT
        self.itemconfigure(self._text_item, fill=color)
        if getattr(self, "_icon_item", None) is not None:
            self._icon_photo = visuals.icon_photo(self._icon_name, 15, color)
            self.itemconfigure(self._icon_item, image=self._icon_photo)

    def set_text(self, text: str):
        self.text_value = text
        self.itemconfigure(self._text_item, text=text)


class Switch(tk.Canvas):
    def __init__(self, master, value: bool = False, command: Callable[[bool], None] | None = None, bg: str = theme.WHITE):
        super().__init__(master, width=46, height=26, bg=bg, highlightthickness=0, bd=0, cursor="hand2")
        self._value = bool(value)
        self._command = command
        self._bg = bg
        self._knob_x = 33 if self._value else 13
        self._tracks = {
            True: visuals.rounded_photo(46, 26, 13, theme.INK, None, 0, bg=None),
            False: visuals.rounded_photo(46, 26, 13, theme.SEARCH_BG, None, 0, bg=None),
        }
        self._track_item = self.create_image(0, 0, anchor="nw", image=self._tracks[self._value])
        self._knob = visuals.rounded_photo(20, 20, 10, theme.WHITE, None, 0, bg=None)
        self._knob_item = self.create_image(self._knob_x, 13, image=self._knob, anchor="center")
        self.bind("<Button-1>", self._toggle)

    def _toggle(self, _event=None):
        self._value = not self._value
        self.itemconfigure(self._track_item, image=self._tracks[self._value])
        target = 33 if self._value else 13
        self._animate(self._knob_x, target)
        if self._command:
            self._command(self._value)

    def _animate(self, start, end, step=0):
        steps = 7
        if step > steps:
            return
        x = start + (end - start) * (step / steps)
        self.coords(self._knob_item, x, 13)
        self._knob_x = x
        self.after(12, lambda: self._animate(start, end, step + 1))

    def get(self) -> bool:
        return self._value

    def set(self, value: bool, notify: bool = False):
        value = bool(value)
        if value == self._value:
            return
        self._value = value
        self.itemconfigure(self._track_item, image=self._tracks[value])
        self._knob_x = 33 if value else 13
        self.coords(self._knob_item, self._knob_x, 13)
        if notify and self._command:
            self._command(value)


# --------------------------------------------------------------------------- #
# Inputs
# --------------------------------------------------------------------------- #
class SearchField(tk.Canvas):
    def __init__(self, master, placeholder: str = "Search", command: Callable[[str], None] | None = None, bg: str = theme.WHITE):
        super().__init__(master, width=260, height=44, bg=bg, highlightthickness=0, bd=0)
        self._img = None
        self._bg_item = None
        self._win = None
        self._placeholder = placeholder
        self._command = command
        self._bg = bg
        self._search_icon = visuals.icon_photo("search", 18, theme.INK)
        self.icon_item = self.create_image(22, 22, image=self._search_icon)
        self.var = tk.StringVar()
        self.entry = tk.Entry(
            self,
            textvariable=self.var,
            bd=0,
            relief="flat",
            bg=theme.SEARCH_BG,
            fg=theme.MUTED,
            insertbackground=theme.INK,
            font=_font(14),
            highlightthickness=0,
        )
        self.entry.insert(0, placeholder)
        self._showing_placeholder = True
        self.entry.bind("<FocusIn>", self._focus_in)
        self.entry.bind("<FocusOut>", self._focus_out)
        self.var.trace_add("write", self._changed)
        self.bind("<Configure>", self._on_configure)
        self.bind("<Button-1>", lambda e: self.entry.focus_set())

    def _on_configure(self, event):
        w, h = event.width, event.height
        if w < 4 or h < 4:
            return
        self._img = visuals.rounded_photo(w, h, min(22, h // 2), theme.SEARCH_BG, theme.INK, 1.6, bg=None)
        if self._bg_item is None:
            self._bg_item = self.create_image(0, 0, anchor="nw", image=self._img)
            self.tag_lower(self._bg_item)
            self.tag_lower(self.icon_item)
        else:
            self.itemconfigure(self._bg_item, image=self._img)
        if self._win is None:
            self._win = self.create_window(44, h // 2, anchor="w", window=self.entry, height=26)
        else:
            self.coords(self._win, 44, h // 2)
        self.itemconfigure(self._win, width=max(20, w - 58))
        self.coords(self.icon_item, 22, h // 2)

    def _focus_in(self, _event=None):
        if self._showing_placeholder:
            self.entry.delete(0, "end")
            self.entry.configure(fg=theme.INK)
            self._showing_placeholder = False

    def _focus_out(self, _event=None):
        if not self.entry.get().strip():
            self.entry.insert(0, self._placeholder)
            self.entry.configure(fg=theme.MUTED)
            self._showing_placeholder = True

    def _changed(self, *_args):
        if self._showing_placeholder:
            return
        if self._command:
            self._command(self.var.get())

    def get(self) -> str:
        return "" if self._showing_placeholder else self.var.get().strip()

    def clear(self):
        self._showing_placeholder = True
        self.entry.delete(0, "end")
        self.entry.insert(0, self._placeholder)
        self.entry.configure(fg=theme.MUTED)
        if self._command:
            self._command("")


class LabeledField(tk.Frame):
    def __init__(
        self,
        master,
        label: str = "",
        value: str = "",
        placeholder: str = "",
        secret: bool = False,
        height: int = 46,
        bg: str = theme.WHITE,
        hint: str = "",
        readonly: bool = False,
        on_enter: Callable | None = None,
    ):
        super().__init__(master, bg=bg)
        self._bg = bg
        if label:
            tk.Label(self, text=label, bg=bg, fg=theme.INK_SOFT, font=_font(11, "semibold"), anchor="w").pack(fill="x", pady=(0, 6))
        self._field = RoundedCanvas(self, radius=12, fill=theme.WHITE, outline=theme.LINE_SOFT, thickness=1.4, bg=bg, height=height)
        self._field.pack(fill="x")
        self._field.pack_propagate(False)
        self.entry = tk.Entry(
            self._field.body,
            bd=0,
            relief="flat",
            bg=theme.WHITE,
            fg=theme.INK,
            insertbackground=theme.INK,
            font=_font(13),
            highlightthickness=0,
            show="•" if secret else "",
        )
        self.entry.pack(fill="both", expand=True, padx=14, pady=8)
        self._placeholder = placeholder
        if value:
            self.entry.insert(0, value)
        elif placeholder and not readonly:
            self._show_placeholder()
        if readonly:
            # Tk drops programmatic inserts once the entry is readonly.
            self.entry.configure(state="readonly", readonlybackground=theme.WHITE)
        self.entry.bind("<FocusIn>", self._focus_in)
        self.entry.bind("<FocusOut>", self._focus_out)
        if on_enter:
            self.entry.bind("<Return>", lambda e: on_enter())
        if hint:
            tk.Label(self, text=hint, bg=bg, fg=theme.MUTED, font=_font(10), anchor="w", justify="left").pack(fill="x", pady=(5, 0))
        self._showing_placeholder = bool(placeholder and not value and not readonly)

    def _show_placeholder(self):
        self.entry.configure(fg=theme.MUTED)
        self.entry.insert(0, self._placeholder)

    def _focus_in(self, _event=None):
        if self._showing_placeholder:
            self.entry.delete(0, "end")
            self.entry.configure(fg=theme.INK)
            self._showing_placeholder = False

    def _focus_out(self, _event=None):
        if not self.entry.get().strip() and self._placeholder:
            self._showing_placeholder = True
            self._show_placeholder()

    def get(self) -> str:
        if self._showing_placeholder:
            return ""
        return self.entry.get()

    def set(self, value: str):
        state = self.entry.cget("state")
        if state == "readonly":
            self.entry.configure(state="normal")
        self.entry.delete(0, "end")
        self._showing_placeholder = False
        self.entry.configure(fg=theme.INK)
        self.entry.insert(0, value or "")
        if state == "readonly":
            self.entry.configure(state="readonly")
        if not value and self._placeholder:
            self._focus_out()

    def focus(self):
        self.entry.focus_set()


class SelectField(LabeledField):
    def __init__(self, master, label: str = "", value: str = "", options: list[str] | None = None, editable: bool = True, **kwargs):
        super().__init__(master, label=label, value=value, readonly=not editable, **kwargs)
        self._values = list(options or [])
        self._menu = tk.Menu(self, tearoff=0, font=_font(11), bg=theme.WHITE, fg=theme.INK, activebackground=theme.CHIP, activeforeground=theme.INK, bd=0)
        self._arrow = IconButton(
            self._field,
            "chevron-down",
            16,
            command=self._popup,
            color=theme.MUTED,
            hover_color=theme.INK,
            bg=theme.WHITE,
            pad=5,
        )
        self._arrow.place(relx=1.0, rely=0.5, x=-8, anchor="e")
        self.entry.pack_configure(padx=(14, 36))
        self._refresh_menu()

    def _refresh_menu(self):
        self._menu.delete(0, "end")
        for option in self._values:
            self._menu.add_command(label=option, command=lambda opt=option: self.set(opt))
        if not self._values:
            self._menu.add_command(label="(no presets)", state="disabled")

    def set_options(self, options: list[str]):
        self._values = list(options)
        self._refresh_menu()

    def _popup(self):
        try:
            x = self._field.winfo_rootx()
            y = self._field.winfo_rooty() + self._field.winfo_height() + 2
            self._menu.tk_popup(x, y)
        finally:
            self._menu.grab_release()


# --------------------------------------------------------------------------- #
# Rich text
# --------------------------------------------------------------------------- #
_INLINE_RE = re.compile(r"(\*\*(.+?)\*\*|`([^`]+)`|\[([^\]]+)\]\(([^)]+)\))")


class RichText(tk.Text):
    def __init__(
        self,
        master,
        text: str = "",
        size: int = 17,
        fg: str = theme.INK,
        bg: str = theme.WHITE,
        weight: str = "regular",
        max_width: int | None = None,
        padx: int = 0,
        pady: int = 0,
    ):
        super().__init__(
            master,
            wrap="word",
            bd=0,
            highlightthickness=0,
            bg=bg,
            fg=fg,
            font=_font(size, weight),
            cursor="arrow",
            spacing1=1,
            spacing3=5,
            padx=padx,
            pady=pady,
            height=1,
            width=1,
            selectbackground=theme.CHIP,
            selectforeground=theme.INK,
        )
        self._size = size
        self._fg = fg
        self._bg = bg
        self._tags_ready = False
        self._links: dict[str, str] = {}
        self._display_lines = 1
        self._fit_job = None
        self._fitting = False
        self._max_width = max_width
        self._last_width = 0
        self._setup_tags()
        if text:
            self.set_markdown(text)
        else:
            self.configure(state="disabled")
        self.bind("<Configure>", self._on_configure)
        self.bind("<Key>", lambda event: "break")

    def _setup_tags(self):
        self.tag_configure("bold", font=_font(self._size, "semibold"))
        self.tag_configure("italic", font=_font(self._size, "regular"), foreground=theme.INK_SOFT)
        self.tag_configure("code", font=theme.mono(self._size - 2), background=theme.CHIP, foreground=theme.INK)
        self.tag_configure("h1", font=_font(self._size + 5, "semibold"), spacing1=10, spacing3=5)
        self.tag_configure("h2", font=_font(self._size + 2, "semibold"), spacing1=8, spacing3=4)
        self.tag_configure("bullet", lmargin1=15, lmargin2=30)
        self.tag_configure("quote", lmargin1=14, lmargin2=14, foreground=theme.INK_SOFT, font=_font(self._size, "regular"))
        self.tag_configure("small", font=_font(self._size - 3), foreground=theme.MUTED)
        self._tags_ready = True

    def _on_configure(self, event):
        if event.width == self._last_width:
            return
        self._last_width = event.width
        self._schedule_fit()

    def _schedule_fit(self):
        if self._fitting:
            return
        if self._fit_job:
            try:
                self.after_cancel(self._fit_job)
            except Exception:
                pass
        self._fit_job = self.after(16, self._fit_height)

    def _fit_height(self):
        self._fit_job = None
        if not self.winfo_exists() or not self.winfo_ismapped():
            self._fit_job = self.after(60, self._fit_height)
            return
        try:
            lines = self.count("1.0", "end-1c", "displaylines")
            if isinstance(lines, tuple):
                lines = lines[0] if lines else 1
            lines = max(1, int(lines or 1))
        except Exception:
            lines = max(1, self.index("end-1c").split(".")[0] if self.index("end-1c") else 1)
        if lines != self._display_lines:
            self._display_lines = lines
            self._fitting = True
            try:
                self.configure(height=lines)
            finally:
                self._fitting = False

    def _insert_inline(self, text: str, base_tags: tuple = ()):
        pos = 0
        for match in _INLINE_RE.finditer(text):
            if match.start() > pos:
                self.insert("end", text[pos : match.start()], base_tags)
            bold, code, label, url = match.group(2), match.group(3), match.group(4), match.group(5)
            if bold is not None:
                self.insert("end", bold, base_tags + ("bold",))
            elif code is not None:
                self.insert("end", code, base_tags + ("code",))
            elif label is not None and url is not None:
                tag = f"link_{len(self._links)}"
                self._links[tag] = url
                self.tag_configure(tag, foreground=theme.BLUE, underline=True, font=_font(self._size, "medium"))
                self.tag_bind(tag, "<Button-1>", lambda e, u=url: webbrowser.open(u))
                self.tag_bind(tag, "<Enter>", lambda e: self.configure(cursor="hand2"))
                self.tag_bind(tag, "<Leave>", lambda e: self.configure(cursor="arrow"))
                self.insert("end", label, base_tags + (tag,))
            pos = match.end()
        if pos < len(text):
            self.insert("end", text[pos:], base_tags)

    def set_markdown(self, text: str):
        self.configure(state="normal")
        self.delete("1.0", "end")
        self._links.clear()
        lines = (text or "").replace("\r\n", "\n").split("\n")
        for raw in lines:
            line = raw.rstrip()
            stripped = line.strip()
            if not stripped:
                self.insert("end", "\n")
                continue
            if stripped.startswith("### "):
                self._insert_inline(stripped[4:], ("h2",))
                self.insert("end", "\n")
            elif stripped.startswith("## "):
                self._insert_inline(stripped[3:], ("h1",))
                self.insert("end", "\n")
            elif stripped.startswith("# "):
                self._insert_inline(stripped[2:], ("h1",))
                self.insert("end", "\n")
            elif stripped.startswith(("- ", "* ", "• ")):
                self.insert("end", "•   ", ("bullet",))
                self._insert_inline(stripped[2:], ("bullet",))
                self.insert("end", "\n", ("bullet",))
            elif stripped.startswith("> "):
                self._insert_inline(stripped[2:], ("quote",))
                self.insert("end", "\n", ("quote",))
            elif stripped in ("---", "***", "___"):
                self.insert("end", "\n")
            else:
                self._insert_inline(line)
                self.insert("end", "\n")
        # trim trailing newline
        if self.get("1.0", "end-1c").endswith("\n"):
            self.delete("end-2c", "end-1c")
        self.configure(state="disabled")
        self._schedule_fit()

    def set_plain(self, text: str):
        self.configure(state="normal")
        self.delete("1.0", "end")
        self.insert("end", text or "")
        self.configure(state="disabled")
        self._schedule_fit()

    def append(self, text: str):
        self.configure(state="normal")
        self.insert("end", text)
        self.configure(state="disabled")
        self._schedule_fit()

    def clear(self):
        self.set_plain("")


# --------------------------------------------------------------------------- #
# Code blocks
# --------------------------------------------------------------------------- #
_KEYWORDS = {
    "def", "class", "import", "from", "return", "if", "elif", "else", "for", "while",
    "try", "except", "finally", "with", "as", "lambda", "yield", "async", "await",
    "pass", "break", "continue", "in", "is", "not", "and", "or", "None", "True",
    "False", "raise", "global", "nonlocal", "assert", "del", "match", "case",
    "public", "private", "protected", "static", "void", "int", "new", "package",
    "final", "extends", "implements", "interface", "throw", "throws", "boolean",
    "double", "float", "long", "char", "byte", "short", "switch", "default",
    "this", "super", "null", "true", "false", "abstract", "synchronized", "volatile",
    "transient", "native", "enum", "record", "var", "const", "let", "function",
    "export", "typeof", "instanceof", "undefined", "of", "include", "define",
    "struct", "template", "namespace", "using", "delete", "nullptr", "package",
    "func", "type", "range", "go", "defer", "chan", "map", "fn", "mut", "pub",
    "use", "mod", "impl", "trait", "self", "Self", "loop", "Some", "Ok", "Err",
    "SELECT", "FROM", "WHERE", "INSERT", "UPDATE", "DELETE", "CREATE", "TABLE",
}
_TYPES = {
    "String", "System", "Scanner", "Integer", "Boolean", "Double", "Float", "Long",
    "Object", "List", "Dict", "Set", "Tuple", "Exception", "PrintStream", "Math",
    "Arrays", "ArrayList", "HashMap", "Func", "Promise", "Array", "Map", "void",
    "int", "float", "double", "long", "char", "boolean", "byte", "str", "bytes",
}
_TOKEN_RE = re.compile(
    r"(#[^\n]*|//[^\n]*|/\*.*?\*/)|(\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'|`[^`]*`)|(\b[A-Za-z_]\w*\b)|(\b\d+(?:\.\d+)?\b)"
)


def syntax_highlight(widget: tk.Text, code: str, language: str):
    widget.tag_configure("kw", foreground=theme.CODE_RED)
    widget.tag_configure("str", foreground=theme.CODE_GREEN)
    widget.tag_configure("num", foreground=theme.CODE_BLUE)
    widget.tag_configure("com", foreground=theme.CODE_MUTED)
    widget.tag_configure("type", foreground=theme.CODE_BLUE)
    widget.tag_configure("func", foreground=theme.CODE_BLUE)
    pos = 0
    for match in _TOKEN_RE.finditer(code):
        if match.start() > pos:
            widget.insert("end", code[pos : match.start()])
        comment, string, word, number = match.group(1), match.group(2), match.group(3), match.group(4)
        if comment:
            widget.insert("end", comment, ("com",))
        elif string:
            widget.insert("end", string, ("str",))
        elif number:
            widget.insert("end", number, ("num",))
        elif word:
            following = code[match.end() : match.end() + 1]
            if word in _KEYWORDS or word.lower() in _KEYWORDS:
                widget.insert("end", word, ("kw",))
            elif word in _TYPES:
                widget.insert("end", word, ("type",))
            elif following == "(":
                widget.insert("end", word, ("func",))
            else:
                widget.insert("end", word)
        pos = match.end()
    if pos < len(code):
        widget.insert("end", code[pos:])


class CodeBlock(Card):
    def __init__(
        self,
        master,
        code: str,
        language: str = "",
        filename: str = "",
        on_apply: Callable[[str, str], None] | None = None,
        on_copy: Callable[[str], None] | None = None,
        bg: str = theme.WHITE,
    ):
        super().__init__(master, radius=13, fill=theme.WHITE, outline=theme.LINE, thickness=1.6, bg=bg, padding=16)
        self.code = code
        self.language = (language or "").strip().lower()
        self.filename = filename
        self.on_apply = on_apply
        self.on_copy = on_copy

        header = tk.Frame(self.body, bg=theme.WHITE)
        header.pack(fill="x", pady=(0, 8))
        left = tk.Frame(header, bg=theme.WHITE)
        left.pack(side="left", fill="x", expand=True)
        if self.language:
            tk.Label(left, text=self.language.upper(), bg=theme.WHITE, fg=theme.MUTED, font=_font(9, "semibold")).pack(side="left")
        if filename:
            tk.Label(left, text=("  " if self.language else "") + filename, bg=theme.WHITE, fg=theme.INK_SOFT, font=_font(10, "medium")).pack(side="left")
        if on_apply and filename:
            IconButton(header, "pencil", 15, command=self._apply, color=theme.MUTED, hover_color=theme.INK, bg=theme.WHITE, pad=4, tooltip="Apply to project").pack(side="right")
        IconButton(header, "copy", 15, command=self._copy, color=theme.MUTED, hover_color=theme.INK, bg=theme.WHITE, pad=4, tooltip="Copy").pack(side="right")

        line_count = max(1, len(code.splitlines()))
        height = min(22, line_count + 1)

        self.text = tk.Text(
            self.body,
            wrap="char",
            bd=0,
            highlightthickness=0,
            bg=theme.WHITE,
            fg=theme.INK,
            font=theme.mono(13),
            padx=0,
            pady=0,
            width=1,
            height=height,
            cursor="xterm",
            selectbackground=theme.CHIP,
            selectforeground=theme.INK,
            spacing1=3,
            spacing2=1,
            spacing3=3,
        )
        self.text.pack(fill="x", anchor="w")
        self.text.insert("end", code)
        syntax_highlight(self.text, code, self.language)
        self.text.configure(state="disabled")
        self.text.bind("<Key>", lambda e: "break")
        self.text.bind("<MouseWheel>", self._scroll)
        self.text.bind("<Button-4>", self._scroll)
        self.text.bind("<Button-5>", self._scroll)

    def _scroll(self, event):
        if getattr(event, "num", None) == 4:
            self.text.yview_scroll(-3, "units")
        elif getattr(event, "num", None) == 5:
            self.text.yview_scroll(3, "units")
        elif getattr(event, "delta", 0):
            self.text.yview_scroll(-int(event.delta / 120) * 3, "units")
        return "break"

    def _copy(self):
        try:
            self.clipboard_clear()
            self.clipboard_append(self.code)
        except Exception:
            pass
        if self.on_copy:
            self.on_copy(self.code)

    def _apply(self):
        if self.on_apply:
            self.on_apply(self.filename, self.code)


# --------------------------------------------------------------------------- #
# Tooltip & toast
# --------------------------------------------------------------------------- #
class Tooltip:
    def __init__(self, widget, text: str, delay: int = 450):
        self.widget = widget
        self.text = text
        self.delay = delay
        self._job = None
        self._tip = None
        widget.bind("<Enter>", self._schedule, add="+")
        widget.bind("<Leave>", self._hide, add="+")
        widget.bind("<ButtonPress>", self._hide, add="+")

    def _schedule(self, _event=None):
        self._job = self.widget.after(self.delay, self._show)

    def _show(self):
        if self._tip or not self.widget.winfo_exists():
            return
        try:
            x = self.widget.winfo_rootx() + self.widget.winfo_width() // 2
            y = self.widget.winfo_rooty() - 8
            tip = tk.Toplevel(self.widget)
            tip.wm_overrideredirect(True)
            tip.wm_attributes("-topmost", True)
            label = tk.Label(
                tip,
                text=self.text,
                bg=theme.INK,
                fg=theme.WHITE,
                font=_font(9),
                padx=8,
                pady=5,
                bd=0,
            )
            label.pack()
            tip.update_idletasks()
            w = tip.winfo_reqwidth()
            tip.geometry(f"+{x - w // 2}+{y - tip.winfo_reqheight()}")
            self._tip = tip
        except Exception:
            self._tip = None

    def _hide(self, _event=None):
        if self._job:
            try:
                self.widget.after_cancel(self._job)
            except Exception:
                pass
            self._job = None
        if self._tip:
            try:
                self._tip.destroy()
            except Exception:
                pass
            self._tip = None


class Toast(tk.Toplevel):
    def __init__(self, master, text: str, icon: str = "check", duration: int = 2400):
        super().__init__(master)
        self.wm_overrideredirect(True)
        self.wm_attributes("-topmost", True)
        try:
            self.wm_attributes("-alpha", 0.0)
        except Exception:
            pass
        card = Card(self, radius=13, fill=theme.INK, outline=None, thickness=0, bg=theme.WHITE, padding=13)
        card.pack()
        row = tk.Frame(card.body, bg=theme.INK)
        row.pack()
        tk.Label(row, image=visuals.icon_photo(icon, 16, theme.WHITE), bg=theme.INK, bd=0).pack(side="left", padx=(0, 9))
        tk.Label(row, text=text, bg=theme.INK, fg=theme.WHITE, font=_font(11, "medium")).pack(side="left")
        self.update_idletasks()
        width, height = self.winfo_reqwidth(), self.winfo_reqheight()
        try:
            x = master.winfo_rootx() + (master.winfo_width() - width) // 2
            y = master.winfo_rooty() + master.winfo_height() - height - 52
            self.geometry(f"+{x}+{y}")
            self.lift()
        except Exception:
            pass
        try:
            self.wm_attributes("-alpha", 0.98)
        except Exception:
            pass
        self.after(duration, self._close)

    def _close(self):
        try:
            self.destroy()
        except Exception:
            pass
