# pastetry

Cross-platform clipboard manager prototype with a daemon + Qt Widgets UI.

## Features implemented

- Clipboard daemon (`pastetry-clipd`) that captures common rich clipboard formats:
  - `text/plain`
  - `text/html`
  - `text/rtf` / `application/rtf`
  - `image/png`
  - `text/uri-list`
- SQLite persistence with:
  - WAL mode
  - FTS5 full-text search
  - Pagination-ready query API
- Blob store for large/rich payloads with deduplication and reference counting.
- Local IPC over `QLocalServer`/`QLocalSocket` with CBOR-framed messages.
- Qt Widgets UI (`pastetry-clip-ui`) with:
  - Tray icon + tray menu (`Open History`, `Open Quick Paste`, `Settings`, `Quit`)
  - Single-instance behavior (second launch toggles quick-paste popup in existing instance)
  - Start-minimized-to-tray lifecycle
  - Configurable global shortcuts with per-action mode:
    - `Disabled`
    - `Direct` (single global key sequence)
    - `Chord` (two-step global sequence)
  - Global slot actions for:
    - Copy recent non-pinned item `#1..#9`
    - Paste recent non-pinned item `#1..#9`
    - Copy pinned item `#1..#9`
    - Paste pinned item `#1..#9`
  - Auto-paste key configuration (default `Ctrl+V`) for paste-style slot actions
  - Quick-paste popup (keyboard-first search + Enter to activate)
  - Quick-paste auto-hide when window loses focus
  - Configurable visible columns for History and Quick Paste tables
  - Configurable preview line count with uniform row heights
  - Manual pinned ordering with drag/drop in History (when search is empty)
  - Settings dialog supports `Apply` without closing
  - Search
  - Paginated list view
  - Activate (restore to clipboard)
  - Pin/unpin
  - Delete
  - Clear unpinned history
- Unit tests for repository insert/detail/blob-read and 12k-entry search pagination.

## Build

```bash
cmake -S . -B build -G Ninja
cmake --build build -j
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

## Run

Terminal 1:

```bash
./build/pastetry-clipd
```

Terminal 2:

```bash
./build/pastetry-clip-ui
```

Optional arguments for both binaries:

- `--data-dir <dir>`
- `--socket <name>`
- If UI cannot reach daemon, it warns and keeps retrying connectivity in the background.

### Global shortcuts

- Configure from tray menu: `Settings`.
- Existing actions and slot actions are configurable independently.
- Each action can be `Disabled`, `Direct`, or `Chord`.
- Chord mode supports a global step-1 key followed by a global step-2 key.
- Defaults are conservative:
  - Existing top-level actions keep prior behavior.
  - Slot actions are disabled by default.
- Current backend targets:
  - Windows: global shortcut supported.
  - Linux X11 (`xcb` session): global shortcut supported.
  - Linux Wayland:
    - Direct shortcut mode is supported through Wayland backend selection.
    - Chord shortcut mode is unavailable in current Wayland builds.
    - Startup uses non-interactive registration; interactive registration is attempted on `Settings` apply.
    - Clipboard daemon auto-enables `QT_WAYLAND_USE_DATA_CONTROL=1` when unset to
      prefer native compositor clipboard-manager support.
- Optional backend override for debugging/support:
  - `PASTETRY_SHORTCUT_BACKEND=auto|windows|x11|wayland_portal|wayland_wlroots|disabled`

### Slot actions

- Recent slots (`recent #1..#9`) resolve from newest non-pinned entries only.
- Pinned slots (`pinned #1..#9`) resolve from manual pinned order.
- Paste slot actions activate entry + send synthetic paste key sequence.
- On Wayland where synthetic paste is unavailable, paste slot actions still activate the
  selected entry and show a one-time manual-paste info hint.
- If slot item is missing, UI shows a warning notification.

### Display preferences

- Configure from tray menu: `Settings`.
- You can choose per-column visibility separately for:
  - History view
  - Quick paste view
- You can also right-click the table header in History/Quick Paste to toggle visible columns directly.
- Preview text line count is configurable (1-12) and applied with fixed row height per entry.
- Preview text uses multiline rendering with last-line ellipsis when content exceeds visible lines.
- Image entries render inline thumbnails in the Preview column with the same fixed row height as text entries.
- Status highlighting uses edge markers (no row background fill):
  - Pinned entries: left bookmark spine.
  - Newly copied entries: top streak (multiple recent rows can be highlighted at once).

## Notes

- This implementation is C++20 + Qt Widgets only.
- On Linux X11, auto-paste simulation for slot actions requires XTest support at build time.
- Linux builds no longer require X11 development packages; X11/XTest integrations are enabled when present.
- On Wayland, when compositor data-control support is unavailable (or probe support is
  not built), clipboard monitoring falls back to degraded Qt mode and may miss some updates.
- Hashing currently uses SHA-256 for blob identity/dedup.
- Clipboard source app/window metadata is stored as `unknown`/empty in this prototype.
- Logging:
  - `pastetry-clipd`: `<data-dir>/logs/pastetry-clipd.log`
  - `pastetry-clip-ui`: `<data-dir>/logs/pastetry-clip-ui.log`
  - logs also mirror to stderr
  - rotation keeps one backup at `*.log.1` when log reaches 5 MB
  - optional filter rules via env: `PASTETRY_LOG_RULES`
