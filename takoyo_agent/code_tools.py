"""Small, deliberate code-execution sandbox for Takoyo."""

from __future__ import annotations

import os
import shlex
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path

from .config import WORKSPACE_DIR, ensure_dirs

MAX_CODE_BYTES = 200_000
MAX_OUTPUT = 80_000
_ALLOWED_COMMANDS = {"python", "python.exe", "python3", "py", "py.exe", "pip", "pip.exe", "pip3", "pip3.exe"}


def _limits() -> dict:
    kwargs = {}
    if os.name == "nt":
        kwargs["creationflags"] = subprocess.CREATE_NO_WINDOW
    return kwargs


def _trim(text: str) -> str:
    if len(text) > MAX_OUTPUT:
        return text[:MAX_OUTPUT] + "\n… output truncated"
    return text


def run_python(code: str, cwd: str | Path | None = None, timeout: int = 30, auto_install: bool = False) -> dict:
    ensure_dirs()
    if not isinstance(code, str) or not code.strip():
        return {"ok": False, "returncode": None, "stdout": "", "stderr": "No code was provided.", "timed_out": False, "duration": 0}
    if len(code.encode("utf-8")) > MAX_CODE_BYTES:
        return {"ok": False, "returncode": None, "stdout": "", "stderr": "Code is too large.", "timed_out": False, "duration": 0}

    workdir = Path(cwd) if cwd else WORKSPACE_DIR
    workdir.mkdir(parents=True, exist_ok=True)
    name = f"takoyo_{uuid.uuid4().hex[:8]}.py"
    script = workdir / name
    script.write_text(code, "utf-8")
    env = os.environ.copy()
    env["PYTHONIOENCODING"] = "utf-8"
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    started = time.time()
    try:
        proc = subprocess.run(
            [sys.executable, "-I", "-X", "utf8", str(script)],
            cwd=str(workdir),
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=max(1, min(int(timeout), 120)),
            env=env,
            **_limits(),
        )
        duration = time.time() - started
        return {
            "ok": proc.returncode == 0,
            "returncode": proc.returncode,
            "stdout": _trim(proc.stdout or ""),
            "stderr": _trim(proc.stderr or ""),
            "timed_out": False,
            "duration": round(duration, 2),
        }
    except subprocess.TimeoutExpired as exc:
        return {
            "ok": False,
            "returncode": None,
            "stdout": _trim((exc.stdout or "") if isinstance(exc.stdout, str) else ""),
            "stderr": "Execution timed out.",
            "timed_out": True,
            "duration": round(time.time() - started, 2),
        }
    except Exception as exc:
        return {"ok": False, "returncode": None, "stdout": "", "stderr": str(exc), "timed_out": False, "duration": 0}
    finally:
        try:
            script.unlink()
        except OSError:
            pass


def run_command(command: str | list[str], cwd: str | Path | None = None, timeout: int = 60) -> dict:
    ensure_dirs()
    if isinstance(command, str):
        try:
            args = shlex.split(command)
        except ValueError as exc:
            return {"ok": False, "returncode": None, "stdout": "", "stderr": f"Could not parse command: {exc}", "timed_out": False, "duration": 0}
    else:
        args = [str(a) for a in command]
    if not args:
        return {"ok": False, "returncode": None, "stdout": "", "stderr": "Empty command.", "timed_out": False, "duration": 0}
    program = os.path.basename(args[0]).lower()
    if program not in _ALLOWED_COMMANDS:
        return {
            "ok": False,
            "returncode": None,
            "stdout": "",
            "stderr": f"'{program}' is not on Takoyo's allowlist (python, pip).",
            "timed_out": False,
            "duration": 0,
        }
    if args[0].lower() in {"python", "python3", "py"}:
        args[0] = sys.executable
    workdir = Path(cwd) if cwd else WORKSPACE_DIR
    workdir.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["PYTHONIOENCODING"] = "utf-8"
    started = time.time()
    try:
        proc = subprocess.run(
            args,
            cwd=str(workdir),
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=max(1, min(int(timeout), 300)),
            env=env,
            **_limits(),
        )
        return {
            "ok": proc.returncode == 0,
            "returncode": proc.returncode,
            "stdout": _trim(proc.stdout or ""),
            "stderr": _trim(proc.stderr or ""),
            "timed_out": False,
            "duration": round(time.time() - started, 2),
        }
    except subprocess.TimeoutExpired:
        return {"ok": False, "returncode": None, "stdout": "", "stderr": "Command timed out.", "timed_out": True, "duration": round(time.time() - started, 2)}
    except Exception as exc:
        return {"ok": False, "returncode": None, "stdout": "", "stderr": str(exc), "timed_out": False, "duration": 0}
