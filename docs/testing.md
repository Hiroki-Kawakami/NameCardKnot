# Testing — simulator UI verification

Scripted, headless UI verification runs the real `app/` + BSP on the host under
the **sim harness** (`esp-devkit/sim_harness/`), driving it from a text script and
capturing frames to JPEG. Deterministic, no host window, host-display-free.

## Running

```sh
nix develop -c ./run.sh simverify simulator/verify/<script>.txt
```

`run.sh simverify` builds, then runs with `SIMULATOR_HEADLESS=1` and
`SIMULATOR_SCRIPT=<script>`. Captures land where the script's `capture` lines
point — by convention `simulator/verify/out/` (gitignored; parent dirs are
auto-created). To eyeball a result, just read the JPEG. `simulator/verify/smoke.txt`
is the minimal example (settle → capture). Add a script per screen/flow as the UI
grows, mirroring `Tab5-ADB/simulator/verify/`.

Scripts that exercise SD see the host directory the redirect points at
(`SIMULATOR_SDCARD_PATH`, default `simulator/sdcard` — gitignored except a
`.gitkeep`, so stage fixtures there). See the SD card seam in the root `CLAUDE.md`.

NVS (idf_compat's JSON-file store) defaults to `nvs_data.json` in the cwd for
interactive runs; headless runs use `build/nvs_headless.json`, deleted at start,
so scripted runs never inherit state (a fresh run boots to Home, not a resumed
card). To test resume, point `SIMULATOR_NVS_PATH` at a prepared file — namespace
`lastcard`, key `src` `"01"` (My Card) or `"02"` + `path` (hex bytes of the
NUL-terminated SD path); `clean` `"01"` exercises the clean-resume path
(paint-nothing first flush, then menu-only refresh — the card is adopted, not
re-drawn). A sim run that sleeps produces `clean` itself
(it survives the failed `bsp_power_off`, like USB power on device), so a
sleep-run followed by a boot-run against the same NVS file covers the full
power cycle.

The idle power-off can be exercised with `SIMULATOR_SLEEP_TIMEOUT_MS` (overrides
every screen's timeout except disabled ones); the sim's `bsp_power_off` logs
`[sim] bsp_power_off: staying on` and returns ESP_FAIL, like a USB-powered
device, so the script can capture the sleep display afterwards.

The card flash stores are backed by `simulator/mycard.img` / `simulator/lastcard.img`
(`SIMULATOR_MYCARD_PATH` / `SIMULATOR_LASTCARD_PATH` override — point lastcard at a
temp file to test the sleep-entry cache and the SD-less restore).

The sim RTC starts valid (host clock); `SIMULATOR_RTC_INVALID=1` starts it
invalid instead, to exercise the boot-time Date & Time gate:
`SIMULATOR_RTC_INVALID=1 nix develop -c ./run.sh simverify simulator/verify/datetime.txt`.

The capture is written in the configured viewing orientation: it applies the SDL
host-view rotation, which headless can only get from the build-time
`SDL_PANEL_DEFAULT_ROTATION` / `SIM_DEFAULT_ROTATION` (the r/l keys never fire
without a window), so it is deterministic. With the default 90° this is the upright
540×960 portrait — the same way a person sees the rotated window — rather than the
panel-native 960×540 with the UI lying on its side. The capture still reflects the
app's own `lv_display_set_rotation`; only the host-view rotation is the addition.

## Script commands

```
wait <ms>                advance for <ms> of main-loop frames
settle [<max_ms>]        advance until idle for ~4 frames (default 5000)
capture <path>           write the current frame as JPEG (parent dirs auto-created)
tap <x> <y> [id]         press+release (id default 0)
down/move <x> <y> [id]   press-hold / move the held pointer
up [id]                  release
quit                     stop (implicit at EOF)
# comments and blank lines are ignored
<name> ...               run an app-registered command (see below)
```

Multi-touch: each `id` is an independent finger, so two `down` with different ids
hold two contacts. The interactive mouse uses id 0.

`tap`/`down`/`move` take **raw panel coordinates** (`paper_s3`: 960×540), *not* the
capture/viewing orientation. With the app's `lv_display_set_rotation(90)` the logical
screen is 540×960, so to hit a widget seen at logical `(lx, ly)` press
`panel_x = ly`, `panel_y = 540 − lx`. `simulator/verify/filebrowser.txt` is a worked
example (home → SD listing → descend into a subdir).

## How the harness is driven (frame handshake, not self-pumping)

The harness does **not** pump the UI. The simulator's single `while` loop
(`simulator/main/main.cpp`) is the only driver; the script interpreter runs on its
own thread and advances in lockstep with the loop via a pthread-cond frame counter:

- `sim_harness_start(getenv("SIMULATOR_SCRIPT"))` — spawn the interpreter
  (NULL/"" ⇒ interactive run, no-op).
- `sim_harness_frame(bool idle)` — called once per loop iteration (after present);
  releases one frame to the interpreter. `idle` = is the UI at rest this frame
  (used by `settle`). Returns **false** once the run finished → break the loop.
- `sim_harness_exit_code()` — process exit code after the run.

`main.cpp` runs the frame loop inside `lvgl_sim_loop()` (from `lvgl.hpp`), which
already computes `idle` as `lv_anim_count_running() == 0` and forwards it to the
`tick` callback — `main.cpp` just returns `sim_harness_frame(is_idle)` from there.

## Portability — capabilities are injected, commands are registered

The harness core (`sim_harness.c`) has **no SDL / LVGL / BSP / app coupling** — it
depends only on pthread + libc. Everything board- or app-specific is injected:

- `sim_harness_set_input_callback(down, up)` and
  `sim_harness_set_capture_callback(cap)` — **`sdl_panel` self-registers these** in
  `sdl_panel_create()`, so the simulator entry needs no wiring for them. Capture =
  panel snapshot + JPEG; the JPEG encoder lives in `sdl_panel.c` so the harness
  core stays BSP-agnostic. **Do not** reintroduce a `bsp_pixel_format_t` dependency
  into the harness (that made `bsp` ↔ harness circular before).
- `sim_harness_register(name, fn, user)` — apps register their own script commands
  (e.g. fault/state injection) from **sim-only** code. Keeps app-specific commands
  out of the core (the original harness hard-coded wifi commands — don't repeat
  that). A handler returns false to stop the run, like `quit`.

## Host unit tests — BSP audio

The audio dispatch + DSP chain + SDL provider have host tests in
`esp-devkit/bsp/test/` (gcc + the `idf_compat` pthread FreeRTOS shim):

```sh
nix develop -c esp-devkit/bsp/test/run.sh                          # test_audio_dsp (DSP math)
TEST=test_bsp_audio nix develop -c esp-devkit/bsp/test/run.sh      # dispatch policy vs stub providers
SIMULATOR_HEADLESS=1 TEST=test_sdl_audio nix develop -c esp-devkit/bsp/test/run.sh
```

`test_bsp_audio` covers caps gating, idempotent open/close, the volume/mute
fades, and the tone-over-PCM synth fallback; `test_sdl_audio` drives the SDL
provider in both PCM and tone_only modes (audible when a sound device exists,
paced null sink headless).

## Host unit tests — `image_processor`

Beyond UI verification, the image library has plain host unit tests (no ESP-IDF,
no LVGL, no SDL — the SoC-free pipeline + decoders only), mirroring the
`esp-devkit/bsp/driver/test` pattern:

```sh
nix develop -c sh components/image_processor/test/run.sh
```

`run.sh` compiles every `src/*.cpp` plus `test/image_processor_test.cpp` with g++
and runs the assertions. Anything device-only in `src/` must stay fenced behind
`ESP_PLATFORM` so it compiles out on the host (only the allocator does today).

Coverage: allocator/`Image` RAII, `InputStream` (read/non-consuming peek),
`sniff` + size guard, the pipeline against synthetic `RowSource`s (luma ordering,
linear-vs-perceptual averaging, error-diffusion mean preservation, L8/I4/I1
packing, invert, truncation), and the PNG/JPEG decoders against real fixtures
(exact pixels for PNG, tolerance for lossy JPEG, plus a decode-time-downscale
check). Fixtures are generated — see `gen_fixtures.py` / `gen_jpeg.c` and
[`image_processor.md`](image_processor.md). The end-to-end app display is checked
by `simulator/verify/namecard.txt` (opens `test.png`, captures the decoded frame).

## Name-card container PDF — `namecard_pdf` + the editor

The container format ([`namecard_pdf.md`](namecard_pdf.md)) is verified across both
languages, with the **TypeScript writer as the source of truth** and the C++ parser
held to byte-identical golden fixtures.

```sh
cd editor && npm install && npm test       # vitest: crc/RLE/footer/glyphs + PDF + truncation==share
cd editor && npm run gen-fixtures          # regenerate components/namecard_pdf/test/fixtures/ (+ manifest.h)
nix develop -c sh components/namecard_pdf/test/run.sh      # C++ parser vs the golden manifest.h
nix develop -c sh components/namecard_pdf/test/run_e2e.sh  # E2E: extracted JPEG -> image_processor::decode_buffer
```

The **cross-language golden** is the linchpin: `gen-fixtures` emits the PDFs plus
`manifest.h` (expected name/message, per-asset offset/length/crc32/sha256,
`base_total_length`); the C++ test reproduces those exactly, confirms
`write_share_pdf` byte-matches the share golden, expands a `name_glyphs` glyph's RLE
against a known bit pattern, and checks corrupt/truncated/null inputs return a
`Status` error. Run `gen-fixtures` after any writer change so both sides stay locked.
`pdfinfo`/`pdftoppm` (poppler) confirm the hand-written xref renders as a real PDF;
`run.sh` accepts `NCK_SAN=1` for ASAN/UBSAN where a sanitizer runtime exists.

The **app-side abstraction** (`NameCardData`, the browser's `FileLoader` over plain
images and `.mnc.pdf`) is LVGL-free, so it has its own host test:

```sh
nix develop -c sh app/test/run.sh   # NameCardData: detect image vs .mnc.pdf, decode display image, metadata
```

It loads the golden `.mnc.pdf` + asset JPEGs (run `gen-fixtures` first), asserting
card detection + name + a decoded display image, the plain-image path, and that a
`.snc.pdf` / missing file fail cleanly. End-to-end in the simulator,
`simulator/verify/mnc_pdf.txt` browses `simulator/sdcard/yamada.mnc.pdf` and
captures the decoded card (the embedded display JPEG is sub-range-decoded — the
`image_processor` `offset/length` path, also unit-tested in
`image_processor_test.cpp`).
