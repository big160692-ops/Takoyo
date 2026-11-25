# Takoyo v2.2.1 "Octo" Release Notes

**Build ID:** 1795577389  
**Date:** November 25, 2026  
**Version:** 2.2.1 (Octo)

## 🎉 Major Fixes

### 1. Dark Mode Text Visibility (Fixed)
- Root cause: Theme tokens (`WHITE`, `INK`, etc.) were mutable and overwritten by the active palette
- QSS baked canonical colors at first load; toggling themes produced white-on-white text
- Fix: Reverted tokens to canonical light constants; `theme::map()` now resolves canonical → active palette everywhere
- Verified: Dark mode now yields `color:#fbfbff; background:#000000`; Nord light/dark both keep text legible

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

## 📦 Artifacts

- **Takoyo.exe** (45.9 MB) — Single-file installer/launcher with embedded Qt, engine, assets
- **Takoyo-2.2.1-Octo-portable.zip** (45.7 MB) — Portable package (exe + install.ps1)

## 🔧 Technical Details

- **Qt:** 6.8.3 (MinGW 13.1)
- **Engine:** llama.cpp with qwen2.5-coder-1.5b (default), qwen2.5-1.5b, qwen2.5-3b, qwen2.5-0.5b
- **Config:** `~/.takoyo/config.json` + `%LOCALAPPDATA%\Takoyo\` for installed app
- **Models:** `~/.takoyo/models/*.gguf`

## 📥 Installation

### Option 1: Run the portable zip
1. Extract `Takoyo-2.2.1-Octo-portable.zip`
2. Run `install.bat` or `powershell -ExecutionPolicy Bypass -File install.ps1`
3. Creates shortcuts in Start Menu and Desktop
4. Registers uninstaller in Windows Apps & Features

### Option 2: Direct executable
- Copy `Takoyo.exe` anywhere and run — self-extracts on first launch to `%LOCALAPPDATA%\Takoyo`

## 🐙 Codename: Octo
*Eight arms, one brain — handles code, files, images, and chat simultaneously.*