"""Takoyo's own AI: a bundled llama.cpp runtime that runs open-weight models on this PC."""

from __future__ import annotations

import json
import os
import platform
import re
import shutil
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path

from .config import ENGINE_DIR, LOGS_DIR, MODELS_DIR, Config, ensure_dirs
from .model_catalog import DEFAULT_LOCAL_MODEL, LOCAL_MODELS, LocalModel, local_model, local_model_by_name

GITHUB_RELEASES = "https://api.github.com/repos/ggml-org/llama.cpp/releases"
USER_AGENT = "Takoyo/1.0 (+local desktop assistant)"
HEALTH_TIMEOUT = 240


class LocalAIError(RuntimeError):
    pass


class LocalAINotReady(LocalAIError):
    def __init__(self, message: str, model_key: str = ""):
        super().__init__(message)
        self.model_key = model_key


def _human_bytes(value: int | float) -> str:
    value = float(value or 0)
    for unit in ("B", "KB", "MB", "GB"):
        if value < 1024 or unit == "GB":
            return f"{value:.1f} {unit}" if unit != "B" else f"{int(value)} B"
        value /= 1024
    return f"{value:.1f} GB"


def _read_json(path: Path) -> dict:
    try:
        data = json.loads(path.read_text("utf-8"))
        return data if isinstance(data, dict) else {}
    except Exception:
        return {}


def _write_json(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=1), "utf-8")


class LocalEngine:
    def __init__(self, config: Config | None = None):
        ensure_dirs()
        self.config = config or Config()
        self._lock = threading.RLock()
        self._proc: subprocess.Popen | None = None
        self._log_handle = None
        self._model_key = ""
        self._external = False
        self._ready = False
        self._starting = False
        self._engine_cache = _read_json(ENGINE_DIR / "engine.json")

    # ------------------------------------------------------------------ #
    # Status
    # ------------------------------------------------------------------ #
    def port(self) -> int:
        try:
            return int(self.config.get("local_port", 8766))
        except Exception:
            return 8766

    def base_url(self) -> str:
        return f"http://127.0.0.1:{self.port()}"

    def model_path(self, key: str | None = None) -> Path:
        model = local_model(key or self.config.get("local_model", DEFAULT_LOCAL_MODEL))
        return MODELS_DIR / model.filename

    def model_installed(self, key: str | None = None) -> bool:
        model = local_model(key or self.config.get("local_model", DEFAULT_LOCAL_MODEL))
        path = MODELS_DIR / model.filename
        try:
            size = path.stat().st_size
        except OSError:
            return False
        return size >= int(model.size_bytes * 0.97)

    def installed_models(self) -> list[str]:
        return [key for key in LOCAL_MODELS if self.model_installed(key)]

    def engine_exe(self) -> Path | None:
        cached = self._engine_cache.get("exe")
        if cached:
            path = Path(cached)
            if path.exists():
                return path
        for path in ENGINE_DIR.rglob("llama-server*"):
            if path.is_file() and path.suffix.lower() in (".exe", ""):
                self._engine_cache["exe"] = str(path)
                _write_json(ENGINE_DIR / "engine.json", self._engine_cache)
                return path
        return None

    def engine_installed(self) -> bool:
        return self.engine_exe() is not None

    def running(self) -> bool:
        with self._lock:
            if self._proc is not None and self._proc.poll() is not None:
                self._proc = None
                self._ready = False
            return self._ready or self._proc is not None

    def status(self) -> dict:
        return {
            "engine": self.engine_installed(),
            "engine_version": self._engine_cache.get("tag", ""),
            "running": self.running(),
            "model": self._model_key or self.config.get("local_model", DEFAULT_LOCAL_MODEL),
            "port": self.port(),
            "installed": self.installed_models(),
            "models": {
                key: {
                    "name": m.name,
                    "size": m.size_label,
                    "installed": self.model_installed(key),
                    "description": m.description,
                    "coding": m.coding,
                }
                for key, m in LOCAL_MODELS.items()
            },
            "unlimited": True,
        }

    # ------------------------------------------------------------------ #
    # Engine download
    # ------------------------------------------------------------------ #
    def _engine_asset(self) -> tuple[str, str]:
        cached = self._engine_cache.get("url")
        if cached:
            return self._engine_cache.get("tag", "cached"), cached
        system = platform.system().lower()
        machine = platform.machine().lower()
        if system == "windows":
            arch = "arm64" if "arm" in machine else "x64"
            pattern = re.compile(rf"^llama-b\d+-bin-win-cpu-{arch}\.zip$")
        elif system == "darwin":
            arch = "arm64" if "arm" in machine or "aarch64" in machine else "x64"
            pattern = re.compile(rf"^llama-b\d+-bin-macos-{arch}\.zip$")
        elif system == "linux":
            arch = "arm64" if "aarch64" in machine or "arm" in machine else "x64"
            pattern = re.compile(rf"^llama-b\d+-bin-ubuntu(?:-[^-]+)*-{arch}\.zip$")
        else:
            raise LocalAIError(f"Automatic engine install is not supported on {platform.system()}.")

        url = f"{GITHUB_RELEASES}?per_page=20"
        request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, "Accept": "application/vnd.github+json"})
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                releases = json.loads(response.read().decode("utf-8"))
        except Exception as exc:
            raise LocalAIError(
                "Could not reach GitHub to download the built-in engine. "
                "Check your connection, or place a llama.cpp build in ~/.takoyo/llama/ manually."
            ) from exc

        for release in releases if isinstance(releases, list) else []:
            for asset in release.get("assets", []):
                name = asset.get("name", "")
                if "cuda" in name or "vulkan" in name or "rocm" in name or "openvino" in name or "sycl" in name or "opencl" in name:
                    continue
                if pattern.match(name):
                    info = {
                        "tag": release.get("tag_name", ""),
                        "url": asset.get("browser_download_url", ""),
                        "name": name,
                        "expected": asset.get("size", 0),
                    }
                    self._engine_cache.update(info)
                    _write_json(ENGINE_DIR / "engine.json", self._engine_cache)
                    return info["tag"], info["url"]
        raise LocalAIError("No matching llama.cpp Windows build was found in the latest releases.")

    def install_engine(self, progress=None, cancel=None) -> Path:
        with self._lock:
            existing = self.engine_exe()
            if existing:
                return existing
            tag, url = self._engine_asset()
            if progress:
                progress(0.01, f"Downloading llama.cpp {tag}…")
            archive = ENGINE_DIR / f"engine-{tag}.zip"
            expected = int(self._engine_cache.get("expected", 0) or 0)
            self._download(url, archive, expected, progress, cancel, base=0.0, span=0.12)
            if cancel is not None and getattr(cancel, "is_set", lambda: False)():
                raise LocalAIError("Install cancelled.")
            target = ENGINE_DIR / tag
            target.mkdir(parents=True, exist_ok=True)
            if progress:
                progress(0.13, "Unpacking the built-in engine…")
            try:
                with zipfile.ZipFile(archive) as zf:
                    base = target.resolve()
                    for member in zf.infolist():
                        dest = (target / member.filename).resolve()
                        if base not in dest.parents and dest != base:
                            continue
                        zf.extract(member, target)
            except zipfile.BadZipFile as exc:
                archive.unlink(missing_ok=True)
                raise LocalAIError("The downloaded engine was incomplete. Please try again.") from exc
            finally:
                try:
                    archive.unlink()
                except OSError:
                    pass
            exe = None
            for path in target.rglob("llama-server*"):
                if path.is_file() and path.suffix.lower() in (".exe", ""):
                    exe = path
                    break
            if not exe:
                raise LocalAIError("llama-server was not found in the downloaded package.")
            self._engine_cache.update({"tag": tag, "exe": str(exe), "url": url})
            _write_json(ENGINE_DIR / "engine.json", self._engine_cache)
            if progress:
                progress(0.15, "Engine ready.")
            return exe

    # ------------------------------------------------------------------ #
    # Model download
    # ------------------------------------------------------------------ #
    def install_model(self, key: str, progress=None, cancel=None) -> Path:
        model = local_model(key)
        path = MODELS_DIR / model.filename
        if self.model_installed(key):
            if progress:
                progress(1.0, f"{model.name} is already installed.")
            return path
        free = shutil.disk_usage(MODELS_DIR).free
        if free < model.size_bytes + 300 * 1024 * 1024:
            raise LocalAIError(
                f"Not enough disk space for {model.name}. "
                f"Needs about {_human_bytes(model.size_bytes)}, only {_human_bytes(free)} free."
            )
        if progress:
            progress(0.16, f"Downloading {model.name} ({model.size_label})…")
        self._download(model.url, path, model.size_bytes, progress, cancel, base=0.16, span=0.84)
        if not self.model_installed(key):
            raise LocalAIError("The model download looks incomplete. Please try again.")
        if progress:
            progress(1.0, f"{model.name} is ready.")
        return path

    def install(self, key: str, progress=None, cancel=None) -> None:
        self.install_engine(progress=progress, cancel=cancel)
        self.install_model(key, progress=progress, cancel=cancel)
        self.config.update({"local_model": key}, save=True)

    def _download(self, url: str, dest: Path, expected: int, progress, cancel, base: float = 0.0, span: float = 1.0) -> None:
        dest.parent.mkdir(parents=True, exist_ok=True)
        part = dest.with_suffix(dest.suffix + ".part")
        downloaded = part.stat().st_size if part.exists() else 0
        if expected and downloaded > expected:
            downloaded = 0
            part.unlink(missing_ok=True)
        headers = {"User-Agent": USER_AGENT, "Accept": "*/*"}
        if downloaded:
            headers["Range"] = f"bytes={downloaded}-"
        request = urllib.request.Request(url, headers=headers)
        last_report = 0.0
        try:
            with urllib.request.urlopen(request, timeout=90) as response:
                if downloaded and getattr(response, "status", 200) != 206:
                    downloaded = 0
                    part.unlink(missing_ok=True)
                total = expected
                length = response.headers.get("Content-Length")
                if length and not total:
                    total = downloaded + int(length)
                mode = "ab" if downloaded else "wb"
                with open(part, mode) as handle:
                    while True:
                        if cancel is not None and getattr(cancel, "is_set", lambda: False)():
                            raise LocalAIError("Download cancelled.")
                        chunk = response.read(1024 * 512)
                        if not chunk:
                            break
                        handle.write(chunk)
                        downloaded += len(chunk)
                        now = time.time()
                        if progress and now - last_report > 0.25:
                            last_report = now
                            fraction = (downloaded / total) if total else 0
                            progress(base + span * min(0.995, fraction), f"Downloading {dest.name.split('-q4')[0]} · {_human_bytes(downloaded)} / {_human_bytes(total) if total else '…'}")
            if not part.exists() or part.stat().st_size < 1024:
                raise LocalAIError("The download did not complete.")
            os.replace(part, dest)
        except LocalAIError:
            raise
        except urllib.error.HTTPError as exc:
            if exc.code == 416:
                part.unlink(missing_ok=True)
            raise LocalAIError(f"Download failed ({exc.code}). Please try again.") from exc
        except Exception as exc:
            raise LocalAIError(f"Download failed: {exc}. Partial progress was kept for a retry.") from exc

    # ------------------------------------------------------------------ #
    # Server lifecycle
    # ------------------------------------------------------------------ #
    def _healthy(self) -> bool:
        try:
            request = urllib.request.Request(self.base_url() + "/health", headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(request, timeout=2) as response:
                return response.status < 500
        except Exception:
            return False

    def _tail_log(self, lines: int = 18) -> str:
        path = LOGS_DIR / "llama-server.log"
        try:
            content = path.read_text("utf-8", "replace").splitlines()[-lines:]
            return "\n".join(content)
        except Exception:
            return ""

    def ensure_ready(self, key: str | None = None, progress=None) -> None:
        key = key or self.config.get("local_model", DEFAULT_LOCAL_MODEL)
        if not self.engine_installed():
            raise LocalAINotReady(
                "Takoyo's built-in engine is not installed yet. Download it in Settings → Built-in AI.",
                model_key=key,
            )
        if not self.model_installed(key):
            raise LocalAINotReady(
                f"{local_model(key).name} is not installed yet. Download it in Settings → Built-in AI.",
                model_key=key,
            )
        with self._lock:
            if self._ready and self.running() and self._model_key == key:
                return
        self.start(key, progress=progress)

    def start(self, key: str | None = None, progress=None) -> None:
        key = key or self.config.get("local_model", DEFAULT_LOCAL_MODEL)
        with self._lock:
            if self._starting:
                raise LocalAIError("The built-in model is already starting.")
            if self._ready and self._model_key == key and self.running():
                return
            self._starting = True
        try:
            exe = self.engine_exe()
            if not exe:
                raise LocalAINotReady(
                    "Takoyo's built-in engine is not installed yet. Download it in Settings → Built-in AI.",
                    model_key=key,
                )
            if not self.model_installed(key):
                raise LocalAINotReady(
                    f"{local_model(key).name} is not installed yet. Download it in Settings → Built-in AI.",
                    model_key=key,
                )
            self.stop()
            model_path = self.model_path(key)
            port = self.port()
            threads = int(self.config.get("local_threads", 0) or 0) or max(2, min(8, (os.cpu_count() or 4) - 1))
            context = int(self.config.get("local_context", 8192) or 8192)
            log_path = LOGS_DIR / "llama-server.log"
            self._log_handle = open(log_path, "ab")
            self._log_handle.write(
                f"\n\n=== Takoyo starting {local_model(key).name} on :{port} at {time.strftime('%Y-%m-%d %H:%M:%S')} ===\n".encode()
            )
            self._log_handle.flush()
            cmd = [
                str(exe),
                "-m", str(model_path),
                "--host", "127.0.0.1",
                "--port", str(port),
                "-c", str(context),
                "-t", str(threads),
                "-np", "1",
                "--no-webui",
                "--jinja",
            ]
            kwargs = {}
            if os.name == "nt":
                kwargs["creationflags"] = subprocess.CREATE_NO_WINDOW
            try:
                self._proc = subprocess.Popen(
                    cmd,
                    cwd=str(exe.parent),
                    stdout=self._log_handle,
                    stderr=subprocess.STDOUT,
                    stdin=subprocess.DEVNULL,
                    **kwargs,
                )
            except Exception as exc:
                raise LocalAIError(f"Could not start the built-in engine: {exc}") from exc
            self._model_key = key
            self._external = False
            self._ready = False
            started = time.time()
            while time.time() - started < HEALTH_TIMEOUT:
                if self._proc.poll() is not None:
                    tail = self._tail_log()
                    raise LocalAIError(
                        "The built-in model stopped while loading. " + (f"\n{tail}" if tail else "")
                    )
                if self._healthy():
                    self._ready = True
                    if progress:
                        progress(1.0, f"{local_model(key).name} is awake.")
                    return
                if progress:
                    elapsed = int(time.time() - started)
                    progress(min(0.98, 0.2 + elapsed / HEALTH_TIMEOUT), f"Waking up {local_model(key).name}… {elapsed}s")
                time.sleep(0.6)
            raise LocalAIError("The built-in model took too long to load. Try a smaller model in Settings.")
        finally:
            with self._lock:
                self._starting = False

    def stop(self) -> None:
        with self._lock:
            proc = self._proc
            self._proc = None
            self._ready = False
            self._model_key = ""
            if proc is not None:
                try:
                    if proc.poll() is None:
                        proc.terminate()
                        try:
                            proc.wait(timeout=6)
                        except subprocess.TimeoutExpired:
                            proc.kill()
                except Exception:
                    pass
            if self._log_handle is not None:
                try:
                    self._log_handle.close()
                except Exception:
                    pass
                self._log_handle = None

    def shutdown(self) -> None:
        self.stop()

    # ------------------------------------------------------------------ #
    # Chat
    # ------------------------------------------------------------------ #
    def stream_chat(self, messages: list[dict], temperature: float = 0.7, max_tokens: int = 2048, cancel=None, key: str | None = None):
        key = key or self.config.get("local_model", DEFAULT_LOCAL_MODEL)
        self.ensure_ready(key)
        payload = {
            "model": key,
            "messages": messages,
            "stream": True,
            "temperature": max(0.0, min(2.0, float(temperature))),
            "max_tokens": int(max_tokens or 2048),
        }
        data = json.dumps(payload).encode("utf-8")
        request = urllib.request.Request(
            self.base_url() + "/v1/chat/completions",
            data=data,
            headers={"Content-Type": "application/json", "User-Agent": USER_AGENT, "Accept": "text/event-stream"},
            method="POST",
        )
        try:
            response = urllib.request.urlopen(request, timeout=600)
        except urllib.error.HTTPError as exc:
            body = exc.read().decode("utf-8", "replace")[:500]
            raise LocalAIError(f"The built-in model returned an error ({exc.code}). {body}") from exc
        except Exception as exc:
            raise LocalAIError(f"Could not reach the built-in model: {exc}") from exc
        try:
            for raw in response:
                if cancel is not None and getattr(cancel, "is_set", lambda: False)():
                    break
                line = raw.decode("utf-8", "replace").strip()
                if not line or not line.startswith("data:"):
                    continue
                payload_line = line[5:].strip()
                if payload_line == "[DONE]":
                    break
                try:
                    event = json.loads(payload_line)
                    choices = event.get("choices") or []
                    if not choices:
                        continue
                    delta = choices[0].get("delta") or {}
                    chunk = delta.get("content")
                    if chunk:
                        yield chunk
                except json.JSONDecodeError:
                    continue
        finally:
            try:
                response.close()
            except Exception:
                pass

    def complete(self, messages: list[dict], temperature: float = 0.7, max_tokens: int = 2048, cancel=None, key: str | None = None) -> str:
        return "".join(self.stream_chat(messages, temperature, max_tokens, cancel=cancel, key=key))


# --------------------------------------------------------------------------- #
# Shared instance
# --------------------------------------------------------------------------- #
_engine_lock = threading.Lock()
_engine: LocalEngine | None = None


def get_engine(config: Config | None = None) -> LocalEngine:
    global _engine
    with _engine_lock:
        if _engine is None:
            _engine = LocalEngine(config)
        elif config is not None:
            _engine.config = config
        return _engine


def setup_local_cli(model_arg: str | None = None) -> int:
    """Download the engine + model from the command line."""
    config = Config()
    engine = LocalEngine(config)
    chosen = DEFAULT_LOCAL_MODEL
    if model_arg:
        candidate = local_model_by_name(model_arg) or LOCAL_MODELS.get(model_arg)
        if not candidate:
            print("Unknown model. Choose one of: " + ", ".join(LOCAL_MODELS))
            return 2
        chosen = candidate.key
    model = local_model(chosen)
    print(f"Takoyo built-in AI setup — {model.name} ({model.size_label})")
    last = [-1]

    def progress(fraction: float, message: str):
        pct = int(fraction * 100)
        if pct != last[0]:
            last[0] = pct
            print(f"\r{pct:3d}%  {message[:88]:<90}", end="", flush=True)

    try:
        engine.install(chosen, progress=progress)
    except KeyboardInterrupt:
        print("\nCancelled.")
        return 130
    except LocalAIError as exc:
        print(f"\nSetup failed: {exc}")
        return 1
    print("\nDone. Takoyo's brain is installed and selected. Run `python main.py` to chat.")
    engine.shutdown()
    return 0
