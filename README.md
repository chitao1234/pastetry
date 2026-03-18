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

## Notes

- This implementation is C++20 + Qt Widgets only.
- Hashing currently uses SHA-256 for blob identity/dedup.
- Clipboard source app/window metadata is stored as `unknown`/empty in this prototype.
