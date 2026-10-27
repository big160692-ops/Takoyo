"""Persistent settings and on-disk locations for Takoyo."""

from __future__ import annotations

import json
import os
import tempfile
import threading
from pathlib import Path

APP_SLUG = "takoyo"


def takoyo_home() -> Path:
    override = os.environ.get("TAKOYO_HOME")
    return Path(override).expanduser() if override else Path.home() / ".takoyo"


HOME = takoyo_home()
MODELS_DIR = HOME / "models"
ENGINE_DIR = HOME / "llama"
IMAGES_DIR = HOME / "images"
CONVERSATIONS_DIR = HOME / "conversations"
PROJECTS_DIR = HOME / "projects"
WORKSPACE_DIR = HOME / "workspace"
LOGS_DIR = HOME / "logs"
TRASH_DIR = HOME / "trash"
CONFIG_PATH = HOME / "config.json"


def ensure_dirs() -> None:
    for path in (HOME, MODELS_DIR, ENGINE_DIR, IMAGES_DIR, CONVERSATIONS_DIR, PROJECTS_DIR, WORKSPACE_DIR, LOGS_DIR, TRASH_DIR):
        path.mkdir(parents=True, exist_ok=True)


DEFAULTS: dict = {
    "version": 1,
    "provider": "local",
    "local_model": "qwen2.5-1.5b",
    "openai_model": "gpt-4o-mini",
    "anthropic_model": "claude-sonnet-4-5",
    "google_model": "gemini-2.5-flash",
    "ollama_model": "qwen2.5-coder:1.5b",
    "openai_api_key": "",
    "anthropic_api_key": "",
    "google_api_key": "",
    "openai_base_url": "https://api.openai.com/v1",
    "ollama_base_url": "http://127.0.0.1:11434",
    "temperature": 0.7,
    "max_tokens": 2048,
    "image_provider": "takoyo",
    "image_size": "1024x1280",
    "allow_code_execution": True,
    "auto_apply_edits": False,
    "api_host": "127.0.0.1",
    "api_port": 8765,
    "api_auto_start": False,
    "local_port": 8766,
    "local_context": 8192,
    "local_threads": 0,
    "sidebar_open": False,
    "window_geometry": "1280x800",
    "last_conversation": "",
    "last_project": "",
    "voice_enabled": True,
}


class Config:
    """Thread-safe JSON configuration with atomic writes."""

    def __init__(self, path: Path | None = None):
        ensure_dirs()
        self.path = Path(path) if path else CONFIG_PATH
        self._lock = threading.RLock()
        self._data = dict(DEFAULTS)
        self.load()

    # -- io -----------------------------------------------------------------
    def load(self) -> None:
        with self._lock:
            try:
                raw = json.loads(self.path.read_text("utf-8"))
                if isinstance(raw, dict):
                    self._data = {**DEFAULTS, **raw}
            except FileNotFoundError:
                pass
            except Exception:
                try:
                    self.path.replace(self.path.with_suffix(".corrupt.json"))
                except OSError:
                    pass

    def save(self) -> None:
        with self._lock:
            ensure_dirs()
            data = json.dumps(self._data, indent=2)
            fd, tmp = tempfile.mkstemp(dir=str(self.path.parent), prefix=".config-", suffix=".tmp")
            try:
                with os.fdopen(fd, "w", encoding="utf-8") as handle:
                    handle.write(data)
                os.replace(tmp, self.path)
            finally:
                if os.path.exists(tmp):
                    try:
                        os.unlink(tmp)
                    except OSError:
                        pass

    # -- access -------------------------------------------------------------
    def get(self, key: str, default=None):
        with self._lock:
            return self._data.get(key, DEFAULTS.get(key, default))

    def __getitem__(self, key: str):
        return self.get(key)

    def set(self, key: str, value, save: bool = True) -> None:
        with self._lock:
            self._data[key] = value
        if save:
            self.save()

    def update(self, values: dict, save: bool = True) -> None:
        with self._lock:
            self._data.update(values)
        if save:
            self.save()

    def as_dict(self) -> dict:
        with self._lock:
            return dict(self._data)

    # -- helpers ------------------------------------------------------------
    def api_key(self, provider: str) -> str:
        provider = (provider or "").lower()
        stored = str(self.get(f"{provider}_api_key", "") or "").strip()
        if stored:
            return stored
        env_names = {
            "openai": ("OPENAI_API_KEY",),
            "anthropic": ("ANTHROPIC_API_KEY",),
            "google": ("GOOGLE_API_KEY", "GEMINI_API_KEY"),
        }.get(provider, ())
        for name in env_names:
            value = os.environ.get(name, "").strip()
            if value:
                return value
        return ""

    def model_for(self, provider: str | None = None) -> str:
        provider = (provider or self.get("provider") or "local").lower()
        if provider == "local":
            return self.get("local_model", "qwen2.5-1.5b")
        return str(self.get(f"{provider}_model", "") or "")

    def set_model_for(self, provider: str, value: str) -> None:
        provider = (provider or "local").lower()
        key = "local_model" if provider == "local" else f"{provider}_model"
        self.set(key, value)

    def base_url(self, provider: str) -> str:
        if provider == "ollama":
            return str(self.get("ollama_base_url", "http://127.0.0.1:11434")).rstrip("/")
        if provider == "openai":
            return str(self.get("openai_base_url", "https://api.openai.com/v1")).rstrip("/")
        return ""
