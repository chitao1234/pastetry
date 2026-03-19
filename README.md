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
  - Configurable global shortcut for quick-paste popup (disabled by default)
  - Quick-paste popup (keyboard-first search + Enter to activate)
  - Quick-paste auto-hide when window loses focus
  - Configurable visible columns for History and Quick Paste tables
  - Configurable preview line count with uniform row heights
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

### Quick-paste shortcut

- Configure from tray menu: `Settings`.
- Default is disabled (no global shortcut).
- Current backend targets:
  - Windows: global shortcut supported.
  - Linux X11 (`xcb` session): global shortcut supported.
  - Linux Wayland: global shortcut registration is unavailable; tray actions still work.

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
- Hashing currently uses SHA-256 for blob identity/dedup.
- Clipboard source app/window metadata is stored as `unknown`/empty in this prototype.
