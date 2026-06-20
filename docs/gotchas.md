# Gotchas

Non-obvious things that have cost (or would cost) a session. Add to this as you
hit new ones.

## Build / environment

- **Nix flakes only see git-tracked files.** After adding new files, `git add`
  them (staging is enough) or the tree the build sees is stale and compiles can
  fail with "not tracked by Git". The `warning: Git tree ... is dirty` line is
  just the uncommitted-changes notice — harmless.
- **`esp-devkit/` is a git submodule** (separate repo). Edits under it are commits
  to *that* repo, not NameCardKnot — commit/push there, then bump the submodule
  pointer here.
- **Delete `build/` after editing any `CMakeLists.txt`.** `run.sh` only runs
  `cmake --fresh` when `build/` is missing, so it won't otherwise pick up build
  graph changes (new sources, new components).
- **This nix shell lacks SDL2 dev headers and libc++ for ad-hoc `gcc`/`clang++`.**
  Raw `-fsyntax-only` of C++ or SDL-using files fails *environmentally* (e.g.
  `cstdlib` / `SDL2/SDL.h` not found) — not a code error. Validate via the real
  build (`nix develop -c ./run.sh ...`), or stub headers for a pure-logic C check.
- **The Bash tool's cwd is per-call.** A bare relative `-Iinc` only resolves if
  that same call `cd`s there first; prefer absolute `-I` paths when checking
  compiles out of band.

## BSP / display

- **An EPD shows nothing from `draw_bitmap` alone.** With the persistent EPD mode
  at its default `BSP_EPD_MODE_NONE`, a draw only updates GRAM. The app must call
  `bsp_display_refresh(area, mode)` (or `bsp_display_set_epd_mode(non-NONE)` first)
  to paint the panel. `paper_s3` is `DIRECT_EPD`, so its app draws then refreshes.
- **`L8` has no SDL grayscale streaming format.** `sdl_panel` expands L8 to RGB24
  at present/capture time (texture is `SDL_PIXELFORMAT_RGB24`); don't expect a
  1-byte-per-pixel SDL texture.
- **SDL/Cocoa is main-thread-only on macOS.** All rendering is deferred:
  draws/flushes/refreshes only mark dirty, and `sdl_panel_present()` (main thread)
  does the SDL render. Likewise `bsp_touch_read` (background task) must not call
  SDL — it only copies a snapshot the main thread maintains in
  `sdl_panel_pump_input()`.
