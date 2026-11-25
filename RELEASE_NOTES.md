# Takoyo v2.2.1 "Octo"

**Build:** 1795577389 | **Date:** November 25, 2026

## 🎉 What's Fixed

### 1. Dark Mode Text Visibility (Root Cause Fixed)
- Theme tokens (`WHITE`, `INK`, etc.) were mutable and overwritten by active palette
- QSS baked canonical colors at first load; toggling themes produced white-on-white text
- **Fix:** Tokens reverted to canonical light constants; `theme::map()` resolves canonical → active palette everywhere
- **Verified:** Dark mode now yields `color:#fbfbff; background:#000000`; Nord light/dark both keep text legible

### 2. Folder Selection Persists & Files Land in Selected Folder
- New `workspace_folder` config key saved on selection, restored on launch
- Home view shows "Working in ‹folder›"
- `applyProposal`/`applyCodeFromChat` write to selected folder (creating subdirs)
- If no folder selected: "Where should Takoyo save this file?" dialog appears, choice remembered

### 3. "Create This Code" No Longer Makes Images
- System prompt over-eagerly triggered image generation for any "create..." request
- Tightened all three prompt variants to require explicit visual noun
- Added native guard in `finishReply` suppressing stray `<takoyo-image>` tags on code/file requests

### 4. Mascot Animation Smoothness
- Pixmap exactly filled widget bounds → float/rotation offsets were clipped (jitter)
- Drawing now leaves proportional margin so motion has room

### 5. File Editing Now Works with Selected Folders (NEW)
- Model was told "No project is open" even with a folder selected → never emitted `write_file`
- Added `kWorkspaceTools` prompt branch naming folder, listing contents, spelling out `write_file` block
- `readRequestedFiles` resolves relative paths against selected folder
- Parser accepts fenced JSON, bare `{"tool":...}` objects, and tagged `<takoyo-tool>` blocks
- Auto-apply (`auto_apply_edits`) now works for selected folders too

### 6. Local Model Download Works
- Picking a not-yet-downloaded model in Model Picker triggers background download via `POST /local/setup`
- llama.cpp (~24 MB) + GGUF (resumable via `.part` + Range) → starts server → flips `local_model`
- UI shows "Downloading X in the background" toast

## 📦 Downloads

| File | Size | Description |
|------|------|-------------|
| `Takoyo.exe` | ~46 MB | Single-file app (self-extracts on first run) |
| `Takoyo-2.2.1-Octo-installer.zip` | ~46 MB | Smart installer + Python script + offline exe |
| `TakoyoEngine.exe` | ~17 MB | Standalone Python engine (for API mode) |

## 🚀 Installation

### Quick Start (Smart Installer)
1. Download `Takoyo-2.2.1-Octo-installer.zip` and extract
2. Run `Takoyo-Setup-2.2.1-Octo.exe`
3. Choose models to download (default: Qwen2.5 Coder 1.5B)
4. Shortcuts created in Start Menu & Desktop

### Command Line (Python)
```powershell
python smart_installer.py --models default   # Install with default model
python smart_installer.py --models all       # All models (~5 GB)
python smart_installer.py --silent           # Non-interactive
python smart_installer.py --uninstall        # Uninstall
```

### Offline / Portable
Just copy `Takoyo.exe` anywhere and run — self-extracts to `%LOCALAPPDATA%\Takoyo` on first launch.

## 🤖 Local Models (Downloaded to `~/.takoyo/models/`)
| Model | Size | Best For |
|-------|------|----------|
| qwen2.5-0.5b | ~500 MB | Quick questions, modest hardware |
| qwen2.5-1.5b | ~1.1 GB | Balanced default |
| **qwen2.5-coder-1.5b** | ~1.1 GB | **Code, refactors, edits (default)** |
| qwen2.5-3b | ~2.1 GB | Better reasoning, needs ~4 GB RAM |

## 🔧 Requirements
- Windows 10/11 (64-bit)
- Python 3.10+ (for smart installer; auto-detected)
- ~2 GB free space (app + one model)
- AVX2 recommended for local inference

## 🐙 Codename: Octo
*Eight arms, one brain — handles code, files, images, and chat simultaneously.*

---

**SHA256 checksums:** See `SHA256SUMS.txt` attached to this release.