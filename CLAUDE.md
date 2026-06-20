# NameCardKnot — Notes for Claude

Firmware for an M5PaperS3-class name-card device: an **ESP32-S3 + 960×540
grayscale EPD** (`paper_s3` board). Built on the reusable **`esp-devkit`** BSP /
simulator infrastructure, with a host SDL simulator so UI/app logic can be
developed and verified without hardware. Early stage — the BSP display + simulator
+ sim harness are in place; device drivers and the app itself are mostly stubs.

> **Keep the docs current.** When you change the build flow, the BSP surface
> (`bsp_*`), the simulator backend, add a board/target, or hit a non-obvious
> gotcha, update CLAUDE.md (and the right `docs/` file) in the same change — they
> are the handoff to the next session. Keep CLAUDE.md lean: deep verification
> detail lives in [`docs/testing.md`](docs/testing.md) and gotchas in
> [`docs/gotchas.md`](docs/gotchas.md) — link, don't inline.

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
./run.sh esp32s3                                  # idf.py -C esp32s3 flash monitor (needs a TTY)
```

(Call as `nix develop -c ./run.sh ...` or from a dev shell.)

## Repo layout

```
app/                  # SHARED app logic (NameCardKnot.cpp: app_entry) — currently a stub
simulator/            # SIMULATOR build root
  main/main.cpp       #   host entry: app_entry() + the SDL present loop + sim-harness frame stepping
  verify/             #   sim-harness scripts; captures land in verify/out/ (gitignored)
esp32s3/              # DEVICE build root (ESP-IDF project; build artifacts gitignored)
esp-devkit/           # SUBMODULE — reusable devkit (separate repo)
  bsp/                #   board support (bsp_*)
    inc/              #     public API: bsp.h, bsp_types.h
    inc_private/      #     internal vtables: bsp_display.h, bsp_touch.h
    src/              #     shared dispatch: bsp_display.c, bsp_touch.c
    devices/          #     DEVICE chip drivers (gt911 — stub)
    simulator/        #     SIM SDL backend: sdl_panel.{c,h}
    boards/paper_s3/  #     per-board bring-up: paper_s3.c (device, stub) + paper_s3_sim.c (sim)
  idf_compat/         #   SIM-only ESP-IDF compat (esp_*, pthread-backed FreeRTOS)
  sim_harness/        #   SIM-only scripted UI verification core (portable, DI) → docs/testing.md
```

Rule of thumb: shared/reusable → `esp-devkit/` (the submodule); app-specific →
`app/`; target-only entry → that target's build root.

### Component wiring

Components are self-describing via `idf_component_register()`. On **device**,
ESP-IDF consumes it directly. On the **simulator**, `simulator/CMakeLists.txt`
defines a shim that folds each component's `SRCS`/`INCLUDE_DIRS` straight into the
one `simulator` executable (so includes are effectively global; `REQUIRES` are
ignored). A component that differs per target branches on `ESP_PLATFORM` (set only
under ESP-IDF) inside its own `CMakeLists.txt` — see `bsp/CMakeLists.txt`. New
simulator component → add it to `SIMULATOR_COMPONENTS` in `simulator/CMakeLists.txt`.

## BSP — the display/touch seam (`bsp_*`)

`app/` calls `bsp_*` on both targets; the per-target split lives inside the BSP.
Drivers are struct-inheritance vtables: a provider embeds `bsp_display_t` /
`bsp_touch_t` as its first member and is registered with
`bsp_display_set_active()` / `bsp_touch_set_active()` by the board's `bsp_init()`.
`src/bsp_display.c` / `src/bsp_touch.c` implement the public API once by
dispatching through the active vtable.

### Display vtable (`inc_private/bsp_display.h`)

- **Always non-NULL:** `draw_bitmap` (blit a rectangle), `deinit`.
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

`sdl_panel_create(config, &display, &touch)` returns both providers (touch
nullable) and **self-registers** its input + capture callbacks with the sim
harness, so the simulator entry needs no wiring for those.

## Verification & gotchas

- **Simulator UI verification** (sim harness, script commands, the DI model):
  [`docs/testing.md`](docs/testing.md).
- **Gotchas** (build/env, EPD, threading): [`docs/gotchas.md`](docs/gotchas.md).
