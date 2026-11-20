"""Local conversation history. Unlimited chats, stored on this computer."""

from __future__ import annotations

import json
import os
import re
import threading
import time
import uuid
from pathlib import Path

from .config import CONVERSATIONS_DIR, TRASH_DIR, ensure_dirs

_ID_RE = re.compile(r"^[A-Za-z0-9._-]{3,80}$")


def _clean_title(text: str, limit: int = 48) -> str:
    one_line = re.sub(r"\s+", " ", (text or "").strip())
    if one_line.startswith("```"):
        return "New chat"
    if len(one_line) > limit:
        one_line = one_line[: limit - 1].rstrip() + "…"
    return one_line or "New chat"


class ConversationStore:
    def __init__(self, directory: Path | None = None):
        ensure_dirs()
        self.dir = Path(directory) if directory else CONVERSATIONS_DIR
        self.dir.mkdir(parents=True, exist_ok=True)
        self._lock = threading.RLock()
        self._recovery_errors: list[str] = []

    def _read_conversation(self, path: Path) -> dict | None:
        try:
            data = json.loads(path.read_text("utf-8"))
            messages = data.get("messages") if isinstance(data, dict) else None
            if isinstance(data, dict) and isinstance(messages, list) and all(
                isinstance(message, dict) for message in messages
            ):
                data["title"] = str(data.get("title") or "New chat")
                data["project_id"] = str(data.get("project_id") or "")
                return data
            raise ValueError("conversation has an invalid shape")
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            quarantine = TRASH_DIR / "conversations" / "corrupt"
            quarantine.mkdir(parents=True, exist_ok=True)
            target = quarantine / f"{path.stem}-{int(time.time() * 1000)}.corrupt"
            try:
                os.replace(path, target)
                self._recovery_errors.append(f"{path.name}: {exc}")
            except OSError as move_error:
                self._recovery_errors.append(f"{path.name}: {exc}; quarantine failed: {move_error}")
            return None

    def take_recovery_errors(self) -> list[str]:
        errors, self._recovery_errors = self._recovery_errors, []
        return errors

    # ------------------------------------------------------------------ #
    def _path(self, cid: str) -> Path:
        if not _ID_RE.match(cid or ""):
            raise ValueError("Invalid conversation id")
        return self.dir / f"{cid}.json"

    def _write(self, conv: dict) -> None:
        conv["updated"] = time.time()
        path = self._path(conv["id"])
        tmp = path.with_suffix(".json.tmp")
        with self._lock:
            tmp.write_text(json.dumps(conv, indent=1, ensure_ascii=False), "utf-8")
            os.replace(tmp, path)

    # ------------------------------------------------------------------ #
    def create(self, title: str = "New chat", project_id: str = "", first_message: str = "") -> dict:
        now = time.time()
        cid = time.strftime("%Y%m%d-%H%M%S", time.localtime(now)) + "-" + uuid.uuid4().hex[:6]
        conv = {
            "id": cid,
            "title": _clean_title(first_message or title),
            "project_id": project_id or "",
            "created": now,
            "updated": now,
            "messages": [],
        }
        self._write(conv)
        return conv

    def get(self, cid: str) -> dict | None:
        if not cid:
            return None
        try:
            path = self._path(cid)
        except ValueError:
            return None
        if not path.exists():
            return None
        data = self._read_conversation(path)
        if data is None:
            return None
        data.setdefault("title", "New chat")
        data.setdefault("project_id", "")
        return data

    def save(self, conv: dict) -> dict:
        self._write(conv)
        return conv

    def append(self, cid: str, role: str, content: str, **extra) -> dict | None:
        conv = self.get(cid)
        if not conv:
            return None
        message = {"role": role, "content": content, "ts": time.time()}
        message.update({k: v for k, v in extra.items() if v is not None})
        conv["messages"].append(message)
        if role == "user" and conv.get("title") in ("", "New chat"):
            conv["title"] = _clean_title(content)
        self._write(conv)
        return conv

    def update_message(self, cid: str, index: int, **fields) -> dict | None:
        conv = self.get(cid)
        if not conv or not (0 <= index < len(conv["messages"])):
            return None
        conv["messages"][index].update(fields)
        self._write(conv)
        return conv

    def rename(self, cid: str, title: str) -> None:
        conv = self.get(cid)
        if not conv:
            return
        conv["title"] = _clean_title(title, 80)
        self._write(conv)

    def delete(self, cid: str) -> None:
        try:
            path = self._path(cid)
            if not path.exists():
                return
            bin_dir = TRASH_DIR / "conversations"
            bin_dir.mkdir(parents=True, exist_ok=True)
            target = bin_dir / f"{cid}-{int(time.time())}.json"
            os.replace(path, target)
        except Exception:
            pass

    # ------------------------------------------------------------------ #
    def summaries(self, project_id: str | None = None, query: str = "") -> list[dict]:
        query = (query or "").strip().lower()
        items: list[dict] = []
        for path in self.dir.glob("*.json"):
            data = self._read_conversation(path)
            if data is None:
                continue
            pid = data.get("project_id", "")
            if project_id is not None and project_id != "__all__" and pid != project_id:
                continue
            title = data.get("title", "New chat")
            messages = data.get("messages", [])
            preview = ""
            for msg in reversed(messages):
                if msg.get("role") in ("user", "assistant") and msg.get("content"):
                    preview = re.sub(r"\s+", " ", msg["content"]).strip()[:90]
                    break
            if query and query not in title.lower() and query not in preview.lower():
                continue
            items.append(
                {
                    "id": data.get("id", path.stem),
                    "title": title,
                    "project_id": pid,
                    "updated": data.get("updated", 0),
                    "message_count": len(messages),
                    "preview": preview,
                    "has_image": any(m.get("images") or m.get("image") for m in messages),
                }
            )
        items.sort(key=lambda x: x.get("updated", 0), reverse=True)
        return items

    def latest_for_project(self, project_id: str) -> dict | None:
        summaries = self.summaries(project_id=project_id)
        return self.get(summaries[0]["id"]) if summaries else None

    def clear_all(self) -> int:
        count = 0
        for path in self.dir.glob("*.json"):
            try:
                self.delete(path.stem)
                count += 1
            except Exception:
                pass
        return count

    def export_to(self, destination: str | Path) -> int:
        conversations = []
        for path in sorted(self.dir.glob("*.json")):
            data = self._read_conversation(path)
            if data is not None:
                conversations.append(data)
        payload = {"format": "takoyo-conversations", "version": 1, "conversations": conversations}
        destination = Path(destination)
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_suffix(destination.suffix + ".tmp")
        temporary.write_text(json.dumps(payload, ensure_ascii=False, indent=1), "utf-8")
        os.replace(temporary, destination)
        return len(conversations)

    def backup(self, destination: str | Path) -> int:
        return self.export_to(destination)

    def restore_from(self, source: str | Path) -> int:
        try:
            payload = json.loads(Path(source).read_text("utf-8"))
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            raise ValueError(f"Could not read conversation backup: {exc}") from exc
        if (not isinstance(payload, dict) or payload.get("format") != "takoyo-conversations"
                or payload.get("version") != 1 or not isinstance(payload.get("conversations"), list)):
            raise ValueError("Unsupported conversation backup format")
        restored = 0
        for original in payload["conversations"]:
            if (not isinstance(original, dict) or not isinstance(original.get("messages"), list)
                    or not all(isinstance(message, dict) for message in original["messages"])):
                continue
            conversation = dict(original)
            cid = str(conversation.get("id", ""))
            try:
                path = self._path(cid)
            except ValueError:
                continue
            if path.exists():
                conversation["id"] = time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:6]
            self._write(conversation)
            restored += 1
        return restored
