# Docs — Screenshots & Demo

This folder will hold the visual assets for the README and portfolios.

## Expected files (placeholders)

- `screenshot-grid.png` — main window, grid view with sidebar (1920×1080 or 1280×800, PNG)
- `screenshot-search.png` — search filter active (optional but recommended)
- `demo.gif` — <30s loop: navigate → search → rename → create folder (max width 960px, <5MB for GitHub)

Until the files exist, `README.md` shows no images. Once added, they render automatically.

## How to capture (Ubuntu)

```bash
# 1. Build release
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# 2. Prepare a photogenic demo dir (no personal data)
mkdir -p /tmp/ff-demo/{Projects,Documents,Downloads,Pictures,Music}
touch /tmp/ff-demo/notes.txt /tmp/ff-demo/todo.md /tmp/ff-demo/report-2026.pdf
./build/fastfiles /tmp/ff-demo

# 3. Screenshot: GNOME Screenshot / Flameshot / Spectacle
# 4. GIF: Peek (apt install peek) or:
ffmpeg -f x11grab -s 1280x800 -i :0.0 -r 15 -t 25 demo-raw.mp4
ffmpeg -i demo-raw.mp4 -vf "fps=12,scale=960:-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse" demo.gif
```

## Checklist before publishing images

- [ ] No personal paths, usernames, or private files visible
- [ ] Window shows `FastFiles` title, sidebar + grid + status bar
- [ ] PNG optimized (`optipng` / `pngquant`), GIF <5MB
- [ ] Reference them from `README.md` (see commented block below)
