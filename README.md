# Takoyo AI Agent

A local desktop AI assistant for **coding, image generation, and general conversation**.
Built with Python + tkinter (runs on your computer). Features your Takoyo octopus mascot as the app icon.

## Features

- **Its own AI** — built-in local model (Qwen2.5 via llama.cpp) that runs on your PC with no account, API key, or cloud service
- **AI Chat** — conversation with the built-in model or OpenAI, Anthropic, Google Gemini, and Ollama
- **Coding help** — write, debug, refactor, and explain code
- **Code editor & runner** — edit Python files and run them safely with captured output
- **Image generation** — DALL·E 3 via OpenAI (saved to a local gallery)
- **File browser** — read/write/delete files and navigate folders
- **Settings** — API keys, base URLs, default model, temperature, max tokens, theme, local model manager
- **Conversation memory** — chat history is saved locally
- **Local API** — token-free HTTP API for chat, images, files, and code execution (OpenAI-compatible)
- **API tab** — start/stop the server, watch live request activity, review endpoint docs

## Project structure

```
Takoyo/
├── main.py                  # Entry point
├── requirements.txt         # Optional dependencies (Pillow for image preview)
├── README.md
├── logo.png                 # App icon (Takoyo mascot)
└── takoyo_agent/
    ├── __init__.py
    ├── config.py            # Settings & persistent config
    ├── model_catalog.py     # Local models Takoyo can run
    ├── local_ai.py          # Downloads + runs the built-in llama.cpp engine
    ├── ai_providers.py      # Local, OpenAI, Anthropic, Google, Ollama providers
    ├── image_gen.py         # DALL-E image generation
    ├── code_tools.py        # Safe code execution & file ops
    ├── memory.py            # Conversation history
    ├── api.py               # Built-in local HTTP API (no auth)
    └── gui.py               # tkinter desktop UI
```

## Quick start

### 1. Install Python (3.10+ recommended)
If you don't have Python, download it from https://python.org

### 2. (Optional) Install Pillow for image previews
```bash
python -m pip install Pillow
```
The app works without it — you'll just see file paths instead of previews in the Image tab.

### 3. Run the app
```bash
cd "C:\Users\Joshua Tallocoy\Downloads\Takoyo"
python main.py
```

### 4. Give Takoyo a brain

**Option A — built-in local AI (recommended, no account):**
```bash
python main.py --setup-local
```
Downloads the llama.cpp engine (~24 MB) and Qwen2.5 1.5B (~1.1 GB) once,
verifies it, and makes it the default. After that Takoyo works offline.
You can also do this from **Settings → Built-in AI** (pick 0.5B / 1.5B / 3B).

**Option B — use a cloud provider:** open **Settings** and enter an API key:

| Provider | API key source | Models |
|----------|----------------|--------|
| OpenAI | https://platform.openai.com/api-keys | gpt-4o, gpt-4o-mini, DALL-E 3 |
| Anthropic | https://console.anthropic.com/ | Claude 3.5 Sonnet, Haiku |
| Google | https://aistudio.google.com/ | Gemini 1.5 / 2.0 |
| Ollama | Run locally: https://ollama.com | Any model you pull |

Then click **Test Connection** to verify.

## Usage

- **Chat**: Pick a provider + model, type a message, press Enter (Shift+Enter for newline).
- **Code**: Open the Code tab, write Python, click Run. Output appears below.
- **Image**: Open the Image tab, enter a prompt, click Generate Image (requires OpenAI key).
- **Files**: Browse, read, write, and delete files.
- **API**: Open the API tab and click **Start API** (or run with `--api`).

## Built-in local AI

Takoyo can be its own AI: it downloads an open-weights model and runs it on
this computer through llama.cpp. Nothing is sent to OpenAI, Anthropic, Google,
or any other AI service, and no API key or account is involved.

- **Where it lives:** `~/.takoyo/llama/` (engine) and `~/.takoyo/models/` (GGUF weights).
- **Platform:** Windows x64/arm64 (it downloads the official llama.cpp CPU build).
- **Models:** Qwen2.5 0.5B, 1.5B (default), and 3B (all Apache-2.0), plus a
  135M model used for smoke tests. Pick one in **Settings → Built-in AI**.
  Takoyo runs these open-weight models itself; it does not call another AI provider.
- **When it starts:** automatically on the first local message; stop it with
  the **Stop** button.
- **Offline:** after the one-time download, no internet is required.
- **Limits:** local models are text-only and smaller than cloud models. Image
  generation still requires an OpenAI key.

Headless setup:

```bash
python main.py --setup-local                    # recommended 1.5B model
python main.py --setup-local qwen2.5-0.5b       # smaller / faster
python main.py --setup-local qwen2.5-3b         # better quality
```

## Local API

Takoyo ships a built-in HTTP API so other programs can use it. It runs on the
Python standard library — no extra dependencies.

```bash
python main.py --api                 # GUI + API together
python main.py --api-only            # headless: API only, no window
python main.py --api --port 9000     # custom port
python main.py --api --host 0.0.0.0  # expose on the network (see warning)
```

Defaults: `http://127.0.0.1:8765`. You can also start/stop it from the **API**
tab and enable **Start with Takoyo**. The `api.host`, `api.port`, and
`api.auto_start` values live in `~/.takoyo/config.json`.

### Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/` | Machine-readable endpoint index |
| GET | `/health` | Server status |
| GET | `/models` | Configured models per provider |
| GET | `/v1/models` | OpenAI-compatible model list |
| POST | `/chat` | Chat using Takoyo's providers + saved conversation memory |
| POST | `/v1/chat/completions` | OpenAI-compatible chat (`stream` supported) |
| POST | `/images/generations` | Generate images (native format) |
| POST | `/v1/images/generations` | OpenAI-compatible image generation (`url` or `b64_json`) |
| GET | `/images` | List generated images |
| GET | `/images/<file>` | Fetch a generated image |
| GET | `/files?path=.` | List a directory |
| GET | `/files/read?path=notes.txt` | Read a file (base64 for binary) |
| POST | `/files/write` | Write a file (`encoding: "base64"` supported) |
| POST | `/files/delete` | Delete a file; directories need `"recursive": true` |
| POST | `/run` | Run Python (`code`) or a whitelisted command (`command`) |
| GET | `/local` | Built-in model status, installed models, catalog |
| POST | `/local/setup` | Start a background download + start (`{"model": "qwen2.5-1.5b"}`) |
| POST | `/local/stop` | Stop the local model process |

### Examples

```bash
# Chat
curl -X POST http://127.0.0.1:8765/chat \
  -H "Content-Type: application/json" \
  -d '{"message": "Explain recursion in one sentence"}'

# Chat with memory: reuse conversation_id across calls
curl -X POST http://127.0.0.1:8765/chat \
  -H "Content-Type: application/json" \
  -d '{"message": "And give an example", "conversation_id": "demo"}'

# OpenAI-compatible (works with any OpenAI SDK pointed at this base URL)
curl -X POST http://127.0.0.1:8765/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{"model": "gpt-4o-mini", "messages": [{"role": "user", "content": "hi"}]}'

# Image
curl -X POST http://127.0.0.1:8765/v1/images/generations \
  -H "Content-Type: application/json" \
  -d '{"prompt": "a friendly purple octopus", "n": 1}'

# Code and files
curl -X POST http://127.0.0.1:8765/run \
  -H "Content-Type: application/json" -d '{"code": "print(2 + 2)"}'
curl -X POST http://127.0.0.1:8765/files/write \
  -H "Content-Type: application/json" -d '{"path": "notes.txt", "content": "hello"}'

# Built-in local AI (no key). Initial setup downloads the model first.
curl -X POST http://127.0.0.1:8765/local/setup \
  -H "Content-Type: application/json" -d '{"model": "qwen2.5-1.5b"}'
curl -s http://127.0.0.1:8765/local          # poll install/run status
curl -X POST http://127.0.0.1:8765/chat \
  -H "Content-Type: application/json" -d '{"provider": "local", "message": "hi"}'
```

Chat accepts optional `provider`, `model`, `temperature`, and `max_tokens`
overrides; `message` (single string) or `messages` (OpenAI-style list).
Conversation history is only saved when you pass `conversation_id`, and only
after a successful reply.

### ⚠ Security warning

As requested, this API has **no authentication, no rate limits, and no quota**.
Anyone who can reach the port can chat, run code, and read, write, or delete
files with your user permissions. It binds to `127.0.0.1` by default for that
reason — only use `--host 0.0.0.0` (or another interface) on a trusted network.
Code execution still requires it to be enabled in Settings.

## Safety

Code execution is sandboxed to a local `takoyo_workspace` folder in your home directory and only whitelisted commands (`python`, `pip`) are allowed. Code execution can be disabled in Settings.

The Local API intentionally has no authentication (see the warning above). Keep it on `127.0.0.1` unless you trust every device that can reach the port.

The local AI setup downloads the llama.cpp engine from its official GitHub
releases and model weights from Hugging Face. Downloads are verified for
completeness; after that, local chat makes no network calls.

## License

MIT