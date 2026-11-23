# Implementation Plan: Takoyo Agentic Upgrade

## Overview

Four features: (1) a **New chat** button in the sidebar Recents section, (2) **13+ selectable color palettes** with instant (no-restart) switching, (3) **agentic file/code tools** — create, edit, read, and search files — with a confirmation prompt before every mutating action, and (4) **plan / browse / open-app** commands. All work happens in the native Qt app (`native/src`); the Python sidecar needs **no changes**.

## Architecture Decisions

**AD-1: Extend the existing `<takoyo-tool>` text protocol; do not migrate to API function-calling.**
The app already parses `read_file` / `list_dir` / `write_file` out of model replies (`util.cpp:403-483`) and executes them in bounded follow-up rounds (`mainwindow.cpp:1281-1448`). New tools (`edit_file`, `search`, `plan`, `open_url`, `open_app`) join that protocol. Rationale: works with all 4 provider adapters today; real `tools`-param support would require changes in `backend.cpp` + `api.py` + OpenAI/Anthropic/Google/Ollama adapters (deferred to a later hardening pass).

**AD-2: Tools execute natively in the Qt process, not via the sidecar.**
Existing `write_file` already executes in `mainwindow.cpp`. `QDesktopServices::openUrl` (browse) and `QProcess::startDetached` (open app) are native Qt. File search uses `QDirIterator` + in-process text scan. No sidecar round-trips.

**AD-3: Confirmation gate for every mutating/executing action.**
Read-only tools (`read_file`, `list_dir`, `search`) run without prompts — they only look. `write_file`, `edit_file`, `open_url`, `open_app` always show a modal confirmation with the exact path / diff / command before executing. A denial is fed back to the model as a tool result so it can adjust. This replaces today's `auto_apply_edits` auto-gate for the new flow.

**AD-4: Palette colors become mutable runtime globals; call sites keep their names.**
`theme::WHITE` etc. stay as-is at all **157 reference sites** — the 18 `inline const QColor` in `theme.h:17-34` become non-const, assigned by `theme::setPalette()`. Paint-time readers (most of the UI is custom-painted) pick up new values on `update()`; no `theme::WHITE()` churn.

**AD-5: Palettes are JSON data files** at `assets/palettes/*.json`, keys = the 18 role names. Bundled code fallback so a missing/corrupt file can never blank the UI. 14 palettes ship at v1:

`dark` (current) · `light` (original) · `solarized-dark` · `solarized-light` · `nord` · `dracula` · `gruvbox` · `catppuccin-mocha` · `monokai` · `rose-pine` · `midnight-blue` · `sepia` · `forest` · `high-contrast`

**AD-6: Instant switching via explicit `applyTheme()` on long-lived widgets + rebuild of dynamic ones.**
Constructor-baked stylesheets (36 sites) are converted to build their strings from `theme::` values at call time, each owning widget gets `applyTheme()`, and chat/sidebar rows (already rebuilt on every `refresh()`) pick up the new palette for free. `theme::restyle()` sets the app-wide `QPalette` (moved from `main.cpp:25-44`) and triggers the refreshes. Re-creating `MainWindow` was rejected: it owns `Backend`, and killing/restarting the sidecar mid-conversation is unacceptable.

**AD-7: New chat stays lazy.** The button resets to the home view (existing `MainWindow::newChat()`); the conversation file is created on first send, so no empty ghost rows appear in Recents.

## Dependency Graph

```
T1 New chat (independent quick win)
T2 Palette core (theme.h/cpp mutable + JSON load)
 └─ T3a Stylesheet conversion: chatview, composer, dialogs
     └─ T3b Stylesheet conversion: mainwindow, homeview, artpanel, sidebar, widgets, strays
         └─ T4 applyTheme() + restyle + startup load
             ├─ T5 Settings palette picker (config + dialog + hook)
             └─ T6 The 14 palette JSON files
T7 Confirmation dialog (foundation for all mutating tools)
 ├─ T8 edit_file + search tools
 │   └─ T9 plan tool + checklist card
 └─ T10 open_url + open_app tools
T11 Final verification (after everything)
```

Palette work runs before tool work on purpose: both touch `mainwindow.cpp` / `chatview.cpp`, so converting stylesheets first avoids re-touching files the tool work modifies.

## Task List

### Phase 1 — Quick win
- [ ] **Task 1:** New chat button in the Recents heading (S)

### Phase 2 — Palette system (highest risk, early = fail fast)
- [ ] **Task 2:** Palette core: mutable `theme::` globals + JSON loading (M)
- [ ] **Task 3a:** Stylesheet conversion batch 1: `chatview`, `composer`, `dialogs` (M)
- [ ] **Task 3b:** Stylesheet conversion batch 2: `mainwindow`, `homeview`, `artpanel`, `sidebar`, `widgets`, stray hex (M)
- [ ] **Task 4:** `applyTheme()` methods + `theme::restyle()` + startup palette load (M)
- [ ] **Task 5:** Palette picker in Settings + `palette` config key + instant apply (S)
- [ ] **Task 6:** Author the 14 palette JSON files (S)

#### Checkpoint: Palette system
- [ ] App builds clean; UI looks byte-identical on `dark` before any switching
- [ ] Switching all 14 palettes from Settings recolors instantly, no restart
- [ ] Screenshots compared against pre-refactor capture

### Phase 3 — Agent file/code tools
- [ ] **Task 7:** Confirmation dialog + wire into existing `write_file` path (M)
- [ ] **Task 8:** `edit_file` + `search` tools: prompt spec, parser, executor (M)
- [ ] **Task 9:** `plan` tool + checklist card rendering in chat (M)

#### Checkpoint: File tools
- [ ] End-to-end: ask Takoyo to create, edit, and search files → each mutating action prompts, denial is reported back to the model
- [ ] Build clean

### Phase 4 — Plan/browse/open
- [ ] **Task 10:** `open_url` + `open_app` tools with confirmation (S)

### Phase 5 — Closeout
- [ ] **Task 11:** Final verification pass: full build, all features manual test, screenshot sweep (S)

#### Checkpoint: Complete
- [ ] All acceptance criteria met; ready for review

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Palette refactor causes visual regressions | High | Checkpoint after T3a/T3b with screenshot diff vs. pre-refactor capture; conversion is mechanical hex→`theme::` substitution |
| Restyle misses a widget (stale colors after switch) | Med | Inventory of 36 stylesheet sites is known (see T3a/T3b); `restyle()` explicitly calls `applyTheme()` on every long-lived owner + refreshes chat/sidebar |
| Text-tag JSON malformation for new tools | Med | Reuse `relaxJson` + balanced-brace scan; strict schema validation with friendly error fed back to model |
| Confirmation dialog during/after stream | Low | Prompt fires in `finishReply` (stream already ended); modal is safe there |
| Search tool slow/huge repos | Med | Cap scanned files (e.g. 5000) and file size (e.g. 1 MB), skip binary + `build/`, `node_modules`, `.git` |
| `open_app` abused | High | Confirm dialog shows full command/path; `startDetached` only after explicit Allow |
| 14 palettes with poor contrast | Med | Derive from established schemes (Solarized, Nord, Dracula…); verify `light` + `high-contrast` text legibility explicitly |
| Light palette leaks from unconverted hex | Med | Final grep gate in T11: no `#[0-9A-Fa-f]{6}` outside `theme.h` + palette JSONs |

## Open Questions (defaults chosen; flag if you disagree)

1. **Read-only tools don't prompt** — assumed from the confirmation question's framing ("creates/edits files or launches apps"). Reads are silent.
2. **No `delete_file` tool in v1** — not requested; deletions stay manual via Project Files dialog.
3. **`plan` renders as a checklist card in chat** (like the existing proposal cards), persisted in the message — not a separate sidebar widget.
4. **Palette picker is a plain dropdown** in Settings (no live preview thumbnails) — thumbnails possible later.
5. **`search` = filename glob + content grep in one tool** (mode field), not two tools.

## Environment / Verification Notes

- Toolchain (installed this session): Qt 6.8.3 `C:\Qt\6.8.3\mingw_64`, MinGW 13.1 `C:\Qt\Tools\mingw1310_64\bin`, Ninja (winget), CMake portable at `%TEMP%\opencode\cmake\...\bin\cmake.exe`.
- Build: `cmake --build native/build` (configure already done; add toolchain `bin` dirs to `PATH` first).
- No unit-test harness exists in the repo — verification is build + run + visual/manual checks per task.
