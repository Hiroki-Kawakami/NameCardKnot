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
