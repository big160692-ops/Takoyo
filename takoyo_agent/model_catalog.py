"""Model catalog: Takoyo's own on-device models and cloud suggestions."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class LocalModel:
    key: str
    name: str
    size_label: str
    size_bytes: int
    filename: str
    url: str
    description: str
    coding: bool = False

    @property
    def hf_repo(self) -> str:
        return "Qwen"


LOCAL_MODELS: dict[str, LocalModel] = {
    "qwen2.5-0.5b": LocalModel(
        key="qwen2.5-0.5b",
        name="Qwen2.5 0.5B",
        size_label="0.5 GB",
        size_bytes=491_400_032,
        filename="qwen2.5-0.5b-instruct-q4_k_m.gguf",
        url="https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf",
        description="Tiny and fast. Good for quick chats on modest hardware.",
    ),
    "qwen2.5-1.5b": LocalModel(
        key="qwen2.5-1.5b",
        name="Qwen2.5 1.5B",
        size_label="1.1 GB",
        size_bytes=1_117_320_736,
        filename="qwen2.5-1.5b-instruct-q4_k_m.gguf",
        url="https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_k_m.gguf",
        description="The recommended balance of quality and speed.",
    ),
    "qwen2.5-coder-1.5b": LocalModel(
        key="qwen2.5-coder-1.5b",
        name="Qwen2.5 Coder 1.5B",
        size_label="1.1 GB",
        size_bytes=1_117_320_768,
        filename="qwen2.5-coder-1.5b-instruct-q4_k_m.gguf",
        url="https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF/resolve/main/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf",
        description="Tuned for code, refactors and project edits.",
        coding=True,
    ),
    "qwen2.5-3b": LocalModel(
        key="qwen2.5-3b",
        name="Qwen2.5 3B",
        size_label="2.1 GB",
        size_bytes=2_104_932_768,
        filename="qwen2.5-3b-instruct-q4_k_m.gguf",
        url="https://huggingface.co/Qwen/Qwen2.5-3B-Instruct-GGUF/resolve/main/qwen2.5-3b-instruct-q4_k_m.gguf",
        description="More capable reasoning, still comfortable on CPU.",
    ),
}

DEFAULT_LOCAL_MODEL = "qwen2.5-1.5b"

PROVIDERS: dict[str, dict] = {
    "local": {
        "label": "Takoyo Built-in",
        "icon": "brain",
        "hint": "On-device · unlimited · offline",
        "key_url": "",
        "models": [m.name for m in LOCAL_MODELS.values()],
    },
    "openai": {
        "label": "OpenAI",
        "icon": "sparkle",
        "hint": "Cloud · uses your API key",
        "key_url": "https://platform.openai.com/api-keys",
        "models": ["gpt-4o-mini", "gpt-4o", "gpt-4.1-mini", "gpt-4.1", "o4-mini"],
    },
    "anthropic": {
        "label": "Anthropic",
        "icon": "sparkle",
        "hint": "Cloud · uses your API key",
        "key_url": "https://console.anthropic.com/settings/keys",
        "models": ["claude-sonnet-4-5", "claude-haiku-4-5", "claude-3-5-sonnet-latest", "claude-3-5-haiku-latest"],
    },
    "google": {
        "label": "Google Gemini",
        "icon": "sparkle",
        "hint": "Cloud · uses your API key",
        "key_url": "https://aistudio.google.com/app/apikey",
        "models": ["gemini-2.5-flash", "gemini-2.5-pro", "gemini-2.0-flash"],
    },
    "ollama": {
        "label": "Ollama",
        "icon": "brain",
        "hint": "Local server you run yourself",
        "key_url": "https://ollama.com",
        "models": ["qwen2.5-coder:1.5b", "qwen2.5:3b", "llama3.2", "deepseek-r1:1.5b"],
    },
}

PROVIDER_ORDER = ["local", "openai", "anthropic", "google", "ollama"]

IMAGE_SIZES = ["1024x1280", "1024x1024", "1024x1536", "1536x1024", "1280x720"]
DALLE_SIZES = ["1024x1024", "1024x1792", "1792x1024"]

IMAGE_PROVIDERS = {
    "takoyo": {
        "label": "Takoyo Art",
        "hint": "Built-in · unlimited · offline",
    },
    "openai": {
        "label": "OpenAI DALL·E 3",
        "hint": "Cloud · uses your OpenAI key",
    },
}


def local_model(key: str) -> LocalModel:
    return LOCAL_MODELS.get(key) or LOCAL_MODELS[DEFAULT_LOCAL_MODEL]


def local_model_by_name(name: str) -> LocalModel | None:
    for model in LOCAL_MODELS.values():
        if model.name.lower() == (name or "").lower() or model.key == name:
            return model
    return None
