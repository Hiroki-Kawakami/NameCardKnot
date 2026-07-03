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
- **Boot does not clear the EPD.** The glass keeps its last image while the
  driver assumes white, so a diff refresh would drive from a wrong `from` gray.
  Establish a baseline first: `bsp_display_clear()` (what `app_entry` does), or
  draw the known on-glass image in `BSP_EPD_MODE_SEED` — adopted with no drive,
  so later draws diff-skip against it. Seed **before** any normal draw of the
  same content (the reverse order stamps PENDING and re-drives the whole image).
  `paper`'s IT8951E can't seed its TCON-internal previous image — the first
  refresh over a seeded area may show artifacts; verify on hardware.
- **The device EPD engine is invisible in the simulator.** On device, `epd_ll` is a
  per-pixel waveform engine (pure core unit-tested via
  `esp-devkit/bsp/driver/epd/test/run.sh`): one uint16 per pixel holds
  `confirmed gray | target gray | waveform id | start frame`, so every pixel
  replays its own from×to waveform independently — no generation slots, no
  concurrency limit. The **diff-skip is done at draw time** — `draw_bitmap`
  compares the new gray against the pixel's target and only stamps changed pixels
  PENDING (no second "displayed" buffer); `refresh(area, mode)` arms the PENDING
  pixels **inside `area`** (the rect is honored). `BSP_EPD_MODE_ALL` (OR'd in)
  drives every pixel of the area, diff or not — idle pixels re-drive from==to,
  the LUT diagonal, which is the ghost clear. `bsp_display_clear()` (a separate
  op; CLEAR is no longer a refresh mode) blanks the whole panel to white. The
  `sdl_panel` EPD model replays no waveform — it just shows the latest framebuffer —
  so none of this is observable in the sim. Verify on hardware: only the changed
  region updates, disjoint regions can update in different phases at once, and a
  periodic QUALITY_ALL clears ghosting.
- **An in-flight waveform is never interrupted — a conflicting call BLOCKS.**
  Interrupting a waveform mid-replay unbalances the panel's DC offset, so the
  engine refuses: `draw_bitmap` over a pixel mid-waveform toward a *different*
  gray, `refresh(..., ALL)` over an area with any in-flight pixel, and
  `bsp_display_clear()` while anything is in flight all **block the caller**
  (the LVGL flush_cb included) until those pixels retire. To shorten the wait,
  the engine skips waveforms' skippable settle tails (`SETTLE` frames, marked
  `0xFFFFFFFF` — DC-neutral) while anyone is blocked; the clear waveform's tail
  is `STOP` (all-zero, non-skippable), so a clear-then-draw sequence can never
  cut the clear short. Redrawing the *same* target gray (idle or in-flight) is
  skipped in `epd_draw_px`, so a dirty box that overlaps unchanged in-flight
  pixels (LVGL joins invalidated areas, so this is common) does **not** re-flash
  or block on them. Don't design UI that redraws the same region faster than its
  waveform (~0.3s QUALITY with settle skipped) — it will stall the UI task by
  construction.
- **`bsp_display_refresh` is async on the device EPD; the scan runs on a task.**
  A non-ALL `refresh` arms the area's PENDING pixels and returns; a pinned
  background task runs a continuous frame loop while any pixel is in flight.
  **One framebuffer** (`state`) — no gram/snapshot/disp. A single mutex guards
  every *write* to `state` and the counters (the draw stamp, the arming, the
  retire pass) plus the short per-frame b1-table build — never the multi-row
  scan, which reads `state` lock-free. The **retire pass is a separate locked
  pass, not folded into the blit** (folding would write `state` during the
  lock-free scan and race the draw). A pixel armed mid-scan carries
  `start = frame + 1` and decodes as "armed" (step 63) for the rest of that
  frame, so a torn frame can never skip a waveform's first step — hence LUTs are
  capped at `EPD_WF_STEP_MAX` (62) frames. (The simulator path stays
  synchronous — this engine lives only in the device driver.)
- **The i80 trans-done ISR must NOT share a core with the scan, or every scanline
  stalls ~50µs.** esp_lcd's `esp_lcd_panel_io_tx_color` ends by re-enabling the
  trans-done interrupt, and with the event already pending the ISR runs *synchronously
  inside `tx_color`* — but only when called on the ISR's own core. esp_lcd registers
  the ISR on whichever core calls `esp_lcd_new_i80_bus`, so `epd_ll_create` runs
  `bus_init` from a short-lived task pinned to the core OPPOSITE
  `cfg->task_affinity` (the refresh-task core). Net effect on hardware: per-line
  `tx_color` dropped ~56µs→~18µs and the blit ~61µs→~38µs (no more ISR contention),
  ED047TC1 full GC16 ~1.75s→~1.3s. So **pin the refresh task (don't leave
  `task_affinity` < 0)** — a floating task can land on the ISR core and stall.
- **The EPD refresh busy-spins a whole core, so polling touch from the UI loses
  taps — use the push callback.** The `ed047tc1` refresh task does real per-line CPU
  work (`while (s_dma_busy) taskYIELD()` + `esp_rom_delay_us`) at priority 5; a
  same-core task at lower priority (e.g. LVGL at 4) gets **zero CPU for the whole
  refresh**, so a tap during a refresh is missed if touch is only polled from the
  LVGL `read_cb`. The seam: set `bsp_config.touch.task_priority` (above the EPD
  task) so the **GT911 reader task** samples the chip independently and *pushes*
  each sample to the app's `bsp_touch_set_event_cb` the moment it arrives (display
  space; count 0 = release). The driver does **not** cache — the app's callback
  owns that policy (NameCardKnot caches the coord under a mutex and latches the
  press edge so a brief tap mid-refresh still clicks once the UI core is free).
  `paper` (IT8951E, synchronous, no refresh task) can leave `bsp_config.touch`
  zeroed and just `bsp_touch_read`. The callback also fires on the **simulator**
  (`sdl_panel` emits it from its input pump), so the app's touch path is identical
  on both targets. (The EPD engine itself is device-only — the sim replays no
  waveform.)
- **Don't permanently `esp_partition_mmap` a big region on the original ESP32.**
  Its read-only data (DROM) mmap window is ~4MB and the app's `.rodata` (fonts/icons,
  ~1.9MB here) already lives there. A persistent 2MB map (the first My Card cut did
  this) leaves almost no window and makes `paper` **freeze silently** on the next
  full EPD refresh — the `esp32s3` window is laid out differently and survived, which
  is exactly how it slipped through. `app/MyCardStore` now reads the header by copy
  (`esp_partition_read`) and maps only the image blob being shown, on demand, via a
  `MappedImage` that unmaps on leave. See [`mycard.md`](mycard.md).
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

### HotKnot (`bsp_hotknot_*`, GT911)

- **Touch recovery after a session needs a wired RESET.** `bsp_hotknot_end()`
  hard-resets the GT911 and re-boots its touch firmware via INT-sync, but
  `gt911_reset` only does a real reset when the board wires the RESET pin, and
  `gt911_int_sync` only runs when INT is output-capable. **M5Paper and M5PaperS3
  leave RESET on the on-module circuit (unwired), so after a HotKnot session the
  chip stays in its SRAM subsystem and won't scan touch until a power cycle** —
  this is a Paper-family constraint, not a HotKnot limitation. A board that wires
  RESET + an output-capable INT (e.g. Tab5) recovers fully. The recovery path is
  capability-driven, so don't assume reset is impossible.
- **The reader task is mandatory and drives the whole session.** `bsp_hotknot_begin`
  returns `ESP_ERR_INVALID_STATE` without it (`bsp_config.touch.task_priority` must
  be > 0). Pair detection, FW load, and receive-polling all run as a session step
  installed on that one task — the chip can't serve touch and HotKnot at once, so
  there is a single I2C owner, lock, and task.
- **Events fire on the reader task, not the UI thread.** The `bsp_hotknot_event_cb_t`
  runs off the LVGL/app context; marshal to it (`lv_async_call`) and keep the cb
  short. `RECEIVED` `data` points at the task's buffer — valid only during the
  callback, so copy it before returning.
- **Touch is alive only through PAIRED.** From `PAIRED` (FW load) until
  `bsp_hotknot_end()` the chip's main firmware isn't running, so `bsp_touch_read`
  / the reader task report nothing. Don't expect taps during data exchange.
- **The two ends must `begin()` with opposite roles** (`SLAVE` vs `MASTER`) — the
  approach command differs (0x20/0x21) and same-role terminals won't pair. Which
  side sends after `READY` is independent of role (either may `bsp_hotknot_send`).

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

## Image decoding (`image_processor`)

- **An EPD draw alone still shows nothing.** `image_processor` only produces the
  L8 buffer; `NameCardScreen` relies on the normal flush → `bsp_display_refresh`
  path (and `QUALITY_ALL` in `onAppear`) to paint it. Same EPD rule as everything
  else above.
- **Dither at the display resolution, show 1:1.** Decode to the on-screen size
  (`target_w/h` = display resolution) and let `lv_image` show it without scaling.
  If LVGL rescales a dithered image the grain turns to mush — the dither pattern is
  only valid at the exact pixel grid it was computed for.
- **16 levels land in the high nibble on purpose.** L8 packs `level*255/(N-1)`; for
  N=16 that is `level*17`, so the byte's top nibble equals the level and maps 1:1
  onto the panel's 4-bit gray. `levels=2`→`FAST`, `levels=16`→`QUALITY`. Don't
  "normalize" L8 values elsewhere or you desync from the panel.
- **A mid-decode failure surfaces as `Truncated`, not the real cause.**
  `RowSource::next_row` is boolean, so the pipeline maps any decoder error during
  row production (corrupt deflate, bad Huffman) to `Status::Truncated`. `open()`
  still returns precise statuses (`UnsupportedFormat`/`DecodeError`/…).
- **Decoders are baseline/8-bit only — by design.** PNG: no interlace, no 16-bit.
  JPEG: baseline SOF0 only (progressive/arithmetic/16-bit/CMYK → `UnsupportedFormat`),
  chroma sampling ≤ 2×2. These return cleanly from `open()`; they don't crash.
- **The in-tree `inflate` is pull-based and resumes mid-block.** It yields exactly
  the bytes asked for and continues a back-reference copy across calls. The subtle
  bug to avoid (already fixed): when a copy fills the output buffer *exactly*
  (`produced==n`, `copy_rem_==0`), re-check the loop bound before decoding the next
  symbol or you emit one byte too many.
- **Regenerate fixtures, don't hand-edit them.** `test/png_fixtures.h` and
  `test/jpeg_fixtures.h` are generated (`gen_fixtures.py` via stdlib zlib;
  `gen_jpeg.c` via libjpeg — note `jpeglib.h` needs `<stdio.h>`/`<stddef.h>`
  included *before* it). Commit the regenerated header.
- **GLOB still needs a cmake re-configure.** `image_processor/CMakeLists.txt` globs
  `src/*.c{,pp}`; after adding a source, delete `build/` (sim) or let `idf.py`
  reconfigure — the same caveat as `app/`.
