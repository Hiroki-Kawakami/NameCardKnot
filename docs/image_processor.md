# image_processor — JPEG/PNG → EPD grayscale pipeline

`components/image_processor` decodes a JPEG or PNG (from an SD file or a memory
buffer) and produces a dithered grayscale buffer sized for the 16-gray EPD. It is
**LVGL-free**: the output is a plain buffer the app wraps into an `lv_image_dsc_t`.
Public API: `inc/image_processor.hpp`.

Since the C image-processing primitives were extracted into the reusable
**`esp-devkit/libs/image_framework`** (imgf) library, this component is now **pure
orchestration**: it owns the public C++ API (`Options`/`Image`/`Status`/`Progress`/
`DecodeJob`), maps `Options` onto the imgf module options, and streams an opened
decoder through the imgf stages. No decoder, resizer, ditherer, packer, color
math, or async runner lives here anymore — see
[`image_framework.md`](../esp-devkit/libs/image_framework) for those.

## Why it exists

The previous path fed the SD image straight to LVGL's built-in decoders, which
gave no control over color/dithering and **silently stopped on large images**
(the decoder allocated the full-resolution canvas in internal RAM and failed with
no error surfaced). This component fixes both: every imgf stage streams
row-by-row so the full-resolution source is never materialized, allocation
failures and oversized inputs return a `Status`, and color conversion + dithering
are explicit options.

## The pipeline (`src/pipeline.cpp`)

One top-to-bottom streaming pass over image_framework. Only per-row working
buffers plus the output are held — never the whole decoded image:

```
imgf_decoder            ── rows of RGB888 / Gray8 at (downscaled) source resolution
   → imgf_recolor       ── to_intensity: linearize (sRGB/power) → Rec709/601/avg luma
       (front)             → Gray8 intensity safe to area-average
   → imgf_resizer       ── box downscale (Gray8→Gray8, geometry only) to target W×H
   → imgf_recolor       ── finalize: output gamma (sRGB/power/EPD LUT) + invert
       (back)
   → imgf_dither        ── quantize to N levels (Bayer ordered, or error diffusion)
   → imgf_raw_encoder   ── L8 (high nibble = gray) / I4 / I1   → imgproc::Image
```

The non-obvious shape is that **`imgf_recolor` straddles the resizer**. To average
correctly in linear light, the front phase converts each source pixel to an
intensity (linear luminance with `linear_downsample`, or perceptual gray without),
the resizer box-averages *that* (it is a geometry-only Gray8→Gray8 pass, not its
own RGB→gray conversion), and the back phase re-encodes the averaged intensity to
the final gray. `linear_downsample` therefore only changes which domain the front
emits and whether the back applies output gamma — both halves come from the same
`imgf_recolor` instance. (Intensity is carried as Gray8, so linear averaging is at
8-bit precision; the difference vs the old 16-bit accumulation is sub-level for
display-sized images.)

The output buffer is allocated with `imgf_alloc(size, opts.alloc_caps)` — PSRAM by
default on device, `malloc` on host. Transient per-row buffers use the internal
heap (`imgf_alloc_internal`).

## Options that matter

| Option | Effect |
|---|---|
| `target_w/h` + `fit` | Output box. `Contain` preserves aspect; `Stretch` is exact. **Decode at the on-screen size and display 1:1** — letting LVGL rescale ruins the dither. Maps to `imgf_resize_opts_t`. |
| `max_src_pixels` | Pre-allocation guard. Drives the JPEG decoder's decode-time downscale (an imgf hint); the source is also rejected (`TooLarge`) here after open if the decoded geometry still exceeds it (PNG ignores the hint, so a huge PNG is caught here). |
| `levels` | 2 → EPD `FAST`; 16 → EPD `QUALITY`. L8 packs `level*255/(N-1)` — for 16 levels each lands exactly in the byte's high nibble, so the dither runs `OUT_GRAY` for L8 and `OUT_INDEX` for I4/I1. |
| `dither` | `None`, `Bayer2/4/8` (stateless, frame-stable — good for EPD), or error diffusion (`FloydSteinberg` default, `Atkinson/Sierra/JJN/Stucki`; `serpentine`). Maps to `imgf_dither_opts_t`. |
| `luma` / `in_gamma` / `out_gamma` | Rec709 (default) / Rec601 / average / custom; sRGB / power / linear in; sRGB / power / **`EpdLut`** out (16-entry measured-reflectance curve for panel calibration). Maps to `imgf_recolor_opts_t`. |
| `invert` | Flips black/white in the recolor finalize. Default off (L8 255 = white). |
| `out` | `L8` (default, 1:1 to EPD + LVGL `L8`), `I4` (16-color palette), `I1` (1bpp). Maps to `imgf_raw_format_t`. The app adapter only wraps `L8` today. |

## Orchestration (`src/image_processor.cpp`)

`decode_file` / `decode_file_parallel` / `decode_buffer` build an `imgf_stream_t`
(file sub-range or buffer), `imgf_sniff` the container head, `imgf_make_decoder` +
`imgf_decoder_open`, guard the source size, then call `run_pipeline`. The stream
source state (`imgf_file_source_t` / `imgf_buffer_source_t`) lives on the caller's
stack and must outlive the decode — the dual path blocks in `run_pipeline` until
both tasks finish, so it does. (`decode_file` re-seeks to `offset` after the sniff
peek: the imgf file source only seeks lazily when `offset > 0`, so a whole-file
decode would otherwise start mid-stream.)

## Status codes

`Ok`, `OpenFailed` (file), `UnsupportedFormat` (container/feature), `DecodeError`
(malformed), `Truncated` (stream ended early), `TooLarge`, `OutOfMemory`,
`BadArgument`, `Cancelled`. `src/pipeline.cpp`'s `status_from_imgf` maps each
`imgf_err_t` onto these.

## App integration

`NameCardScreen` decodes at `lv_display_get_{horizontal,vertical}_resolution`
with `Fit::Contain` + `levels=16`, owns the `imgproc::Image`, and wraps it into an
`lv_image_dsc_t` via `app/lv_image_adapter.hpp` (L8 → `LV_COLOR_FORMAT_L8`). The
Image must outlive the descriptor and the `lv_image`.

## Profiling (`IMGPROC_PROFILE`)

`decode_*` prints a one-line per-decode breakdown via `printf` (device: idf
monitor; simulator: terminal). On by default; the host unit tests build with
`-DIMGPROC_PROFILE=0`. With the stage internals now inside imgf, this times only
the orchestration boundaries:

```
[imgproc] 540x960 src=1080x1920 jpeg  total=7860us  decode=1562us  color+down=330us  dither=5780us
```

- **decode** — `imgf_decoder_next_row` (entropy decode + the raw SD/file reads).
- **color+down** — `imgf_recolor` (front+back) + `imgf_resizer`.
- **dither** — finalize + `imgf_dither` + `imgf_raw_encoder` pack.

### Threading model (dual-core producer/consumer split)

The decode runs on **two tasks** via `imgf_async`'s non-blocking job API so the
device's two cores work in parallel and `DecodeJob` itself spawns no task of its
own. The cut is after the decoder: the **producer** (JPEG/PNG entropy decode —
the heavy, input-dependent half) publishes source rows into an `imgf_async` ring;
the **consumer** (recolor → resize → dither → pack) drains them.

- `decode_file_async(path, opts, task_priority, task_core)` builds a `DecodeJob`
  and calls `DecodeJob::start()` → `async_decode_start` (`src/pipeline.cpp`),
  which **synchronously on the caller** opens the file/decoder and allocates the
  output (Option A — cheap; the progress modal is already up), then
  `imgf_async_start` spawns the producer (on the caller's core/priority — decode
  time swings widely with the image) and the consumer (on the other core) and
  returns immediately. `task_core`/`priority` of `-1` means the core not running
  the caller / the caller's priority.
- `DecodeJob`'s polling accessors (`state()`/`progress_pct()`/`status()`/
  `take_image()`) call a private `sync_()` that, once `imgf_async_job_done()` is
  true, calls `imgf_async_job_join()` to reclaim the job, aggregate the status,
  and move the image out. Dropping the `shared_ptr` mid-decode cancels + joins in
  `~DecodeJob`, so the worker tasks never outlive the job's `Progress`.
- The `Consumer` struct (`src/pipeline.cpp`) owns the output buffer + the imgf
  module handles and is shared by the serial and async paths, so they can't drift.
  The consumer keeps draining the ring past the last output row so the producer
  never blocks; cancellation/`Truncated`/`OutOfMemory` propagate through the ring
  abort + the aggregated `DecodeJob` status.
- Each profiling field has a single writer (producer: `decode`; consumer:
  `transform`/`dither`), so the overlapping stages don't race `g_prof`. Since they
  overlap, the per-stage numbers can sum to more than `total` — `total` is the
  real wall clock.

The `IMGPROC_ASYNC` macro (default on; host unit tests build `-DIMGPROC_ASYNC=0`)
gates the async path — with it off, `decode_file_async` decodes synchronously and
serially via `decode_file`. If `imgf_async_start` can't spawn the tasks,
`DecodeJob::start()` also falls back to the synchronous serial decode. FreeRTOS
comes from ESP-IDF on device; `imgf_async` uses pthreads on the host/simulator, so
the host tests (`run.sh` builds both `IMGPROC_ASYNC=0` and `=1`) exercise the real
two-task path.

## Testing

Host unit tests (`test/run.sh`, g++, no ESP-IDF) compile the imgf C sources and
link them with the C++ orchestration, then exercise the **public API end-to-end**:
container sniffing, the full decode→recolor→resize→dither→pack path, the option
surface (target/fit/levels/out/invert), the size guard, sub-range decode, and the
async job. The per-stage internals (decoders, recolor luma/gamma ordering, resize,
dither, pack) have their own white-box tests in
`esp-devkit/libs/image_framework/test/run.sh`. End-to-end display is checked
headless via `simulator/verify/namecard.txt`. See [`testing.md`](testing.md).
