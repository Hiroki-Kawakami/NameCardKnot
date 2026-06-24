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
  at its default `BSP_EPD_MODE_NONE`, a draw only stamps the framebuffer. The app
  must call `bsp_display_refresh(area, mode)` (or `bsp_display_set_epd_mode(non-NONE)`
  first) to paint the panel. `paper_s3` is `DIRECT_EPD`, so its app draws then refreshes.
- **The device EPD engine is invisible in the simulator.** On device, `epd_ll` is a
  transaction-index waveform engine (pure core unit-tested via
  `esp-devkit/bsp/driver/test/run.sh`):
  one byte per pixel holds `[7:4]=gray, [3:0]=transaction id`; up to 15 generations
  drive concurrently. The **diff-skip is done at draw time** — `draw_bitmap` compares
  the new gray against the byte's current nibble and only stamps an id where they
  differ (no second "displayed" buffer). `BSP_EPD_MODE_FULL` (OR'd in) aborts every
  in-flight generation and drives the whole panel for a ghost-clearing flush. The
  `sdl_panel` EPD model replays no waveform — it just shows the latest framebuffer —
  so none of this is observable in the sim. Verify on hardware: only the changed
  region updates, disjoint regions can update in different phases at once, and a
  periodic full QUALITY clears ghosting.
- **`bsp_display_refresh` is async on the device EPD; the scan runs on a task.**
  A partial `refresh` binds the open generation's waveform LUT and activates it
  (O(1)), returning immediately; a pinned background task runs a continuous frame
  loop while any generation is ACTIVE. **One framebuffer** (`state`) — no
  gram/snapshot/disp. A single mutex guards every *write* to `state`/`tx`/`pending_id`
  (the draw stamp, the LUT bind, the terminal id→0 reclaim) plus the short per-frame
  action-table build — never the multi-row scan, which reads `state` lock-free (byte
  reads are atomic). Consequences of the single buffer: (1) no frozen snapshot, so
  **redrawing a pixel mid-flight with a *different* target interrupts its waveform**
  (restarts under the new generation) — harmless, recovered next refresh, but a
  transient artifact. Redrawing the *same* target gray (idle or in-flight) is
  skipped in `epd_draw_pixel`, so a dirty box that overlaps unchanged in-flight
  pixels (LVGL joins invalidated areas, so this is common) does **not** re-flash
  them; (2) the **terminal reclaim is a separate locked pass, not folded into the
  blit** (folding would write `state` during the lock-free scan and race the draw).
  A generation activated mid-scan is left at frame 0 and rendered next frame
  (`epd_frame_mark`'s active-mask gates `epd_frame_advance`), never advanced past
  its own first frame. A `FULL`/`CLEAR` refresh subsumes concurrent draws — treat it
  as a quiescent reset, not a mid-animation op. The task is pinned to core 0 to keep
  its DMA busy-waits off the LVGL core. (The simulator path stays synchronous — this
  engine lives only in the device driver.)
- **The simulator SD redirect only catches calls compiled into the executable.**
  `sd_redirect.c` overrides `open`/`fopen`/`opendir`/`stat`/`rename`/`unlink` by
  defining them in the binary so the static link binds the app's references to them
  (verified to work on macOS too). Calls made from a *prebuilt* shared library are
  not intercepted, and a file API the app uses that isn't in that list passes
  through untranslated — add it to `sd_redirect.c` if a mount-point path can reach
  it. There is no real filesystem: `bsp_sd_mount` just records the path prefix.
- **`L8` has no SDL grayscale streaming format.** `sdl_panel` expands L8 to RGB24
  at present/capture time (texture is `SDL_PIXELFORMAT_RGB24`); don't expect a
  1-byte-per-pixel SDL texture.
- **SDL/Cocoa is main-thread-only on macOS.** All rendering is deferred:
  draws/flushes/refreshes only mark dirty, and `sdl_panel_present()` (main thread)
  does the SDL render. Likewise `bsp_touch_read` (background task) must not call
  SDL — it only copies a snapshot the main thread maintains in
  `sdl_panel_pump_input()`.

## LVGL / ui_framework

- **LVGL runs on a different thread than your app task.** On device, esp_lvgl_port
  owns an LVGL task; on the simulator, `lv_timer_handler()` runs inside
  `lvgl_sim_loop()` on the main thread (`simulator/main/main.cpp`) while the app's
  FreeRTOS task is a separate pthread. Two portable ways to mutate widgets from
  another task: take LVGL's global lock with **`lv_lock()`/`lv_unlock()`** (works on
  both targets because `LV_USE_OS` is set — FreeRTOS on device, PThread on the sim —
  so it serialises against the timer handler), or marshal onto the LVGL context with
  **`lv_async_call()`** (from `lvgl.hpp`). `ScreenManager` uses the latter (see
  `retire()`), and `app_entry()` loads its first screen via `lv_async_call`.
  (`lvgl_port_lock()` is the device-only equivalent of `lv_lock`; prefer `lv_lock`
  in code shared with the simulator.)
- **`lvgl_port_init()` is the one init call on both targets.** Device gets the real
  `esp_lvgl_port`; the simulator gets a same-signature shim in `lvgl.cpp`
  (`#ifndef ESP_PLATFORM`) that does `lv_init()` + an SDL tick. So app/board code
  calls `lvgl_port_init(&cfg)` identically — the per-target split lives in the
  shim, not in `#ifdef`s scattered through the app.
- **The BSP↔LVGL binding + EPD policy are app-side, not in `ui_framework`.**
  `ui_framework` is panel-agnostic (LVGL port + UI utilities only). The
  `lv_display`/`lv_indev`, flush callback, buffer, and EPD refresh policy live in
  `app/NameCardKnot.cpp` — written against `bsp_*` + `lvgl` so the one file serves
  both targets. Don't push panel assumptions back down into the shared component.
- **EPD repaint is decided at flush time, not draw time.** The app's flush_cb blits
  each area to GRAM (`bsp_display_draw_bitmap`), accumulates the dirty rect, and on
  the *last* partial flush calls `bsp_display_refresh(dirty, mode)`. The mode is the
  app's own policy: `epd_set_default_refresh_mode()` (standing) /
  `epd_set_next_refresh_mode()` (overrides the next refresh) in `NameCardKnot.hpp`.
- **Display rotation = LVGL for layout/touch + BSP for pixels.** The app calls
  `lv_display_set_rotation()` (currently `ROTATION_90` = 90° CCW) so LVGL lays the
  UI out in portrait and auto-rotates touch input — that's the single source of
  truth for orientation. But in `LV_DISPLAY_RENDER_MODE_PARTIAL` with L8, LVGL core
  does **not** physically rotate the rendered pixels (matrix rotation only works in
  FULL/DIRECT mode and needs `LV_DRAW_TRANSFORM_USE_MATRIX`, off in our `lv_conf.h`).
  Rather than rotate into a scratch buffer and copy again, the flush_cb forwards the
  rotation to **`bsp_display_draw_bitmap(rect, px_map, rotation)`**, which fuses the
  transpose into the unavoidable framebuffer write (the sim backends use the shared
  `bsp_blit_rotated`; the device `epd_ll` inlines the same source↔panel mapping into
  its diff+stamp stamp loop). `BSP_ROTATION_*` mirror `lv_display_rotation_t` (a
  `static_assert` in the app locks this) so the flush_cb just forwards
  `lv_display_get_rotation()` — no second place to keep in sync. The transpose math
  matches LVGL's own `rotate{90,180,270}_l8`, so output is pixel-identical. The flush
  area is still mapped to panel coords with `lv_display_rotate_area`, and
  `lv_display_create` is given the *physical* panel size. Flip CW/CCW by swapping the
  one `ROTATION_90`↔`ROTATION_270` in `lvgl_init()`.
- **Two LVGL builds, one API.** Device pulls LVGL + esp_lvgl_port as managed
  components (esp_lvgl_port declared in `ui_framework/idf_component.yml`; configured
  via sdkconfig Kconfig); the simulator `FetchContent`s upstream LVGL
  (`release/v9.5`) configured by `ui_framework/sim/lv_conf.h`. Only the major.minor
  need match — a sim `lv_conf.h` from a slightly newer 9.x is fine (LVGL fills
  unknown knobs with defaults). Override the sim config dir with the
  `UI_FRAMEWORK_LV_CONF_DIR` CMake cache var before configuring.
