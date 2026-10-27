"""Projects: real folders on disk that Takoyo can read, discuss and edit."""

from __future__ import annotations

import json
import os
import re
import shutil
import time
import uuid
from pathlib import Path

from .config import PROJECTS_DIR, TRASH_DIR, ensure_dirs

META_NAME = ".takoyo.json"
_ID_RE = re.compile(r"^[A-Za-z0-9._-]{1,80}$")
_SKIP_DIRS = {
    ".git", ".hg", ".svn", "node_modules", "__pycache__", ".venv", "venv",
    "env", "dist", "build", ".next", ".cache", ".idea", ".vscode",
    "target", "bin", "obj",
}
_TEXT_EXT = {
    ".py", ".pyw", ".js", ".jsx", ".ts", ".tsx", ".json", ".html", ".htm",
    ".css", ".scss", ".sass", ".md", ".txt", ".java", ".c", ".h", ".cpp",
    ".hpp", ".cs", ".go", ".rs", ".rb", ".php", ".sh", ".bat", ".ps1", ".sql",
    ".yaml", ".yml", ".toml", ".ini", ".cfg", ".xml", ".svg", ".env", ".lua",
    ".kt", ".swift", ".dart", ".r", ".m", ".vue", ".svelte",
}
_SECRET_SUFFIXES = {".pem", ".key", ".p12", ".pfx", ".jks", ".keystore", ".ppk"}
_SECRET_RE = re.compile(
    r"(^\.env|^\.netrc$|^\.pgpass$|^id_(rsa|dsa|ecdsa|ed25519)|credential|secret|"
    r"password|passwd|(^|[._-])api[_-]?key|(^|[._-])access[_-]?key|private[_-]?key)",
    re.I,
)


def _is_secret(rel: str) -> bool:
    path = Path(rel)
    if path.suffix.lower() in _SECRET_SUFFIXES:
        return True
    return bool(_SECRET_RE.search(path.name))


def _slug(name: str) -> str:
    value = re.sub(r"[^a-z0-9]+", "-", (name or "").lower()).strip("-")
    value = value[:36] or "project"
    return value


def _looks_text(data: bytes) -> bool:
    if not data:
        return True
    if b"\x00" in data[:4096]:
        return False
    try:
        data[:4096].decode("utf-8")
        return True
    except UnicodeDecodeError:
        sample = data[:4096]
        printable = sum(1 for b in sample if 32 <= b < 127 or b in (9, 10, 13))
        return printable / max(1, len(sample)) > 0.85


class ProjectStore:
    def __init__(self, directory: Path | None = None):
        ensure_dirs()
        self.dir = Path(directory) if directory else PROJECTS_DIR
        self.dir.mkdir(parents=True, exist_ok=True)

    # ------------------------------------------------------------------ #
    def _pid(self, pid: str) -> str:
        if not _ID_RE.match(pid or ""):
            raise ValueError("Invalid project id")
        return pid

    def root(self, pid: str) -> Path:
        return self.dir / self._pid(pid)

    def _meta_path(self, pid: str) -> Path:
        return self.root(pid) / META_NAME

    def _read_meta(self, pid: str) -> dict | None:
        try:
            data = json.loads(self._meta_path(pid).read_text("utf-8"))
            if not isinstance(data, dict):
                return None
            data.setdefault("id", pid)
            data.setdefault("name", pid)
            return data
        except Exception:
            return None

    def _write_meta(self, pid: str, meta: dict) -> None:
        meta["updated"] = time.time()
        path = self._meta_path(pid)
        tmp = path.with_suffix(".tmp")
        tmp.write_text(json.dumps(meta, indent=1, ensure_ascii=False), "utf-8")
        os.replace(tmp, path)

    # ------------------------------------------------------------------ #
    def list(self, query: str = "") -> list[dict]:
        query = (query or "").strip().lower()
        items: list[dict] = []
        for child in self.dir.iterdir():
            if not child.is_dir() or not _ID_RE.match(child.name):
                continue
            meta = self._read_meta(child.name)
            if not meta:
                meta = {"id": child.name, "name": child.name, "created": child.stat().st_mtime, "updated": child.stat().st_mtime}
            if query and query not in meta.get("name", "").lower() and query not in meta.get("description", "").lower():
                continue
            meta["file_count"] = sum(1 for p in child.rglob("*") if p.is_file() and p.name != META_NAME and not any(part in _SKIP_DIRS for part in p.parts))
            items.append(meta)
        items.sort(key=lambda m: m.get("updated", 0), reverse=True)
        return items

    def get(self, pid: str) -> dict | None:
        if not pid:
            return None
        try:
            root = self.root(pid)
        except ValueError:
            return None
        if not root.is_dir():
            return None
        meta = self._read_meta(pid)
        return meta or {"id": pid, "name": pid, "created": root.stat().st_mtime, "updated": root.stat().st_mtime}

    def create(self, name: str, description: str = "") -> dict:
        base = _slug(name)
        pid = f"{base}-{uuid.uuid4().hex[:5]}"
        root = self.dir / pid
        root.mkdir(parents=True, exist_ok=False)
        now = time.time()
        meta = {
            "id": pid,
            "name": (name or "New project").strip()[:80],
            "description": (description or "").strip()[:240],
            "created": now,
            "updated": now,
        }
        self._write_meta(pid, meta)
        return meta

    def rename(self, pid: str, name: str) -> dict | None:
        meta = self.get(pid)
        if not meta:
            return None
        meta["name"] = (name or meta["name"]).strip()[:80]
        self._write_meta(pid, meta)
        return meta

    def set_description(self, pid: str, description: str) -> dict | None:
        meta = self.get(pid)
        if not meta:
            return None
        meta["description"] = (description or "").strip()[:240]
        self._write_meta(pid, meta)
        return meta

    def delete(self, pid: str) -> None:
        root = self.root(pid)
        if not root.is_dir():
            return
        bin_root = TRASH_DIR / "projects"
        bin_root.mkdir(parents=True, exist_ok=True)
        target = bin_root / f"{pid}-{int(time.time())}"
        shutil.move(str(root), str(target))

    # ------------------------------------------------------------------ #
    def safe_path(self, pid: str, rel: str) -> Path:
        rel = (rel or "").replace("\\", "/").strip()
        if rel.startswith("/") or re.match(r"^[A-Za-z]:", rel):
            raise ValueError("Use a path relative to the project")
        root = self.root(pid).resolve()
        target = (root / rel).resolve()
        if target != root and root not in target.parents:
            raise ValueError("Path escapes the project")
        if target.name == META_NAME:
            raise ValueError("That is Takoyo's project file")
        return target

    def files(self, pid: str, max_files: int = 800) -> list[dict]:
        root = self.root(pid)
        if not root.is_dir():
            return []
        out: list[dict] = []
        for path in root.rglob("*"):
            if len(out) >= max_files:
                break
            try:
                rel_parts = path.relative_to(root).parts
                if any(part in _SKIP_DIRS or part.startswith(".") for part in rel_parts[:-1]):
                    continue
                if path.name == META_NAME:
                    continue
                if path.is_dir():
                    continue
                stat = path.stat()
                rel = path.relative_to(root).as_posix()
                out.append(
                    {
                        "path": rel,
                        "name": path.name,
                        "size": stat.st_size,
                        "modified": stat.st_mtime,
                        "text": path.suffix.lower() in _TEXT_EXT or stat.st_size == 0,
                    }
                )
            except (OSError, ValueError):
                continue
        out.sort(key=lambda f: f["modified"], reverse=True)
        return out

    def read_text(self, pid: str, rel: str, limit: int = 400_000) -> str:
        path = self.safe_path(pid, rel)
        if not path.is_file():
            raise FileNotFoundError(rel)
        data = path.read_bytes()[: limit + 1]
        if not _looks_text(data):
            raise ValueError("This file looks binary")
        text = data[:limit].decode("utf-8", "replace")
        if len(data) > limit:
            text += "\n… (truncated)"
        return text

    def read_binary(self, pid: str, rel: str) -> bytes:
        path = self.safe_path(pid, rel)
        if not path.is_file():
            raise FileNotFoundError(rel)
        return path.read_bytes()

    def write_text(self, pid: str, rel: str, content: str) -> dict:
        path = self.safe_path(pid, rel)
        if not path.suffix and not path.name:
            raise ValueError("Choose a file name")
        path.parent.mkdir(parents=True, exist_ok=True)
        text = (content or "").replace("\r\n", "\n")
        if len(text.encode("utf-8")) > 2_000_000:
            raise ValueError("File is too large to save from the editor")
        path.write_text(text, "utf-8", newline="\n")
        meta = self.get(pid)
        if meta:
            self._write_meta(pid, meta)
        return {"path": rel, "size": path.stat().st_size}

    def create_file(self, pid: str, rel: str, content: str = "") -> dict:
        path = self.safe_path(pid, rel)
        if path.exists():
            raise FileExistsError(rel)
        return self.write_text(pid, rel, content)

    def delete_file(self, pid: str, rel: str) -> None:
        path = self.safe_path(pid, rel)
        if not path.exists():
            return
        bin_root = TRASH_DIR / "files" / pid
        target = bin_root / f"{int(time.time())}-{path.name}"
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(path), str(target))
        meta = self.get(pid)
        if meta:
            self._write_meta(pid, meta)

    # ------------------------------------------------------------------ #
    def tree_text(self, pid: str, max_entries: int = 160) -> str:
        files = [f for f in self.files(pid, max_files=max_entries) if not _is_secret(f["path"])]
        if not files:
            return "(empty project)"
        return "\n".join(f"{f['path']}  ({f['size']} B)" for f in files)

    def context_for_ai(self, pid: str, prompt: str = "", max_chars: int = 22_000) -> str:
        meta = self.get(pid)
        if not meta:
            return ""
        files = [f for f in self.files(pid, max_files=300) if not _is_secret(f["path"])]
        prompt_l = (prompt or "").lower()
        mentioned = [f for f in files if f["name"].lower() in prompt_l or f["path"].lower() in prompt_l]
        mentioned_paths = {f["path"] for f in mentioned}
        candidates = mentioned + [
            f
            for f in files
            if f["path"] not in mentioned_paths and f["text"] and f["size"] <= 200_000
        ]
        chunks: list[str] = [
            f'Open project: "{meta.get("name")}" ({pid})',
        ]
        if meta.get("description"):
            chunks.append("Description: " + meta["description"])
        chunks.append("File tree:\n" + self.tree_text(pid))
        budget = max_chars - sum(len(c) for c in chunks) - 400
        included = 0
        for f in candidates:
            if included >= 7 or budget <= 0:
                break
            if not f["text"] or f["size"] > 200_000:
                continue
            try:
                content = self.read_text(pid, f["path"], limit=6000)
            except Exception:
                continue
            if budget - len(content) - len(f["path"]) - 20 < 0:
                content = content[: max(0, budget - 300)]
            if not content:
                break
            chunks.append(f'\n--- file: {f["path"]} ---\n{content}\n--- end: {f["path"]} ---')
            budget -= len(content) + len(f["path"]) + 40
            included += 1
        if included == 0:
            chunks.append("(No text files have been added yet.)")
        return "\n\n".join(chunks)
