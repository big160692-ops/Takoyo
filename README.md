# Takoyo AI Agent

A local desktop AI assistant for **building, generating, coding, and chatting**.
Built with Python + tkinter and drawn in a quiet white-and-ink style with the
Takoyo octopus mascot.

**Release 2.00** — the sidebar and art panel now slide in and out cleanly instead
of clipping mid-motion, alongside the floating mascot, animated thinking dots,
and other motion feedback.

## Features

- **Its own AI** — bundled llama.cpp runtime + Qwen2.5 model that runs on your
  PC. No account, no API key, no cloud, and no per-message limits.
- **Unlimited chats** — the local model streams every reply; history is saved
  locally and organised by project.
- **Unlimited image generation** — "Takoyo Art" paints original images on this
  computer with no key and no quota. DALL·E 3 is optional if you prefer it.
- **Project editing** — open a real folder as a project and ask Takoyo to
  scaffold, refactor, or fix it. Suggested files appear as inline cards with
  **Apply**, or apply automatically if you enable it.
- **Project file browser** — read, edit, create, and delete files inside the
  project without leaving the app.
- **Cloud models when you want them** — OpenAI, Anthropic, Google Gemini, and
  Ollama, with a connection test.
- **Local API** — token-free OpenAI-compatible HTTP API for chat, images, files,
  and code execution.
- **Subtle motion** — the mascot floats, the sidebar and art panel glide, and
  thinking, status, and toast feedback animate.

## The interface

The app follows a three-screen layout:

1. **Home** — the mascot, the tagline, and one outlined composer with a
   paperclip and microphone.
2. **Sidebar** — search, **PROJECTS**, and **RECENTS**. It slides away for a
   full-width chat and returns with the panel icon.
3. **Chat** — right-aligned outlined user bubbles, assistant replies with a mini
   mascot, full-width code blocks, and a right-hand art panel for generated
   images.

## Project structure

```
Takoyo/
├── main.py                  # Entry point (GUI, API, setup)
├── requirements.txt         # Pillow (required for the custom UI)
├── README.md
├── logo.png                 # App icon
├── assets/
│   ├── mascot.png           # Octopus mascot
│   ├── mascot_white.png
│   └── fonts/               # Bundled Poppins faces
└── takoyo_agent/
    ├── __init__.py
    ├── animation.py         # Easing, color blending, and cancellable tweens
    ├── config.py            # Settings & persistent config
    ├── model_catalog.py     # Local + cloud model catalog
    ├── local_ai.py          # Downloads + runs the built-in llama.cpp engine
    ├── ai_providers.py      # Local, OpenAI, Anthropic, Google, Ollama providers
    ├── prompting.py         # System prompt, project-edit & image tool tags
    ├── image_gen.py         # Takoyo Art + optional DALL·E 3
    ├── code_tools.py        # Safe code execution & file ops
    ├── memory.py            # Conversation history
    ├── projects.py          # Project folders, safe paths, file tree
    ├── api.py               # Built-in local HTTP API
    ├── theme.py             # Palette, fonts, DPI, measuring
    ├── widgets.py           # Cards, buttons, composer, code blocks, fields
    ├── visuals.py           # Antialiased mascot, wordmark, icons
    ├── dialogs.py           # Settings, project files, new project
    └── gui.py               # App shell, sidebar, home, chat, art panel
```

## Quick start

### 1. Install Python

Python 3.10+ is recommended. On Windows, get it from https://python.org.

### 2. Install dependencies

```bash
python -m pip install -r requirements.txt
```

Pillow draws the antialiased outlines, mascot, wordmark, and art, so the app
needs it to start.

### 3. Run the app

```bash
cd path\to\Takoyo
python main.py
```

### 4. Give Takoyo a brain

**Option A — built-in local AI (recommended, no account):**

```bash
python main.py --setup-local
```

This downloads the llama.cpp engine (~24 MB) and Qwen2.5 1.5B (~1.1 GB) once,
verifies them, and makes the model the default. You can do the same from
**Settings → Built-in AI** (pick 0.5B / 1.5B / 3B). After that, chats and
Takoyo Art work offline.

**Option B — use a cloud provider:** open **Settings → Chat** and enter a key.

| Provider | API key source | Models |
|----------|----------------|--------|
| OpenAI | https://platform.openai.com/api-keys | gpt-4o, gpt-4o-mini, DALL·E 3 |
| Anthropic | https://console.anthropic.com/ | Claude 3.5 Sonnet, Haiku |
| Google | https://aistudio.google.com/ | Gemini 1.5 / 2.0 |
| Ollama | Run locally: https://ollama.com | Any model you pull |

Then click **Test connection** to verify.

## Usage

- **Chat** — type in the composer and press Enter (Shift+Enter for a newline).
  The built-in model starts automatically the first time you send a message.
- **Switch models** — the sun icon opens Settings; the image icon opens the art
  panel when a chat has generated images.
- **Projects** — use **+** beside PROJECTS to create a folder. Takoyo writes
  only inside that folder.
- **Edit files with AI** — ask for a change. Each proposed file appears as a
  code block; click **Apply** to write it. **Settings → Permissions** can apply
  edits automatically.
- **Files** — the **Files** button in the chat header opens the project editor
  (create, edit, save, delete).
- **Images** — ask Takoyo to generate an image, or use the image button. Art is
  saved in `~/.takoyo/images` and shown in the right-hand panel.

## Build the Windows executable

`Takoyo.spec` bundles the app into one windowed exe with the Takoyo icon,
Poppins fonts, mascot and art engine inside.

```bash
python -m pip install pyinstaller
python -m PyInstaller Takoyo.spec
```

The result is `dist/Takoyo.exe` (~20 MB). Builds must be made on Windows.
A prebuilt copy is included at the project root as `Takoyo.exe`.

## Built-in local AI

Takoyo can be its own AI: it downloads open-weight model files and runs them on
this computer through llama.cpp. Nothing is sent to OpenAI, Anthropic, Google,
or any other AI service, and no API key or account is involved.

- **Where it lives:** `~/.takoyo/llama/` (engine) and `~/.takoyo/models/` (GGUF weights).
- **Platform:** Windows x64/arm64 (official llama.cpp CPU build).
- **Models:** Qwen2.5 0.5B, 1.5B (default), and 3B (all Apache-2.0), plus a
  135M model used for smoke tests. Pick one in **Settings → Built-in AI**.
- **When it starts:** automatically on the first local message; stop it from the
  composer while it is thinking, or from **Settings → Built-in AI**.
- **Offline:** after the one-time download, no internet is required.
- **Limits:** local models are text-only and smaller than cloud models. Images
  use the separate offline Takoyo Art painter (or DALL·E 3 with an OpenAI key).

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

Defaults: `http://127.0.0.1:8765`. You can also start/stop it from
**Settings → Local API**. The `api_host`, `api_port`, and `api_auto_start`
values live in `~/.takoyo/config.json`.

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
  -H "Content-Type: application/json" -d '{"path": "workspace/notes.txt", "content": "hello"}'

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

File paths are relative to `~/.takoyo`. Reads can open anything under that
folder; **writes and deletes are limited to `workspace/` and `projects/`**,
and deleted paths are moved to `~/.takoyo/trash`.

### ⚠ Security warning

As requested, this API has **no authentication, no rate limits, and no quota**.
Anyone who can reach the port can chat, run code, read files under `~/.takoyo`,
and write or delete files inside `workspace/` and `projects/`. It binds to
`127.0.0.1` by default for that reason — only use `--host 0.0.0.0` (or another
interface) on a trusted network. Code execution still requires it to be
enabled in Settings.

## Safety

- Project edits are resolved inside the project folder; path traversal is
  rejected and obvious secret files (`.env`, keys, credentials) are hidden from
  the file tree and AI context.
- Deleting a project moves it to `~/.takoyo/trash` instead of erasing it.
- Code execution is sandboxed to a local `takoyo_workspace` folder in your home
  directory and only whitelisted commands (`python`, `pip`) are allowed. It can
  be disabled in **Settings → Permissions**.
- The Local API's write and delete endpoints only accept paths inside
  `workspace/` and `projects/`.
- The Local API intentionally has no authentication (see the warning above).
  Keep it on `127.0.0.1` unless you trust every device that can reach the port.
- The local AI setup downloads the llama.cpp engine from its official GitHub
  releases and model weights from Hugging Face. Downloads are verified for
  completeness; after that, local chat makes no network calls.

## License

MIT
