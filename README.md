# Takoyo AI Agent

A local desktop AI assistant for **coding, image generation, and general conversation**.
Built with Python + tkinter (runs on your computer). Features your Takoyo octopus mascot as the app icon.

## Features

- **AI Chat** — conversation with multiple providers (OpenAI, Anthropic, Google Gemini, local Ollama)
- **Coding help** — write, debug, refactor, and explain code
- **Code editor & runner** — edit Python files and run them safely with captured output
- **Image generation** — DALL·E 3 via OpenAI (saved to a local gallery)
- **File browser** — read/write/delete files and navigate folders
- **Settings** — API keys, base URLs, default model, temperature, max tokens, theme
- **Conversation memory** — chat history is saved locally

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
    ├── ai_providers.py      # OpenAI, Anthropic, Google, Ollama providers
    ├── image_gen.py         # DALL-E image generation
    ├── code_tools.py        # Safe code execution & file ops
    ├── memory.py            # Conversation history
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

### 4. Configure an AI provider
Open **Settings** and enter an API key for at least one provider:

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

## Safety

Code execution is sandboxed to a local `takoyo_workspace` folder in your home directory and only whitelisted commands (`python`, `pip`) are allowed. Code execution can be disabled in Settings.

## License

MIT