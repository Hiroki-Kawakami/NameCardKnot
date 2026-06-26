# NameCardKnot — Notes for Claude

Firmware for an M5Paper-class name-card device on a **960×540 grayscale EPD**.
Two boards are supported, selected per build target (no runtime detection):
`paper_s3` (**ESP32-S3 + ED047TC1** i80 EPD, build root `esp32s3/`) and `paper`
(**ESP32 + IT8951E** SPI EPD, build root `esp32/`). Built on the reusable
**`esp-devkit`** BSP / simulator infrastructure, with a host SDL simulator so
UI/app logic can be developed and verified without hardware. The BSP display +
simulator + sim harness are in place, and the **device EPD + touch paths are
implemented**: on `paper_s3` the ED047TC1 panel over the ESP32-S3 i80 bus is a
transaction-index waveform engine driving up to 15 concurrent refresh
generations on an async background task, with a `BSP_EPD_MODE_FULL` flag for
ghost-clear full flushes; on `paper` the IT8951E TCON is driven over SPI
(synchronous load-image + display, no async task). Both use the in-tree `gt911`
I2C touch driver: `bsp_touch_read` polls by default, or the app spawns a
decoupled reader task (`bsp_config.touch`) that **pushes** each sample to
`bsp_touch_set_event_cb` so taps survive a starved UI core — the app caches/latches
the pushed coords itself (see [`docs/gotchas.md`](docs/gotchas.md)). **LVGL is
wired up** via the `ui_framework` LVGL port abstraction + an app-side BSP↔LVGL
binding, and **SD-card access is in** (`bsp_sd_*`: FAT-over-SDSPI on device, a
host-directory redirect in the simulator). The name-card image path uses an
**in-tree, dependency-free `image_processor`** (JPEG/PNG decode → grayscale →
dither → EPD-ready L8). The app is a small screen set (home, SD file browser,
name card).

> **Keep the docs current.** When you change the build flow, the BSP surface
> (`bsp_*`), the simulator backend, add a board/target, or hit a non-obvious
> gotcha, update CLAUDE.md (and the right `docs/` file) in the same change — they
> are the handoff to the next session. Keep CLAUDE.md lean: deep verification
> detail lives in [`docs/testing.md`](docs/testing.md), gotchas in
> [`docs/gotchas.md`](docs/gotchas.md), and the image pipeline in
> [`docs/image_processor.md`](docs/image_processor.md) — link, don't inline.

> **Comment only what earns it.** Write a code comment when its absence would
> cause a concrete mistake later — not to narrate the obvious or to hedge. In
> particular: don't explain one mechanism by describing its alternatives (e.g. the
> locking story inside an `lv_async_call` comment), and in a per-target file don't
> describe the other target at length (the device `CMakeLists.txt` needn't explain
> the host build). When in doubt, leave it out.

## Build environment

The ESP-IDF (v5.4.3) toolchain and the host-simulator tools (cmake, ninja, gcc,
SDL2, libjpeg, cjson) all come from the Nix flake. **Always run build commands
through `nix develop -c <cmd>`** (or from inside a `nix develop` shell) — never
invoke `idf.py` / `cmake` directly. `.envrc` is `use flake`, so direnv users get
the shell automatically. (See [`docs/gotchas.md`](docs/gotchas.md) for the
git-tracked-files and submodule caveats.)

**`esp-devkit/` is a git submodule** (`github.com/Hiroki-Kawakami/esp-devkit`)
holding the `bsp`, `idf_compat`, and `sim_harness` components. Edits there are
commits to *that* repo, not NameCardKnot. `app/`, `simulator/`, and `esp32s3/` are
this repo.

## Targets & commands

```sh
./run.sh                                          # build + run the interactive SDL simulator
./run.sh simverify simulator/verify/smoke.txt     # headless scripted verification → docs/testing.md
./run.sh esp32s3                                  # idf.py -C esp32s3 flash monitor (needs a TTY)  — paper_s3 board
./run.sh esp32                                     # idf.py -C esp32 flash monitor (needs a TTY)    — paper board
```

(Call as `nix develop -c ./run.sh ...` or from a dev shell.) The simulator
defaults to the `paper_s3` board; build the `paper` board's sim with
`cmake -S simulator -B build -DBSP_BOARD=paper`.

## Repo layout

```
app/                  # SHARED app logic (NameCardKnot.cpp: app_entry + the BSP<->LVGL binding)
  screens/            #   Screen subclasses (Home, FileBrowser, NameCard) loaded via screen_manager
  resources/          #   generated LVGL assets (resources.{c,h} aggregates them into `R`); see below
  lv_image_adapter.hpp #  imgproc::Image -> lv_image_dsc_t (keeps image_processor LVGL-free)
components/           # APP-specific reusable components (container; each subdir is a component)
  image_processor/    #   self-contained JPEG/PNG decode -> grayscale/dither -> L8/I4/I1 → docs/image_processor.md
    inc/              #     public API: image_processor.hpp (Options/Image/Status/decode_*)
    src/              #     pipeline + in-tree decoders (inflate, png, baseline jpeg); no external deps
    test/             #     host unit tests (run.sh, g++) + fixture generators (gen_fixtures.py, gen_jpeg.c)
  namecard_pdf/       #   parse the editor's container PDF (NCK footer index) + write share-only PDF → docs/namecard_pdf.md
    inc/src/          #     public API: namecard_pdf.hpp (parse_buffer/open_file/read_asset/write_share_pdf/parse_name_glyphs); LVGL-free, dep-free
    test/             #     host tests (run.sh, run_e2e.sh→image_processor) against TS golden fixtures/ (manifest.h)
editor/               # TypeScript/React/Vite SPA that AUTHORS the container PDF (no backend)
  src/lib/namecard-pdf/ # framework-free PDF writer: buildNameCardPdf/buildSharePdf + footer/rle/glyphs + gen-fixtures (vitest) → docs/namecard_pdf.md
simulator/            # SIMULATOR build root
  main/main.cpp       #   host entry: app_entry() then lvgl_sim_loop() (LVGL present loop + sim-harness frame stepping)
  verify/             #   sim-harness scripts; captures land in verify/out/ (gitignored)
esp32s3/              # DEVICE build root for the paper_s3 board (ESP-IDF; build artifacts gitignored)
esp32/                # DEVICE build root for the paper board (mirrors esp32s3/; sets BSP_BOARD=paper)
esp-devkit/           # SUBMODULE — reusable devkit (separate repo)
  bsp/                #   board support (bsp_*)
    inc/              #     public API: bsp.h, bsp_types.h
    inc_private/      #     internal vtables: bsp_display.h, bsp_touch.h
    src/              #     shared dispatch: bsp_display.c, bsp_touch.c
    devices/          #     DEVICE chip drivers: ed047tc1 (i80 EPD descriptor) + it8951e (SPI EPD: driver + it8951e_epd bsp_display provider) + gt911 (I2C touch)
    driver/           #     ESP32-S3 low-level: epd_ll.c (i80 bus + CKV/SPV/LE scan + the refresh engine) + epd_waveform.h (SoC-free transaction-index core, host-tested) + test/
    simulator/        #     SIM SDL backend: sdl_panel.{c,h}
    boards/<board>/   #     per-board bring-up + board.cmake (source/requirement lists); paper_s3 (ED047TC1) and paper (IT8951E, shared EPD/SD SPI bus, paper_config.h)
  idf_compat/         #   SIM-only ESP-IDF compat (esp_*, pthread-backed FreeRTOS)
  sim_harness/        #   SIM-only scripted UI verification core (portable, DI) → docs/testing.md
  ui_framework/       #   LVGL port abstraction (panel-agnostic) + reusable UI utils
    inc/              #     lvgl.hpp (lvgl++ helpers + lvgl_port shim decl) + screen{,_manager}.hpp + widgets/ (layout.hpp)
    src/              #     lvgl.cpp (sim lvgl_port_init/lvgl_sim_loop shim), screen_manager.cpp
    sim/lv_conf.h     #     default LVGL config for the simulator build (overridable)
```

Rule of thumb: panel/board-reusable → `esp-devkit/` (the submodule); app-specific
but reusable across screens → `components/` (in this repo); app glue → `app/`;
target-only entry → that target's build root.

### Component wiring

Components are self-describing via `idf_component_register()`. On **device**,
ESP-IDF consumes it directly. On the **simulator**, `simulator/CMakeLists.txt`
defines a shim that folds each component's `SRCS`/`INCLUDE_DIRS` straight into the
one `simulator` executable (so includes are effectively global; `REQUIRES` are
ignored). A component that differs per target branches on `ESP_PLATFORM` (set only
under ESP-IDF) inside its own `CMakeLists.txt` — see `bsp/CMakeLists.txt`. New
simulator component → add it to `SIMULATOR_COMPONENTS` in `simulator/CMakeLists.txt`.
(`ui_framework` is the worked example of a component that *also* pulls a host
library: its simulator branch `FetchContent`s LVGL and links it onto the
`simulator` target directly — the shim only folds sources, not external targets.)

On **device**, each build root (`esp32s3/CMakeLists.txt`, `esp32/CMakeLists.txt`)
lists `../components` (a *container* — each subdir is a component, so new
components there are auto-discovered, no edit needed), `../esp-devkit/bsp`, and
`../esp-devkit/ui_framework` in `EXTRA_COMPONENT_DIRS` (`idf_compat`/`sim_harness`
are host-only), and sets `BSP_BOARD` (a cache var, `paper_s3` / `paper`) *before*
`project()` to pick the board. Any component that `#include`s `bsp.h` must name
`bsp` in its `REQUIRES` (e.g. `app/CMakeLists.txt`, which also names
`ui_framework` and `image_processor`).
(`image_processor` is in `SIMULATOR_COMPONENTS` for the host build; its device
requirements are the always-present `heap` for `heap_caps_*`, `esp_timer` for the
`IMGPROC_PROFILE` stage timing, and `freertos` for the `IMGPROC_ASYNC` decode task
(`DecodeJob`) — the simulator gets FreeRTOS from `idf_compat`, the host unit tests
build both off.) The bsp component's per-target source list, include dirs, and
`PRIV_REQUIRES` are *not* hard-coded in `bsp/CMakeLists.txt`: it `include()`s the
selected board's `boards/<board>/board.cmake`, which fills `BOARD_DEVICE_SRCS` /
`BOARD_DEVICE_PRIV_INCLUDE_DIRS` / `BOARD_DEVICE_PRIV_REQUIRES` (device) and
`BOARD_SIM_SRCS` (simulator). Adding a board = a new `boards/<board>/` dir with a
`board.cmake` + a build root that sets `BSP_BOARD`. Per board:
- **paper_s3** (ED047TC1 i80 EPD): `esp_lcd` in `PRIV_REQUIRES`, `ed047tc1`/`driver`
  in `PRIV_INCLUDE_DIRS`; SD on its own SPI2 bus (`paper_s3_sd.c`).
- **paper** (IT8951E SPI EPD): `driver` (spi_master) + `esp_timer` in
  `PRIV_REQUIRES`, `it8951e` in `PRIV_INCLUDE_DIRS`, *no* `esp_lcd`; `it8951e.c` is
  the SPI TCON driver and `it8951e_epd.c` the `bsp_display_t` provider. The EPD and
  microSD share one SPI bus that `paper.c` owns (`paper_config.h` pins), so
  `paper_sd.c` only attaches/detaches the card device — it never inits/frees the bus.

Touch on both is the in-tree `gt911` driver (no managed dependency) — it adds the
`gt911` dir to `PRIV_INCLUDE_DIRS` and relies on `driver` for `i2c_master`/`gpio`;
the board's `*_panel.c` brings up the I2C bus and registers the provider. Both SD
paths add `fatfs`/`sdmmc`/`esp_driver_sdmmc`/`esp_driver_sdspi` to `PRIV_REQUIRES`;
the simulator folds `simulator/sd_redirect.c` instead.

## BSP — the display/touch seam (`bsp_*`)

`app/` calls `bsp_*` on both targets; the per-target split lives inside the BSP.
Drivers are struct-inheritance vtables: a provider embeds `bsp_display_t` /
`bsp_touch_t` as its first member and is registered with
`bsp_display_set_active()` / `bsp_touch_set_active()` by the board's `bsp_init()`.
`src/bsp_display.c` / `src/bsp_touch.c` implement the public API once by
dispatching through the active vtable.

### Display vtable (`inc_private/bsp_display.h`)

- **Always non-NULL:** `draw_bitmap` (blit a rectangle; its `bsp_rotation_t` arg
  un-rotates the source into the panel-coordinate rect, fused into the copy via the
  shared `bsp_blit_rotated` helper), `deinit`.
- **Optional (NULL when absent):** `get_framebuffers` + `flush` (host-framebuffer
  fast path — MIPI), `set_brightness` (backlight), and `set_epd_mode` + `refresh`
  (EPD only). The public `bsp_display_*` wrappers no-op when the op is NULL, so the
  API is uniform across panel types.

### Panel types & the EPD model

`bsp_display_type_t`: `SPI`, `MIPI_DSI`, `SPI_EPD`, `DIRECT_EPD`. EPD refresh
(`bsp_epd_mode_t` = `NONE`/`FAST`/`QUALITY`):

- `set_epd_mode(mode)` sets a **persistent** mode. `draw_bitmap` updates GRAM but
  only paints the panel when the mode is **not** `NONE`.
- `refresh(area, mode)` paints the latest GRAM with a one-shot mode (does not
  change the persistent mode).

(Consequence: a draw alone shows nothing on EPD — see [`docs/gotchas.md`](docs/gotchas.md).)

Below this seam the device driver (`driver/epd_ll.c`) is a **transaction-index
waveform engine**: one byte per pixel (`[7:4]=gray, [3:0]=tx id`), the diff-skip
done at draw time, up to 15 generations driving concurrently on the async task,
`FULL`/`CLEAR` aborting all in-flight. Its SoC-free core (`driver/epd_waveform.h`)
is host unit-tested via `esp-devkit/bsp/driver/test/run.sh`. Details + the single-buffer trade-offs:
[`docs/gotchas.md`](docs/gotchas.md). The simulator's `sdl_panel` EPD path replays
no waveform, so none of the engine is observable there — verify on hardware.

### Simulator backend (`bsp/simulator/sdl_panel.c`)

One SDL backend mimics all three panel families behind the same vtable, chosen by
`sdl_panel_config_t::type`:

- **MIPI:** host framebuffers; `flush(idx)` repoints what is presented (no copy).
- **SPI:** `draw_bitmap` blits straight to the on-glass buffer, shown immediately.
- **EPD:** `draw_bitmap` writes a GRAM buffer; the glass updates only on a
  non-NONE mode or `refresh`.

Presentation is deferred to the main thread (SDL/Cocoa is main-thread-only);
`sdl_panel_present()` does the actual render each loop iteration. Touch is a
mutex-guarded multi-touch snapshot (`bsp_touch_read` copies it on a background
task; the main thread samples the mouse in `sdl_panel_pump_input()`). See
[`docs/gotchas.md`](docs/gotchas.md) for the threading rules.

The window is **resizable with an aspect-preserving letterbox**: `present` fits the
(rotated) panel image into the drawable centered via `content_fit`, painting black
margins; `window_to_panel` inverts the same fit so touch stays aligned at any size.
The **r/l keys rotate the host view** (ESC quits): `present` rotates the texture
with `SDL_RenderCopyEx` and the window aspect is reset for 90/270; mouse coords
are un-rotated back in `window_to_panel`. This is a viewing convenience only — the
panel buffers and touch coordinate space are unchanged (independent of the app's
own `lv_display_set_rotation`). The initial rotation defaults to 0; set it at
configure time with the `SIM_DEFAULT_ROTATION` CMake cache var (0/90/180/270,
e.g. `-DSIM_DEFAULT_ROTATION=90`), which feeds the `SDL_PANEL_DEFAULT_ROTATION`
compile define. The harness **JPEG capture honors this rotation** so headless
verification images come out in the viewing orientation (deterministic, since
headless never runs the keys) — see [`docs/testing.md`](docs/testing.md).

`sdl_panel_create(config, &display, &touch)` returns both providers (touch
nullable) and **self-registers** its input + capture callbacks with the sim
harness, so the simulator entry needs no wiring for those.

### SD card (`bsp_sd_*`)

Outside the display/touch vtables: `bsp_sd_mount`/`bsp_sd_unmount`/`bsp_sd_is_mounted`
(`bsp.h`), implemented per target with no vtable since there is one SD provider.

- **device** (`boards/<board>/*_sd.c`): FAT over the SDSPI host. On `paper_s3` the
  card has its own dedicated SPI2 bus (`paper_s3_sd.c` inits/frees it); on `paper`
  it shares the IT8951E's SPI bus owned by `paper.c`, so `paper_sd.c` only
  mounts/unmounts the card and never touches the bus. `config` NULL/zero fields
  take the documented defaults (`max_files` 0→5, `max_freq_khz`
  0→`SDMMC_FREQ_DEFAULT` (20MHz) — 40MHz over SPI fails to init some cards,
  more so on `paper`'s EPD-shared bus).
- **simulator** (`simulator/sd_redirect.c`): "mounting" just maps the mount point
  onto a host directory (`SIMULATOR_SDCARD_PATH`, default `simulator/sdcard`).
  App code keeps using plain POSIX I/O — the file defines `open`/`fopen`/`opendir`/
  `stat`/`rename`/`unlink`, which the static link binds over libc, translating
  mount-point paths and forwarding the rest via `dlsym(RTLD_NEXT)`. Interposition
  caveat: [`docs/gotchas.md`](docs/gotchas.md).

The app wraps the mount at `/sdcard` behind `mount_sd_card()`/`unmount_sd_card()`
(`NameCardKnot.hpp`); `FileBrowserScreen` is the consumer.

## UI framework — LVGL port abstraction + UI utilities

`ui_framework/` is deliberately thin and panel-agnostic (no display, no touch, no
EPD) so esp-devkit stays reusable across boards. It ships two things:

**1. The LVGL port, abstracted by mirroring `esp_lvgl_port`'s API.** App code calls
`lvgl_port_init(&cfg)` identically on both targets:
- **device:** `lvgl.hpp` includes the real `<esp_lvgl_port.h>` (a managed dep
  declared in `ui_framework/idf_component.yml`), which owns the LVGL task + tick.
  LVGL config comes from sdkconfig (Kconfig `CONFIG_LV_*`).
- **simulator:** `lvgl.cpp` provides a tiny shim (`#ifndef ESP_PLATFORM`) with the
  same `lvgl_port_cfg_t` + `lvgl_port_init()` (= `lv_init()` + an SDL tick/delay)
  plus `lvgl_sim_loop(tick)` — the host present loop that pumps input, runs
  `lv_timer_handler()`, presents, and calls back `tick(is_idle)` per frame
  (`simulator/main/main.cpp` passes `sim_harness_frame`). Upstream LVGL is fetched
  with `FetchContent` (`release/v9.5`) and configured by the in-tree
  `ui_framework/sim/lv_conf.h` (override the dir with the `UI_FRAMEWORK_LV_CONF_DIR`
  CMake cache var). Only the two LVGL major.minor versions need agree.

**2. Reusable, panel-agnostic UI building blocks:**
- **`lvgl.hpp`** (lvgl++): std::function wrappers — `lv_async_call`,
  `lv_obj_add_event_fn`. To touch the UI from another task/thread, either lock with
  `lv_lock()`/`lv_unlock()` (portable — `LV_USE_OS` is FreeRTOS on device, PThread on
  the sim) or marshal onto the LVGL context with `lv_async_call` (runs the closure
  next tick).
- **`ScreenManager`/`Screen`**: a `shared_ptr`-based navigation stack
  (`load`/`push`/`pop`/`top`) that swaps the LVGL theme per screen and defers a
  leaving screen's destruction via `retire()` → `lv_async_call` (so freeing
  `root_` never deletes the active screen mid-event-dispatch). No panel knowledge.
- **`widgets/layout.hpp`** (pulled in by `lvgl.hpp`): flex-layout helpers —
  `lv_container_create` (style-stripped, optionally flex/colored), `lv_spacer_create`
  (grow-able), and `lv_{hor,ver}_separator_create`.

### The BSP↔LVGL binding + EPD policy live in the app

`app/NameCardKnot.cpp` creates the `lv_display`/`lv_indev`, owns the buffer +
pixel format, and supplies the flush callback — written only against `bsp_*` +
`lvgl` (both target-transparent), so the **same file compiles for simulator and
device**. The flush_cb blits each area with `bsp_display_draw_bitmap`, accumulates
the dirty rect, and on the last partial flush calls `bsp_display_refresh(dirty,
mode)`. The EPD refresh mode is the app's own simple policy, exposed via
`NameCardKnot.hpp`: `epd_set_default_refresh_mode()` (the standing mode) and
`epd_set_next_refresh_mode()` (overrides the next refresh). A new board re-tailors
this small glue; esp-devkit itself stays panel-free. Threading and EPD timing
caveats: [`docs/gotchas.md`](docs/gotchas.md).

### Screens & resources (app-side)

`app/screens/` holds the `Screen` subclasses; each builds its tree in `build()`
and is loaded via the `screen_manager`. `app_entry()` loads the first screen
(`HomeScreen`) with `lv_async_call` (onto the LVGL context). Current screens:
`HomeScreen` (entry menu), `FileBrowserScreen`, `NameCardScreen`.
`FileBrowserScreen` is a `NavigationScreen` that lists the mounted SD directory
via POSIX `readdir` — folders first, case-insensitive sort, paged 10 rows/screen,
descending into subdirs via an internal path stack (so `back()` pops the stack
before leaving the screen). Tapping an image opens a **progress modal** and starts
an **async decode** (`imgproc::decode_file_async`, not LVGL's built-in decoders) at
the display resolution; an `lv_timer` polls the `DecodeJob`, drives the bar
(throttled — each EPD refresh is costly), and on completion pushes
`NameCardScreen` with the decoded `imgproc::Image`. The modal stays up until the
decode finishes or a cancel completes, so the browser owns the job for its whole
life. `NameCardScreen` just owns that `Image` and shows it 1:1 via `lv_image`
through `lv_image_adapter.hpp`.

`app/resources/` holds the UI assets, all `#include`-able C with no build step in
the repo: `converted/` is generated output (LVGL image converter for the `*_80px`
icons; lv_font_conv for `lucide_40` — see the `Opts:` header in each file for the
exact command), `lucide_font.h` maps Lucide glyph names to UTF-8 codepoints, and
the hand-written `resources.{c,h}` gathers them into the single `const struct
Resources R` that app code reads (`R.icon.*`, `R.font.*`). `Lucide_License.txt` is
the ISC license for the icon set. All of `app/` is GLOB'd into the build, so new
files are picked up after a cmake re-run (see `app/CMakeLists.txt`).

## Image processing — `components/image_processor`

Self-contained, **LVGL-free and dependency-free** library that turns an SD/buffer
JPEG or PNG into an EPD-ready buffer. One streaming pipeline (`run_pipeline`):
in-tree decoder (`RowSource`) → box downscale → color convert (linearize → Rec709
luma → gamma) → dither (Bayer / error-diffusion, 2 or 16 levels) → pack
(L8 high-nibble = EPD gray / I4 / I1). It streams row-by-row, never materializing
the full-resolution source, so large images no longer OOM silently — they return a
`Status`. The app uses the async `imgproc::decode_file_async` → `DecodeJob`, which
runs the decode across **both cores** — a producer task (the entropy decode, on the
caller's `task_priority`/`task_core`) feeds a consumer task (color+downscale+dither,
on the other core) through a source-row ring — polled for progress/cancel
(`decode_file`/`decode_buffer` are the synchronous, serial forms); the BSP↔LVGL-style binding
to `lv_image_dsc_t` lives in `app/lv_image_adapter.hpp`, keeping the component
panel-/UI-agnostic. Threading model: [`docs/image_processor.md`](docs/image_processor.md).

Both decoders are written in-tree (no zlib/libjpeg/lodepng): a pull-based
streaming `inflate` for PNG and a baseline-only JPEG decoder that downscales while
decoding (1/1..1/8). The SoC-free pipeline + decoders are host unit-tested via
`components/image_processor/test/run.sh` (g++, no ESP-IDF), with fixtures built by
`gen_fixtures.py` (PNG, stdlib zlib) and `gen_jpeg.c` (JPEG, libjpeg). Design
detail, option semantics, and trade-offs: [`docs/image_processor.md`](docs/image_processor.md).

## Name-card container PDF — `editor/` (writer) + `components/namecard_pdf` (reader)

The editor authors a **dual-purpose PDF**: a normal 2-page document (page 1 = the
540×960 display image, page 2 = a 1920×1080 share confirmation face) that *also*
carries machine-readable data for the device. Data (name/url/message UTF-8,
display/share JPEGs, an optional `name_glyphs` glyph-supplement blob) is embedded
as ordinary PDF objects; an appended **NCK footer** is a `{type,offset,length,crc}`
index + a fixed 24-byte trailer, so the device reads the last bytes, seeks, and
pulls payloads **without parsing PDF syntax**. The file is laid out as `(A)` a
complete share-only PDF + footer A, then `(B)` an incremental update adding the
display image + footer B — so the device makes a **share-only PDF by truncating at
`base_total_length`** (byte copy, no re-serialization). The TS writer is the source
of truth; the C++ parser is held to **byte-identical golden fixtures** (TS
`gen-fixtures` → `manifest.h`). Both are **decoupled from WebApp/LVGL and not yet
wired into the app builds** (integration — sim/device build entries, the
`name_glyphs`→`lv_font` adapter, canvas glyph rasterization — is a later task).
Format spec, byte layouts, file locations, and test commands:
[`docs/namecard_pdf.md`](docs/namecard_pdf.md).

## Verification & gotchas

- **Simulator UI verification** (sim harness, script commands, the DI model):
  [`docs/testing.md`](docs/testing.md).
- **Image-processor host unit tests** (decode/pipeline, fixtures):
  [`docs/testing.md`](docs/testing.md).
- **Gotchas** (build/env, EPD, threading, image decode): [`docs/gotchas.md`](docs/gotchas.md).
