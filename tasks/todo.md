# Takoyo Upgrade — Task List

Full design context: [`plan.md`](plan.md). Build: add `C:\Qt\Tools\mingw1310_64\bin` to `PATH`, then `cmake --build native/build` (Qt at `C:\Qt\6.8.3\mingw_64`).

---

## Task 1: New chat button in Recents heading

**Description:** The PROJECTS heading already renders a plus button via `addSectionHeading(..., onNew)`; RECENTS passes no callback. Add a `newChatRequested` signal to Sidebar, pass a lambda at the RECENTS heading, and wire it to the existing `MainWindow::newChat()`.

**Acceptance criteria:**
- [ ] A plus icon appears next to the RECENTS heading (same style as PROJECTS)
- [ ] Clicking it resets to the home view (composer focused), same as Ctrl+N
- [ ] No empty conversation file is created until the first message is sent
- [ ] Existing PROJECTS plus behavior unchanged

**Verification:**
- [ ] Build succeeds: `cmake --build native/build`
- [ ] Manual: launch app → plus visible on RECENTS → click → home view → type/send → row appears in Recents titled from first message

**Dependencies:** None
**Files likely touched:** `native/src/sidebar.h`, `native/src/sidebar.cpp`, `native/src/mainwindow.cpp`
**Estimated scope:** S (3 files, ~10 lines)

---

## Task 2: Palette core — mutable theme globals + JSON loading

**Description:** Convert the 18 `inline const QColor` constants in `theme.h:17-34` to mutable `inline QColor`, add a `Palette` struct + `theme::setPalette(const Palette &)` that assigns all of them, and a loader `theme::loadPalette(const QString &id)` that reads `assets/palettes/<id>.json` (keys = the 18 constant names), falling back to the bundled dark values on missing/corrupt files. Radii stay compile-time.

**Acceptance criteria:**
- [ ] All 157 existing `theme::` references compile unchanged
- [ ] `loadPalette("dark")` reproduces today's colors exactly
- [ ] Corrupt/missing JSON falls back to dark without crashing
- [ ] `setPalette()` updates every color constant

**Verification:**
- [ ] Build succeeds (zero reference-site edits expected in this task)
- [ ] Manual: app looks identical after adding `loadPalette("dark")` call

**Dependencies:** None
**Files likely touched:** `native/src/theme.h`, `native/src/theme.cpp`
**Estimated scope:** M (2 files, new struct/loader ~120 lines)

---

## Task 3a: Stylesheet conversion batch 1 — chatview, composer, dialogs

**Description:** Replace every hardcoded `#RRGGBB` inside `setStyleSheet` strings with values interpolated from `theme::` (`.arg(theme::X.name())` or a small formatter helper). Sites: chatview.cpp (21 stylesheet calls, incl. markdown CSS at 68-73), composer.cpp (6), dialogs.cpp (`kDialogStyle` 35-68 → build function). Also `widgets.h` member defaults are batch 2's job — do not touch here.

**Acceptance criteria:**
- [ ] Zero hex literals remain in chatview.cpp, composer.cpp, dialogs.cpp except none
- [ ] `kDialogStyle` becomes a function of the current palette
- [ ] Visual output on the dark palette is pixel-identical to before

**Verification:**
- [ ] Build succeeds
- [ ] `grep -E "#[0-9A-Fa-f]{6}"` clean on the three files
- [ ] Screenshot: chat view + composer + a dialog match pre-conversion capture

**Dependencies:** Task 2
**Files likely touched:** `native/src/chatview.cpp`, `native/src/composer.cpp`, `native/src/dialogs.cpp`
**Estimated scope:** M (3 files, ~30 substitution sites)

---

## Task 3b: Stylesheet conversion batch 2 — remaining files + strays

**Description:** Same conversion for mainwindow.cpp (incl. `menuStyle()` 391-394, buildUi backgrounds), homeview.cpp (incl. inline `QColor("#3A3B3E")` at :64), artpanel.cpp, sidebar.cpp, widgets.cpp, plus `widgets.h:33/64/65` member defaults → `theme::` values, and `widgets.cpp:374-376` magic RGB lerp → named tokens.

**Acceptance criteria:**
- [ ] Zero hex literals outside `theme.h` anywhere in `native/src`
- [ ] `menuStyle()` builds from palette
- [ ] Widget member defaults (`fill_`, `color_`, `hoverColor_`) read from `theme::`

**Verification:**
- [ ] Build succeeds
- [ ] Repo-wide grep gate: `grep -rE "#[0-9A-Fa-f]{6}" native/src` → only `theme.h`
- [ ] Screenshot: sidebar, home, menus, art panel match pre-conversion

**Dependencies:** Task 3a
**Files likely touched:** `native/src/mainwindow.cpp`, `native/src/homeview.cpp`, `native/src/artpanel.cpp`, `native/src/sidebar.cpp`, `native/src/widgets.cpp`, `native/src/widgets.h`
**Estimated scope:** M (6 files, ~20 substitution sites)

---

## Task 4: applyTheme() + restyle() + startup load

**Description:** Move the `QPalette` block from `main.cpp:25-44` into `theme::applyAppPalette()`. Give each long-lived stylesheet-owning widget an `applyTheme()` (MainWindow, Composer, Sidebar, HomeView, ArtPanel, dialogs' style helpers, SearchField/TextButton where needed) that re-runs its `setStyleSheet` calls. Add `theme::restyle(QWidget *root)` that applies the app palette, calls `applyTheme()` on the owners, and triggers `chat_->refresh()` / `sidebar_->refresh()` (rows rebuild = free recolor). Load the configured palette in `main.cpp` before `MainWindow` construction.

**Acceptance criteria:**
- [ ] Startup loads the configured palette before first paint
- [ ] `theme::restyle()` recolors every open widget with no restart
- [ ] No widget retains stale colors after a switch (chat, sidebar, composer, menus, tooltips)

**Verification:**
- [ ] Build succeeds
- [ ] Manual: temporary debug toggle (or early Settings row from T5) switches dark↔light live; inspect every screen: home, chat, sidebar, context menu, tooltip, scrollbar, settings dialog

**Dependencies:** Tasks 3a, 3b
**Files likely touched:** `native/src/theme.h`, `native/src/theme.cpp`, `native/src/main.cpp`, `native/src/mainwindow.cpp`, `native/src/composer.cpp`, `native/src/sidebar.cpp`, `native/src/homeview.cpp`, `native/src/artpanel.cpp`
**Estimated scope:** M (8 files, mechanical)

---

## Task 5: Palette picker in Settings + config key + instant apply

**Description:** Add `"palette": "dark"` to `Config::defaults()`, a `QComboBox *palette_` row to `SettingsDialog` (image-engine pattern, dialogs.cpp:437-449) populated from the palette registry, persist in `save()`, and in the `openSettings()` accepted-lambda (mainwindow.cpp:1908-1913) call `theme::loadPalette()` + `theme::restyle()`.

**Acceptance criteria:**
- [ ] Settings shows a Palette dropdown listing all shipped palettes
- [ ] Selection persists across restarts
- [ ] Clicking Save recolors the app instantly (no restart)
- [ ] Cancel discards the change

**Verification:**
- [ ] Build succeeds
- [ ] Manual: switch palettes, close/reopen app → persisted; Cancel → no change

**Dependencies:** Task 4, Task 6 (needs at least the two base palettes to exist; ship with Task 6 if ordered later — minimum viable: dark+light present)
**Files likely touched:** `native/src/config.cpp`, `native/src/dialogs.h`, `native/src/dialogs.cpp`, `native/src/mainwindow.cpp`
**Estimated scope:** S (4 files, ~50 lines)

---

## Task 6: Author 14 palette JSON files

**Description:** Create `assets/palettes/{dark,light,solarized-dark,solarized-light,nord,dracula,gruvbox,catppuccin-mocha,monokai,rose-pine,midnight-blue,sepia,forest,high-contrast}.json`, each defining all 18 role keys with coherent values derived from the named scheme. `dark` = current values, `light` = original Takoyo light theme (from `takoyo_agent/theme.py`: WHITE #FFFFFF, INK #0B0B0D, LINE #E7E7E9, etc.).

**Acceptance criteria:**
- [ ] 14 valid JSON files, each with all 18 keys
- [ ] `dark` byte-matches current theme; `light` matches the original light theme
- [ ] Every palette passes a legibility spot-check (text vs ground contrast) in the running app

**Verification:**
- [ ] JSON validation (parse each file)
- [ ] Manual: cycle all 14 in Settings; screenshot each; check chat text, chips, sidebar, error cards, code blocks remain readable

**Dependencies:** Task 2 (schema), pairs with Task 5
**Files likely touched:** `assets/palettes/*.json` (new, 14 files)
**Estimated scope:** S (data files, no code)

---

### Checkpoint: Palette system
- [ ] Build clean; `dark` looks identical to pre-refactor screenshots
- [ ] All 14 palettes switch instantly from Settings and persist
- [ ] Grep gate: no hex outside `theme.h` + palette JSONs

---

## Task 7: Confirmation dialog + wire into write_file

**Description:** New `ConfirmActionDialog` (dialogs.h/cpp): shows action type (Create/Write/Edit/Open), target path or full command, a unified diff for edits / raw content for creates, Allow / Deny buttons. Integrate at the `write_file` execution point in `finishReply` (mainwindow.cpp:1309-1339): every write now prompts (replacing blind `auto_apply_edits` for this path); Deny feeds a `user_denied` tool result back to the model and continues the follow-up loop. Read-only tools stay silent.

**Acceptance criteria:**
- [ ] Every `write_file` shows the dialog with exact path + content/diff before touching disk
- [ ] Allow → file written (existing behavior); Deny → nothing written, model receives denial notice and can retry/explain
- [ ] Dialog is themed (uses current palette)
- [ ] Read tools (`read_file`, `list_dir`) never prompt

**Verification:**
- [ ] Build succeeds
- [ ] Manual: ask Takoyo to create a file → dialog → Deny → file absent, reply acknowledges; repeat → Allow → file present

**Dependencies:** None (parallel-safe with palette work, but merge after Task 4 to get themed dialog for free — if run before, dialog uses dark theme only)
**Files likely touched:** `native/src/dialogs.h`, `native/src/dialogs.cpp`, `native/src/mainwindow.cpp`
**Estimated scope:** M (3 files, new dialog ~120 lines + integration)

---

## Task 8: edit_file + search tools

**Description:** Extend the tool protocol with two commands:
- `edit_file {path, find, replace}` (exact-match replace, fail if not found/ambiguous) — parsed in `util::parseReply`, executed with the same root-jail as `write_file`, gated by the Task 7 dialog with a rendered diff.
- `search {query, mode: "filename"|"content", glob?}` — `QDirIterator` walk (skip binary, `build/`, `.git`, `node_modules`; caps: 5000 files / 1 MB each), returns path:line matches.
Update the `kProjectTools` prompt spec (mainwindow.cpp:123-148) so the model knows both commands.

**Acceptance criteria:**
- [ ] Model can surgically edit an existing file via `edit_file` (multi-edit round-trips work)
- [ ] `search` finds by filename and by content with line numbers
- [ ] `edit_file` always prompts with a diff (Task 7 dialog); `search` never prompts
- [ ] Out-of-jail paths rejected as today (`util.cpp:476-478`)

**Verification:**
- [ ] Build succeeds
- [ ] Manual: "edit X line Y in foo.py" → diff dialog → Allow → change applied
- [ ] Manual: "find every file mentioning <symbol>" → results returned, no dialog
- [ ] Manual: attempt edit outside project root → rejected

**Dependencies:** Task 7
**Files likely touched:** `native/src/util.cpp`, `native/src/util.h`, `native/src/mainwindow.cpp`
**Estimated scope:** M (3 files, ~150 lines)

---

## Task 9: plan tool + checklist card

**Description:** New `plan {steps: ["..."]}` tool: parsed like the others, rendered as a checklist card in the assistant message (follow `addProposalCard` pattern, chatview.cpp), stored in message `extra` so it persists and re-renders on refresh. Model is prompted (kProjectTools) to emit a plan when asked to plan/outline next steps; re-emitting replaces the card.

**Acceptance criteria:**
- [ ] "Plan the next steps for X" produces a checklist card with the steps
- [ ] Card persists across conversation reload and palette switches
- [ ] Re-planning updates the existing card rather than stacking duplicates
- [ ] No confirmation dialog (plan is inert)

**Verification:**
- [ ] Build succeeds
- [ ] Manual: plan request → card renders → switch palette → card recolors → reload conversation → card persists

**Dependencies:** None strictly; coordinate with Task 7 only for the shared parse/execute plumbing
**Files likely touched:** `native/src/util.cpp`, `native/src/util.h`, `native/src/chatview.cpp`, `native/src/mainwindow.cpp`
**Estimated scope:** M (4 files, ~120 lines)

---

### Checkpoint: File tools
- [ ] End-to-end: create, edit, search all work; every mutation prompts; denial round-trips
- [ ] Build clean

---

## Task 10: open_url + open_app tools

**Description:** Two confirm-gated commands:
- `open_url {url}` → validate `http/https` scheme → Task 7 dialog shows full URL → Allow → `QDesktopServices::openUrl`.
- `open_app {name, args?}` → resolve against a safe allowlist heuristic (existing installed app name or absolute path) → dialog shows exact command → Allow → `QProcess::startDetached`.
Update `kProjectTools` prompt spec. Denials feed back to the model like Task 7.

**Acceptance criteria:**
- [ ] "Open https://example.com" prompts, then opens the default browser
- [ ] "Open Notepad" (or a named app) prompts with the exact launch line, then starts it
- [ ] Deny → nothing launches, model is told
- [ ] Non-http schemes (file:, javascript:) rejected outright

**Verification:**
- [ ] Build succeeds
- [ ] Manual: both tools allow + deny paths; verify browser/ app actually opens on Allow

**Dependencies:** Task 7
**Files likely touched:** `native/src/util.cpp`, `native/src/util.h`, `native/src/mainwindow.cpp`
**Estimated scope:** S (3 files, ~80 lines)

---

## Task 11: Final verification pass

**Description:** Full sweep: clean rebuild, all features manual test, screenshot set for every palette, grep gates, no-regression check on existing features (image gen, projects, backups, streaming).

**Acceptance criteria:**
- [ ] Clean configure + build with zero warnings introduced
- [ ] All 14 palettes screenshotted and legible
- [ ] New chat, file tools, plan, browse, open-app all pass their manual checks
- [ ] Existing features (chat streaming, projects, settings, image gen) unaffected
- [ ] Grep gates: no stray hex; no debug leftovers

**Verification:**
- [ ] `cmake --build native/build` clean
- [ ] Run through every acceptance box above

**Dependencies:** Tasks 1-10
**Files likely touched:** none (verification only; fixes as found)
**Estimated scope:** S

---

### Checkpoint: Complete
- [ ] All acceptance criteria met; ready for review
