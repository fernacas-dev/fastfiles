# FastFiles

Minimalist, fast, keyboard-friendly graphical file manager for Linux.

Small C codebase (~3.2k lines in `src/`) built on **SDL3 + FreeType + POSIX**, with event-driven rendering (~0% idle CPU), safe filesystem operations, and zero background daemons.

> Built by [IdeaForgePR](https://github.com/IdeaForgePR) — also featured in the author's personal portfolio.

---

## Features

- **Browse** directories with grid view, dirs-first + case-insensitive sorting
- **Navigate**: Back / Forward / Up / Home, sidebar (Home, Desktop, Documents, Downloads, Pictures, Music, Videos), editable path bar
- **Open** files with the desktop default app via `xdg-open` (safe `fork` + `execlp`, no `system()`)
- **File operations**: create folder, rename, copy, cut/move, delete (with confirmation)
- **Search**: live case-insensitive filter of the current directory
- **Hidden files** toggle (`Ctrl+H`)
- **Selection**: mouse click, double-click to open, arrows + Enter, `Ctrl+A`
- **Context menu**, dialogs (new folder, rename, confirm delete, properties, error), status bar with free-space info
- **Config** via `~/.config/fastfiles/config` (`key=value`, no JSON/YAML)
- **Safety**: never follows symlinks recursively, `O_NOFOLLOW` on copy, copy+delete only on successful cross-filesystem move, no silent overwrites
- **Unicode**: UTF-8 filenames render correctly; invalid sequences fall back to `?` without crashing

### Keyboard shortcuts

| Keys | Action |
|---|---|
| Arrows / Enter / Backspace | Move selection / Open / Parent dir |
| `F5` / `F2` / `Delete` | Refresh / Rename / Delete |
| `Ctrl+L` / `Ctrl+F` | Focus path / Focus search |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copy / Cut / Paste |
| `Ctrl+A` / `Ctrl+H` / `Ctrl+Shift+N` | Select all / Hidden files / New folder |
| `Ctrl+Q` / `Esc` | Quit / Close menu-dialog / Clear search |

## Tech stack

- Language: **C17**
- UI/render/input: **SDL3**
- Text: **FreeType** (glyph cache, 1024 entries)
- Filesystem: **POSIX** (`opendir`/`readdir`, `stat`/`lstat`, `mkdir`/`rename`/`unlink`)
- Build: **CMake ≥ 3.22** + `pkg-config`
- Quality gates: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wformat=2 -Werror`, ASan+UBSan, LSan suppressions only for third-party SDL/dbus/drm singletons

## Quick start

### Dependencies (Ubuntu/Debian)

```bash
sudo apt install build-essential cmake pkg-config libsdl3-dev libfreetype-dev
```

### Build & run

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/fastfiles            # opens $HOME
./build/fastfiles ~/Documents
./build/fastfiles --help
./build/fastfiles --version
```

### Install

```bash
cmake --install build   # installs binary + fastfiles.desktop
```

### Sanitizer build + tests

```bash
cmake -B build-san -DCMAKE_BUILD_TYPE=Debug -DFF_SANITIZERS=ON
cmake --build build-san -j$(nproc)
ctest --test-dir build-san -V
```

Tests use only `mkdtemp` directories under `/tmp` — never your real `$HOME` for mutations. Covers: listing, sorting, hidden files, filter, rename, copy (incl. 70KB file + recursive dir), move, symlink/broken-link safety, permission errors, Unicode/long/space/dash names, app-layer navigate/history/clipboard.

## Configuration

File: `~/.config/fastfiles/config`

```ini
show_hidden=false
sidebar=true
view=grid
icon_size=48
font_size=15
confirm_delete=true
# font_path=/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
```

Unknown keys print a warning and are ignored.

## Project structure

```
src/
  main.c    # CLI (--help/--version/--config/PATH) + event-driven main loop
  app.c/h   # central state, navigation history, clipboard, ops (create/rename/delete/copy/move)
  fs.c/h    # POSIX filesystem layer: list/sort/filter, safe delete/copy/move
  ui.c/h    # SDL3 rendering: header, sidebar, grid, status, menu, dialogs + hit-testing
  input.c/h # SDL event -> app actions (keyboard, mouse, text input, scrolling)
  config.c/h# key=value config loader
  font.c/h  # FreeType wrapper with glyph cache + UTF-8 decode
tests/
  test_fs.c   # filesystem unit tests
  test_app.c  # headless app-layer tests (SDL offscreen driver)
  lsan.supp   # LSan suppressions for SDL/dbus/drm only
fastfiles.desktop  # XDG desktop entry
```

Design notes:

- `main.c`: `SDL_WaitEventTimeout(250ms)` + `dirty` flag — renders only on state change; cursor blink re-renders only when a text field/dialog is visible.
- `ui.c`: renders only visible grid cells (`scroll_offset`, early `continue` for off-screen rows); handles 50k-file dirs without 50k UI objects.
- `fs.c`: flat `realloc` array (`64 → *2`), `qsort` dirs-first; recursive delete/copy never follow symlinks (`lstat` + `readlink` recreate).
- No threads, no network, no indexing daemon in V1.

## Screenshots

<!-- Uncomment once the files exist in docs/ (see docs/CAPTURE-GUIDE.md):
![FastFiles grid view](docs/screenshot-grid.png)
![FastFiles search](docs/screenshot-search.png)
![FastFiles demo](docs/demo.gif)
-->

> Screenshots pending — see `docs/CAPTURE-GUIDE.md` for the 5-min capture recipe (`docs/screenshot-grid.png`, `docs/demo.gif`).

## Roadmap (V2 ideas, out of scope for V1)

Trash (FreeDesktop spec), list view, multi-select, drag & drop, inotify refresh, tabs, bookmarks, thumbnails. Each must justify its complexity against the goal: *the smallest practical graphical file manager*.

## Contributing

Keep it small and explicit: direct C, small functions, no new dependencies without POSIX-first evaluation, `-Werror` clean, tests + sanitizers green.

## License

MIT — see [LICENSE](LICENSE).

## Author / Portfolio

- Personal portfolio: _TODO: add your portfolio URL_
- Company: **IdeaForgePR** — _TODO: add company portfolio URL_
- Project: FastFiles — minimalist Linux file manager (C17, SDL3, FreeType, CMake)
