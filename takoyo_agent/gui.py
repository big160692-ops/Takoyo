"""The Takoyo desktop interface: white space, ink lines, Poppins and an octopus."""

from __future__ import annotations

import json
import os
import queue
import re
import shutil
import threading
import time
import tkinter as tk
import traceback
from pathlib import Path
from tkinter import filedialog

from . import ai_providers, model_catalog, prompting, theme, visuals
from .config import Config
from .image_gen import ImageGenerator
from .local_ai import LocalAIError, LocalAINotReady, get_engine
from .memory import ConversationStore
from .projects import ProjectStore
from .widgets import (
    Card,
    CodeBlock,
    IconButton,
    RichText,
    RoundedCanvas,
    ScrollArea,
    SearchField,
    TextButton,
    Toast,
    Tooltip,
)

TOOL_RE = re.compile(r"<takoyo-tool>\s*(\{.*?\})\s*</takoyo-tool>", re.S | re.I)
IMAGE_TAG_RE = re.compile(r"<takoyo-image>(.*?)</takoyo-image>", re.S | re.I)
FENCE_RE = re.compile(r"```([^\n`]*)\n(.*?)```", re.S)
IMAGE_COMMAND_RE = re.compile(r"^\s*/(?:image|img|art|draw|paint)\b[:,]?\s*", re.I)


# --------------------------------------------------------------------------- #
# Small helpers
# --------------------------------------------------------------------------- #
def _open_path(path: str | Path) -> None:
    path = str(path)
    try:
        if os.name == "nt":
            os.startfile(path)  # type: ignore[attr-defined]
        else:
            import webbrowser

            webbrowser.open(Path(path).as_uri())
    except Exception:
        pass


def _ago(stamp: float) -> str:
    try:
        stamp = float(stamp)
    except Exception:
        return ""
    delta = max(0, time.time() - stamp)
    if delta < 60:
        return "now"
    if delta < 3600:
        return f"{int(delta // 60)}m"
    if delta < 86400:
        return f"{int(delta // 3600)}h"
    if delta < 86400 * 7:
        return f"{int(delta // 86400)}d"
    return time.strftime("%b %d", time.localtime(stamp))


def _short(text: str, limit: int = 34) -> str:
    text = re.sub(r"\s+", " ", (text or "").strip())
    return text if len(text) <= limit else text[: limit - 1].rstrip() + "…"


def _paint_tree(widget, color: str) -> None:
    try:
        widget.configure(bg=color)
    except Exception:
        pass
    for child in widget.winfo_children():
        _paint_tree(child, color)


def _bind_tree(widget, sequence: str, handler) -> None:
    try:
        widget.bind(sequence, handler)
    except Exception:
        pass
    for child in widget.winfo_children():
        _bind_tree(child, sequence, handler)


def image_request_prompt(text: str) -> str:
    text = (text or "").strip()
    if not text:
        return ""
    command = IMAGE_COMMAND_RE.match(text)
    if command:
        return text[command.end() :].strip() or "a calm Takoyo landscape"
    low = text.lower()
    if re.search(r"\b(draw|paint|illustrate|sketch|render)\b", low):
        return text
    if re.search(r"\b(generate|create|make|design)\b[^.!?\n]{0,34}\b(image|picture|illustration|artwork|art|poster|wallpaper|logo)\b", low):
        return text
    if re.match(r"^(an?\s+)?(image|picture|illustration|photo|poster|wallpaper)\s+(of|showing)\b", low):
        return text
    return ""


def parse_reply(text: str) -> tuple[str, list[dict], list[str]]:
    proposals: list[dict] = []
    for match in TOOL_RE.finditer(text or ""):
        try:
            data = json.loads(match.group(1))
        except Exception:
            continue
        if not isinstance(data, dict) or data.get("tool") != "write_file":
            continue
        path = str(data.get("path") or "").strip().replace("\\", "/")
        content = data.get("content")
        if not path or not isinstance(content, str):
            continue
        if path.startswith("/") or re.match(r"^[A-Za-z]:", path) or ".." in path.split("/"):
            continue
        proposals.append({"path": path, "content": content, "applied": False})
    cleaned = TOOL_RE.sub("", text or "")
    prompts = [p.strip() for p in IMAGE_TAG_RE.findall(cleaned) if p.strip()]
    cleaned = IMAGE_TAG_RE.sub("", cleaned).strip()
    # de-duplicate prompts while keeping order
    seen: set[str] = set()
    unique: list[str] = []
    for prompt in prompts:
        if prompt not in seen:
            seen.add(prompt)
            unique.append(prompt)
    return cleaned, proposals, unique


def split_fences(text: str) -> list[tuple[str, str | tuple[str, str]]]:
    """Return ('text', str) and ('code', (info, code)) segments."""
    segments: list[tuple[str, str | tuple[str, str]]] = []
    pos = 0
    for match in FENCE_RE.finditer(text or ""):
        before = text[pos : match.start()]
        if before.strip():
            segments.append(("text", before))
        segments.append(("code", (match.group(1).strip(), match.group(2).rstrip("\n"))))
        pos = match.end()
    after = text[pos:]
    if after.strip():
        segments.append(("text", after))
    return segments


def split_info(info: str) -> tuple[str, str]:
    language = ""
    filename = ""
    for token in (info or "").split():
        if token.startswith("file="):
            filename = token[5:].strip("\"'")
        elif not language:
            language = token
    if not language and filename:
        suffix = Path(filename).suffix.lower().lstrip(".")
        language = suffix if suffix else ""
    return language, filename


# --------------------------------------------------------------------------- #
# Rows & buttons
# --------------------------------------------------------------------------- #
class HoverRow(tk.Frame):
    def __init__(self, master, command=None, base: str = theme.WHITE, hover: str = theme.HOVER, **kwargs):
        super().__init__(master, bg=base, cursor="hand2", **kwargs)
        self._base = base
        self._hover = hover
        self._command = command
        self._hovered = False
        self.bind("<Enter>", self._on_enter)
        self.bind("<Leave>", self._on_leave)
        self.bind("<Button-1>", self._on_click)

    def bind_tree(self):
        _bind_tree(self, "<Button-1>", self._on_click)
        return self

    def _set(self, color: str):
        if color == self._base:
            self._hovered = False
        else:
            self._hovered = True
        _paint_tree(self, color)

    def _on_enter(self, _event=None):
        self._set(self._hover)

    def _on_leave(self, _event=None):
        self.after(20, self._check_leave)

    def _check_leave(self):
        try:
            x, y = self.winfo_pointerxy()
            inside = (
                self.winfo_rootx() <= x <= self.winfo_rootx() + self.winfo_width()
                and self.winfo_rooty() <= y <= self.winfo_rooty() + self.winfo_height()
            )
        except Exception:
            inside = False
        if not inside:
            self._set(self._base)

    def _on_click(self, _event=None):
        if self._command:
            self._command()


class CircleButton(tk.Canvas):
    def __init__(
        self,
        master,
        icon: str = "arrow-up",
        size: int = 38,
        command=None,
        bg: str = theme.WHITE,
        fill: str = theme.INK,
        icon_color: str = theme.WHITE,
    ):
        super().__init__(master, width=size, height=size, bg=bg, highlightthickness=0, bd=0, cursor="hand2")
        self._size = size
        self._command = command
        self._fill = fill
        self._bg = bg
        self._enabled = True
        self._oval = self.create_oval(1, 1, size - 1, size - 1, fill=fill, outline="")
        self._photo = visuals.icon_photo(icon, max(12, int(size * 0.42)), icon_color)
        self._icon_item = self.create_image(size // 2, size // 2, image=self._photo)
        self._icon = icon
        self.bind("<Enter>", self._on_enter)
        self.bind("<Leave>", self._on_leave)
        self.bind("<Button-1>", self._on_click)

    def _on_enter(self, _event=None):
        if self._enabled:
            self.itemconfigure(self._oval, fill="#2C2C31")

    def _on_leave(self, _event=None):
        if self._enabled:
            self.itemconfigure(self._oval, fill=self._fill)

    def _on_click(self, _event=None):
        if self._enabled and self._command:
            self._command()

    def set_icon(self, icon: str):
        self._icon = icon
        self._photo = visuals.icon_photo(icon, max(12, int(self._size * 0.42)), theme.WHITE)
        self.itemconfigure(self._icon_item, image=self._photo)

    def set_enabled(self, enabled: bool):
        self._enabled = enabled
        self.configure(cursor="hand2" if enabled else "arrow")
        self.itemconfigure(self._oval, fill=self._fill if enabled else theme.SEARCH_BG)
        self._photo = visuals.icon_photo(self._icon, max(12, int(self._size * 0.42)), theme.WHITE if enabled else theme.MUTED)
        self.itemconfigure(self._icon_item, image=self._photo)


# --------------------------------------------------------------------------- #
# Composer
# --------------------------------------------------------------------------- #
class Composer(tk.Frame):
    def __init__(self, master, app, width: int | None = None):
        super().__init__(master, bg=theme.WHITE)
        self.app = app
        self.attachment: dict | None = None
        self._thinking = False

        kwargs = {"width": width} if width else {}
        self.surface = RoundedCanvas(
            self,
            radius=18,
            fill=theme.WHITE,
            outline=theme.INK,
            thickness=1.5,
            bg=theme.WHITE,
            height=152,
            **kwargs,
        )
        if width:
            self.surface.pack(anchor="center")
        else:
            self.surface.pack(fill="x")
        self.surface._size = (0, 0)

        body = self.surface.body
        body.grid_columnconfigure(0, weight=1)
        body.grid_rowconfigure(0, weight=1)

        self.text_holder = tk.Frame(body, bg=theme.WHITE)
        self.text_holder.grid(row=0, column=0, sticky="nsew", padx=18, pady=(14, 0))
        self.text = tk.Text(
            self.text_holder,
            bd=0,
            relief="flat",
            bg=theme.WHITE,
            fg=theme.INK,
            insertbackground=theme.INK,
            font=theme.font(15),
            highlightthickness=0,
            wrap="word",
            undo=True,
            height=3,
            padx=0,
            pady=0,
            spacing1=1,
            spacing3=3,
        )
        self.text.pack(fill="both", expand=True)
        self._mascot_ph = visuals.mascot_photo(17)
        self.placeholder = tk.Label(
            self.text_holder,
            text="Press and Type to start...",
            bg=theme.WHITE,
            fg=theme.MUTED,
            font=theme.font(16),
            anchor="nw",
            justify="left",
            cursor="xterm",
        )
        self.placeholder.place(x=1, y=0)
        self.placeholder.bind("<Button-1>", lambda e: self.text.focus_set())
        self._ph_text = "Press and Type to start..."

        bottom = tk.Frame(body, bg=theme.WHITE, height=38)
        bottom.grid(row=1, column=0, sticky="ew", padx=10, pady=(0, 8))
        self.bottom = bottom
        self.attach_btn = IconButton(
            bottom,
            "paperclip",
            19,
            command=self._attach,
            color=theme.INK_SOFT,
            hover_color=theme.INK,
            bg=theme.WHITE,
            pad=6,
            tooltip="Attach a file",
        )
        self.attach_btn.pack(side="left")
        self.chip = tk.Frame(bottom, bg=theme.CHIP)
        self.chip_label = tk.Label(self.chip, text="", bg=theme.CHIP, fg=theme.INK_SOFT, font=theme.font(10, "medium"))
        self.chip_label.pack(side="left", padx=(7, 4), pady=3)
        self.chip_close = IconButton(
            self.chip, "close", 11, command=self._clear_attachment, color=theme.MUTED, hover_color=theme.INK, bg=theme.CHIP, pad=3
        )
        self.chip_close.pack(side="right", padx=(0, 4))
        self.chip_icon = tk.Label(self.chip, image=visuals.icon_photo("paperclip", 11, theme.MUTED), bg=theme.CHIP)
        self.chip_icon.pack(side="left", padx=(6, 0))

        right = tk.Frame(bottom, bg=theme.WHITE)
        right.pack(side="right")
        self.mic = IconButton(
            right,
            "mic",
            19,
            command=self._voice,
            color=theme.MUTED,
            hover_color=theme.INK,
            bg=theme.WHITE,
            pad=6,
            tooltip="Voice input",
        )
        self.mic.pack(side="right", padx=(2, 0))
        self.send = CircleButton(right, "arrow-up", 36, command=self._submit, bg=theme.WHITE)
        self.send.pack(side="right", padx=(4, 2))
        self.stop = CircleButton(right, "stop", 36, command=app.stop_streaming, bg=theme.WHITE)
        self._stop_visible = False

        self.text.bind("<KeyRelease>", self._changed, add="+")
        self.text.bind("<<Paste>>", lambda e: self.after_idle(self._changed), add="+")
        self.text.bind("<Return>", self._on_return)
        self.text.bind("<Shift-Return>", lambda e: None)
        self.text.bind("<Button-1>", lambda e: self.text.focus_set(), add="+")
        self._refresh_buttons()

    # ------------------------------------------------------------------ #
    def get_text(self) -> str:
        return self.text.get("1.0", "end-1c")

    def set_text(self, text: str):
        self.text.delete("1.0", "end")
        self.text.insert("1.0", text or "")
        self.text.focus_set()
        self._changed()

    def focus(self):
        self.text.focus_set()

    def clear(self):
        self.text.delete("1.0", "end")
        self._clear_attachment()
        self._changed()
        if not self._thinking:
            self.placeholder.place(x=1, y=0)

    # ------------------------------------------------------------------ #
    def _on_return(self, event):
        if event.state & 0x0001:
            return None
        self._submit()
        return "break"

    def _submit(self):
        if self._thinking:
            return
        text = self.get_text().strip()
        if not text and not self.attachment:
            return
        self.app.send_from_composer(self)

    def _voice(self):
        self.app.start_voice(self)

    def _changed(self, _event=None):
        text = self.get_text()
        if text.strip():
            self.placeholder.place_forget()
        elif not self._thinking:
            self.placeholder.place(x=1, y=0)
        self._refresh_buttons()

    def _refresh_buttons(self):
        if self._thinking:
            self.send.pack_forget()
            self.mic.pack_forget()
            if self.app.streaming and self.app.cancel_token is not None:
                if not self._stop_visible:
                    self.stop.pack(side="right", padx=(4, 2))
                    self._stop_visible = True
            else:
                if self._stop_visible:
                    self.stop.pack_forget()
                    self._stop_visible = False
            return
        if self._stop_visible:
            self.stop.pack_forget()
            self._stop_visible = False
        if self.get_text().strip() or self.attachment:
            self.mic.pack_forget()
            if not self.send.winfo_ismapped():
                self.send.pack(side="right", padx=(4, 2))
            self.send.set_enabled(True)
        else:
            self.send.pack_forget()
            if not self.mic.winfo_ismapped():
                self.mic.pack(side="right", padx=(2, 0))

    # ------------------------------------------------------------------ #
    def _attach(self):
        path = filedialog.askopenfilename(title="Attach a file", parent=self.winfo_toplevel())
        if not path:
            return
        try:
            data = Path(path).read_bytes()
        except Exception as exc:
            self.app.toast(f"Could not read that file: {exc}", "close")
            return
        name = Path(path).name
        try:
            content = data[:160_000].decode("utf-8")
            if len(data) > 160_000:
                content += "\n… (truncated)"
        except UnicodeDecodeError:
            content = f"[binary file · {len(data):,} bytes]"
        self.attachment = {"name": name, "content": content, "path": path}
        self.chip_label.configure(text=_short(name, 26))
        self.chip.pack(side="left", padx=(6, 4))
        self._changed()

    def _clear_attachment(self):
        self.attachment = None
        self.chip.pack_forget()
        self._refresh_buttons()

    def set_thinking(self, thinking: bool, label: str = "Thinking…"):
        self._thinking = thinking
        if thinking:
            self.text.configure(state="disabled")
            self.placeholder.configure(text="  " + label, image=self._mascot_ph, compound="left", fg=theme.MUTED)
            self.placeholder.place(x=1, y=0)
        else:
            self.text.configure(state="normal")
            self.placeholder.configure(text=self._ph_text, image="", compound="none", fg=theme.MUTED)
            if self.get_text().strip():
                self.placeholder.place_forget()
            else:
                self.placeholder.place(x=1, y=0)
        self._stop_visible = False
        self.stop.pack_forget()
        self._refresh_buttons()


# --------------------------------------------------------------------------- #
# Header
# --------------------------------------------------------------------------- #
class Header(tk.Frame):
    def __init__(self, master, app):
        super().__init__(master, bg=theme.WHITE, height=66)
        self.app = app
        self.pack_propagate(False)
        self.logo = tk.Frame(self, bg=theme.WHITE)
        self.logo.pack(side="left", padx=(20, 0), pady=16)
        self.sidebar_btn = IconButton(
            self.logo,
            "sidebar",
            25,
            command=app.toggle_sidebar,
            color=theme.INK,
            hover_color=theme.INK,
            bg=theme.WHITE,
            hover_bg=None,
            pad=2,
            tooltip="Toggle sidebar",
        )
        self.sidebar_btn.pack(side="left")
        self.wordmark = tk.Label(self.logo, image=visuals.wordmark_photo(20), bg=theme.WHITE, bd=0, cursor="hand2")
        self.wordmark.pack(side="left", padx=(9, 0))
        self.wordmark.bind("<Button-1>", lambda e: app.show_home())

        right = tk.Frame(self, bg=theme.WHITE)
        right.pack(side="right", padx=(0, 18), pady=16)
        self.art_btn = IconButton(
            right,
            "image",
            18,
            command=app.toggle_art,
            color=theme.MUTED,
            hover_color=theme.INK,
            bg=theme.WHITE,
            hover_bg=None,
            pad=5,
            tooltip="Art panel",
        )
        self.art_btn.pack(side="right", padx=4)
        self.gear_btn = IconButton(
            right,
            "gear",
            18,
            command=app.open_settings,
            color=theme.MUTED,
            hover_color=theme.INK,
            bg=theme.WHITE,
            hover_bg=None,
            pad=5,
            tooltip="Settings",
        )
        self.gear_btn.pack(side="right", padx=4)

    def set_sidebar(self, open_: bool):
        if open_:
            self.logo.pack_forget()
        else:
            if not self.logo.winfo_ismapped():
                self.logo.pack(side="left", padx=(20, 0), pady=16)

    def set_art(self, visible: bool):
        self.art_btn.set_icon("image", theme.INK if visible else theme.MUTED)


# --------------------------------------------------------------------------- #
# Home
# --------------------------------------------------------------------------- #
class HomeView(tk.Frame):
    def __init__(self, master, app):
        super().__init__(master, bg=theme.WHITE)
        self.app = app
        self.inner = tk.Frame(self, bg=theme.WHITE)
        self.inner.place(relx=0.5, rely=0.5, anchor="center")

        self.scope = tk.Frame(self.inner, bg=theme.WHITE)
        self.scope_icon = tk.Label(self.scope, image=visuals.icon_photo("folder", 14, theme.MUTED), bg=theme.WHITE)
        self.scope_icon.pack(side="left")
        self.scope_label = tk.Label(self.scope, text="", bg=theme.WHITE, fg=theme.MUTED, font=theme.font(11, "medium"))
        self.scope_label.pack(side="left", padx=(6, 0))

        self.mascot = tk.Label(self.inner, image=visuals.mascot_photo(238), bg=theme.WHITE, bd=0)
        self.mascot.pack(pady=(0, 18))
        self.tagline = tk.Label(
            self.inner,
            text="Want to build, generate, or just want a\nchat. Let me Know",
            bg=theme.WHITE,
            fg=theme.INK,
            font=theme.font(26),
            justify="center",
        )
        self.tagline.pack(pady=(0, 24))
        self.composer = Composer(self.inner, app, width=760)
        self.composer.pack()
        self.set_project(None)

    def set_project(self, meta: dict | None):
        if meta:
            self.scope_label.configure(text=f"Working in {_short(meta.get('name', 'project'), 30)}")
            if not self.scope.winfo_ismapped():
                self.scope.pack(pady=(0, 14), before=self.mascot)
            self.scope_label.configure(cursor="hand2")
            self.scope_label.bind("<Button-1>", lambda e: self.app.open_project_files())
            self.scope_icon.bind("<Button-1>", lambda e: self.app.open_project_files())
        else:
            self.scope.pack_forget()

    def focus(self):
        self.composer.focus()


# --------------------------------------------------------------------------- #
# Sidebar
# --------------------------------------------------------------------------- #
class Sidebar(tk.Frame):
    def __init__(self, master, app):
        super().__init__(master, bg=theme.WHITE, width=396)
        self.app = app
        self.grid_propagate(False)
        self.pack_propagate(False)
        self.card = Card(
            self,
            radius=22,
            fill=theme.WHITE,
            outline=theme.INK,
            thickness=1.6,
            bg=theme.WHITE,
            padding=8,
        )
        self.card.pack(fill="both", expand=True, padx=(16, 0), pady=16)
        body = self.card.body

        head = tk.Frame(body, bg=theme.WHITE)
        head.pack(fill="x", padx=18, pady=(16, 12))
        IconButton(
            head,
            "sidebar",
            24,
            command=app.toggle_sidebar,
            color=theme.INK,
            hover_color=theme.INK,
            bg=theme.WHITE,
            hover_bg=None,
            pad=2,
            tooltip="Hide sidebar",
        ).pack(side="left")
        tk.Label(head, image=visuals.wordmark_photo(19), bg=theme.WHITE, bd=0, cursor="hand2").pack(
            side="left", padx=(9, 0)
        )

        self.search = SearchField(body, placeholder="Search", command=app.on_sidebar_search, bg=theme.WHITE)
        self.search.pack(fill="x", padx=16, pady=(0, 12), ipady=0)
        self.search.configure(height=42)

        footer = tk.Frame(body, bg=theme.WHITE)
        footer.pack(side="bottom", fill="x")
        tk.Frame(footer, bg=theme.LINE_SOFT, height=1).pack(fill="x", padx=16, pady=(4, 0))
        self.status_row = HoverRow(footer, command=app.open_settings, base=theme.WHITE)
        self.status_row.pack(fill="x", padx=8, pady=8)
        inner = tk.Frame(self.status_row, bg=theme.WHITE)
        inner.pack(fill="x", padx=8, pady=6)
        self.dot = tk.Canvas(inner, width=9, height=9, bg=theme.WHITE, highlightthickness=0, bd=0)
        self.dot.pack(side="left", padx=(0, 8))
        self.dot_id = self.dot.create_oval(1, 1, 8, 8, fill=theme.FAINT, outline="")
        self.status_label = tk.Label(inner, text="Built-in AI", bg=theme.WHITE, fg=theme.MUTED, font=theme.font(10, "medium"))
        self.status_label.pack(side="left")
        IconButton(
            inner, "gear", 15, command=app.open_settings, color=theme.MUTED, hover_color=theme.INK, bg=theme.WHITE, pad=3
        ).pack(side="right")
        self.status_row.bind_tree()

        self.scroll = ScrollArea(body, bg=theme.WHITE)
        self.scroll.pack(fill="both", expand=True, padx=(8, 4), pady=(0, 4))
        content = self.scroll.inner

        projects_head = tk.Frame(content, bg=theme.WHITE)
        projects_head.pack(fill="x", padx=10, pady=(2, 3))
        tk.Label(projects_head, text="PROJECTS", bg=theme.WHITE, fg=theme.INK, font=theme.font(11, "semibold")).pack(side="left")
        IconButton(
            projects_head,
            "plus",
            17,
            command=app.new_project_dialog,
            color=theme.INK,
            hover_color=theme.INK,
            bg=theme.WHITE,
            hover_bg=None,
            pad=2,
            tooltip="New project",
        ).pack(side="right")
        self.projects_box = tk.Frame(content, bg=theme.WHITE)
        self.projects_box.pack(fill="x", padx=4, pady=(0, 10))

        recents_head = tk.Frame(content, bg=theme.WHITE)
        recents_head.pack(fill="x", padx=10, pady=(12, 3))
        tk.Label(recents_head, text="RECENTS", bg=theme.WHITE, fg=theme.INK, font=theme.font(11, "semibold")).pack(side="left")
        self.recents_box = tk.Frame(content, bg=theme.WHITE)
        self.recents_box.pack(fill="x", padx=4, pady=(0, 12))

    # ------------------------------------------------------------------ #
    def refresh(self):
        for box in (self.projects_box, self.recents_box):
            for child in box.winfo_children():
                child.destroy()
        query = self.app.sidebar_query
        projects = self.app.projects.list(query=query)
        if not projects:
            tk.Label(
                self.projects_box,
                text="No projects yet" if not query else "No matches",
                bg=theme.WHITE,
                fg=theme.FAINT,
                font=theme.font(10),
                anchor="w",
            ).pack(fill="x", padx=10, pady=(2, 4))
        for meta in projects[:24]:
            self._project_row(meta)
        conversations = self.app.conversations.summaries(query=query)[:30]
        if not conversations:
            tk.Label(
                self.recents_box,
                text="Your chats will live here" if not query else "No matches",
                bg=theme.WHITE,
                fg=theme.FAINT,
                font=theme.font(10),
                anchor="w",
            ).pack(fill="x", padx=10, pady=2)
        for summary in conversations:
            self._recent_row(summary)
        self.refresh_status()

    def _project_row(self, meta: dict):
        selected = bool(self.app.current_project and self.app.current_project.get("id") == meta.get("id"))
        base = theme.CHIP if selected else theme.WHITE
        row = HoverRow(self.projects_box, command=lambda m=meta: self.app.open_project(m), base=base, hover=theme.HOVER)
        row.pack(fill="x", pady=1)
        inner = tk.Frame(row, bg=base)
        inner.pack(fill="x", padx=8, pady=6)
        tk.Label(inner, image=visuals.icon_photo("folder", 15, theme.INK if selected else theme.MUTED), bg=base).pack(
            side="left", padx=(2, 8)
        )
        text = tk.Frame(inner, bg=base)
        text.pack(side="left", fill="x", expand=True)
        tk.Label(text, text=_short(meta.get("name", "Project"), 30), bg=base, fg=theme.INK, font=theme.font(12, "medium"), anchor="w").pack(fill="x")
        count = meta.get("file_count", 0)
        tk.Label(
            text,
            text=f"{count} file{'s' if count != 1 else ''}",
            bg=base,
            fg=theme.MUTED,
            font=theme.font(9),
            anchor="w",
        ).pack(fill="x")
        more = IconButton(
            inner,
            "more",
            14,
            command=lambda m=meta: self.app.project_menu(m, more),
            color=theme.MUTED,
            hover_color=theme.INK,
            bg=base,
            pad=4,
        )
        more.pack(side="right")
        row.bind_tree()
        for widget in (row, inner, text):
            widget.bind("<Button-3>", lambda e, m=meta: self.app.project_menu(m, e.widget), add="+")

    def _recent_row(self, summary: dict):
        selected = bool(self.app.current_conversation and self.app.current_conversation.get("id") == summary.get("id"))
        base = theme.CHIP if selected else theme.WHITE
        row = HoverRow(
            self.recents_box,
            command=lambda s=summary: self.app.load_conversation(s["id"]),
            base=base,
            hover=theme.HOVER,
        )
        row.pack(fill="x", pady=1)
        inner = tk.Frame(row, bg=base)
        inner.pack(fill="x", padx=8, pady=5)
        tk.Label(inner, image=visuals.icon_photo("chat", 14, theme.INK if selected else theme.MUTED), bg=base).pack(
            side="left", padx=(2, 8)
        )
        text = tk.Frame(inner, bg=base)
        text.pack(side="left", fill="x", expand=True)
        tk.Label(
            text,
            text=_short(summary.get("title", "New chat"), 34),
            bg=base,
            fg=theme.INK,
            font=theme.font(11, "medium"),
            anchor="w",
        ).pack(fill="x")
        meta_bits = []
        if summary.get("project_id"):
            project = self.app.projects.get(summary["project_id"])
            if project:
                meta_bits.append(project.get("name", ""))
        if summary.get("has_image"):
            meta_bits.append("image")
        if meta_bits:
            tk.Label(text, text=" · ".join(meta_bits), bg=base, fg=theme.MUTED, font=theme.font(9), anchor="w").pack(fill="x")
        tk.Label(inner, text=_ago(summary.get("updated", 0)), bg=base, fg=theme.FAINT, font=theme.font(9)).pack(side="right", padx=(4, 2))
        row.bind_tree()
        for widget in (row, inner, text):
            widget.bind("<Button-3>", lambda e, s=summary: self.app.conversation_menu(s, e.widget), add="+")

    def refresh_status(self):
        text, color = self.app.provider_status()
        self.status_label.configure(text=text)
        self.dot.itemconfigure(self.dot_id, fill=color)
        if self.app.api_server is not None:
            port = self.app.api_server.server_address[1]
            self.status_label.configure(text=f"{text} · API :{port}")


# --------------------------------------------------------------------------- #
# Chat
# --------------------------------------------------------------------------- #
class ChatView(tk.Frame):
    def __init__(self, master, app):
        super().__init__(master, bg=theme.WHITE)
        self.app = app
        self._live_text = None
        self._live_label = None
        self._current = None

        self.composer_host = tk.Frame(self, bg=theme.WHITE)
        self.composer_host.pack(side="bottom", fill="x", padx=16, pady=(4, 14))
        self.composer = Composer(self.composer_host, app)
        self.composer.pack(fill="x")

        self.projectbar = tk.Frame(self, bg=theme.WHITE)
        self.holder = tk.Frame(self, bg=theme.WHITE)
        self.holder.pack(fill="both", expand=True, pady=(4, 0))
        self.scroll = ScrollArea(self.holder, bg=theme.WHITE)
        self.scroll.pack(fill="both", expand=True, padx=(0, 2))
        self.scroll_btn = IconButton(
            self.holder,
            "arrow-down",
            17,
            command=self.scroll.scroll_to_bottom,
            color=theme.INK,
            hover_color=theme.INK,
            bg=theme.WHITE,
            hover_bg=theme.HOVER,
            pad=7,
            outline=True,
            tooltip="Jump to latest",
        )
        self._scroll_visible = False
        self._tick_scroll()
        self.set_project(None)

    # ------------------------------------------------------------------ #
    def set_project(self, meta: dict | None):
        for child in self.projectbar.winfo_children():
            child.destroy()
        if not meta:
            self.projectbar.pack_forget()
            return
        if not self.projectbar.winfo_ismapped():
            self.projectbar.pack(side="top", fill="x", padx=28, pady=(0, 2), before=self.holder)
        inner = tk.Frame(self.projectbar, bg=theme.WHITE)
        inner.pack(fill="x", pady=2)
        tk.Label(inner, image=visuals.icon_photo("folder", 15, theme.INK), bg=theme.WHITE).pack(side="left", padx=(0, 7))
        tk.Label(inner, text=meta.get("name", "Project"), bg=theme.WHITE, fg=theme.INK, font=theme.font(13, "semibold")).pack(side="left")
        description = _short(meta.get("description", ""), 60)
        if description:
            tk.Label(inner, text="· " + description, bg=theme.WHITE, fg=theme.MUTED, font=theme.font(10)).pack(
                side="left", padx=(8, 0)
            )
        IconButton(inner, "close", 13, command=self.app.close_project, color=theme.MUTED, hover_color=theme.INK, bg=theme.WHITE, pad=4, tooltip="Close project").pack(side="right")
        IconButton(inner, "plus", 16, command=lambda: self.app.new_chat(), color=theme.MUTED, hover_color=theme.INK, bg=theme.WHITE, pad=3, tooltip="New chat in this project").pack(side="right", padx=4)
        TextButton(inner, "Files", command=self.app.open_project_files, variant="secondary", size=10, height=29).pack(side="right", padx=(0, 6))

    # ------------------------------------------------------------------ #
    def refresh(self, conv: dict | None):
        self._current = conv
        self._live_text = None
        self._live_label = None
        for child in self.scroll.inner.winfo_children():
            child.destroy()
        if not conv or not conv.get("messages"):
            return
        project = self.app.projects.get(conv.get("project_id", "")) if conv.get("project_id") else None
        self.set_project(project)
        messages = conv.get("messages", [])
        last = len(messages) - 1
        for index, message in enumerate(messages):
            self._render_message(message, index, is_last=(index == last))
        self.after(40, self.scroll.scroll_to_bottom)

    def clear(self):
        self._current = None
        self._live_text = None
        self._live_label = None
        for child in self.scroll.inner.winfo_children():
            child.destroy()
        self.set_project(None)

    def scroll_to_bottom(self):
        self.scroll.scroll_to_bottom()

    # ------------------------------------------------------------------ #
    def _message_row(self):
        row = tk.Frame(self.scroll.inner, bg=theme.WHITE)
        row.pack(fill="x", padx=34, pady=(9, 3))
        return row

    def _render_message(self, message: dict, index: int, is_last: bool = False):
        role = message.get("role")
        if role == "user":
            self._render_user(message)
            return
        content = message.get("content") or ""
        images = message.get("images") or []
        edits = message.get("edits") or []
        error = message.get("error")
        if not content.strip() and not images and not edits and not error:
            return
        row = self._message_row()
        tk.Label(row, image=visuals.mascot_photo(25), bg=theme.WHITE, bd=0).pack(side="left", anchor="n", padx=(0, 9), pady=(1, 0))
        column = tk.Frame(row, bg=theme.WHITE)
        column.pack(side="left", fill="both", expand=True)

        if content.strip():
            for kind, value in split_fences(content):
                if kind == "text":
                    rich = RichText(column, text=str(value), size=15, bg=theme.WHITE)
                    rich.pack(fill="x", anchor="w", pady=(0, 2))
                else:
                    info, code = value  # type: ignore[misc]
                    language, filename = split_info(info)
                    block = CodeBlock(
                        column,
                        code=code,
                        language=language,
                        filename=filename,
                        on_apply=self.app.apply_edit_from_chat if filename else None,
                        bg=theme.WHITE,
                    )
                    block.pack(fill="x", anchor="w", pady=(8, 8))

        for meta in images:
            self._image_card(column, meta)
        for edit in edits:
            self._proposal_card(column, edit, index)
        if error:
            self._error_card(column, error, message.get("error_kind", ""))
        if content.strip() or images or edits or error:
            pad = (0, 6) if is_last else (0, 2)
            column.pack_configure(pady=pad)

    def _render_user(self, message: dict):
        row = self._message_row()
        wrap = tk.Frame(row, bg=theme.WHITE)
        wrap.pack(side="right", anchor="e")
        card = Card(wrap, radius=16, fill=theme.WHITE, outline=theme.INK, thickness=1.45, bg=theme.WHITE, padding=14)
        card.pack(anchor="e")
        text = message.get("content") or ""
        view_w = self.scroll.canvas.winfo_width() or self.winfo_width() or 900
        width = int(min(520, max(180, view_w - 90)))
        if text.strip():
            tk.Label(
                card.body,
                text=text,
                bg=theme.WHITE,
                fg=theme.INK,
                font=theme.font(14),
                wraplength=width,
                justify="left",
                anchor="w",
            ).pack(anchor="w")
        attachment = message.get("attachment") or {}
        if attachment.get("name"):
            chip = tk.Frame(card.body, bg=theme.CHIP)
            chip.pack(anchor="w", pady=(8, 0) if text.strip() else 0)
            tk.Label(chip, image=visuals.icon_photo("paperclip", 11, theme.MUTED), bg=theme.CHIP).pack(
                side="left", padx=(6, 4), pady=3
            )
            tk.Label(
                chip,
                text=_short(attachment["name"], 30),
                bg=theme.CHIP,
                fg=theme.INK_SOFT,
                font=theme.font(10, "medium"),
            ).pack(side="left", padx=(0, 7), pady=3)

    def _image_card(self, parent, meta: dict):
        path = Path(meta.get("path", ""))
        if not path.exists():
            return
        card = Card(parent, radius=13, fill=theme.CHIP, outline=None, thickness=0, bg=theme.WHITE, padding=8)
        card.pack(anchor="w", pady=(7, 6))
        photo = visuals.art_photo(path, 320, 214, 10)
        image = tk.Label(card.body, image=photo, bg=theme.CHIP, bd=0, cursor="hand2")
        image.pack()
        image.bind("<Button-1>", lambda e, m=meta: self.app.show_artifact(m))
        caption = tk.Label(
            card.body,
            text="Takoyo Art · " + _short(meta.get("prompt", ""), 48),
            bg=theme.CHIP,
            fg=theme.MUTED,
            font=theme.font(9),
            anchor="w",
        )
        caption.pack(fill="x", pady=(6, 2))
        actions = tk.Frame(card.body, bg=theme.CHIP)
        actions.pack(fill="x")
        TextButton(actions, "View", command=lambda m=meta: self.app.show_artifact(m), variant="secondary", size=9, height=26).pack(side="left")
        TextButton(actions, "Save", command=lambda m=meta: self.app.save_artifact(m), variant="secondary", size=9, height=26).pack(
            side="left", padx=6
        )

    def _proposal_card(self, parent, edit: dict, message_index: int):
        applied = bool(edit.get("applied"))
        card = Card(parent, radius=13, fill=theme.CHIP, outline=None, thickness=0, bg=theme.WHITE, padding=13)
        card.pack(anchor="w", pady=(7, 6))
        head = tk.Frame(card.body, bg=theme.CHIP)
        head.pack(fill="x")
        tk.Label(head, image=visuals.icon_photo("code", 15, theme.INK), bg=theme.CHIP).pack(side="left", padx=(0, 7))
        tk.Label(
            head,
            text=edit.get("path", "file"),
            bg=theme.CHIP,
            fg=theme.INK,
            font=theme.font(11, "semibold"),
        ).pack(side="left")
        if applied:
            tk.Label(head, image=visuals.icon_photo("check", 13, theme.SUCCESS), bg=theme.CHIP).pack(side="right")
            tk.Label(head, text="Applied", bg=theme.CHIP, fg=theme.SUCCESS, font=theme.font(9, "medium")).pack(
                side="right", padx=(0, 4)
            )
        else:
            TextButton(
                head,
                "Apply",
                command=lambda: self.app.apply_proposal(edit, message_index),
                variant="primary",
                size=9,
                height=27,
            ).pack(side="right")
        content = edit.get("content", "")
        lines = content.splitlines()
        preview = "\n".join(lines[:9])
        if len(lines) > 9:
            preview += f"\n… {len(lines) - 9} more lines"
        tk.Label(
            card.body,
            text=preview,
            bg=theme.CHIP,
            fg=theme.INK_SOFT,
            font=theme.mono(10),
            justify="left",
            anchor="w",
            wraplength=max(300, min(620, (self.winfo_width() or 900) - 190)),
        ).pack(fill="x", pady=(7, 0))

    def _error_card(self, parent, message: str, kind: str):
        card = Card(parent, radius=13, fill="#FFF7F6", outline="#F1D3D0", thickness=1.2, bg=theme.WHITE, padding=13)
        card.pack(anchor="w", pady=(7, 6))
        head = tk.Frame(card.body, bg="#FFF7F6")
        head.pack(fill="x")
        tk.Label(head, image=visuals.icon_photo("close", 13, theme.DANGER), bg="#FFF7F6").pack(side="left", padx=(0, 6))
        tk.Label(head, text="Takoyo hit a snag", bg="#FFF7F6", fg=theme.DANGER, font=theme.font(10, "semibold")).pack(side="left")
        tk.Label(
            card.body,
            text=message,
            bg="#FFF7F6",
            fg=theme.INK_SOFT,
            font=theme.font(11),
            wraplength=max(320, min(600, (self.winfo_width() or 900) - 220)),
            justify="left",
            anchor="w",
        ).pack(fill="x", pady=(6, 2))
        if kind in ("local", "provider", "image"):
            label = "Set up built-in AI" if kind == "local" else "Open settings"
            TextButton(card.body, label, command=self.app.open_settings, variant="secondary", size=9, height=27).pack(
                anchor="w", pady=(5, 0)
            )

    # ------------------------------------------------------------------ #
    def start_stream(self):
        row = self._message_row()
        tk.Label(row, image=visuals.mascot_photo(25), bg=theme.WHITE, bd=0).pack(side="left", anchor="n", padx=(0, 9), pady=(1, 0))
        column = tk.Frame(row, bg=theme.WHITE)
        column.pack(side="left", fill="both", expand=True)
        self._live_label = tk.Label(
            column,
            text="  Takoyo is thinking…",
            image=visuals.mascot_photo(17),
            compound="left",
            bg=theme.WHITE,
            fg=theme.MUTED,
            font=theme.font(12),
        )
        self._live_label.pack(anchor="w", pady=(2, 0))
        self._live_text = RichText(column, text="", size=15, bg=theme.WHITE)
        self._scroll_if_bottom()
        self.app.root.after(50, self.scroll_to_bottom)

    def append_stream(self, text: str):
        if not self._live_text:
            return
        try:
            if not self._live_text.winfo_exists():
                return
        except Exception:
            return
        if self._live_label is not None:
            try:
                self._live_label.destroy()
            except Exception:
                pass
            self._live_label = None
            self._live_text.pack(fill="x", anchor="w")
        was_bottom = self._is_bottom()
        self._live_text.append(text)
        if was_bottom:
            self.app.root.after(20, self.scroll_to_bottom)

    def _is_bottom(self) -> bool:
        try:
            _first, last = self.scroll.canvas.yview()
            return last >= 0.96
        except Exception:
            return True

    def _scroll_if_bottom(self):
        if self._is_bottom():
            self.scroll.scroll_to_bottom()

    def _tick_scroll(self):
        try:
            if not self.winfo_exists():
                return
            first, last = self.scroll.canvas.yview()
            bbox = self.scroll.canvas.bbox("all")
            tall = bool(bbox and bbox[3] - bbox[1] > self.scroll.canvas.winfo_height() + 8)
            show = tall and first > 0.001 and last < 0.999
            if show and not self._scroll_visible:
                self.scroll_btn.place(relx=1.0, rely=1.0, x=-18, y=-10, anchor="se")
                self._scroll_visible = True
            elif not show and self._scroll_visible:
                self.scroll_btn.place_forget()
                self._scroll_visible = False
        except Exception:
            pass
        try:
            self.after(320, self._tick_scroll)
        except Exception:
            pass


# --------------------------------------------------------------------------- #
# Art panel
# --------------------------------------------------------------------------- #
class ArtPanel(tk.Frame):
    def __init__(self, master, app):
        super().__init__(master, bg=theme.WHITE)
        self.app = app
        self._meta: dict | None = None
        self._generating = False
        self._photo = None
        self._size = (0, 0)
        self._render_job = None
        self._pulse_job = None
        self._tools_visible = False

        self.card = Card(self, radius=22, fill=theme.WHITE, outline=theme.INK, thickness=1.6, bg=theme.WHITE, padding=14)
        self.card.pack(fill="both", expand=True, padx=(0, 16), pady=16)
        self.stage = tk.Frame(self.card.body, bg=theme.CHIP)
        self.stage.pack(fill="both", expand=True)
        self.canvas = tk.Canvas(self.stage, bg=theme.CHIP, highlightthickness=0, bd=0)
        self.canvas.pack(fill="both", expand=True)

        self.state_frame = tk.Frame(self.canvas, bg=theme.CHIP)
        self.state_mascot = tk.Label(self.state_frame, image=visuals.mascot_photo(92), bg=theme.CHIP, bd=0)
        self.state_mascot.pack(pady=(0, 8))
        self.state_title = tk.Label(self.state_frame, text="Takoyo Art", bg=theme.CHIP, fg=theme.INK, font=theme.font(17, "semibold"))
        self.state_title.pack()
        self.state_sub = tk.Label(
            self.state_frame,
            text="Images you make will appear here",
            bg=theme.CHIP,
            fg=theme.MUTED,
            font=theme.font(11),
            wraplength=300,
            justify="center",
        )
        self.state_sub.pack(pady=(4, 0))

        self.tools = tk.Frame(self.stage, bg=theme.WHITE, bd=0)
        self.prompt_chip = tk.Label(
            self.stage, text="", bg=theme.WHITE, fg=theme.INK_SOFT, font=theme.font(9, "medium"), padx=10, pady=6
        )
        self._tool_buttons = []
        for icon, tip, command in (
            ("external", "Open image", self._open),
            ("download", "Save a copy", self._save),
            ("refresh", "Paint it again", self._regenerate),
            ("close", "Hide art panel", lambda: self.app.show_art(False)),
        ):
            button = IconButton(
                self.tools,
                icon,
                16,
                command=command,
                color=theme.INK_SOFT,
                hover_color=theme.INK,
                bg=theme.WHITE,
                pad=6,
                tooltip=tip,
            )
            button.pack(side="left", padx=1)
            self._tool_buttons.append(button)

        self.stage.bind("<Configure>", self._on_configure)
        self.stage.bind("<Enter>", lambda e: self._set_tools(True))
        self.stage.bind("<Leave>", lambda e: self.after(30, self._check_tools))
        self.state_frame.bind("<Enter>", lambda e: self._set_tools(False))
        self.set_empty()

    # ------------------------------------------------------------------ #
    def _on_configure(self, event=None):
        size = (self.stage.winfo_width(), self.stage.winfo_height())
        if size == self._size or size[0] < 40 or size[1] < 40:
            return
        self._size = size
        if self._render_job:
            try:
                self.after_cancel(self._render_job)
            except Exception:
                pass
        self._render_job = self.after(40, self._render)

    def _render(self):
        self._render_job = None
        w, h = self.stage.winfo_width(), self.stage.winfo_height()
        if w < 40 or h < 40:
            return
        if self._meta and Path(self._meta.get("path", "")).exists():
            self.canvas.delete("image")
            self._photo = visuals.art_photo(self._meta["path"], w - 2, h - 2, 16)
            self.canvas.create_image(1, 1, anchor="nw", image=self._photo, tags="image")
            self.canvas.tag_lower("image")
            self._place_state(False)
            self._set_tools(self._tools_visible)
        else:
            self.canvas.delete("image")
            self._place_state(True)

    def _place_state(self, visible: bool):
        if visible:
            self.state_frame.place(relx=0.5, rely=0.5, anchor="center")
        else:
            self.state_frame.place_forget()

    def _size_state(self):
        try:
            self.state_window.destroy()
        except Exception:
            pass

    # ------------------------------------------------------------------ #
    def set_empty(self):
        self._meta = None
        self._generating = False
        self._stop_pulse()
        self.canvas.delete("all")
        self.state_title.configure(text="Takoyo Art")
        self.state_sub.configure(text="Images you make will appear here")
        self._place_state(True)
        self._set_tools(False)
        self._size = (0, 0)

    def set_generating(self, prompt: str):
        self._generating = True
        self.canvas.delete("image")
        self.state_title.configure(text="Painting")
        self.state_sub.configure(text=_short(prompt, 60))
        self._place_state(True)
        self._set_tools(False)
        self._pulse()

    def set_artifact(self, meta: dict):
        self._meta = meta
        self._generating = False
        self._stop_pulse()
        self.prompt_chip.configure(text=_short(meta.get("prompt", "Takoyo Art"), 48))
        self._size = (0, 0)
        self._on_configure()
        self._render()
        self._set_tools(True)

    def finish_generating(self):
        self._generating = False
        self._stop_pulse()
        if self._meta:
            self.set_artifact(self._meta)
        else:
            self.set_empty()

    # ------------------------------------------------------------------ #
    def _set_tools(self, visible: bool):
        if visible and self._meta:
            if not self.tools.winfo_ismapped():
                self.tools.place(relx=1.0, rely=1.0, x=-14, y=-14, anchor="se")
                self.prompt_chip.place(relx=0.0, rely=1.0, x=14, y=-14, anchor="sw")
            self._tools_visible = True
        else:
            self.tools.place_forget()
            self.prompt_chip.place_forget()
            self._tools_visible = False

    def _check_tools(self):
        try:
            x, y = self.stage.winfo_pointerxy()
            inside = (
                self.stage.winfo_rootx() <= x <= self.stage.winfo_rootx() + self.stage.winfo_width()
                and self.stage.winfo_rooty() <= y <= self.stage.winfo_rooty() + self.stage.winfo_height()
            )
        except Exception:
            inside = False
        if not inside:
            self._set_tools(False)

    def _pulse(self):
        if not self._generating:
            return
        dots = "." * (int(time.time() * 2) % 3 + 1)
        try:
            self.state_title.configure(text="Painting" + dots)
            self._pulse_job = self.after(420, self._pulse)
        except Exception:
            self._pulse_job = None

    def _stop_pulse(self):
        if self._pulse_job:
            try:
                self.after_cancel(self._pulse_job)
            except Exception:
                pass
            self._pulse_job = None

    def _open(self):
        if self._meta:
            _open_path(self._meta.get("path", ""))

    def _save(self):
        if self._meta:
            self.app.save_artifact(self._meta)

    def _regenerate(self):
        if self._meta:
            self.app.regenerate_artifact(self._meta)


# --------------------------------------------------------------------------- #
# The app
# --------------------------------------------------------------------------- #
class TakoyoApp:
    def __init__(self, root: tk.Tk, config: Config | None = None, start_api: bool = False, api_host=None, api_port=None):
        self.root = root
        self.config = config or Config()
        self.projects = ProjectStore()
        self.conversations = ConversationStore()
        self.images = ImageGenerator(self.config)
        self.engine = get_engine(self.config)
        self.queue: queue.Queue = queue.Queue()
        self.sidebar_query = ""
        self.current_project: dict | None = None
        self.current_conversation: dict | None = None
        self.settings = None
        self.files_dialog = None
        self.api_server = None
        self._api_args = (api_host, api_port)
        self.streaming = False
        self.cancel_token = None
        self._stream_cid = ""
        self._stream_index = -1
        self._image_jobs = 0
        self._art_visible = False
        self._closed = False
        self._install_state: dict | None = None
        self._test_serial = 0

        self.root.title("Takoyo")
        self.root.configure(bg=theme.WHITE)
        try:
            self._icon_photo = visuals.app_icon_photo(64)
            self.root.iconphoto(True, self._icon_photo)
        except Exception:
            self._icon_photo = None
        self.root.minsize(1020, 640)
        geometry = str(self.config.get("window_geometry", "1280x800") or "1280x800")
        if not re.match(r"^\d+x\d+([+-]\d+[+-]\d+)?$", geometry):
            geometry = "1280x800"
        self.root.geometry(geometry)
        self.root.grid_rowconfigure(0, weight=1)
        self.root.grid_columnconfigure(1, weight=1)

        self.sidebar = Sidebar(self.root, self)

        self.main = tk.Frame(self.root, bg=theme.WHITE)
        self.main.grid(row=0, column=1, sticky="nsew")
        self.header = Header(self.main, self)
        self.header.pack(fill="x")
        self.body = tk.Frame(self.main, bg=theme.WHITE)
        self.body.pack(fill="both", expand=True)
        self.body.grid_rowconfigure(0, weight=1)
        self.body.grid_columnconfigure(0, weight=1)

        self.home = HomeView(self.body, self)
        self.chat = ChatView(self.body, self)
        self.art = ArtPanel(self.body, self)
        self.composers = [self.home.composer, self.chat.composer]

        self.home.grid(row=0, column=0, sticky="nsew")
        self.chat.grid(row=0, column=0, sticky="nsew")
        self.home.lift()
        self.art.grid_forget()

        pid = str(self.config.get("last_project", "") or "")
        if pid:
            meta = self.projects.get(pid)
            if meta:
                self.current_project = meta
        self.home.set_project(self.current_project)
        self.app_visible_sidebar = bool(self.config.get("sidebar_open", False))
        if self.app_visible_sidebar:
            self.sidebar.grid_forget()
            self.sidebar.grid(row=0, column=0, sticky="ns")
            self.header.set_sidebar(True)
        self.refresh_sidebar()
        self.home.focus()

        self.root.protocol("WM_DELETE_WINDOW", self.on_close)
        self.root.bind("<Control-n>", lambda e: self.new_chat())
        self.root.bind("<Control-comma>", lambda e: self.open_settings())
        self.root.bind("<Control-b>", lambda e: self.toggle_sidebar())
        self.root.bind("<Escape>", self._escape)
        self._drain_queue()

        if start_api or bool(self.config.get("api_auto_start", False)):
            self.start_api_server(*self._api_args)

    # ------------------------------------------------------------------ #
    # State
    # ------------------------------------------------------------------ #
    def toast(self, text: str, icon: str = "check"):
        try:
            Toast(self.root, text, icon=icon)
        except Exception:
            pass

    def provider_status(self) -> tuple[str, str]:
        provider = str(self.config.get("provider", "local") or "local").lower()
        if provider == "local":
            key = self.config.model_for("local")
            ready = self.engine.engine_installed() and self.engine.model_installed(key)
            name = model_catalog.local_model(key).name
            if ready:
                return f"{name} · ready", theme.SUCCESS
            return "Built-in AI · finish setup", theme.DANGER
        label = model_catalog.PROVIDERS.get(provider, {}).get("label", provider.title())
        model = self.config.model_for(provider)
        return f"{label} · {_short(model, 22)}", theme.SUCCESS if self.config.api_key(provider) else theme.MUTED

    def refresh_sidebar(self):
        try:
            self.sidebar.refresh()
        except Exception:
            pass

    def _sync_busy(self):
        busy = self.streaming or self._image_jobs > 0
        label = "Thinking…" if self.streaming else "Painting…"
        for composer in self.composers:
            try:
                composer.set_thinking(busy, label=label)
            except Exception:
                pass

    # ------------------------------------------------------------------ #
    # Views
    # ------------------------------------------------------------------ #
    def show_home(self):
        if self._art_visible:
            self.show_art(False)
        self.home.grid(row=0, column=0, sticky="nsew")
        self.home.lift()
        self.home.set_project(self.current_project)
        self.home.focus()

    def show_chat(self):
        self.chat.grid(row=0, column=0, sticky="nsew")
        self.chat.lift()
        if self.current_conversation:
            project = self.projects.get(self.current_conversation.get("project_id", "")) if self.current_conversation.get("project_id") else None
            self.chat.set_project(project)
        self.chat.composer.focus()

    def toggle_sidebar(self):
        self.app_visible_sidebar = not self.app_visible_sidebar
        if self.app_visible_sidebar:
            self.sidebar.grid_forget()
            self.sidebar.grid(row=0, column=0, sticky="ns")
        else:
            self.sidebar.grid_forget()
        self.header.set_sidebar(self.app_visible_sidebar)
        self.config.set("sidebar_open", self.app_visible_sidebar)

    def show_art(self, visible: bool, silent: bool = False):
        if visible == self._art_visible:
            return
        if visible:
            if self.root.winfo_width() < 1120 and not silent:
                self.toast("Widen the window to keep the art panel open", "image")
                return
            self.body.grid_columnconfigure(0, weight=3, minsize=380)
            self.body.grid_columnconfigure(1, weight=2, minsize=360)
            self.art.grid_forget()
            self.art.grid(row=0, column=1, sticky="nsew")
            self.art.lift()
            self._art_visible = True
        else:
            self.art.grid_forget()
            self.body.grid_columnconfigure(0, weight=1, minsize=0)
            self.body.grid_columnconfigure(1, weight=0, minsize=0)
            self._art_visible = False
        self.header.set_art(self._art_visible)

    def toggle_art(self):
        if not self._art_visible:
            latest = self.images.latest()
            if latest:
                self.art.set_artifact(latest)
            else:
                self.art.set_empty()
        self.show_art(not self._art_visible)

    def show_artifact(self, meta: dict):
        self.art.set_artifact(meta)
        self.show_art(True)

    def save_artifact(self, meta: dict):
        path = Path(meta.get("path", ""))
        if not path.exists():
            self.toast("That image is missing", "close")
            return
        target = filedialog.asksaveasfilename(
            parent=self.root,
            title="Save image",
            initialfile=path.name,
            defaultextension=".png",
            filetypes=[("PNG image", "*.png"), ("All files", "*.*")],
        )
        if not target:
            return
        try:
            shutil.copy2(path, target)
            self.toast("Image saved")
        except Exception as exc:
            self.toast(f"Could not save: {exc}", "close")

    def regenerate_artifact(self, meta: dict):
        prompt = meta.get("prompt") or ""
        if not prompt:
            return
        self.art.set_generating(prompt)
        self.show_art(True)
        self._image_jobs += 1
        threading.Thread(target=self._image_worker, args=(prompt, "", -1, False), daemon=True).start()

    # ------------------------------------------------------------------ #
    # Conversations
    # ------------------------------------------------------------------ #
    def new_chat(self, project_id: str | None = None):
        if self.streaming:
            self.toast("Stop the current reply first", "stop")
            return
        self.current_conversation = None
        self.config.set("last_conversation", "")
        self.chat.clear()
        self.show_home()
        self.refresh_sidebar()

    def load_conversation(self, cid: str):
        conv = self.conversations.get(cid)
        if not conv:
            self.toast("That chat is no longer here", "close")
            return
        self.current_conversation = conv
        self.config.set("last_conversation", cid)
        pid = conv.get("project_id", "")
        self.current_project = self.projects.get(pid) if pid else None
        if pid and not self.current_project:
            self.current_project = None
        if self.current_project:
            self.config.set("last_project", self.current_project.get("id", ""))
        if conv.get("messages"):
            self.show_chat()
            self.chat.refresh(conv)
        else:
            self.show_home()
        self.refresh_sidebar()

    def open_project(self, meta: dict):
        if self.streaming:
            self.toast("Stop the current reply first", "stop")
            return
        self.current_project = meta
        self.config.set("last_project", meta.get("id", ""))
        latest = self.conversations.latest_for_project(meta.get("id", ""))
        if latest and latest.get("messages"):
            self.current_conversation = latest
            self.config.set("last_conversation", latest.get("id", ""))
            self.show_chat()
            self.chat.refresh(latest)
            self.chat.scroll_to_bottom()
        else:
            self.current_conversation = None
            self.config.set("last_conversation", "")
            self.chat.clear()
            self.show_home()
        self.refresh_sidebar()

    def close_project(self):
        self.current_project = None
        self.config.set("last_project", "")
        self.current_conversation = None
        self.config.set("last_conversation", "")
        self.chat.clear()
        self.show_home()
        self.refresh_sidebar()

    def project_menu(self, meta: dict, anchor):
        from .dialogs import ask_confirm, ask_text

        menu = tk.Menu(self.root, tearoff=0, font=theme.font(11), bg=theme.WHITE, fg=theme.INK, activebackground=theme.CHIP, activeforeground=theme.INK, bd=0)
        menu.add_command(label="New chat here", command=lambda: (self.open_project(meta), self.new_chat()))
        menu.add_command(label="Open files", command=lambda: (self.open_project(meta), self.open_project_files()))
        menu.add_separator()
        menu.add_command(label="Rename", command=lambda: self.rename_project_dialog(meta))
        menu.add_command(label="Delete project", command=lambda: self.delete_project_dialog(meta))
        try:
            menu.tk_popup(anchor.winfo_rootx(), anchor.winfo_rooty() + anchor.winfo_height())
        finally:
            menu.grab_release()

    def rename_project_dialog(self, meta: dict):
        from .dialogs import ask_text

        name = ask_text(self, "Rename project", "Project name", meta.get("name", ""))
        if not name:
            return
        updated = self.projects.rename(meta.get("id", ""), name)
        if updated:
            if self.current_project and self.current_project.get("id") == updated.get("id"):
                self.current_project = updated
                self.home.set_project(updated)
                if self.current_conversation:
                    self.chat.set_project(updated)
            self.refresh_sidebar()
            self.toast("Project renamed")

    def delete_project_dialog(self, meta: dict):
        from .dialogs import ask_confirm

        if not ask_confirm(
            self,
            "Delete project?",
            f"“{meta.get('name', 'Project')}” will move to Takoyo's trash folder. You can still recover it from there.",
            confirm="Move to trash",
            danger=True,
        ):
            return
        pid = meta.get("id", "")
        try:
            self.projects.delete(pid)
        except Exception as exc:
            self.toast(f"Could not delete: {exc}", "close")
            return
        if self.current_project and self.current_project.get("id") == pid:
            self.close_project()
        self.refresh_sidebar()
        self.toast("Project moved to trash")

    def conversation_menu(self, summary: dict, anchor):
        from .dialogs import ask_confirm, ask_text

        menu = tk.Menu(self.root, tearoff=0, font=theme.font(11), bg=theme.WHITE, fg=theme.INK, activebackground=theme.CHIP, activeforeground=theme.INK, bd=0)
        menu.add_command(
            label="Rename",
            command=lambda: self.rename_conversation_dialog(summary),
        )
        menu.add_command(label="Delete chat", command=lambda: self.delete_conversation_dialog(summary))
        try:
            menu.tk_popup(anchor.winfo_rootx(), anchor.winfo_rooty() + anchor.winfo_height())
        finally:
            menu.grab_release()

    def rename_conversation_dialog(self, summary: dict):
        from .dialogs import ask_text

        title = ask_text(self, "Rename chat", "Chat title", summary.get("title", "New chat"))
        if not title:
            return
        self.conversations.rename(summary["id"], title)
        if self.current_conversation and self.current_conversation.get("id") == summary["id"]:
            self.current_conversation = self.conversations.get(summary["id"])
        self.refresh_sidebar()

    def delete_conversation_dialog(self, summary: dict):
        from .dialogs import ask_confirm

        if not ask_confirm(self, "Delete chat?", f"“{summary.get('title', 'Chat')}” will move to Takoyo's trash.", confirm="Move to trash", danger=True):
            return
        self.conversations.delete(summary["id"])
        if self.current_conversation and self.current_conversation.get("id") == summary["id"]:
            self.current_conversation = None
            self.chat.clear()
            self.show_home()
        self.refresh_sidebar()
        self.toast("Chat moved to trash")

    def on_sidebar_search(self, query: str):
        self.sidebar_query = (query or "").strip()
        self.refresh_sidebar()

    # ------------------------------------------------------------------ #
    # Sending
    # ------------------------------------------------------------------ #
    def _ensure_conversation(self, text: str) -> dict:
        if self.current_conversation:
            return self.current_conversation
        pid = self.current_project.get("id", "") if self.current_project else ""
        conv = self.conversations.create(first_message=text or "New chat", project_id=pid)
        self.current_conversation = conv
        self.config.set("last_conversation", conv["id"])
        return conv

    def send_from_composer(self, composer: Composer):
        if self.streaming or self._image_jobs > 0:
            self.toast("Takoyo is still working — stop it first", "stop")
            return
        text = composer.get_text().strip()
        attachment = composer.attachment
        if not text and not attachment:
            return
        composer.clear()
        self._send(text, attachment)

    def _send(self, text: str, attachment: dict | None):
        display = text
        if not display and attachment:
            display = f"Attached {attachment.get('name', 'a file')}"
        conv = self._ensure_conversation(display)
        cid = conv["id"]
        extra = {"attachment": attachment} if attachment else {}
        conv = self.conversations.append(cid, "user", display, **extra)
        if not conv:
            self.toast("Could not save this chat", "close")
            return
        self.current_conversation = conv
        if not self.current_project and conv.get("project_id"):
            self.current_project = self.projects.get(conv["project_id"])
        self.show_chat()
        self.chat.refresh(conv)
        self.chat.scroll_to_bottom()
        self.refresh_sidebar()

        prompt = image_request_prompt(text)
        if prompt:
            self._start_direct_image(cid, prompt, display)
            return

        index = len(conv["messages"])
        history = [dict(m) for m in conv["messages"]]
        conv = self.conversations.append(cid, "assistant", "")
        self.current_conversation = conv
        self._stream_cid = cid
        self._stream_index = index
        self.cancel_token = ai_providers.CancelToken()
        self.streaming = True
        self.chat.start_stream()
        self._sync_busy()
        project = self.current_project or {}
        threading.Thread(
            target=self._chat_worker,
            args=(cid, index, history, text, attachment, project.get("id", ""), project.get("name", "")),
            daemon=True,
        ).start()

    def _chat_worker(self, cid, index, history, prompt_text, attachment, project_id, project_name):
        token = self.cancel_token
        parts: list[str] = []
        try:
            context = ""
            if project_id:
                context = self.projects.context_for_ai(project_id, prompt_text)
            system = prompting.build_system_prompt(project_name, context, project_id)
            cleaned_history = []
            for message in history:
                role = message.get("role")
                content = message.get("content") or ""
                if role not in ("user", "assistant"):
                    continue
                att = message.get("attachment")
                if att and att.get("name"):
                    content += prompting.attachment_note(att["name"], att.get("content", ""))
                if not content.strip():
                    continue
                cleaned_history.append({"role": role, "content": content})
            messages = prompting.build_messages(system, cleaned_history)
            for piece in ai_providers.stream_chat(self.config, messages, cancel=token):
                if token is not None and token.is_set():
                    break
                parts.append(piece)
                self.queue.put(("chunk", cid, piece))
            cancelled = bool(token is not None and token.is_set())
            self.queue.put(("done", cid, index, "".join(parts), cancelled))
        except LocalAINotReady as exc:
            self.queue.put(("error", cid, index, str(exc), "local"))
        except (LocalAIError,) as exc:
            self.queue.put(("error", cid, index, str(exc), "local"))
        except ai_providers.ProviderError as exc:
            self.queue.put(("error", cid, index, str(exc), "provider"))
        except Exception as exc:
            traceback.print_exc()
            self.queue.put(("error", cid, index, f"{type(exc).__name__}: {exc}", "error"))

    def stop_streaming(self):
        if self.streaming and self.cancel_token is not None:
            self.cancel_token.cancel()
            self.toast("Stopping…", "stop")

    # ------------------------------------------------------------------ #
    # Queue
    # ------------------------------------------------------------------ #
    def _drain_queue(self):
        try:
            for _ in range(240):
                try:
                    item = self.queue.get_nowait()
                except queue.Empty:
                    break
                try:
                    self._dispatch(item)
                except Exception:
                    traceback.print_exc()
        finally:
            if not self._closed:
                try:
                    self.root.after(25, self._drain_queue)
                except Exception:
                    pass

    def _dispatch(self, item):
        kind = item[0]
        if kind == "chunk":
            _, cid, text = item
            if self.current_conversation and self.current_conversation.get("id") == cid and self.chat._live_text is not None:
                self.chat.append_stream(text)
            return
        if kind == "done":
            _, cid, index, full, cancelled = item
            self._finish_reply(cid, index, full, cancelled)
            return
        if kind == "error":
            _, cid, index, message, error_kind = item
            self._finish_error(cid, index, message, error_kind)
            return
        if kind == "image_ready":
            _, meta, cid, index, prompt, direct = item
            self._finish_image(meta, cid, index, prompt, direct)
            return
        if kind == "image_error":
            _, message, cid, index, direct = item
            self._fail_image(message, cid, index, direct)
            return
        if kind == "voice":
            _, text = item
            composer = self.chat.composer if self.chat.winfo_ismapped() else self.home.composer
            composer.set_text((composer.get_text() + " " + text).strip())
            return
        if kind == "voice_error":
            _, message = item
            self.toast(message, "mic")
            return
        if kind == "install_progress":
            _, fraction, message = item
            self._install_state = {"fraction": fraction, "message": message}
            if self.settings is not None:
                self.settings.on_install_progress(fraction, message)
            return
        if kind == "install_done":
            _, key = item
            self._install_state = None
            if self.settings is not None:
                self.settings.on_install_done(key)
            self.refresh_sidebar()
            self.toast("Built-in AI is ready")
            return
        if kind == "install_error":
            _, message = item
            self._install_state = None
            if self.settings is not None:
                self.settings.on_install_error(message)
            self.toast("Install failed", "close")
            return
        if kind == "test_result":
            _, serial, ok, message = item
            if self.settings is not None:
                self.settings.on_test_result(serial, ok, message)
            return

    def _finish_reply(self, cid: str, index: int, full: str, cancelled: bool):
        conv = self.conversations.get(cid)
        if not conv or not (0 <= index < len(conv["messages"])):
            self.streaming = False
            self.cancel_token = None
            self._sync_busy()
            return
        cleaned, proposals, prompts = parse_reply(full)
        if not cleaned.strip():
            if prompts:
                cleaned = f"Made you an image of **{prompts[0]}**."
            elif proposals:
                cleaned = "I've prepared the change — apply it when you're ready."
            elif cancelled:
                cleaned = "_Stopped._"
        fields = {"content": cleaned, "edits": proposals, "images": conv["messages"][index].get("images") or []}
        conv = self.conversations.update_message(cid, index, **fields)
        conv_pid = conv.get("project_id") or None
        if proposals and conv_pid and self.config.get("auto_apply_edits"):
            for edit in proposals:
                if self._write_project_file(edit["path"], edit["content"], project_id=conv_pid):
                    edit["applied"] = True
            conv = self.conversations.update_message(cid, index, edits=proposals)

        self.streaming = False
        self.cancel_token = None
        if self.current_conversation and self.current_conversation.get("id") == cid:
            self.current_conversation = conv
            self.chat.refresh(conv)
            self.chat.scroll_to_bottom()
        if len(prompts) > 0:
            self.art.set_generating(prompts[0])
            self.show_art(True, silent=True)
            for prompt in prompts[:3]:
                self._begin_image(prompt, cid, index, False)
        self._sync_busy()
        self.refresh_sidebar()

    def _finish_error(self, cid: str, index: int, message: str, error_kind: str):
        conv = self.conversations.get(cid)
        if conv and 0 <= index < len(conv["messages"]):
            conv = self.conversations.update_message(cid, index, content="", error=message, error_kind=error_kind)
            if self.current_conversation and self.current_conversation.get("id") == cid:
                self.current_conversation = conv
                self.chat.refresh(conv)
                self.chat.scroll_to_bottom()
        self.streaming = False
        self.cancel_token = None
        self._sync_busy()
        self.toast("Takoyo hit a snag", "close")

    # ------------------------------------------------------------------ #
    # Images
    # ------------------------------------------------------------------ #
    def _start_direct_image(self, cid: str, prompt: str, display: str):
        conv = self.conversations.append(cid, "assistant", "Painting…")
        if not conv:
            return
        index = len(conv["messages"]) - 1
        self.current_conversation = conv
        self.chat.refresh(conv)
        self.chat.scroll_to_bottom()
        self.art.set_generating(prompt)
        self.show_art(True, silent=True)
        self._begin_image(prompt, cid, index, True)

    def _begin_image(self, prompt: str, cid: str, index: int, direct: bool):
        self._image_jobs += 1
        self._sync_busy()
        threading.Thread(target=self._image_worker, args=(prompt, cid, index, direct), daemon=True).start()

    def _image_worker(self, prompt: str, cid: str, index: int, direct: bool):
        try:
            meta = self.images.generate(prompt)
            self.queue.put(("image_ready", meta, cid, index, prompt, direct))
        except Exception as exc:
            message = str(exc)
            self.queue.put(("image_error", message, cid, index, direct))

    def _finish_image(self, meta: dict, cid: str, index: int, prompt: str, direct: bool):
        self._image_jobs = max(0, self._image_jobs - 1)
        if cid:
            conv = self.conversations.get(cid)
            if conv and 0 <= index < len(conv["messages"]):
                message = conv["messages"][index]
                images = list(message.get("images") or [])
                images.append(meta)
                fields = {"images": images, "error": None}
                if direct:
                    fields["content"] = f"Here’s **{_short(prompt, 80)}** — painted with Takoyo Art. Click the image to open it full size."
                conv = self.conversations.update_message(cid, index, **fields)
                if self.current_conversation and self.current_conversation.get("id") == cid:
                    self.current_conversation = conv
                    self.chat.refresh(conv)
                    self.chat.scroll_to_bottom()
        self.art.set_artifact(meta)
        self.show_art(True, silent=True)
        self._sync_busy()
        self.toast("Image ready", "image")
        self.refresh_sidebar()

    def _fail_image(self, message: str, cid: str, index: int, direct: bool):
        self._image_jobs = max(0, self._image_jobs - 1)
        if cid:
            conv = self.conversations.get(cid)
            if conv and 0 <= index < len(conv["messages"]):
                fields = {"error": message, "error_kind": "image"}
                if direct:
                    fields["content"] = ""
                conv = self.conversations.update_message(cid, index, **fields)
                if self.current_conversation and self.current_conversation.get("id") == cid:
                    self.current_conversation = conv
                    self.chat.refresh(conv)
        self.art.finish_generating()
        if not self._art_visible:
            self.show_art(False)
        self._sync_busy()
        self.toast("Image generation failed", "close")

    # ------------------------------------------------------------------ #
    # Editing projects
    # ------------------------------------------------------------------ #
    def _write_project_file(self, path: str, content: str, project_id: str | None = None) -> bool:
        pid = project_id or (self.current_project or {}).get("id")
        if not pid:
            return False
        try:
            self.projects.write_text(pid, path, content)
            return True
        except Exception as exc:
            self.toast(f"Could not save {path}: {exc}", "close")
            return False

    def apply_edit_from_chat(self, filename: str, code: str):
        if not self.current_project:
            self.toast("Open a project before applying code", "folder")
            return
        from .dialogs import ask_confirm

        exists = False
        try:
            target = self.projects.safe_path(self.current_project["id"], filename)
            exists = target.exists()
        except Exception as exc:
            self.toast(str(exc), "close")
            return
        question = f"Overwrite “{filename}” with the new version?" if exists else f"Create “{filename}” in {self.current_project.get('name', 'the project')}?"
        if not ask_confirm(self, "Apply this code?", question, confirm="Save file"):
            return
        if self._write_project_file(filename, code):
            self.toast(f"Saved {filename}")
            if self.files_dialog is not None:
                self.files_dialog.reload()
            self.refresh_sidebar()

    def apply_proposal(self, edit: dict, message_index: int):
        conv = self.current_conversation
        pid = (conv or {}).get("project_id") or None
        if not pid:
            self.toast("This chat isn't linked to a project", "folder")
            return
        from .dialogs import ask_confirm

        project_name = self.current_project.get("name") if self.current_project else pid
        path = edit.get("path", "")
        exists = False
        try:
            target = self.projects.safe_path(pid, path)
            exists = target.exists()
        except Exception as exc:
            self.toast(str(exc), "close")
            return
        question = f"Replace “{path}” in {project_name}?" if exists else f"Create “{path}”?"
        if not ask_confirm(self, "Apply edit?", question + "\n\nTakoyo's version is a complete file, so the old contents are replaced.", confirm="Apply"):
            return
        if not self._write_project_file(path, edit.get("content", ""), project_id=pid):
            return
        if conv and 0 <= message_index < len(conv["messages"]):
            edits = list(conv["messages"][message_index].get("edits") or [])
            for item in edits:
                if item.get("path") == path:
                    item["applied"] = True
            conv = self.conversations.update_message(conv["id"], message_index, edits=edits)
            self.current_conversation = conv
            self.chat.refresh(conv)
        self.toast(f"Saved {path}")
        if self.files_dialog is not None and self.current_project and self.current_project.get("id") == pid:
            self.files_dialog.reload()
        self.refresh_sidebar()

    def new_project_dialog(self):
        from .dialogs import NewProjectDialog

        NewProjectDialog(self, self.root)

    def open_project_files(self):
        from .dialogs import ProjectFilesDialog

        if not self.current_project:
            self.toast("Open a project first", "folder")
            return
        if self.files_dialog is not None:
            try:
                self.files_dialog.lift()
                self.files_dialog.focus_force()
                return
            except Exception:
                self.files_dialog = None
        self.files_dialog = ProjectFilesDialog(self, self.root)

    # ------------------------------------------------------------------ #
    # Settings & API
    # ------------------------------------------------------------------ #
    def open_settings(self):
        from .dialogs import SettingsDialog

        if self.settings is not None:
            try:
                self.settings.lift()
                self.settings.focus_force()
                return
            except Exception:
                self.settings = None
        self.settings = SettingsDialog(self, self.root)

    def on_settings_saved(self):
        self.refresh_sidebar()
        if self.current_project:
            meta = self.projects.get(self.current_project.get("id", ""))
            if meta:
                self.current_project = meta
                self.home.set_project(meta)

    def start_api_server(self, host=None, port=None):
        if self.api_server is not None:
            return
        try:
            from .api import start_api

            server, _thread = start_api(config=self.config, host=host, port=port)
            self.api_server = server
            address = server.server_address
            self.config.update({"api_host": address[0], "api_port": address[1]})
            self.toast(f"API listening on {address[0]}:{address[1]}", "code")
            self.refresh_sidebar()
        except Exception as exc:
            self.toast(f"API could not start: {exc}", "close")

    def stop_api_server(self):
        if self.api_server is None:
            return
        try:
            from .api import stop_api

            stop_api(self.api_server)
        except Exception:
            pass
        self.api_server = None
        self.refresh_sidebar()
        self.toast("API stopped", "stop")

    def test_provider(self, settings, provider: str):
        self._test_serial += 1
        serial = self._test_serial

        def worker():
            try:
                reply = ai_providers.test_connection(self.config, provider)
                self.queue.put(("test_result", serial, True, reply))
            except Exception as exc:
                self.queue.put(("test_result", serial, False, str(exc)))

        threading.Thread(target=worker, daemon=True).start()

    # ------------------------------------------------------------------ #
    # Voice
    # ------------------------------------------------------------------ #
    def start_voice(self, composer: Composer):
        self.toast("Listening…", "mic")

        def worker():
            try:
                import speech_recognition as sr
            except Exception:
                self.queue.put(("voice_error", "Voice input needs SpeechRecognition and PyAudio. Install them, then try again."))
                return
            try:
                recognizer = sr.Recognizer()
                with sr.Microphone() as source:
                    recognizer.adjust_for_ambient_noise(source, duration=0.4)
                    audio = recognizer.listen(source, timeout=6, phrase_time_limit=15)
                text = recognizer.recognize_google(audio)
                self.queue.put(("voice", text))
            except Exception as exc:
                self.queue.put(("voice_error", f"Voice input did not catch that: {exc}"))

        threading.Thread(target=worker, daemon=True).start()

    # ------------------------------------------------------------------ #
    def _escape(self, _event=None):
        if self.settings is not None:
            self.settings.close()
        elif self.files_dialog is not None:
            self.files_dialog.close()

    def on_close(self):
        if self._closed:
            return
        self._closed = True
        try:
            if self.cancel_token is not None:
                self.cancel_token.cancel()
        except Exception:
            pass
        try:
            self.config.set("window_geometry", self.root.geometry())
            self.config.set("sidebar_open", self.app_visible_sidebar)
        except Exception:
            pass
        try:
            if self.api_server is not None:
                from .api import stop_api

                stop_api(self.api_server)
        except Exception:
            pass
        try:
            self.engine.shutdown()
        except Exception:
            pass
        try:
            self.root.destroy()
        except Exception:
            pass


# --------------------------------------------------------------------------- #
def main(start_api: bool = False, api_host=None, api_port=None):
    theme.enable_dpi_awareness()
    theme.load_fonts()
    root = tk.Tk()
    app = TakoyoApp(root, Config(), start_api=start_api, api_host=api_host, api_port=api_port)
    root.mainloop()
    return app


if __name__ == "__main__":
    main()
