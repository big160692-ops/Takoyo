"""Chat providers: Takoyo's built-in engine plus optional cloud and Ollama models."""

from __future__ import annotations

import json
import urllib.error
import urllib.parse
import urllib.request
from typing import Iterator

from . import model_catalog
from .config import Config
from .local_ai import LocalAIError, get_engine

USER_AGENT = "Takoyo/1.0"


class ProviderError(RuntimeError):
    pass


class CancelToken:
    def __init__(self):
        import threading

        self._event = threading.Event()

    def cancel(self) -> None:
        self._event.set()

    def is_set(self) -> bool:
        return self._event.is_set()

    cancelled = property(is_set)


# --------------------------------------------------------------------------- #
# HTTP helpers
# --------------------------------------------------------------------------- #
def _open(request: urllib.request.Request, timeout: int = 300):
    try:
        return urllib.request.urlopen(request, timeout=timeout)
    except urllib.error.HTTPError as exc:
        body = ""
        try:
            body = exc.read().decode("utf-8", "replace")
        except Exception:
            pass
        message = body
        try:
            data = json.loads(body)
            err = data.get("error")
            if isinstance(err, dict):
                message = err.get("message") or err.get("status") or body
            elif isinstance(err, str):
                message = err
        except Exception:
            pass
        if exc.code == 401:
            message = "The API key was rejected. Check it in Settings."
        elif exc.code == 403:
            message = "This key is not allowed to use that model."
        elif exc.code == 404:
            message = "The model or endpoint was not found. Check the model name and base URL."
        elif exc.code == 429:
            message = "Rate limited by the provider. Wait a moment and try again."
        raise ProviderError(f"{message[:600]} (HTTP {exc.code})") from exc
    except urllib.error.URLError as exc:
        raise ProviderError(f"Could not reach the provider: {exc.reason}") from exc
    except Exception as exc:
        raise ProviderError(f"Network error: {exc}") from exc


def _post_json(url: str, payload: dict, headers: dict, timeout: int = 300):
    data = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=data,
        headers={"Content-Type": "application/json", "User-Agent": USER_AGENT, **headers},
        method="POST",
    )
    return _open(request, timeout=timeout)


def _iter_sse(response) -> Iterator[str]:
    pending: list[str] = []
    try:
        for raw in response:
            line = raw.decode("utf-8", "replace").rstrip("\r\n")
            if not line:
                if pending:
                    yield "\n".join(pending)
                    pending = []
                continue
            if line.startswith(":"):
                continue
            if line.startswith("data:"):
                pending.append(line[5:].lstrip())
        if pending:
            yield "\n".join(pending)
    finally:
        try:
            response.close()
        except Exception:
            pass


def _iter_lines(response) -> Iterator[str]:
    try:
        for raw in response:
            line = raw.decode("utf-8", "replace").strip()
            if line:
                yield line
    finally:
        try:
            response.close()
        except Exception:
            pass


def _canceled(cancel) -> bool:
    return bool(cancel is not None and getattr(cancel, "is_set", lambda: False)())


# --------------------------------------------------------------------------- #
# Key & model helpers
# --------------------------------------------------------------------------- #
def _require_key(config: Config, provider: str) -> str:
    key = config.api_key(provider)
    if not key:
        label = model_catalog.PROVIDERS[provider]["label"]
        raise ProviderError(f"Add your {label} API key in Settings to use this provider.")
    return key


def _model(config: Config, provider: str, model: str | None) -> str:
    chosen = (model or "").strip() or config.model_for(provider)
    if not chosen:
        defaults = model_catalog.PROVIDERS[provider]["models"]
        chosen = defaults[0] if defaults else ""
    return chosen


# --------------------------------------------------------------------------- #
# Providers
# --------------------------------------------------------------------------- #
def _openai_stream(config: Config, messages: list[dict], model: str, temperature: float, max_tokens: int, cancel) -> Iterator[str]:
    key = _require_key(config, "openai")
    payload = {
        "model": model,
        "messages": messages,
        "temperature": float(temperature),
        "stream": True,
    }
    if model.startswith(("o1", "o3", "o4", "gpt-5")):
        payload["max_completion_tokens"] = int(max_tokens)
    else:
        payload["max_tokens"] = int(max_tokens)
    request_headers = {"Authorization": f"Bearer {key}", "Accept": "text/event-stream"}
    response = _post_json(config.base_url("openai") + "/chat/completions", payload, request_headers, timeout=600)
    for payload_line in _iter_sse(response):
        if _canceled(cancel):
            break
        if payload_line.strip() == "[DONE]":
            break
        try:
            event = json.loads(payload_line)
        except json.JSONDecodeError:
            continue
        if event.get("error"):
            raise ProviderError(str(event["error"].get("message", event["error"]))[:500])
        choices = event.get("choices") or []
        if not choices:
            continue
        delta = choices[0].get("delta") or {}
        content = delta.get("content")
        if isinstance(content, str) and content:
            yield content


def _anthropic_stream(config: Config, messages: list[dict], model: str, temperature: float, max_tokens: int, cancel) -> Iterator[str]:
    key = _require_key(config, "anthropic")
    system = "\n\n".join(m["content"] for m in messages if m.get("role") == "system")
    convo: list[dict] = []
    for message in messages:
        if message.get("role") not in ("user", "assistant"):
            continue
        content = message.get("content", "")
        if not content.strip():
            continue
        role = message["role"]
        if convo and convo[-1]["role"] == role:
            convo[-1]["content"] += "\n\n" + content
        else:
            convo.append({"role": role, "content": content})
    if convo and convo[0]["role"] == "assistant":
        convo.insert(0, {"role": "user", "content": "Continue."})
    payload = {
        "model": model,
        "max_tokens": int(max_tokens),
        "temperature": float(temperature),
        "messages": convo,
        "stream": True,
    }
    if system:
        payload["system"] = system
    headers = {
        "x-api-key": key,
        "anthropic-version": "2023-06-01",
        "Accept": "text/event-stream",
    }
    response = _post_json("https://api.anthropic.com/v1/messages", payload, headers, timeout=600)
    for payload_line in _iter_sse(response):
        if _canceled(cancel):
            break
        if not payload_line or payload_line.startswith("event:"):
            continue
        try:
            event = json.loads(payload_line)
        except json.JSONDecodeError:
            continue
        kind = event.get("type")
        if kind == "content_block_delta":
            delta = event.get("delta") or {}
            if delta.get("type") in ("text_delta", None):
                text = delta.get("text")
                if text:
                    yield text
        elif kind == "error":
            error = event.get("error") or {}
            raise ProviderError(str(error.get("message", "Anthropic returned an error."))[:500])
        elif kind == "message_stop":
            break


def _google_stream(config: Config, messages: list[dict], model: str, temperature: float, max_tokens: int, cancel) -> Iterator[str]:
    key = _require_key(config, "google")
    system = "\n\n".join(m["content"] for m in messages if m.get("role") == "system")
    contents: list[dict] = []
    for message in messages:
        if message.get("role") not in ("user", "assistant"):
            continue
        text = message.get("content", "")
        if not text.strip():
            continue
        role = "model" if message["role"] == "assistant" else "user"
        if contents and contents[-1]["role"] == role:
            contents[-1]["parts"].append({"text": text})
        else:
            contents.append({"role": role, "parts": [{"text": text}]})
    if contents and contents[0]["role"] == "model":
        contents.insert(0, {"role": "user", "parts": [{"text": "Continue."}]})
    payload = {
        "contents": contents,
        "generationConfig": {
            "temperature": float(temperature),
            "maxOutputTokens": int(max_tokens),
        },
    }
    if system:
        payload["systemInstruction"] = {"parts": [{"text": system}]}
    model_name = urllib.parse.quote(model, safe="")
    url = (
        f"https://generativelanguage.googleapis.com/v1beta/models/{model_name}:streamGenerateContent"
        f"?alt=sse&key={urllib.parse.quote(key)}"
    )
    response = _post_json(url, payload, {"Accept": "text/event-stream"}, timeout=600)
    for payload_line in _iter_sse(response):
        if _canceled(cancel):
            break
        try:
            event = json.loads(payload_line)
        except json.JSONDecodeError:
            continue
        if event.get("error"):
            raise ProviderError(str(event["error"].get("message", "Gemini returned an error."))[:500])
        feedback = event.get("promptFeedback") or {}
        if feedback.get("blockReason"):
            raise ProviderError("Gemini blocked this prompt: " + str(feedback.get("blockReason")))
        candidates = event.get("candidates") or []
        if not candidates:
            continue
        parts = ((candidates[0].get("content") or {}).get("parts")) or []
        for part in parts:
            text = part.get("text")
            if isinstance(text, str) and text:
                yield text


def _ollama_stream(config: Config, messages: list[dict], model: str, temperature: float, max_tokens: int, cancel) -> Iterator[str]:
    payload = {
        "model": model,
        "messages": messages,
        "stream": True,
        "options": {"temperature": float(temperature), "num_predict": int(max_tokens)},
    }
    response = _post_json(config.base_url("ollama") + "/api/chat", payload, {"Accept": "application/x-ndjson"}, timeout=600)
    for line in _iter_lines(response):
        if _canceled(cancel):
            break
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        if event.get("error"):
            raise ProviderError(str(event["error"])[:500])
        message = event.get("message") or {}
        content = message.get("content")
        if isinstance(content, str) and content:
            yield content
        if event.get("done"):
            break


def stream_chat(
    config: Config,
    messages: list[dict],
    provider: str | None = None,
    model: str | None = None,
    temperature: float | None = None,
    max_tokens: int | None = None,
    cancel=None,
) -> Iterator[str]:
    provider = (provider or config.get("provider") or "local").lower()
    chosen_model = _model(config, provider, model)
    temp = float(config.get("temperature", 0.7) if temperature is None else temperature)
    tokens = int(config.get("max_tokens", 2048) if max_tokens is None else max_tokens)
    if provider == "local":
        engine = get_engine(config)
        yield from engine.stream_chat(messages, temperature=temp, max_tokens=tokens, cancel=cancel, key=chosen_model)
    elif provider == "openai":
        yield from _openai_stream(config, messages, chosen_model, temp, tokens, cancel)
    elif provider == "anthropic":
        yield from _anthropic_stream(config, messages, chosen_model, temp, tokens, cancel)
    elif provider == "google":
        yield from _google_stream(config, messages, chosen_model, temp, tokens, cancel)
    elif provider == "ollama":
        yield from _ollama_stream(config, messages, chosen_model, temp, tokens, cancel)
    else:
        raise ProviderError(f"Unknown provider: {provider}")


def complete_chat(
    config: Config,
    messages: list[dict],
    provider: str | None = None,
    model: str | None = None,
    temperature: float | None = None,
    max_tokens: int | None = None,
    cancel=None,
) -> str:
    return "".join(
        stream_chat(
            config,
            messages,
            provider=provider,
            model=model,
            temperature=temperature,
            max_tokens=max_tokens,
            cancel=cancel,
        )
    )


def test_connection(config: Config, provider: str | None = None) -> str:
    provider = (provider or config.get("provider") or "local").lower()
    messages = [
        {"role": "system", "content": "You are a connection test. Reply with exactly: OK"},
        {"role": "user", "content": "ping"},
    ]
    text = complete_chat(config, messages, provider=provider, temperature=0, max_tokens=24)
    return text.strip()[:120] or "Connected."
