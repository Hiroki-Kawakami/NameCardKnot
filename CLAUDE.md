# NameCardKnot — Notes for Claude

Firmware for an M5Paper-class name-card device on a **960×540 grayscale EPD**.
Two boards are supported, selected per build target (no runtime detection):
`paper_s3` (**ESP32-S3 + ED047TC1** i80 EPD, build root `esp32s3/`) and `paper`
(**ESP32 + IT8951E** SPI EPD, build root `esp32/`). Built on the reusable
**`esp-devkit`** BSP / simulator infrastructure, with a host SDL simulator so
UI/app logic can be developed and verified without hardware. The BSP display +
simulator + sim harness are in place, and the **device EPD + touch paths are
implemented**: on `paper_s3` the ED047TC1 panel over the ESP32-S3 i80 bus is a
per-pixel waveform engine (each pixel replays its own from×to waveform on an
async background task; in-flight waveforms are never interrupted — conflicting
draws block), with a `BSP_EPD_MODE_ALL` flag for ghost-clear area flushes and a
separate `bsp_display_clear()`; on `paper` the IT8951E TCON is driven over SPI
(synchronous load-image + display, no async task). Both use the in-tree `gt911`
I2C touch driver: the BSP's shared dispatch task polls it and **pushes** each
sample to `bsp_touch_set_event_cb` so taps survive a starved UI core. The app
sets that task through `bsp_config.dispatch` and caches/latches the pushed coords
itself (see [`docs/gotchas.md`](docs/gotchas.md)). **LVGL is
wired up** via the `ui_framework` LVGL port abstraction + an app-side BSP↔LVGL
binding, and **SD-card access is in** (`bsp_sd_*`: FAT-over-SDSPI on device, a
host-directory redirect in the simulator). The name-card image path uses
**`components/image_processor`** (JPEG/PNG decode → grayscale → dither →
EPD-ready L8), now **pure orchestration over the reusable C `image_framework`**
library (`esp-devkit/libs/image_framework`) that holds the actual primitives. The
app is a small screen set (home, SD file browser, name card).

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

The ESP-IDF (v6.0.2) toolchain and the host-simulator tools (cmake, ninja, gcc,
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
  image_processor/    #   JPEG/PNG decode -> grayscale/dither -> L8/I4/I1; orchestration over image_framework → docs/image_processor.md
    inc/              #     public API: image_processor.hpp (Options/Image/Status/decode_*)
    src/              #     pipeline (maps Options→imgf opts) + orchestration + async DecodeJob; no decoders/dither/pack of its own
    test/             #     host unit tests (run.sh links the imgf C sources, g++)
  namecard_pdf/       #   parse the editor's container PDF (NCK footer index) + write share-only PDF → docs/namecard_pdf.md
    inc/src/          #     public API: namecard_pdf.hpp (parse_buffer/open_file/read_asset/write_share_pdf/parse_name_glyphs); LVGL-free, dep-free
    test/             #     host tests (run.sh, run_e2e.sh→image_processor) against TS golden fixtures/ (manifest.h)
editor/               # TypeScript/React/Vite SPA that AUTHORS the container PDF (no backend)
  src/lib/namecard-pdf/ # framework-free PDF writer: buildNameCardPdf/buildSharePdf + footer/rle/glyphs + gen-fixtures (vitest) → docs/namecard_pdf.md
simulator/            # SIMULATOR build root
  main/main.cpp       #   host entry: app_entry() then lvgl_sim_loop() (LVGL present loop + sim-harness frame stepping)
  verify/             #   sim-harness scripts; captures land in verify/out/ (gitignored)
esp32s3/              # DEVICE build root for the paper_s3 board (ESP-IDF; build artifacts gitignored)
esp32/                # DEVICE build root for the paper board (selected in sdkconfig.defaults)
esp-devkit/           # SUBMODULE — reusable devkit (separate repo)
  bsp/                #   board support (bsp_*)
    inc/              #     public API: bsp.h, bsp_types.h
    inc_private/      #     internal vtables: bsp_display.h, bsp_touch.h
    src/              #     shared vtable dispatch + touch/button/audio dispatch task
    devices/          #     DEVICE chip drivers: ed047tc1 (i80 EPD descriptor) + it8951e (SPI EPD: driver + it8951e_epd bsp_display provider) + gt911 (I2C touch) + bm8563 (I2C RTC)
    driver/epd/       #     ESP32-S3 low-level: epd_ll.c (i80 bus + CKV/SPV/LE scan + the refresh engine) + epd_waveform.h (SoC-free per-pixel core, host-tested) + epd_waveform_lut.h (LUT authoring macros) + test/
    driver/pwm_buzzer/#     generic LEDC passive-buzzer bsp_audio provider (TONE-only)
    simulator/        #     SIM SDL backend: sdl_panel.{c,h} + sdl_audio.{c,h}
    test/             #     host tests: audio dispatch / DSP / SDL provider → docs/testing.md
    boards/<board>/   #     per-board bring-up + board.cmake (source/requirement lists); paper_s3 (ED047TC1) and paper (IT8951E, shared EPD/SD SPI bus, paper_config.h)
  idf_compat/         #   SIM-only ESP-IDF compat (esp_*, pthread-backed FreeRTOS)
  sim_harness/        #   SIM-only scripted UI verification core (portable, DI) → docs/testing.md
  ui_framework/       #   LVGL port abstraction (panel-agnostic) + reusable UI utils
    inc/              #     lvgl.hpp (lvgl++ helpers + lvgl_port shim decl) + screen{,_manager}.hpp + widgets/ (layout.hpp)
    src/              #     lvgl.cpp (sim lvgl_port_init/lvgl_sim_loop shim), screen_manager.cpp
    sim/lv_conf.h     #     default LVGL config for the simulator build (overridable)
  libs/image_framework/ # reusable dep-free C image library (imgf_*): decode/resize/recolor/dither/encode/async/alloc/stream/sniff
    inc/src/          #     public API: imgf_*.h; in-tree baseline-JPEG/PNG decoders, JPEG encoder, box/bilinear resizer, recolor (luma/gamma)
    test/             #     host unit tests (run.sh, gcc C11)
```

Rule of thumb: panel/board-reusable → `esp-devkit/` (the submodule); app-specific
but reusable across screens → `components/` (in this repo); app glue → `app/`;
target-only entry → that target's build root.

### Component wiring

Components are self-describing via `idf_component_register()`. Both build paths
include `esp-devkit/devkit.cmake`; keep the top-level `project()` literal after
the appropriate init macro because CMake pre-scans it when selecting a toolchain.

On **device**, each build root calls
`devkit_idf_init(UI_FRAMEWORK COMPONENT_DIRS ../components ../app)`. The macro
registers the devkit components and the app component container in
`EXTRA_COMPONENT_DIRS`, then trims the build to `main`'s dependency graph. Board
selection is Kconfig-backed: `esp32/sdkconfig.defaults` sets
`CONFIG_BSP_BOARD_PAPER`, while `esp32s3/sdkconfig.defaults` sets
`CONFIG_BSP_BOARD_PAPER_S3`. Any component that includes `bsp.h` must still name
`bsp` in its `REQUIRES`; each target's `main` names `app` in `PRIV_REQUIRES` so
the trimmed dependency graph reaches the application and its transitive deps.

On the **simulator**, `devkit_simulator_init()` runs before the literal
`project()`, then `devkit_simulator(...)` creates the executable, supplies the
shared `idf_component_register()` shim, and adds all devkit components. App-local
components remain explicit `COMPONENT_DIRS` arguments because `components/` is a
container without its own `CMakeLists.txt`; add a new simulator component there.
The shim folds sources/includes into one executable and ignores IDF-only
`REQUIRES`. `ui_framework` still `FetchContent`s LVGL and links it directly.

The bsp component gets its per-target source list, include dirs, requirements,
and `BOARD_TARGET` from `boards/<board>/board.cmake`. The early IDF configure pass
uses the union of requirements for boards matching `IDF_TARGET`; the real pass
resolves the Kconfig-selected board. Adding a board requires its directory,
`board.cmake`, Kconfig entry, and a target's sdkconfig selection. Per board:
- **paper_s3** (ED047TC1 i80 EPD): `esp_lcd` plus the EPD driver; the generic
  `sd_spi` provider manages its dedicated SPI2 bus.
- **paper** (IT8951E SPI EPD): no `esp_lcd`; the generic `sd_spi` provider borrows
  the SPI bus owned by `paper.c`, shared with the TCON.

`image_processor` names `image_framework` in `PRIV_REQUIRES`, plus `esp_timer`
for profiling and `freertos` for async decode. The simulator gets FreeRTOS from
`idf_compat`/pthreads; the host unit tests build the async path off.

Touch on both is the in-tree `gt911` driver (no managed dependency) — it adds the
`gt911` dir to `PRIV_INCLUDE_DIRS` and relies on `driver` for `i2c_master`/`gpio`.
The board file (`<board>.c`) owns the shared I2C bus (like the SPI bus): it brings
the bus up once, hands the handle to `*_panel.c` for the touch provider, and
creates the in-tree `bm8563` RTC (`devices/bm8563`, also in `PRIV_INCLUDE_DIRS`)
on the same bus. Both SD paths add `fatfs`/`sdmmc`/`esp_driver_sdmmc`/
`esp_driver_sdspi` to `PRIV_REQUIRES`; the simulator folds `simulator/sd_redirect.c`
+ `simulator/rtc_sim.c` instead.

## BSP — the display/touch seam (`bsp_*`)

`app/` calls `bsp_*` on both targets; the per-target split lives inside the BSP.
Drivers are struct-inheritance vtables: a provider embeds `bsp_display_t` /
`bsp_touch_t` / `bsp_rtc_t` as its first member and is registered with
`bsp_display_set_active()` / `bsp_touch_set_active()` / `bsp_rtc_set_active()` by
the board's `bsp_init()`. `src/bsp_display.c` / `src/bsp_touch.c` / `src/bsp_rtc.c`
implement the public API once by dispatching through the active vtable.

### Display vtable (`inc_private/bsp_display.h`)

- **Always non-NULL:** `draw_bitmap` (blit a rectangle; its `bsp_rotation_t` arg
  un-rotates the source into the panel-coordinate rect, fused into the copy via the
  shared `bsp_blit_rotated` helper), `deinit`.
- **Optional (NULL when absent):** `get_framebuffers` + `flush` (host-framebuffer
  fast path — MIPI), `set_brightness` (backlight), and `set_epd_mode` + `refresh` +
  `wait_idle` (EPD only). The public `bsp_display_*` wrappers no-op when the op is
  NULL, so the API is uniform across panel types.

### Panel types & the EPD model

`bsp_display_type_t`: `SPI`, `MIPI_DSI`, `SPI_EPD`, `DIRECT_EPD`. EPD refresh
(`bsp_epd_mode_t` = `NONE`/`FAST`/`QUALITY`, OR-able `BSP_EPD_MODE_ALL`):

- `set_epd_mode(mode)` sets a **persistent** mode. `draw_bitmap` updates GRAM but
  only paints the panel when the mode is **not** `NONE`.
- `refresh(area, mode)` paints the latest GRAM **of `area`** with a one-shot mode
  (does not change the persistent mode). `BSP_EPD_MODE_ALL` drives every pixel of
  the area, diff or not — the ghost clear.
- `clear()` (`bsp_display_clear`) blanks the whole panel to white — the
  known-baseline reset (not a refresh mode).
- `BSP_EPD_MODE_SEED`: a draw in this mode adopts the pixels as already on glass
  (no drive) — restores diff-tracking after a power cycle. IT8951E can only
  update GRAM (its TCON-internal previous image is not seedable).
- `bsp_display_wait_idle()` blocks until in-flight updates retire — the gate
  before cutting power.
- Bring-up does **not** clear the panel; the app establishes the baseline via
  the first screen's own full (`QUALITY_ALL`) refresh — a `clean_resume` boot
  even skips that (adopts the card already on glass). `bsp_display_clear()`
  remains available for a known-baseline reset (e.g. `GrayscaleTestScreen`).

(Consequence: a draw alone shows nothing on EPD — see [`docs/gotchas.md`](docs/gotchas.md).)

Below this seam the device driver (`driver/epd/epd_ll.c`) is a **per-pixel
waveform engine**: one uint16 per pixel (confirmed gray, target gray, waveform
id, start frame), the diff-skip done at draw time, every pixel replaying its own
from×to×step waveform on the async task (no generation/slot limit). In-flight
waveforms are **never interrupted** (DC-offset protection): a conflicting draw /
ALL-refresh / clear **blocks the caller** until the pixels retire, with the
skippable settle tails (`SETTLE` LUT frames) cut short to release it sooner —
the clear waveform's `STOP` tail always runs, so clear-then-draw can't truncate
the clear. Panel LUTs are `lut[steps][16]` authored with the
`driver/epd/epd_waveform_lut.h` macros (`F` = by current gray, `T` = by target
gray, ≤62 steps). The SoC-free core (`driver/epd/epd_waveform.h`) is host
unit-tested via `esp-devkit/bsp/driver/epd/test/run.sh`. Details + blocking
trade-offs: [`docs/gotchas.md`](docs/gotchas.md). The simulator's `sdl_panel`
EPD path replays no waveform, so none of the engine is observable there —
verify on hardware.

### Simulator backend (`bsp/simulator/sdl_panel.c`)

One SDL backend mimics all three panel families behind the same vtable, chosen by
`sdl_panel_config_t::type`:

- **MIPI:** host framebuffers; `flush(idx)` repoints what is presented (no copy).
- **SPI:** `draw_bitmap` blits straight to the on-glass buffer, shown immediately.
- **EPD:** `draw_bitmap` writes a GRAM buffer; the glass updates only on a
  non-NONE mode or `refresh`.

Presentation is deferred to the main thread (SDL/Cocoa is main-thread-only);
`sdl_panel_present()` does the actual render each loop iteration. Touch is a
mutex-guarded multi-touch snapshot: the main thread samples the mouse in
`sdl_panel_pump_input()`, and the BSP dispatch task polls that snapshot. See
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

`bsp_sd_mount`/`bsp_sd_unmount`/`bsp_sd_is_mounted` are the public seam. Device
boards register a `bsp_sd_t` provider; the simulator implements the same calls as
its host-directory redirect.

- **device** (`driver/sd_spi/`): FAT over SDSPI. `paper_s3` creates the provider
  with a managed dedicated SPI2 bus; `paper` creates it with a borrowed bus owned
  by `paper.c` and shared with IT8951E. `config` NULL/zero fields take provider
  defaults (`max_files` 0→5, `max_freq_khz` 0→20MHz).
- **simulator** (`simulator/sd_redirect.c`): "mounting" just maps the mount point
  onto a host directory (`SIMULATOR_SDCARD_PATH`, default `simulator/sdcard`).
  App code keeps using plain POSIX I/O — the file defines `open`/`fopen`/`opendir`/
  `stat`/`rename`/`unlink`, which the static link binds over libc, translating
  mount-point paths and forwarding the rest via `dlsym(RTLD_NEXT)`. Interposition
  caveat: [`docs/gotchas.md`](docs/gotchas.md).

The app wraps the mount at `/sdcard` behind `mount_sd_card()`/`unmount_sd_card()`
(`NameCardKnot.hpp`); `FileBrowserScreen` is the consumer.

### RTC (`bsp_rtc_*`)

External I2C RTC behind the `bsp_rtc_t` vtable (`inc_private/bsp_rtc.h`):
`get_time`/`set_time`/`time_is_valid`/`deinit` always present; the countdown timer
(`timer_start(seconds, repeat)`/`timer_stop`/`timer_is_expired`/`timer_clear`) and
`set_int_cb` are optional (NULL → `ESP_ERR_NOT_SUPPORTED`); with no provider the
calls return `ESP_ERR_INVALID_STATE`. `bsp_rtc_datetime_t` is a plain calendar
struct (no `<time.h>` in the BSP). The one-shot/interval timer maps onto the
chip's countdown block; `repeat` distinguishes auto-reload from one-shot (the
driver's INT handler stops a one-shot on the expiry it services).

- **device** (`devices/bm8563/`, both boards): BM8563 on the same I2C bus as the
  GT911 touch — the board file (`<board>.c`) brings the bus up and creates the RTC
  (non-fatal, like touch), `*_panel.c` only consumes the bus for touch. `int_io`
  is `GPIO_NUM_NC` — the INT pin gates the M5Paper power
  rail, not a readable GPIO, so `set_int_cb` is unavailable but the timer's INT
  still asserts in hardware (use it to wake from sleep, then `timer_is_expired` +
  `timer_stop` on boot). RX8130 (`devices/rx8130/`) is the future second chip; the
  seam is chip-agnostic so a new board just calls its `*_rtc_create()`.
- **simulator** (`simulator/rtc_sim.c`): `get_time` reads the host clock offset by
  the last `set_time` (which recomputes the offset); starts valid, or invalid if
  `SIMULATOR_RTC_INVALID` is set (non-`"0"`) — see [`docs/testing.md`](docs/testing.md).
  Timer ops absent. Registered by the board's `*_sim.c`.

SoC↔chip time sync (SNTP, restoring system time after VSYS drop) is **app policy,
not BSP** — the BSP exposes the chip mechanism (`time_is_valid` included) and the
app drives the direction/timing.

### HotKnot (`bsp_hotknot_*`)

Proximity peer-to-peer over the GT911 panel, behind the same vtable pattern
(`bsp_hotknot_t` in `inc_private/bsp_hotknot.h`, dispatch in `src/bsp_hotknot.c`,
no provider → `ESP_ERR_NOT_SUPPORTED`). The only provider is GT911:
`gt911_hotknot_create()` binds to the active touch chip (`gt911_active_handle`,
file-static) and the board registers it after touch. The callback-based public
API (`bsp_hotknot_begin(role, cb)` / `send` / `end`, bsp.h) hides FW load, ACK,
and chip status — the session state machine runs **on the shared BSP dispatch
task** via an installable session step (`gt911_internal.h`), so touch + HotKnot
share one I2C owner/lock/task. Pairing,
ready, and received frames arrive as events. Board-specific SNR tuning lives in
`gt911_config_t.hotknot` (set per board in `*_panel.c`), not driver macros. Touch
recovery after a session is **capability-driven**: it needs a wired RESET +
output-capable INT, which Paper/PaperS3 lack (RESET unwired → power cycle), so
that is a board constraint, not a HotKnot assumption — see
[`docs/gotchas.md`](docs/gotchas.md).

### Audio (`bsp_audio_*`)

Capability-based (`bsp_audio_get_caps`: `PCM`/`TONE`/`SPEAKER`/`HEADPHONE`)
behind the same vtable pattern (`bsp_audio_t` in `inc_private/bsp_audio.h`,
dispatch in `src/bsp_audio.c`; calls outside the caps →
`ESP_ERR_NOT_SUPPORTED`). The dispatch owns all policy: the volume curve
(linear-in-dB, a fading software gain through the `audio_dsp` chain,
`inc/audio_dsp.h`), the speaker route (ON/AUTO/OFF + dispatch-driven headphone
polling),
click-free open/close sequencing, and the **tone fallback** — `bsp_audio_tone`
works with `CAP_TONE` (hardware buzzer) *or* `CAP_PCM` (sine synthesized by a
lazily-created task; `ESP_ERR_INVALID_STATE` while an app PCM stream is open).
`bsp_audio_open`/`close` are idempotent; open on a running stream with a new
format reconfigures — there is no separate reconfig API, the provider `open()`
op is defined as callable while running. Volume/mute are `CAP_PCM`-gated;
buzzer loudness is fixed. `bsp_config.audio` selects `dsp_mode`/`speaker_mode`.

- **paper_s3 device**: `driver/pwm_buzzer/` (generic LEDC provider — fixed 50%
  duty, pin idles low for the AC-coupled gate drive) on GPIO21; `bsp_power_restart`/
  `bsp_power_off` call `bsp_audio_quiesce()`. The `paper` board has no audio.
- **simulator**: `simulator/sdl_audio.c` mirrors the board's caps — the
  paper_s3 sim registers `tone_only` (square-wave synth, buzzer timbre); PCM
  mode (for speaker boards) exercises the full DSP/route path with SDL
  queue-audio backpressure, silent-but-paced under `SIMULATOR_HEADLESS`.
- Host tests (`esp-devkit/bsp/test/run.sh`): [`docs/testing.md`](docs/testing.md).

### Power (`bsp_power_*`)

Two unrelated things under one prefix. **Controls** (always available, board free
functions — not a vtable): `bsp_power_off` (cut VSYS; `ESP_FAIL` when external
power holds the rail up), `bsp_power_restart` (soft `esp_restart`),
`bsp_power_hw_reset` (hardware power-cycle, e.g. via the RTC countdown).
**Sensing** is capability-based behind the usual vtable (`bsp_power_t` in
`inc_private/bsp_power.h`, dispatch in `src/bsp_power.c`, provider registered by
`bsp_power_set_active`; no provider → caps 0, calls → `ESP_ERR_NOT_SUPPORTED` /
`vbus_present` false): `bsp_power_get_caps` (`BATTERY`/`VBUS`),
`bsp_power_get_battery_voltage` (mV, the ground truth), `bsp_power_get_battery_level`
(linear **0..100** map of the terminal voltage between the board's empty/full
endpoints — a coarse gauge, **not** a true SoC, since these boards have no fuel
gauge; % / voltage→level curve stays app policy above this), and
`bsp_power_vbus_present`.

- **simulator** (`simulator/power_sim.c`): fake battery/VBUS so the seam is
  exercisable — level from `SIMULATOR_BATTERY_PERCENT` (default 76), voltage
  derived over a 1S Li-ion range, VBUS present unless `SIMULATOR_VBUS=0`.
- **device** (`driver/adc_battery/`): generic provider over `adc_oneshot` +
  `adc_cali` — reads the battery on an ADC pin through a resistor divider (no
  fuel-gauge IC); config = ADC channel/atten + divider + empty/full mV +
  optional VBUS-sense GPIO. **paper** (GPIO35/ADC1_CH7, no VBUS) and **paper_s3**
  (GPIO3/ADC1_CH2 off MPWR_EN, +VBUS from USB_DET on GPIO5) both wired: ×2
  22K/22K divider, 3300–4200 mV.

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
- **`widgets/fonts.hpp`**: an app-injectable font seam for `widgets/navigation.cpp`
  + `widgets/modal.cpp` (title/body) — `lv_widgets_set_fonts(title, body)`, falling
  back to `&lv_font_montserrat_32`/`_24` until called. Keeps esp-devkit font-free;
  the app wires its own chains in (see Strings/UiFont below).

### The BSP↔LVGL binding + EPD policy live in the app

`app/NameCardKnot.cpp` creates the `lv_display`/`lv_indev`, owns the buffer +
pixel format, and supplies the flush callback — written only against `bsp_*` +
`lvgl` (both target-transparent), so the **same file compiles for simulator and
device**. The flush_cb blits each area with `bsp_display_draw_bitmap`, accumulates
the dirty rect, and on the last partial flush calls `bsp_display_refresh(dirty,
mode)`. The EPD refresh mode is the app's own simple policy, exposed via
`NameCardKnot.hpp`: `epd_set_default_refresh_mode()` (the standing mode) and
`epd_set_next_refresh_mode()` (overrides the next refresh; an explicit `NONE`
suppresses that flush's refresh and drops its dirty rect — the clean-resume
first paint). A new board re-tailors
this small glue; esp-devkit itself stays panel-free. Threading and EPD timing
caveats: [`docs/gotchas.md`](docs/gotchas.md).

### Screens & resources (app-side)

`app/screens/` holds the `Screen` subclasses; each builds its tree in `build()`
and is loaded via the `screen_manager`. `app_entry()` picks the first screen
with `lv_async_call` (onto the LVGL context): the very first gate is the
language, ahead of everything below — if NVS `settings::language()` is unset
(first boot), it clears the panel and loads `LanguageSelectScreen(Mode::Boot)`;
tapping a language calls `strings::set()` and runs the same continuation the
rest of this gate would have run directly. A pending `app/BootMessage`
record takes priority over everything below — it
loads `BootMessageScreen` as a modal without clearing the EPD first (only the
modal rect is driven), and OK returns to the resumed card screen or Home.
Otherwise, if `app/LastCard` records a card being displayed — My Card or an SD path, saved by
`NameCardScreen::onAppear` and cleared when the user leaves it — it reopens
`NameCardScreen` directly (power-off resume, no Home in between); otherwise it
loads `DateTimeScreen` (`Nav::Boot`) if `bsp_rtc_time_is_valid` comes back
`ESP_OK` and false (RTC never set), else `HomeScreen` (an RTC-read error also
falls through to `HomeScreen` — no provider shouldn't force the date screen).
An SD card restores from the lastcard flash cache when NVS `cpath`
matches (`NameCardData::load_lastcard`: MMIO display blob, else a sync decode of
the cached PDF — no SD needed), falling back to the SD file. The NVS `clean`
flag (`lastcard::clean_resume`) records "the glass shows a fully-rendered card
(no modal)": `NameCardScreen` sets it while showing the card and clears it on
leave / when a modal opens. Bring-up neither clears nor seeds the panel; on a
`clean_resume` boot the resume **adopts** the card the glass still shows — the
first paint refreshes nothing (explicit `NONE`) and only the always-open menu's
rect is driven (`refreshMenu`), so a wake never re-flashes the card. Otherwise
`NameCardScreen::onAppear` drives a one-shot `QUALITY_ALL` that repaints the
whole card from scratch (no `bsp_display_clear()`). Closing the menu likewise
does a full-screen clean refresh (`clearDisplay` → white → `QUALITY_ALL` repaint
of the card), so ghosts never accumulate — hence seeding is no longer needed and
the resume can paint fast without regard to prior on-glass content. Current
screens:
`HomeScreen` (entry menu), `FileBrowserScreen`, `NameCardScreen`, `SettingsScreen`,
`DateTimeScreen`, `GalleryScreen`, `SharedCardScreen`, `LanguageSelectScreen`,
`AcknowledgementsScreen`.
`HomeScreen` also shows the current date (`bsp_rtc_get_time` +
`bsp_rtc_time_is_valid`, top-left, refreshed in `build()`/`onAppear()`, blank when
invalid) and its Settings button pushes `SettingsScreen` (a `NavigationScreen`
with "Date & Time", "Languages", "Flip Screen", and "Acknowledgements" rows). The
Acknowledgements row pushes `AcknowledgementsScreen`, a `NavigationScreen` showing
the consolidated third-party license text (`app/screens/AcknowledgementsText.hpp`)
as one wrapped label clipped to a viewport, paged one viewport-height at a time by
prev/next buttons (no touch-drag scroll — EPD can't animate a fling). The Languages row
pushes `LanguageSelectScreen(Mode::Settings)`: tapping the already-active
language just pops back, tapping the other one calls `settings::set_language()`
and `bsp_power_restart()` (every already-built screen must reread `S()`/`ui_font_*()`,
so a restart is simpler than rebuilding the tree live). `DateTimeScreen` is a modal-on-white
Year/Month/Day/Hour/Minute stepper (`Nav::Boot` from the boot gate above, OK-only,
loads `HomeScreen`; `Nav::Back` from `SettingsScreen`, adds Cancel, pops) that
computes the weekday (Sakamoto's algorithm) and calls `bsp_rtc_set_time` on OK.
`FileBrowserScreen` is a `NavigationScreen` that lists the mounted SD directory
via POSIX `readdir` — folders first, case-insensitive sort, paged 10 rows/screen,
descending into subdirs via an internal path stack (so `back()` pops the stack
before leaving the screen). Tapping a file opens a **progress modal** and starts
an **async load** behind `FileLoader` (`app/FileLoader.hpp`) — the abstract
progress/state/cancel/label interface the browser polls. `NameCardData`
(`app/NameCardData.{hpp,cpp}`) is the concrete loader: it abstracts a plain image
*and* a `.mnc.pdf` (detects via `nckpdf::open_file`, then decodes either the whole
file or just the embedded display JPEG byte range with
`imgproc::decode_file_async(..., offset, length)`), and stays LVGL-free. The
browser holds one `FileLoader` plus a completion `std::function` callback, so a
new file type is just a new loader + callback at the call site, not new per-type
modal code. An `lv_timer` polls the loader, drives the bar (throttled — each EPD
refresh is costly), and on completion runs the callback, which pushes
`NameCardScreen` with the loaded `NameCardData`. A `.snc.pdf` instead opens
`SharedCardScreen` directly (sync metadata parse, no progress modal); any other
file type shows an error modal. `NameCardScreen` has two nav
modes: `Nav::Back` (pushed from the browser; the menu's bottom-left button pops
back) and `Nav::Home` (loaded from Home / boot resume; the button loads
`HomeScreen`) — so a resume boot never needs a screen under it. Its menu is a
**self-made bottom sheet** (no `lv_modal`/scrim): tapping the card toggles it.
Opening only invalidates the menu object (open = QUALITY, off the card pixels);
closing does a full-screen clean refresh (`clearDisplay` → white → `QUALITY_ALL`
repaint of the card, ghost-free) — except the Info button, which closes the menu
with just its own rect since the modal scrim dirties the full screen anyway. The
menu **always starts open** when the screen opens (from the browser, Home's My
Card, or a boot resume). It also accepts a still-Loading
`NameCardData` (boot SD resume) and polls it with an `lv_timer`, falling back to
Home on failure. `NameCardScreen` keeps the
`shared_ptr<NameCardData>` (so the buffer outlives the `lv_image`) and shows it 1:1
via `display_view()` — an `L8View` (`app/L8View.hpp`) that unifies an SD load's
decoded image and a cached card's mmap blob, blitted through `l8view_fill_lv_dsc`
(`app/lv_image_adapter.hpp`). Its `Info` modal shows the name/url + a QR
(`lv_qrcode`) of the url; the name font is a Montserrat → NotoSansJP → embedded
glyph supplement fallback chain built by `app/NameFont.hpp` (over the
`app/lv_glyph_font.hpp` adapter: `name_glyphs` blob → runtime `lv_font_t`, A8 mask
expansion; `NameCardData::name_glyphs()` parses/owns the blob).

`HomeScreen`'s Gallery button pushes `GalleryScreen`, a `NavigationScreen` listing
`RECEIVED_CARDS_DIR`'s `*.snc.pdf` by name (`CardNameLabel::load_file`) and received
time (file mtime — real once `rtc_sync_system_time()` has synced the system clock
from the RTC), newest first, 8 rows/page; tapping a row opens `SharedCardScreen`
(`Nav::Back`, pops back), the share-only viewer over `SharedCardData` — name, the
share image(s) 1:1, and conditional URL/QR, Message, and image-1/2 toggle buttons
per what the card provides. The receive flow closes the loop:
`TransferScreen::finalizeReceived` renames the incoming file into
`RECEIVED_CARDS_DIR` and calls `lastcard::save_received(path)` (a one-shot NVS
pending-open record); `TransferScreen` itself never shows the result — it saves
a `bootmsg` (`ShareFailed`/`ReceiveFailed`/`TransferFailed`, or `TransferComplete`
on success) and calls `bsp_power_hw_reset()` ("Finalizing" stays on glass through the
reset; touch is dead after a HotKnot session). At boot, `BootMessageScreen` for
`TransferComplete` consumes `lastcard::take_received()` itself and, when a card
is pending, offers Open Card (`SharedCardScreen(Nav::Received)`) / Back instead
of a single OK. `app_entry`'s own `lastcard::take_received()` fallback now only
fires after a failed-reset manual reboot (the `bootmsg` record was cleared there
but the pending-open one wasn't); a still-openable card loads
`SharedCardScreen(Nav::Received)` directly, whose back button calls
`make_resumed_card_screen()` (`NameCardKnot.hpp`, the boot resume logic factored
out of `app_entry`) to reopen the `NameCardScreen` still recorded in lastcard NVS
if any (e.g. Receive was opened from its menu), else `HomeScreen`. A HotKnot
failure past pairing has no tappable UI (touch is dead), so `HotKnotScreen::
failAndReboot` instead saves an `app/BootMessage` record and calls
`bsp_power_hw_reset()`; on `paper` with USB power a failed reset clears the record
again and loads `BootMessageScreen` (`Mode::ResetFailed`) directly with a
"press the reset button" hint — the message is already on glass, so the manual
reset boots normally.

**Card flash storage** (`app/CardStore`): `cardstore::Store` manages a dedicated
2MB partition as a custom blob index (PDF + L8 image caches), no filesystem. Two
instances: `cardstore::mycard()` (the imported My Card: PDF + display/preview/name)
and `cardstore::lastcard()` (the sleep-entry cache of the displayed card: PDF +
display). The header is read into RAM and the PDF copied out; only the image blob
being shown is mmap'd on demand (a `MappedImage` RAII handle, unmapped on leave) —
a permanent 2MB map starves the original ESP32's ~4MB DROM mmap window and hangs
paper. Power-fail safety: erase → blob writes → commit, where commit appends the
header to the next 128-byte slot of the header sector, so a Writer may commit
per blob and an interrupted write keeps everything committed so far readable.
`HomeScreen`'s My Card button uses the preview blob and opens via
`NameCardData::load_cached()`; the **Edit** button runs `FileBrowserScreen` in
`Mode::ImportMyCard` (lists `.mnc.pdf` only), whose `ImportJob` (a `FileLoader`)
decodes the caches and writes the partition. The name is drawn as a live
`lv_label` from the stored PDF via `app/CardNameLabel` (over the `NameFont`
chain), shared by Home and `SleepScreen`. Full spec:
[`docs/mycard.md`](docs/mycard.md).

**Idle power-off** is app policy in `app/Power.{hpp,cpp}`: a 1s LVGL watchdog
(started by `app_entry`) cuts VSYS via `bsp_power_off` after inactivity.
Timeouts are owner-declared in `onAppear` — `power::set_timeout(this, ms)` —
and revert to the 5min default once that screen stops being current
(NameCardScreen 60s, TransferScreen 0 = never). Sleep display: NameCardScreen
current with no menu/modal open → nothing drawn (the glass already shows the
card); menu/modal open → closed + full-screen `QUALITY_ALL` repaint of the bare
card (`clearDisplay`); NameCardScreen below the stack →
popped back to (QUALITY, so the unchanged card diff-skips); otherwise
`SleepScreen` (My Card preview + board-specific wake hint; tap → Home for the
USB-powered case). Then `bsp_display_wait_idle` (park the EPD before flash
writes) → `lastcard::save_cache` (cache the displayed card's PDF + L8 to the
lastcard partition; skipped for My Card or an unchanged card) → SD unmount →
`bsp_hotknot_end` → RTC timer stop → `bsp_power_off`; a failed power-off (USB
keeps VSYS up) re-arms the countdown. `clean_resume` is owned by `NameCardScreen`
(not touched here); the glass still shows the card, so it stays valid. The NVS
lastcard record is untouched, so wake
resumes to the right screen. The sim's `bsp_power_off` stays alive (ESP_FAIL,
like USB); `SIMULATOR_SLEEP_TIMEOUT_MS` shortens every timeout for scripted
verification.

`app/resources/` holds the UI assets, all `#include`-able C with no build step in
the repo: `converted/` is generated output (LVGL image converter for the `*_80px`
icons; lv_font_conv for `lucide_40` — see the `Opts:` header in each file for the
exact command), `lucide_font.h` maps Lucide glyph names to UTF-8 codepoints, and
the hand-written `resources.{c,h}` gathers them into the single `const struct
Resources R` that app code reads (`R.icon.*`, `R.font.*`). `Lucide_License.txt` is
the ISC license for the icon set. All of `app/` is GLOB'd into the build, so new
files are picked up after a cmake re-run (see `app/CMakeLists.txt`).

**i18n (Japanese/English)**: `app/Strings.{hpp,cpp}` is a flat, concept-named
`struct Strings` (`close`, `cancel`, `items_page_fmt`, ...) with an En and a Ja
table; screens read the active one through `S()` and switch it with
`strings::set(Lang)`. `app/UiFont.{hpp,cpp}` builds three Montserrat→NotoSansJP
fallback chains (`ui_font_24/32/48()`) that every screen uses in place of
`&lv_font_montserrat_*`; `ui_font_init()` also feeds the LVGL theme default
(`lv_theme_mono_init(..., ui_font_24())`) and the `ui_framework` widget-font seam
(`lv_widgets_set_fonts(ui_font_32(), ui_font_24())`), both called once from
`lvgl_init()`. `settings::language()`/`set_language()` (`Nvs.hpp`, NVS key
`lang`) persist the choice.

## Image processing — `components/image_processor` over `esp-devkit/libs/image_framework`

The actual image-processing primitives live in the reusable, **LVGL-free and
dependency-free C** library `esp-devkit/libs/image_framework` (`imgf_*`): in-tree
baseline-JPEG/PNG decoders (+ a JPEG encoder), a box/bilinear `imgf_resizer`,
`imgf_recolor` (linearize → Rec709/601/avg/custom luma → sRGB/power/EPD-LUT gamma +
invert), `imgf_dither` (Bayer / error diffusion), `imgf_raw_encoder` (L8/I4/I1),
the `imgf_async` two-task pipeline runner, and `imgf_alloc`/`imgf_stream`/
`imgf_sniff`. Each is host unit-tested in `libs/image_framework/test/run.sh` (gcc,
C11).

`components/image_processor` is now the **EPD-specific orchestration** on top:
`src/pipeline.cpp` maps `Options` onto the imgf module options and streams an
opened decoder through `imgf_recolor` (front) → `imgf_resizer` (Gray8 downscale) →
`imgf_recolor` (finalize) → `imgf_dither` → `imgf_raw_encoder`. The non-obvious bit
is that **recolor straddles the resizer** so the box average stays in linear light
(the resizer is geometry-only Gray8→Gray8). It still owns the public C++ API
(`imgproc::Options/Image/Status/decode_*`) and the async `decode_file_async` →
`DecodeJob` (producer = decode, consumer = color+dither, split via `imgf_async`);
the binding to `lv_image_dsc_t` lives in `app/lv_image_adapter.hpp`. Host tests
(`components/image_processor/test/run.sh`) link the imgf C sources and cover the
public API end-to-end. Design detail, the recolor straddle, and the option surface:
[`docs/image_processor.md`](docs/image_processor.md).

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
`gen-fixtures` → `manifest.h`). The reader is **wired into the app**: `app/` lists
`namecard_pdf` in `REQUIRES`, the simulator in
`devkit_simulator(COMPONENT_DIRS ...)`, and the browser opens `.mnc.pdf` via
`NameCardData` (see Screens above). Still LVGL-free
itself; the `name_glyphs`→`lv_font` adapter is now done app-side
(`app/lv_glyph_font.hpp`, used by `NameCardScreen`'s name font fallback chain).
Still to do — the editor's canvas glyph rasterization (rare-kanji names), and a
`.snc.pdf` viewer. Format spec, byte
layouts, file locations, and test commands:
[`docs/namecard_pdf.md`](docs/namecard_pdf.md).

The editor is published to GitHub Pages by
`.github/workflows/editor-pages.yml`: changes under `editor/` on `main` run
`npm ci`, the Vitest suite, and the production build, then deploy `editor/dist/`
through the Pages artifact workflow. The repository's Pages source must be set
to **GitHub Actions** before the first deployment.

## Verification & gotchas

- **Simulator UI verification** (sim harness, script commands, the DI model):
  [`docs/testing.md`](docs/testing.md).
- **Image-processor host unit tests** (decode/pipeline, fixtures):
  [`docs/testing.md`](docs/testing.md).
- **Container PDF tests** (TS vitest, `namecard_pdf` + `NameCardData` host tests,
  cross-language golden): [`docs/testing.md`](docs/testing.md).
- **Gotchas** (build/env, EPD, threading, image decode): [`docs/gotchas.md`](docs/gotchas.md).
