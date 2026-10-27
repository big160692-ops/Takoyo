"""Takoyo dialogs: settings, project creation and a small project code editor."""

from __future__ import annotations

import os
import re
import threading
import tkinter as tk
import webbrowser
from pathlib import Path
from tkinter import filedialog

from . import model_catalog, theme, visuals
from .ai_providers import CancelToken
from .local_ai import LocalAIError
from .model_catalog import DALLE_SIZES, IMAGE_PROVIDERS, IMAGE_SIZES, LOCAL_MODELS, local_model, local_model_by_name
from .widgets import (
    Card,
    IconButton,
    LabeledField,
    ScrollArea,
    SelectField,
    Switch,
    TextButton,
    Tooltip,
)

_BG = theme.WHITE


# --------------------------------------------------------------------------- #
# Base dialog
# --------------------------------------------------------------------------- #
class BaseDialog(tk.Toplevel):
    def __init__(self, app, master, title: str, width: int, height: int):
        super().__init__(master)
        self.app = app
        self._closed = False
        self.withdraw()
        self.overrideredirect(True)
        self.configure(bg=theme.INK)
        self._dw, self._dh = self._clamp(width, height)
        outer = tk.Frame(self, bg=theme.INK)
        outer.pack(fill="both", expand=True)
        self.shell = tk.Frame(outer, bg=_BG)
        self.shell.pack(fill="both", expand=True, padx=1, pady=1)
        self.geometry(f"{self._dw}x{self._dh}")
        self.update_idletasks()
        self._center(master)
        try:
            self.transient(master)
        except Exception:
            pass
        self.protocol("WM_DELETE_WINDOW", self.close)
        self.bind("<Escape>", lambda e: self.close())
        self.deiconify()
        self.update_idletasks()
        self.lift()
        try:
            self.grab_set()
        except Exception:
            pass
        self.after(60, self.focus_force)

    def _clamp(self, width: int, height: int) -> tuple[int, int]:
        try:
            sw = self.winfo_screenwidth()
            sh = self.winfo_screenheight()
        except Exception:
            return width, height
        return min(width, max(360, sw - 80)), min(height, max(280, sh - 80))

    def _center(self, master):
        try:
            w, h = self._dw, self._dh
            if master is not None and master.winfo_ismapped():
                x = master.winfo_rootx() + (master.winfo_width() - w) // 2
                y = master.winfo_rooty() + (master.winfo_height() - h) // 2
            else:
                x = (self.winfo_screenwidth() - w) // 2
                y = (self.winfo_screenheight() - h) // 2
            x = max(20, min(x, self.winfo_screenwidth() - w - 20))
            y = max(20, min(y, self.winfo_screenheight() - h - 60))
            self.geometry(f"+{x}+{y}")
        except Exception:
            pass

    def make_draggable(self, *widgets):
        for widget in widgets:
            widget.bind("<ButtonPress-1>", self._drag_start, add="+")
            widget.bind("<B1-Motion>", self._drag_move, add="+")

    def _drag_start(self, event):
        self._drag = (event.x_root - self.winfo_x(), event.y_root - self.winfo_y())

    def _drag_move(self, event):
        if not hasattr(self, "_drag"):
            return
        dx, dy = self._drag
        self.geometry(f"+{event.x_root - dx}+{event.y_root - dy}")

    def header(self, title: str, subtitle: str = "") -> tk.Frame:
        head = tk.Frame(self.shell, bg=_BG)
        head.pack(fill="x", padx=24, pady=(18, 4))
        tk.Label(head, text=title, bg=_BG, fg=theme.INK, font=theme.font(21, "semibold")).pack(side="left")
        IconButton(
            head,
            "close",
            16,
            command=self.close,
            color=theme.MUTED,
            hover_color=theme.INK,
            bg=_BG,
            pad=5,
            tooltip="Close",
        ).pack(side="right")
        if subtitle:
            tk.Label(
                self.shell,
                text=subtitle,
                bg=_BG,
                fg=theme.MUTED,
                font=theme.font(10),
                justify="left",
                anchor="w",
                wraplength=self._dw - 60,
            ).pack(fill="x", padx=24, pady=(0, 8))
        self.make_draggable(head)
        for child in head.winfo_children():
            if isinstance(child, tk.Label):
                self.make_draggable(child)
        return head

    def close(self):
        if self._closed:
            return
        self._closed = True
        try:
            self.grab_release()
        except Exception:
            pass
        try:
            self.destroy()
        except Exception:
            pass


# --------------------------------------------------------------------------- #
# Tiny prompts
# --------------------------------------------------------------------------- #
class TextPromptDialog(BaseDialog):
    def __init__(self, app, title: str, label: str, initial: str = ""):
        super().__init__(app, app.root, title, 440, 268)
        self.result: str | None = None
        self.header(title)
        self.field = LabeledField(self.shell, label=label, value=initial, height=46, on_enter=self._ok)
        self.field.pack(fill="x", padx=24, pady=(6, 4))
        footer = tk.Frame(self.shell, bg=_BG)
        footer.pack(side="bottom", fill="x", padx=24, pady=(10, 20))
        TextButton(footer, "Cancel", command=self.close, variant="secondary", size=11).pack(side="right")
        TextButton(footer, "Save", command=self._ok, variant="primary", size=11).pack(side="right", padx=(0, 8))
        self.bind("<Return>", lambda e: self._ok())
        self.after(90, self.field.focus)

    def _ok(self):
        value = self.field.get().strip()
        if not value:
            return
        self.result = value
        self.close()


class ConfirmDialog(BaseDialog):
    def __init__(self, app, title: str, message: str, confirm: str = "Continue", danger: bool = False):
        super().__init__(app, app.root, title, 480, 306)
        self.result = False
        self.header(title)
        tk.Label(
            self.shell,
            text=message,
            bg=_BG,
            fg=theme.INK_SOFT,
            font=theme.font(11),
            justify="left",
            anchor="w",
            wraplength=416,
        ).pack(fill="x", padx=24, pady=(6, 8))
        footer = tk.Frame(self.shell, bg=_BG)
        footer.pack(side="bottom", fill="x", padx=24, pady=(10, 20))
        TextButton(footer, "Cancel", command=self.close, variant="secondary", size=11).pack(side="right")
        TextButton(
            footer,
            confirm,
            command=self._yes,
            variant="danger" if danger else "primary",
            size=11,
        ).pack(side="right", padx=(0, 8))
        self.bind("<Return>", lambda e: self._yes())

    def _yes(self):
        self.result = True
        self.close()


def ask_text(app, title: str, label: str, initial: str = "") -> str | None:
    dialog = TextPromptDialog(app, title, label, initial)
    app.root.wait_window(dialog)
    return dialog.result


def ask_confirm(app, title: str, message: str, confirm: str = "Continue", danger: bool = False) -> bool:
    dialog = ConfirmDialog(app, title, message, confirm=confirm, danger=danger)
    app.root.wait_window(dialog)
    return dialog.result


# --------------------------------------------------------------------------- #
# New project
# --------------------------------------------------------------------------- #
class NewProjectDialog(BaseDialog):
    def __init__(self, app, master):
        super().__init__(app, master, "New project", 500, 402)
        self.header("New project", "Takoyo creates a real folder on this computer, so it can build and edit the files with you.")
        self.name = LabeledField(self.shell, label="Project name", value="", placeholder="My new website", on_enter=self._create)
        self.name.pack(fill="x", padx=24, pady=(12, 12))
        self.description = LabeledField(
            self.shell, label="What is it? (optional)", value="", placeholder="A portfolio with a gallery", on_enter=self._create
        )
        self.description.pack(fill="x", padx=24)
        footer = tk.Frame(self.shell, bg=_BG)
        footer.pack(side="bottom", fill="x", padx=24, pady=(10, 20))
        TextButton(footer, "Cancel", command=self.close, variant="secondary", size=11).pack(side="right")
        TextButton(footer, "Create project", command=self._create, variant="primary", size=11).pack(side="right", padx=(0, 8))
        self.after(90, self.name.focus)

    def _create(self):
        name = self.name.get().strip()
        if not name:
            self.app.toast("Give the project a name first", "folder")
            self.name.focus()
            return
        try:
            meta = self.app.projects.create(name, self.description.get().strip())
        except Exception as exc:
            self.app.toast(f"Could not create the project: {exc}", "close")
            return
        self.close()
        self.app.open_project(meta)
        self.app.toast("Project created")


# --------------------------------------------------------------------------- #
# Settings
# --------------------------------------------------------------------------- #
class SettingsDialog(BaseDialog):
    def __init__(self, app, master):
        super().__init__(app, master, "Settings", 724, 748)
        self.provider_key = str(app.config.get("provider", "local") or "local").lower()
        self._image_label_to_key = {info["label"]: key for key, info in IMAGE_PROVIDERS.items()}
        self._install_token: CancelToken | None = None
        self._installing = False
        self._provider_rows: dict[str, dict] = {}
        self._progress = 0.0

        self.header("Settings", "Choose Takoyo's brain, how it paints, and how much it can touch on this computer.")

        footer = tk.Frame(self.shell, bg=_BG)
        footer.pack(side="bottom", fill="x", padx=24, pady=(10, 18))
        tk.Frame(footer, bg=theme.LINE_SOFT, height=1).pack(fill="x", pady=(0, 12))
        row = tk.Frame(footer, bg=_BG)
        row.pack(fill="x")
        TextButton(row, "Open Takoyo folder", command=lambda: _open_folder(self.app), variant="ghost", size=9, height=30).pack(side="left")
        TextButton(row, "Cancel", command=self.close, variant="secondary", size=11).pack(side="right")
        TextButton(row, "Save changes", command=lambda: self._save(close=True), variant="primary", size=11).pack(
            side="right", padx=(0, 8)
        )

        self.scroll = ScrollArea(self.shell, bg=_BG)
        self.scroll.pack(fill="both", expand=True, padx=(24, 12), pady=(4, 0))
        content = self.scroll.inner

        # -- provider ------------------------------------------------------ #
        _section(content, "CHAT", "Takoyo's brain", "Use the built-in model for unlimited offline chats, or connect a cloud model with your own key.")
        provider_card = Card(content, radius=16, fill=_BG, outline=theme.LINE_SOFT, thickness=1.2, bg=_BG, padding=6)
        provider_card.pack(fill="x", pady=(6, 0))
        for key in model_catalog.PROVIDER_ORDER:
            info = model_catalog.PROVIDERS[key]
            selected = key == self.provider_key
            row = HoverRow(provider_card.body, command=lambda k=key: self._select_provider(k), base=theme.CHIP if selected else _BG)
            row.pack(fill="x")
            inner = tk.Frame(row, bg=theme.CHIP if selected else _BG)
            inner.pack(fill="x", padx=10, pady=7)
            tk.Label(inner, image=visuals.icon_photo(info.get("icon", "sparkle"), 17, theme.INK), bg=theme.CHIP if selected else _BG).pack(
                side="left", padx=(2, 9)
            )
            texts = tk.Frame(inner, bg=theme.CHIP if selected else _BG)
            texts.pack(side="left", fill="x", expand=True)
            tk.Label(
                texts,
                text=info["label"],
                bg=theme.CHIP if selected else _BG,
                fg=theme.INK,
                font=theme.font(12, "medium"),
                anchor="w",
            ).pack(fill="x")
            tk.Label(
                texts,
                text=info.get("hint", ""),
                bg=theme.CHIP if selected else _BG,
                fg=theme.MUTED,
                font=theme.font(9),
                anchor="w",
            ).pack(fill="x")
            check = tk.Label(inner, bg=theme.CHIP if selected else _BG)
            if selected:
                check.configure(image=visuals.icon_photo("check", 15, theme.INK))
            check.pack(side="right", padx=(6, 2))
            row.bind_tree()
            self._provider_rows[key] = {"row": row, "check": check}

        self.conn_box = tk.Frame(content, bg=_BG)
        self.conn_box.pack(fill="x", pady=(12, 0))
        self._build_connection()

        # -- built in ------------------------------------------------------ #
        _section(content, "BUILT-IN AI", "Takoyo's own model", "Downloads llama.cpp plus an open Qwen model. Once installed, chats and images never touch the internet.")
        self.builtin_card = Card(content, radius=16, fill=theme.CHIP, outline=None, thickness=0, bg=_BG, padding=15)
        self.builtin_card.pack(fill="x", pady=(6, 0))
        body = self.builtin_card.body
        self.engine_status = tk.Label(body, text="", bg=theme.CHIP, fg=theme.INK, font=theme.font(11, "semibold"), anchor="w")
        self.engine_status.pack(fill="x")
        self.model_status = tk.Label(body, text="", bg=theme.CHIP, fg=theme.MUTED, font=theme.font(10), anchor="w")
        self.model_status.pack(fill="x", pady=(2, 10))
        self.local_select = SelectField(
            body,
            label="Install model",
            value=local_model(app.config.model_for("local")).name,
            options=[m.name for m in LOCAL_MODELS.values()],
            editable=False,
            bg=theme.CHIP,
        )
        self.local_select.pack(fill="x", pady=(0, 10))
        self.progress_canvas = tk.Canvas(body, height=9, bg=theme.CHIP, highlightthickness=0, bd=0)
        self.progress_canvas.pack(fill="x", pady=(0, 5))
        self.progress_canvas.bind("<Configure>", lambda e: self._draw_progress())
        self.progress_label = tk.Label(body, text="", bg=theme.CHIP, fg=theme.MUTED, font=theme.font(9), anchor="w", justify="left")
        self.progress_label.pack(fill="x")
        actions = tk.Frame(body, bg=theme.CHIP)
        actions.pack(fill="x", pady=(9, 0))
        self.install_btn = TextButton(actions, "Download & install", command=self._install, variant="primary", size=10, height=32)
        self.install_btn.pack(side="left")
        self.cancel_install_btn = TextButton(actions, "Cancel", command=self._cancel_install, variant="secondary", size=10, height=32)
        self.cancel_install_btn.pack(side="left", padx=(8, 0))
        self.cancel_install_btn.pack_forget()
        self._draw_progress()
        self._refresh_builtin()

        # -- images -------------------------------------------------------- #
        _section(content, "IMAGES", "Make unlimited art", "Takoyo Art paints original images on this PC with no limits. DALL·E 3 is available if you prefer it.")
        image_row = tk.Frame(content, bg=_BG)
        image_row.pack(fill="x", pady=(6, 0))
        image_row.grid_columnconfigure(0, weight=1)
        image_row.grid_columnconfigure(1, weight=1)
        current_image = str(app.config.get("image_provider", "takoyo") or "takoyo")
        self.image_engine = SelectField(
            image_row,
            label="Image engine",
            value=IMAGE_PROVIDERS.get(current_image, IMAGE_PROVIDERS["takoyo"])["label"],
            options=[info["label"] for info in IMAGE_PROVIDERS.values()],
            editable=False,
        )
        self.image_engine.grid(row=0, column=0, sticky="ew", padx=(0, 7))
        self.image_size = SelectField(
            image_row,
            label="Size",
            value=str(app.config.get("image_size", "1024x1280")),
            options=IMAGE_SIZES + [s for s in DALLE_SIZES if s not in IMAGE_SIZES],
            editable=False,
        )
        self.image_size.grid(row=0, column=1, sticky="ew", padx=(7, 0))
        tk.Label(
            content,
            text="Takoyo Art: unlimited, offline, no key. DALL·E 3 uses your OpenAI key.",
            bg=_BG,
            fg=theme.MUTED,
            font=theme.font(9),
            anchor="w",
        ).pack(fill="x", pady=(6, 0))

        # -- safety -------------------------------------------------------- #
        _section(content, "PERMISSIONS", "What Takoyo may do", "Everything is local. Project writes stay inside the folder you opened.")
        self.code_switch = self._switch_row(
            content,
            "Allow Python and pip via the API",
            "The local /run endpoint can execute short scripts inside the workspace.",
            bool(app.config.get("allow_code_execution", True)),
        )
        self.auto_switch = self._switch_row(
            content,
            "Apply project edits automatically",
            "When off, Takoyo proposes each file and waits for your Apply click.",
            bool(app.config.get("auto_apply_edits", False)),
        )

        # -- api ----------------------------------------------------------- #
        _section(content, "LOCAL API", "Use Takoyo from other apps", "An OpenAI-compatible endpoint on this computer. It has no login, so keep the host on 127.0.0.1.")
        self.api_port = LabeledField(content, label="Port", value=str(app.config.get("api_port", 8765)), height=44)
        self.api_port.pack(fill="x", pady=(6, 8))
        api_row = tk.Frame(content, bg=_BG)
        api_row.pack(fill="x")
        self.api_status = tk.Label(api_row, text="", bg=_BG, fg=theme.MUTED, font=theme.font(10), anchor="w")
        self.api_status.pack(side="left")
        self.api_toggle_btn = TextButton(api_row, "Start API", command=self._toggle_api, variant="secondary", size=10, height=32)
        self.api_toggle_btn.pack(side="right")
        self.api_url = tk.Label(
            content, text="", bg=_BG, fg=theme.BLUE, font=theme.font(9, "medium"), anchor="w", cursor="hand2"
        )
        self.api_url.pack(fill="x", pady=(6, 0))
        self.api_url.bind("<Button-1>", lambda e: self._copy_api())
        self._refresh_api()

        # -- about --------------------------------------------------------- #
        tk.Frame(content, bg=theme.LINE_SOFT, height=1).pack(fill="x", pady=(18, 10))
        tk.Label(
            content,
            text="Takoyo 1.0 · your chats, projects and images live in ~/.takoyo",
            bg=_BG,
            fg=theme.FAINT,
            font=theme.font(9),
        ).pack(anchor="w", pady=(0, 16))

    # ------------------------------------------------------------------ #
    def _select_provider(self, key: str):
        if key == self.provider_key:
            return
        self.provider_key = key
        for name, parts in self._provider_rows.items():
            selected = name == key
            base = theme.CHIP if selected else _BG
            parts["row"]._base = base
            parts["row"]._set(base)
            if selected:
                parts["check"].configure(image=visuals.icon_photo("check", 15, theme.INK))
            else:
                parts["check"].configure(image="")
        self._build_connection()
        self._refresh_builtin()

    def _build_connection(self):
        for child in self.conn_box.winfo_children():
            child.destroy()
        provider = self.provider_key
        info = model_catalog.PROVIDERS[provider]
        if provider == "local":
            names = [m.name for m in LOCAL_MODELS.values()]
            value = local_model(self.app.config.model_for("local")).name
        else:
            names = info["models"]
            value = self.app.config.model_for(provider) or (names[0] if names else "")
        self.model = SelectField(self.conn_box, label="Model", value=value, options=names, editable=True)
        self.model.pack(fill="x", pady=(0, 10))
        self.key_field = None
        self.base_field = None
        if provider == "ollama":
            self.base_field = LabeledField(
                self.conn_box,
                label="Ollama server",
                value=str(self.app.config.get("ollama_base_url", "http://127.0.0.1:11434")),
                height=44,
            )
            self.base_field.pack(fill="x", pady=(0, 10))
        elif provider != "local":
            self.key_field = LabeledField(
                self.conn_box,
                label=f"{info['label']} API key",
                value=self.app.config.api_key(provider),
                placeholder="Paste your key",
                secret=True,
                height=44,
                hint="Stored only on this computer.",
            )
            self.key_field.pack(fill="x", pady=(0, 10))
            url = info.get("key_url", "")
            if url:
                link = tk.Label(
                    self.conn_box,
                    text="Get a key ↗",
                    bg=_BG,
                    fg=theme.BLUE,
                    font=theme.font(10, "medium"),
                    cursor="hand2",
                )
                link.pack(anchor="w", pady=(0, 9))
                link.bind("<Button-1>", lambda e, u=url: webbrowser.open(u))
        if provider == "local":
            ready = self.app.engine.engine_installed() and self.app.engine.model_installed(self.app.config.model_for("local"))
            hint = "Runs completely on this computer. Unlimited messages, no key." if ready else "Not installed yet — use the download below, or pick a cloud model."
            color = theme.MUTED if ready else theme.DANGER
        else:
            hint = info.get("hint", "")
            color = theme.MUTED
        tk.Label(self.conn_box, text=hint, bg=_BG, fg=color, font=theme.font(9), anchor="w", justify="left", wraplength=600).pack(
            fill="x", pady=(0, 10)
        )
        test_row = tk.Frame(self.conn_box, bg=_BG)
        test_row.pack(fill="x")
        TextButton(test_row, "Test connection", command=self._test, variant="secondary", size=10, height=32).pack(side="left")
        self.test_status = tk.Label(test_row, text="", bg=_BG, fg=theme.MUTED, font=theme.font(9), anchor="w")
        self.test_status.pack(side="left", padx=(10, 0))

    # ------------------------------------------------------------------ #
    def _refresh_builtin(self):
        engine_ok = self.app.engine.engine_installed()
        active_key = self.app.config.model_for("local")
        active_ok = self.app.engine.model_installed(active_key)
        self.engine_status.configure(
            text=("Engine installed" if engine_ok else "Engine not installed") + " · " + ("model ready" if active_ok else "model missing"),
            fg=theme.INK if engine_ok and active_ok else theme.DANGER,
        )
        installed = self.app.engine.installed_models()
        if installed:
            self.model_status.configure(text="Installed: " + ", ".join(local_model(k).name for k in installed))
        else:
            self.model_status.configure(text="Nothing downloaded yet. About 0.5–2 GB depending on the model.")
        try:
            self.local_select.set(local_model(active_key).name)
        except Exception:
            pass
        self.install_btn.set_text("Reinstall model" if engine_ok and active_ok else "Download & install")
        self.install_btn.set_enabled(not self._installing)
        if engine_ok and active_ok and not self.progress_label.cget("text"):
            self._progress = 1.0
            self._draw_progress()
            self.progress_label.configure(text="Takoyo's brain is installed and ready.")

    def _draw_progress(self):
        canvas = getattr(self, "progress_canvas", None)
        if canvas is None:
            return
        canvas.delete("all")
        w = canvas.winfo_width()
        if w < 6:
            canvas.after(40, self._draw_progress)
            return
        y = 4
        canvas.create_line(4, y, w - 4, y, fill=theme.SEARCH_BG, width=8, capstyle="round")
        if self._progress > 0:
            canvas.create_line(4, y, max(8, 4 + (w - 8) * self._progress), y, fill=theme.INK, width=8, capstyle="round")

    def _install(self):
        if self._installing:
            return
        chosen = local_model_by_name(self.local_select.get())
        if not chosen:
            return
        self._installing = True
        self._install_token = CancelToken()
        self.install_btn.set_enabled(False)
        self.cancel_install_btn.pack(side="left", padx=(8, 0))
        self.progress_label.configure(text=f"Preparing {chosen.name}…", fg=theme.MUTED)
        key = chosen.key
        token = self._install_token
        app = self.app

        def worker():
            try:
                app.engine.install(
                    key,
                    progress=lambda fraction, message: app.queue.put(("install_progress", fraction, message)),
                    cancel=token,
                )
                app.queue.put(("install_done", key))
            except Exception as exc:
                app.queue.put(("install_error", str(exc)))

        threading.Thread(target=worker, daemon=True).start()

    def _cancel_install(self):
        if self._install_token is not None:
            self._install_token.cancel()
            self.progress_label.configure(text="Cancelling after this chunk…")

    def on_install_progress(self, fraction: float, message: str):
        self._progress = max(0.0, min(1.0, float(fraction or 0)))
        try:
            self._draw_progress()
        except Exception:
            pass
        self.progress_label.configure(text=message, fg=theme.MUTED)

    def on_install_done(self, key: str):
        self._installing = False
        self._install_token = None
        self._progress = 1.0
        try:
            self._draw_progress()
        except Exception:
            pass
        self.progress_label.configure(text=f"{local_model(key).name} is installed and selected.", fg=theme.SUCCESS)
        self.cancel_install_btn.pack_forget()
        self.install_btn.set_enabled(True)
        self.install_btn.set_text("Reinstall model")
        self._refresh_builtin()
        if self.provider_key == "local":
            try:
                self.model.set(local_model(key).name)
            except Exception:
                pass

    def on_install_error(self, message: str):
        self._installing = False
        self._install_token = None
        self.cancel_install_btn.pack_forget()
        self.install_btn.set_enabled(True)
        self.progress_label.configure(text=message, fg=theme.DANGER)

    # ------------------------------------------------------------------ #
    def _test(self):
        if not self._save(silent=True):
            return
        try:
            self.test_status.configure(text="Testing…", fg=theme.MUTED)
        except Exception:
            return
        self.app.test_provider(self, self.provider_key)

    def on_test_result(self, serial: int, ok: bool, message: str):
        try:
            if ok:
                self.test_status.configure(text="Connected · " + _short(message, 40), fg=theme.SUCCESS)
                self.app.toast("Connection works")
            else:
                self.test_status.configure(text=_short(message, 76), fg=theme.DANGER)
                self.app.toast("Connection failed", "close")
        except Exception:
            pass

    # ------------------------------------------------------------------ #
    def _switch_row(self, parent, title: str, subtitle: str, value: bool) -> Switch:
        row = tk.Frame(parent, bg=_BG)
        row.pack(fill="x", pady=5)
        text = tk.Frame(row, bg=_BG)
        text.pack(side="left", fill="x", expand=True)
        tk.Label(text, text=title, bg=_BG, fg=theme.INK, font=theme.font(11, "medium"), anchor="w").pack(fill="x")
        tk.Label(text, text=subtitle, bg=_BG, fg=theme.MUTED, font=theme.font(9), anchor="w", justify="left").pack(fill="x")
        switch = Switch(row, value=value, bg=_BG)
        switch.pack(side="right", padx=(12, 2))
        return switch

    # ------------------------------------------------------------------ #
    def _save(self, silent: bool = False, close: bool = False) -> bool:
        provider = self.provider_key
        values: dict = {"provider": provider}
        model_value = self.model.get().strip()
        if not model_value:
            self.app.toast("Choose a model first", "close")
            return False
        if provider == "local":
            chosen = local_model_by_name(model_value)
            if chosen:
                values["local_model"] = chosen.key
        else:
            values[f"{provider}_model"] = model_value
            if provider == "ollama" and self.base_field is not None:
                values["ollama_base_url"] = self.base_field.get().strip() or "http://127.0.0.1:11434"
            elif self.key_field is not None:
                values[f"{provider}_api_key"] = self.key_field.get().strip()
        image_key = self._image_label_to_key.get(self.image_engine.get(), "takoyo")
        values["image_provider"] = image_key
        values["image_size"] = self.image_size.get().strip() or "1024x1280"
        values["allow_code_execution"] = bool(self.code_switch.get())
        values["auto_apply_edits"] = bool(self.auto_switch.get())
        try:
            port = int(self.api_port.get().strip() or 8765)
            if not (1024 <= port <= 65535):
                raise ValueError
        except Exception:
            self.app.toast("The API port must be between 1024 and 65535", "close")
            return False
        values["api_port"] = port
        self.app.config.update(values)
        self.app.on_settings_saved()
        if not silent:
            self.app.toast("Settings saved")
        if close:
            self.close()
        return True

    # ------------------------------------------------------------------ #
    def _refresh_api(self):
        running = self.app.api_server is not None
        if running:
            address = self.app.api_server.server_address
            self.api_status.configure(text=f"Running on {address[0]}:{address[1]}", fg=theme.SUCCESS)
            self.api_toggle_btn.set_text("Stop API")
            self.api_url.configure(text=f"http://{address[0]}:{address[1]}/v1 · click to copy")
        else:
            self.api_status.configure(text="Not running", fg=theme.MUTED)
            self.api_toggle_btn.set_text("Start API")
            self.api_url.configure(text="")
        try:
            self.api_toggle_btn.set_enabled(True)
        except Exception:
            pass

    def _toggle_api(self):
        if self.app.api_server is not None:
            self.app.stop_api_server()
            self._refresh_api()
            return
        if not self._save(silent=True):
            return
        try:
            port = int(self.api_port.get().strip() or 8765)
        except Exception:
            port = 8765
        self.app.start_api_server(None, port)
        self._refresh_api()

    def _copy_api(self):
        if self.app.api_server is None:
            return
        address = self.app.api_server.server_address
        url = f"http://{address[0]}:{address[1]}/v1"
        try:
            self.clipboard_clear()
            self.clipboard_append(url)
            self.app.toast("API URL copied")
        except Exception:
            pass

    def close(self):
        if self.app.settings is self:
            self.app.settings = None
        super().close()


# --------------------------------------------------------------------------- #
# Project files
# --------------------------------------------------------------------------- #
class ProjectFilesDialog(BaseDialog):
    def __init__(self, app, master):
        super().__init__(app, master, "Project files", 1000, 680)
        self.project = app.current_project or {}
        self.project_id = self.project.get("id", "")
        self.selected: str | None = None
        self._rows: dict[str, tk.Frame] = {}
        self._file_count = 0
        self.header(
            self.project.get("name", "Project"),
            f"Project files · {app.projects.root(self.project_id)}",
        )
        self.shell.bind("<Control-s>", lambda e: self._save())

        body = tk.Frame(self.shell, bg=_BG)
        body.pack(fill="both", expand=True, padx=20, pady=(4, 18))
        body.grid_rowconfigure(0, weight=1)
        body.grid_columnconfigure(0, weight=0, minsize=278)
        body.grid_columnconfigure(1, weight=1)

        left = tk.Frame(body, bg=_BG, width=278)
        left.grid(row=0, column=0, sticky="nsew", padx=(0, 14))
        left.grid_propagate(False)
        toolbar = tk.Frame(left, bg=_BG)
        toolbar.pack(fill="x", pady=(0, 6))
        tk.Label(toolbar, text="FILES", bg=_BG, fg=theme.INK, font=theme.font(10, "semibold")).pack(side="left")
        self.count_label = tk.Label(toolbar, text="", bg=_BG, fg=theme.MUTED, font=theme.font(9))
        self.count_label.pack(side="left", padx=(7, 0))
        IconButton(toolbar, "refresh", 14, command=self.reload, color=theme.MUTED, hover_color=theme.INK, bg=_BG, pad=4, tooltip="Refresh").pack(side="right")
        IconButton(toolbar, "plus", 16, command=self._new_file, color=theme.INK, hover_color=theme.INK, bg=_BG, hover_bg=None, pad=3, tooltip="New file").pack(side="right", padx=(0, 3))
        self.list_area = ScrollArea(left, bg=_BG)
        self.list_area.pack(fill="both", expand=True)
        self.rows_box = self.list_area.inner

        right = tk.Frame(body, bg=_BG)
        right.grid(row=0, column=1, sticky="nsew")
        right.grid_rowconfigure(1, weight=1)
        right.grid_columnconfigure(0, weight=1)
        head = tk.Frame(right, bg=_BG)
        head.grid(row=0, column=0, sticky="ew", pady=(0, 6))
        self.path_label = tk.Label(head, text="Choose a file", bg=_BG, fg=theme.MUTED, font=theme.font(11, "medium"), anchor="w")
        self.path_label.pack(side="left", fill="x", expand=True)
        self.delete_btn = TextButton(head, "Delete", command=self._delete, variant="danger", size=9, height=29)
        self.delete_btn.pack(side="right")
        self.delete_btn.set_enabled(False)
        self.save_btn = TextButton(head, "Save", command=self._save, variant="primary", size=9, height=29)
        self.save_btn.pack(side="right", padx=(0, 7))
        self.save_btn.set_enabled(False)
        IconButton(head, "external", 15, command=self._open_folder, color=theme.MUTED, hover_color=theme.INK, bg=_BG, pad=5, tooltip="Open folder").pack(side="right", padx=(0, 5))

        editor_frame = tk.Frame(right, bg=theme.WHITE, highlightthickness=1, highlightbackground=theme.LINE_SOFT, bd=0)
        editor_frame.grid(row=1, column=0, sticky="nsew")
        editor_frame.grid_rowconfigure(0, weight=1)
        editor_frame.grid_columnconfigure(0, weight=1)
        self.editor = tk.Text(
            editor_frame,
            bd=0,
            relief="flat",
            bg=theme.WHITE,
            fg=theme.INK,
            insertbackground=theme.INK,
            font=theme.mono(13),
            wrap="none",
            undo=True,
            padx=14,
            pady=12,
            spacing1=1,
            spacing3=2,
            selectbackground=theme.CHIP,
            selectforeground=theme.INK,
            highlightthickness=0,
        )
        self.editor.grid(row=0, column=0, sticky="nsew")
        yscroll = tk.Scrollbar(editor_frame, orient="vertical", command=self.editor.yview, width=10, bd=0, relief="flat", highlightthickness=0)
        yscroll.grid(row=0, column=1, sticky="ns")
        xscroll = tk.Scrollbar(editor_frame, orient="horizontal", command=self.editor.xview, bd=0, relief="flat", highlightthickness=0)
        xscroll.grid(row=1, column=0, sticky="ew")
        self.editor.configure(yscrollcommand=yscroll.set, xscrollcommand=xscroll.set)
        self.editor.bind("<Tab>", self._tab)
        self.placeholder = tk.Label(
            editor_frame,
            text="Choose a file on the left,\nor create a new one with +.",
            bg=theme.WHITE,
            fg=theme.FAINT,
            font=theme.font(13),
            justify="center",
        )
        self.placeholder.place(relx=0.5, rely=0.45, anchor="center")
        self.editor.configure(state="disabled")
        self.reload()

    # ------------------------------------------------------------------ #
    def _tab(self, event):
        if str(self.editor.cget("state")) == "disabled":
            return "break"
        self.editor.insert("insert", "    ")
        return "break"

    def reload(self):
        meta = self.app.projects.get(self.project_id)
        if not meta:
            self.app.toast("This project no longer exists", "close")
            self.close()
            return
        self.project = meta
        for child in self.rows_box.winfo_children():
            child.destroy()
        self._rows.clear()
        files = self.app.projects.files(self.project_id)
        self._file_count = len(files)
        self.count_label.configure(text=f"{len(files)}")
        if not files:
            tk.Label(
                self.rows_box,
                text="Empty project\nAsk Takoyo to build something.",
                bg=_BG,
                fg=theme.FAINT,
                font=theme.font(10),
                justify="left",
                anchor="w",
            ).pack(fill="x", padx=8, pady=8)
        for item in files:
            self._file_row(item)
        if self.selected and self.selected in self._rows:
            self._select_row(self.selected)
            self._load(self.selected, keep=True)
        elif self.selected:
            self._clear_editor()

    def _file_row(self, item: dict):
        rel = item["path"]
        selected = rel == self.selected
        base = theme.CHIP if selected else _BG
        row = HoverRow(self.rows_box, command=lambda p=rel: self._load(p), base=base)
        row.pack(fill="x", pady=1)
        inner = tk.Frame(row, bg=base)
        inner.pack(fill="x", padx=8, pady=5)
        tk.Label(inner, image=visuals.icon_photo("file", 14, theme.MUTED), bg=base).pack(side="left", padx=(2, 7))
        texts = tk.Frame(inner, bg=base)
        texts.pack(side="left", fill="x", expand=True)
        tk.Label(texts, text=Path(rel).name, bg=base, fg=theme.INK, font=theme.font(10, "medium"), anchor="w").pack(fill="x")
        parent = str(Path(rel).parent.as_posix())
        if parent and parent != ".":
            tk.Label(texts, text=parent, bg=base, fg=theme.MUTED, font=theme.font(8), anchor="w").pack(fill="x")
        row.bind_tree()
        self._rows[rel] = row

    def _select_row(self, rel: str):
        for path, row in self._rows.items():
            base = theme.CHIP if path == rel else _BG
            row._base = base
            row._set(base)

    def _load(self, rel: str, keep: bool = False):
        try:
            content = self.app.projects.read_text(self.project_id, rel)
        except FileNotFoundError:
            self.app.toast("That file is gone", "close")
            self.reload()
            return
        except Exception as exc:
            self.editor.configure(state="normal")
            self.editor.delete("1.0", "end")
            self.editor.insert("1.0", f"[Takoyo can't edit this file here]\n\n{exc}")
            self.editor.configure(state="disabled")
            self.placeholder.place_forget()
            self.selected = rel
            self._select_row(rel)
            self.path_label.configure(text=rel, fg=theme.INK_SOFT)
            self.save_btn.set_enabled(False)
            self.delete_btn.set_enabled(True)
            return
        self.selected = rel
        self._select_row(rel)
        self.path_label.configure(text=rel, fg=theme.INK)
        self.placeholder.place_forget()
        self.editor.configure(state="normal")
        self.editor.delete("1.0", "end")
        self.editor.insert("1.0", content)
        self.editor.mark_set("insert", "1.0")
        self.editor.see("1.0")
        self.save_btn.set_enabled(True)
        self.delete_btn.set_enabled(True)
        if not keep:
            self.list_area.canvas.yview_moveto(0)

    def _clear_editor(self):
        self.selected = None
        self.editor.configure(state="normal")
        self.editor.delete("1.0", "end")
        self.editor.configure(state="disabled")
        self.placeholder.place(relx=0.5, rely=0.45, anchor="center")
        self.path_label.configure(text="Choose a file", fg=theme.MUTED)
        self.save_btn.set_enabled(False)
        self.delete_btn.set_enabled(False)
        for row in self._rows.values():
            row._base = _BG
            row._set(_BG)

    def _save(self):
        if not self.selected:
            return
        if str(self.editor.cget("state")) == "disabled":
            return
        content = self.editor.get("1.0", "end-1c")
        try:
            self.app.projects.write_text(self.project_id, self.selected, content)
        except Exception as exc:
            self.app.toast(f"Could not save: {exc}", "close")
            return
        self.app.toast(f"Saved {Path(self.selected).name}")
        self.reload()
        self.app.refresh_sidebar()

    def _new_file(self):
        path = ask_text(self.app, "New file", "Path (e.g. src/main.py)", "")
        if not path:
            return
        path = path.strip().replace("\\", "/")
        try:
            self.app.projects.create_file(self.project_id, path, "")
        except FileExistsError:
            self.app.toast("That file already exists", "close")
            self._load(path)
            return
        except Exception as exc:
            self.app.toast(str(exc), "close")
            return
        self.selected = path
        self.reload()
        self._load(path)
        self.app.refresh_sidebar()

    def _delete(self):
        if not self.selected:
            return
        rel = self.selected
        if not ask_confirm(
            self.app,
            "Delete file?",
            f"“{rel}” will move to Takoyo's trash folder.",
            confirm="Move to trash",
            danger=True,
        ):
            return
        try:
            self.app.projects.delete_file(self.project_id, rel)
        except Exception as exc:
            self.app.toast(f"Could not delete: {exc}", "close")
            return
        self.selected = None
        self._clear_editor()
        self.reload()
        self.app.refresh_sidebar()
        self.app.toast("File moved to trash")

    def _open_folder(self):
        try:
            _open_folder(self.app, self.project_id)
        except Exception:
            pass

    def close(self):
        if self.app.files_dialog is self:
            self.app.files_dialog = None
        super().close()


# --------------------------------------------------------------------------- #
# Utilities
# --------------------------------------------------------------------------- #
class HoverRow(tk.Frame):
    """Shared hover row (dialogs import it locally to avoid a gui cycle)."""

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
        _bind(self, "<Button-1>", self._on_click)
        return self

    def _paint(self, color: str):
        _paint(self, color)

    def _set(self, color: str):
        self._hovered = color != self._base
        _paint(self, color)

    def _on_enter(self, _event=None):
        self._set(self._hover)

    def _on_leave(self, _event=None):
        self.after(20, self._check)

    def _check(self):
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


def _paint(widget, color: str):
    try:
        widget.configure(bg=color)
    except Exception:
        pass
    for child in widget.winfo_children():
        _paint(child, color)


def _bind(widget, sequence: str, handler):
    try:
        widget.bind(sequence, handler)
    except Exception:
        pass
    for child in widget.winfo_children():
        _bind(child, sequence, handler)


def _section(parent, kicker: str, title: str, subtitle: str = "") -> tk.Frame:
    frame = tk.Frame(parent, bg=_BG)
    frame.pack(fill="x", pady=(18, 2))
    tk.Label(frame, text=kicker, bg=_BG, fg=theme.MUTED, font=theme.font(9, "semibold"), anchor="w").pack(fill="x")
    tk.Label(frame, text=title, bg=_BG, fg=theme.INK, font=theme.font(17, "semibold"), anchor="w").pack(fill="x", pady=(1, 2))
    if subtitle:
        tk.Label(
            frame,
            text=subtitle,
            bg=_BG,
            fg=theme.MUTED,
            font=theme.font(9),
            anchor="w",
            justify="left",
            wraplength=610,
        ).pack(fill="x")
    return frame


def _short(text: str, limit: int) -> str:
    text = re.sub(r"\s+", " ", (text or "").strip())
    return text if len(text) <= limit else text[: limit - 1].rstrip() + "…"


def _open_folder(app, project_id: str | None = None):
    root = app.projects.root(project_id) if project_id else Path.home() / ".takoyo"
    try:
        if os.name == "nt":
            os.startfile(str(root))  # type: ignore[attr-defined]
        else:
            webbrowser.open(root.as_uri())
    except Exception:
        app.toast("Could not open the folder", "folder")
