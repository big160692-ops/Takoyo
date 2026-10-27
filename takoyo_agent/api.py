"""Takoyo's local, token-free HTTP API (standard library only)."""

from __future__ import annotations

import base64
import json
import mimetypes
import os
import shutil
import threading
import time
import urllib.parse
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from . import ai_providers, code_tools, image_gen, model_catalog, prompting
from .config import HOME, IMAGES_DIR, PROJECTS_DIR, TRASH_DIR, WORKSPACE_DIR, Config, ensure_dirs
from .local_ai import LocalAIError, LocalAINotReady, get_engine
from .memory import ConversationStore
from .projects import ProjectStore

MAX_BODY = 8 * 1024 * 1024
_STARTED_AT = time.time()
_LOCAL_JOBS: dict = {}
_JOBS_LOCK = threading.Lock()


class ApiError(Exception):
    def __init__(self, message: str, status: int = 400):
        super().__init__(message)
        self.status = status


def _safe_under(root: Path, rel: str) -> Path:
    raw = (rel or ".").strip()
    if raw.startswith(("http://", "https://")):
        raise ApiError("Only local paths are supported")
    path = Path(raw)
    target = (path if path.is_absolute() else root / path)
    try:
        target = target.resolve()
        root = root.resolve()
    except Exception as exc:
        raise ApiError(f"Invalid path: {exc}") from exc
    if target != root and root not in target.parents:
        raise ApiError("Path escapes Takoyo's home folder", 403)
    return target


def _writable_under(rel: str) -> Path:
    target = _safe_under(HOME, rel)
    allowed = [PROJECTS_DIR.resolve(), WORKSPACE_DIR.resolve()]
    if not any(target == a or a in target.parents for a in allowed):
        raise ApiError("Writes are limited to the workspace and projects folders", 403)
    return target


def _matches_target(target: Path, root: Path) -> bool:
    target = target.resolve()
    root = root.resolve()
    return target == root or root in target.parents


def _trash(target: Path, category: str) -> Path:
    bin_dir = TRASH_DIR / category
    bin_dir.mkdir(parents=True, exist_ok=True)
    dest = bin_dir / f"{int(time.time())}-{target.name}"
    shutil.move(str(target), str(dest))
    return dest


def _image_payload(item: dict, host: str, b64: bool = False) -> dict:
    path = Path(item.get("path", ""))
    payload = {
        "prompt": item.get("prompt", ""),
        "provider": item.get("provider", "takoyo"),
        "model": item.get("model", "Takoyo Art"),
        "width": item.get("width", 0),
        "height": item.get("height", 0),
        "seed": item.get("seed"),
        "created": item.get("created", 0),
    }
    if b64:
        try:
            payload["b64_json"] = base64.b64encode(path.read_bytes()).decode("ascii")
        except Exception:
            payload["b64_json"] = ""
    payload["url"] = f"http://{host}/images/{urllib.parse.quote(path.name)}"
    payload["path"] = str(path)
    return payload


class TakoyoAPIHandler(BaseHTTPRequestHandler):
    server_version = "TakoyoAPI/1.0"
    protocol_version = "HTTP/1.1"
    config: Config
    conversations: ConversationStore
    projects: ProjectStore
    images: image_gen.ImageGenerator

    # ------------------------------------------------------------------ #
    def log_message(self, fmt, *args):  # quiet
        log_path = self.config.get("api_log", "")
        if log_path:
            try:
                with open(log_path, "a", encoding="utf-8") as handle:
                    handle.write("%s - %s\n" % (self.address_string(), fmt % args))
            except Exception:
                pass

    def _cors(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, Authorization")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("X-Takoyo", "local")

    def _send_json(self, data, status: int = 200):
        raw = json.dumps(data, ensure_ascii=False, default=str).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self._cors()
        self.end_headers()
        try:
            self.wfile.write(raw)
        except (BrokenPipeError, ConnectionAbortedError):
            pass

    def _error(self, message: str, status: int = 400):
        self._send_json({"error": {"message": message, "type": "takoyo_error"}}, status=status)

    def _read_body(self) -> dict:
        length = int(self.headers.get("Content-Length") or 0)
        if length > MAX_BODY:
            raise ApiError("Request body is too large", 413)
        if length <= 0:
            return {}
        raw = self.rfile.read(length)
        try:
            data = json.loads(raw.decode("utf-8"))
        except Exception:
            raise ApiError("Body must be valid JSON")
        if not isinstance(data, dict):
            raise ApiError("Body must be a JSON object")
        return data

    def _host(self) -> str:
        return self.headers.get("Host") or f"{self.server.server_address[0]}:{self.server.server_address[1]}"

    # ------------------------------------------------------------------ #
    def do_OPTIONS(self):
        self.send_response(204)
        self._cors()
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path.rstrip("/") or "/"
        query = urllib.parse.parse_qs(parsed.query)
        try:
            if path == "/":
                return self._send_json(
                    {
                        "name": "Takoyo",
                        "version": "1.0",
                        "auth": "none (keep this on 127.0.0.1)",
                        "endpoints": [
                            "GET /health", "GET /models", "GET /v1/models",
                            "POST /chat", "POST /v1/chat/completions",
                            "POST /images/generations", "POST /v1/images/generations",
                            "GET /images", "GET /images/<file>",
                            "GET /files?path=.", "GET /files/read?path=...",
                            "POST /files/write", "POST /files/delete",
                            "POST /run", "GET /local", "POST /local/setup", "POST /local/stop",
                        ],
                    }
                )
            if path == "/health":
                return self._send_json(
                    {
                        "status": "ok",
                        "uptime": round(time.time() - _STARTED_AT, 1),
                        "provider": self.config.get("provider"),
                        "local": get_engine(self.config).running(),
                    }
                )
            if path == "/models":
                return self._send_json(self._models_payload())
            if path == "/v1/models":
                return self._send_json(self._openai_models_payload())
            if path == "/images":
                return self._send_json({"data": [_image_payload(item, self._host()) for item in self.images.list_images()]})
            if path.startswith("/images/"):
                return self._serve_image(path.split("/images/", 1)[1])
            if path == "/files":
                return self._send_json(self._list_files(query.get("path", ["."])[0]))
            if path == "/files/read":
                return self._send_json(self._read_file(query.get("path", [""])[0]))
            if path == "/local":
                status = get_engine(self.config).status()
                status["job"] = _LOCAL_JOBS.get("status", "idle")
                status["job_error"] = _LOCAL_JOBS.get("error", "")
                return self._send_json(status)
            return self._error("Not found", 404)
        except ApiError as exc:
            return self._error(str(exc), exc.status)
        except Exception as exc:
            return self._error(f"Unexpected error: {exc}", 500)

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path.rstrip("/") or "/"
        try:
            body = self._read_body()
            if path == "/chat":
                return self._chat(body)
            if path == "/v1/chat/completions":
                return self._openai_chat(body)
            if path in ("/images/generations", "/v1/images/generations"):
                return self._generate_images(body, openai=path.startswith("/v1"))
            if path == "/files/write":
                return self._write_file(body)
            if path == "/files/delete":
                return self._delete_file(body)
            if path == "/run":
                return self._run(body)
            if path == "/local/setup":
                return self._local_setup(body)
            if path == "/local/stop":
                get_engine(self.config).stop()
                return self._send_json({"ok": True, "running": False})
            return self._error("Not found", 404)
        except ApiError as exc:
            return self._error(str(exc), exc.status)
        except Exception as exc:
            return self._error(f"Unexpected error: {exc}", 500)

    # ------------------------------------------------------------------ #
    def _models_payload(self) -> dict:
        providers = {}
        for key, info in model_catalog.PROVIDERS.items():
            providers[key] = {
                "label": info["label"],
                "models": info["models"],
                "default": self.config.model_for(key),
                "configured": key == "local" or key == "ollama" or bool(self.config.api_key(key)),
            }
        return {"default": self.config.get("provider"), "providers": providers, "local": get_engine(self.config).status()}

    def _openai_models_payload(self) -> dict:
        now = int(time.time())
        items = []
        local = get_engine(self.config)
        for key in model_catalog.LOCAL_MODELS:
            items.append({"id": key, "object": "model", "created": now, "owned_by": "takoyo", "installed": local.model_installed(key)})
        for provider, info in model_catalog.PROVIDERS.items():
            if provider == "local":
                continue
            for model in info["models"]:
                items.append({"id": model, "object": "model", "created": now, "owned_by": provider})
        return {"object": "list", "data": items}

    def _serve_image(self, name: str):
        name = urllib.parse.unquote(name)
        target = (IMAGES_DIR / name).resolve()
        if target.parent != IMAGES_DIR.resolve() or not target.is_file():
            return self._error("Image not found", 404)
        mime = mimetypes.guess_type(target.name)[0] or "application/octet-stream"
        data = target.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "max-age=3600")
        self._cors()
        self.end_headers()
        try:
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionAbortedError):
            pass

    # ------------------------------------------------------------------ #
    def _build_history(self, body: dict) -> tuple[list[dict], str, dict | None]:
        cid = str(body.get("conversation_id", "") or "")
        message = body.get("message")
        raw_messages = body.get("messages")
        conv = self.conversations.get(cid) if cid else None
        history: list[dict] = []
        if conv:
            for item in conv.get("messages", []):
                content = item.get("content", "")
                if item.get("image"):
                    content += f"\n\n[Earlier generated image: {item['image']}]"
                history.append({"role": item.get("role", "user"), "content": content})
        if isinstance(raw_messages, list):
            for item in raw_messages:
                if not isinstance(item, dict):
                    continue
                role = item.get("role", "user")
                content = item.get("content", "")
                if isinstance(content, list):
                    content = " ".join(part.get("text", "") for part in content if isinstance(part, dict))
                if role in ("user", "assistant", "system") and isinstance(content, str) and content.strip():
                    history.append({"role": role, "content": content})
        elif isinstance(message, str) and message.strip():
            history.append({"role": "user", "content": message.strip()})
        if not history:
            raise ApiError("Provide 'message' or 'messages'")
        system_override = str(body.get("system", "") or "")
        user_text = history[-1]["content"] if history and history[-1]["role"] == "user" else ""
        project_id = str(body.get("project_id", "") or "")
        project_context = ""
        project_name = ""
        if project_id:
            meta = self.projects.get(project_id)
            if not meta:
                raise ApiError("Project not found", 404)
            project_name = meta.get("name", project_id)
            project_context = self.projects.context_for_ai(project_id, prompt=user_text)
        system = prompting.build_system_prompt(project_name, project_context, project_id)
        if system_override:
            system = system_override + "\n\n" + system
        filtered = [m for m in history if m["role"] != "system"]
        if any(m["role"] == "system" for m in history):
            system = "\n\n".join(m["content"] for m in history if m["role"] == "system") + "\n\n" + system
        messages = prompting.build_messages(system, filtered, max_history_chars=14_000)
        return messages, cid, conv

    def _chat(self, body: dict):
        messages, cid, conv = self._build_history(body)
        provider = body.get("provider") or self.config.get("provider")
        model = body.get("model") or None
        temperature = body.get("temperature")
        max_tokens = body.get("max_tokens")
        try:
            reply = ai_providers.complete_chat(
                self.config,
                messages,
                provider=provider,
                model=model,
                temperature=float(temperature) if temperature is not None else None,
                max_tokens=int(max_tokens) if max_tokens is not None else None,
            )
        except LocalAINotReady as exc:
            raise ApiError(str(exc), 503)
        except LocalAIError as exc:
            raise ApiError(str(exc), 502)
        except ai_providers.ProviderError as exc:
            raise ApiError(str(exc), 502)
        saved = None
        if cid:
            if not conv:
                conv = self.conversations.create(project_id=str(body.get("project_id", "") or ""), first_message=messages[-1]["content"][:80])
                cid = conv["id"]
            self.conversations.append(cid, "user", messages[-1]["content"])
            self.conversations.append(cid, "assistant", reply)
            saved = cid
        return self._send_json(
            {
                "reply": reply,
                "conversation_id": saved,
                "provider": provider,
                "model": model or self.config.model_for(provider),
            }
        )

    def _openai_chat(self, body: dict):
        raw = body.get("messages")
        if not isinstance(raw, list) or not raw:
            raise ApiError("'messages' must be a non-empty list")
        messages = []
        has_system = False
        for item in raw:
            if not isinstance(item, dict):
                continue
            role = item.get("role")
            content = item.get("content")
            if isinstance(content, list):
                content = " ".join(p.get("text", "") for p in content if isinstance(p, dict))
            if role not in ("system", "user", "assistant") or not isinstance(content, str):
                continue
            has_system = has_system or role == "system"
            messages.append({"role": role, "content": content})
        if not messages:
            raise ApiError("No valid messages")
        if not has_system:
            messages.insert(0, {"role": "system", "content": prompting.BASE})
        provider = body.get("provider") or self.config.get("provider")
        model = body.get("model") or self.config.model_for(provider)
        stream = bool(body.get("stream"))
        temperature = body.get("temperature")
        max_tokens = body.get("max_tokens") or body.get("max_completion_tokens")
        chunk_id = "chatcmpl-" + uuid.uuid4().hex[:24]
        created = int(time.time())

        if not stream:
            try:
                text = ai_providers.complete_chat(
                    self.config,
                    messages,
                    provider=provider,
                    model=model,
                    temperature=float(temperature) if temperature is not None else None,
                    max_tokens=int(max_tokens) if max_tokens is not None else None,
                )
            except (LocalAINotReady, LocalAIError, ai_providers.ProviderError) as exc:
                raise ApiError(str(exc), 503 if isinstance(exc, LocalAINotReady) else 502)
            return self._send_json(
                {
                    "id": chunk_id,
                    "object": "chat.completion",
                    "created": created,
                    "model": model,
                    "choices": [
                        {
                            "index": 0,
                            "message": {"role": "assistant", "content": text},
                            "finish_reason": "stop",
                        }
                    ],
                    "usage": {"prompt_tokens": 0, "completion_tokens": 0, "total_tokens": 0},
                }
            )

        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "close")
        self._cors()
        self.end_headers()
        self.close_connection = True

        def send(obj):
            try:
                self.wfile.write(("data: " + json.dumps(obj, ensure_ascii=False) + "\n\n").encode("utf-8"))
                self.wfile.flush()
            except (BrokenPipeError, ConnectionAbortedError):
                raise

        try:
            send(
                {
                    "id": chunk_id,
                    "object": "chat.completion.chunk",
                    "created": created,
                    "model": model,
                    "choices": [{"index": 0, "delta": {"role": "assistant"}, "finish_reason": None}],
                }
            )
            for delta in ai_providers.stream_chat(self.config, messages, provider=provider, model=model, temperature=float(temperature) if temperature is not None else None, max_tokens=int(max_tokens) if max_tokens is not None else None):
                send(
                    {
                        "id": chunk_id,
                        "object": "chat.completion.chunk",
                        "created": created,
                        "model": model,
                        "choices": [{"index": 0, "delta": {"content": delta}, "finish_reason": None}],
                    }
                )
            send(
                {
                    "id": chunk_id,
                    "object": "chat.completion.chunk",
                    "created": created,
                    "model": model,
                    "choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}],
                }
            )
            self.wfile.write(b"data: [DONE]\n\n")
            self.wfile.flush()
        except (BrokenPipeError, ConnectionAbortedError):
            pass
        except Exception as exc:
            try:
                send({"error": {"message": str(exc), "type": "takoyo_error"}})
            except Exception:
                pass

    # ------------------------------------------------------------------ #
    def _generate_images(self, body: dict, openai: bool = False):
        prompt = body.get("prompt")
        if not isinstance(prompt, str) or not prompt.strip():
            raise ApiError("Provide a 'prompt'")
        provider = body.get("provider") or self.config.get("image_provider", "takoyo")
        size = body.get("size") or self.config.get("image_size", "1024x1280")
        want_b64 = body.get("response_format") == "b64_json"
        count = int(body.get("n", 1) or 1)
        count = max(1, min(4, count))
        results = []
        try:
            for _ in range(count):
                results.append(self.images.generate(prompt, provider=provider, size=size))
        except image_gen.ImageError as exc:
            raise ApiError(str(exc), 502)
        payloads = [_image_payload(item, self._host(), b64=want_b64) for item in results]
        if openai:
            data = []
            for payload in payloads:
                item = {"revised_prompt": payload["prompt"]}
                if want_b64:
                    item["b64_json"] = payload.get("b64_json", "")
                else:
                    item["url"] = payload["url"]
                data.append(item)
            return self._send_json({"created": int(time.time()), "data": data})
        return self._send_json({"created": int(time.time()), "data": payloads})

    # ------------------------------------------------------------------ #
    def _list_files(self, rel: str) -> dict:
        target = _safe_under(HOME, rel)
        if not target.exists():
            raise ApiError("Path not found", 404)
        if not target.is_dir():
            raise ApiError("Path is not a directory")
        items = []
        try:
            for child in sorted(target.iterdir(), key=lambda p: (not p.is_dir(), p.name.lower())):
                try:
                    stat = child.stat()
                    items.append(
                        {
                            "name": child.name,
                            "path": str(child.relative_to(HOME)).replace("\\", "/"),
                            "is_dir": child.is_dir(),
                            "size": stat.st_size,
                            "modified": stat.st_mtime,
                        }
                    )
                except OSError:
                    continue
        except PermissionError:
            raise ApiError("Permission denied", 403)
        return {"path": rel, "items": items}

    def _read_file(self, rel: str) -> dict:
        if not rel:
            raise ApiError("Provide 'path'")
        target = _safe_under(HOME, rel)
        if not target.is_file():
            raise ApiError("File not found", 404)
        data = target.read_bytes()
        if len(data) > 4 * 1024 * 1024:
            raise ApiError("File is too large (4 MB limit)")
        try:
            text = data.decode("utf-8")
            return {"path": rel, "content": text, "encoding": "utf-8", "size": len(data)}
        except UnicodeDecodeError:
            return {"path": rel, "content": base64.b64encode(data).decode("ascii"), "encoding": "base64", "size": len(data)}

    def _write_file(self, body: dict):
        rel = body.get("path")
        if not isinstance(rel, str) or not rel.strip():
            raise ApiError("Provide 'path'")
        target = _writable_under(rel)
        content = body.get("content", "")
        encoding = body.get("encoding", "utf-8")
        if not isinstance(content, str):
            raise ApiError("'content' must be a string")
        try:
            data = base64.b64decode(content) if encoding == "base64" else content.encode("utf-8")
        except Exception as exc:
            raise ApiError(f"Invalid content: {exc}")
        if len(data) > 4 * 1024 * 1024:
            raise ApiError("File is too large (4 MB limit)")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        return self._send_json({"ok": True, "path": str(target.relative_to(HOME)).replace("\\", "/"), "size": len(data)})

    def _delete_file(self, body: dict):
        rel = body.get("path")
        if not isinstance(rel, str) or not rel.strip():
            raise ApiError("Provide 'path'")
        target = _writable_under(rel)
        if not target.exists():
            raise ApiError("Path not found", 404)
        if target == WORKSPACE_DIR.resolve() or target == PROJECTS_DIR.resolve():
            raise ApiError("Refusing to delete a root folder", 403)
        if target.is_dir() and not body.get("recursive"):
            raise ApiError("Directory deletion needs \"recursive\": true")
        dest = _trash(target, "api")
        return self._send_json({"ok": True, "trashed": str(dest)})

    def _run(self, body: dict):
        if not self.config.get("allow_code_execution", True):
            raise ApiError("Code execution is disabled in Settings", 403)
        if isinstance(body.get("code"), str):
            return self._send_json(code_tools.run_python(body["code"], timeout=int(body.get("timeout", 30) or 30)))
        if isinstance(body.get("command"), (str, list)):
            return self._send_json(code_tools.run_command(body["command"], timeout=int(body.get("timeout", 60) or 60)))
        raise ApiError("Provide 'code' or 'command'")

    def _local_setup(self, body: dict):
        engine = get_engine(self.config)
        requested = str(body.get("model", "") or self.config.get("local_model", ""))
        model = model_catalog.local_model_by_name(requested) or model_catalog.LOCAL_MODELS.get(requested)
        if not model:
            raise ApiError("Unknown model. Use one of: " + ", ".join(model_catalog.LOCAL_MODELS))
        with _JOBS_LOCK:
            if _LOCAL_JOBS.get("status") == "installing":
                return self._send_json({"ok": True, "status": "installing", "model": _LOCAL_JOBS.get("model")})
            _LOCAL_JOBS.update({"status": "installing", "model": model.key, "error": ""})

        def worker():
            try:
                engine.install(model.key)
                engine.start(model.key)
                with _JOBS_LOCK:
                    _LOCAL_JOBS["status"] = "ready"
            except Exception as exc:
                with _JOBS_LOCK:
                    _LOCAL_JOBS["status"] = "error"
                    _LOCAL_JOBS["error"] = str(exc)

        threading.Thread(target=worker, daemon=True, name="takoyo-local-setup").start()
        return self._send_json({"ok": True, "status": "installing", "model": model.key})


class TakoyoHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def start_api(config: Config | None = None, host: str | None = None, port: int | None = None):
    ensure_dirs()
    config = config or Config()
    host = host or str(config.get("api_host", "127.0.0.1"))
    port = int(port or config.get("api_port", 8765) or 8765)
    conversations = ConversationStore()
    projects = ProjectStore()
    images = image_gen.ImageGenerator(config)
    engine = get_engine(config)

    class Handler(TakoyoAPIHandler):
        pass

    Handler.config = config
    Handler.conversations = conversations
    Handler.projects = projects
    Handler.images = images

    server = TakoyoHTTPServer((host, port), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True, name="takoyo-api")
    thread.start()
    return server, thread


def stop_api(server: TakoyoHTTPServer | None) -> None:
    if server is None:
        return
    try:
        server.shutdown()
    except Exception:
        pass
    try:
        server.server_close()
    except Exception:
        pass


def run_api_server(host: str | None = None, port: int | None = None) -> None:
    config = Config()
    server, _thread = start_api(config=config, host=host, port=port)
    address = server.server_address
    print(f"Takoyo API listening on http://{address[0]}:{address[1]}")
    print("No authentication: keep it on 127.0.0.1. Ctrl+C to stop.")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nStopping Takoyo API…")
    finally:
        stop_api(server)
        get_engine(config).shutdown()
