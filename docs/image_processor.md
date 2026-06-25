# image_processor — JPEG/PNG → EPD grayscale pipeline

`components/image_processor` decodes a JPEG or PNG (from an SD file or a memory
buffer) and produces a dithered grayscale buffer sized for the 16-gray EPD. It is
**LVGL-free and has no external dependencies** — the JPEG/PNG decoders and the
zlib `inflate` are all in-tree, so the simulator and the device run the exact same
code. Public API: `inc/image_processor.hpp`.

## Why it exists

The previous path fed the SD image straight to LVGL's built-in decoders, which
gave no control over color/dithering and **silently stopped on large images**
(the decoder allocated the full-resolution canvas in internal RAM and failed with
no error surfaced). This library fixes both: every stage streams row-by-row so the
full-resolution source is never materialized, allocation failures and oversized
inputs return a `Status`, and color conversion + dithering are explicit options.

## The pipeline (`src/pipeline.cpp`, `run_pipeline`)

One top-to-bottom streaming pass. Only per-row working buffers plus the output are
held — never the whole decoded image:

```
RowSource (decoder)  ── rows of RGB888 / Gray8 at (downscaled) source resolution
   → box downscale    ── area-average to the exact target W×H (H within row, V by
                          push-driven accumulation of source rows; Q16 weights)
   → color convert    ── linearize (sRGB/power) → Rec709/601/avg luma → output gamma
   → dither           ── quantize to N levels (Bayer ordered, or error diffusion)
   → pack             ── L8 (high nibble = gray) / I4 / I1   → imgproc::Image
```

`linear_downsample` decides where output gamma is applied: averaging in **linear
light** (default, photometrically correct) defers gamma to after the box filter;
averaging in perceptual space applies it before. Both share one code path
(`ColorPipeline::to_intensity` produces the right domain, `finalize` matches it).

The output buffer is allocated with `img_alloc(size, opts.alloc_caps)` — PSRAM by
default on device, `malloc` on host. Transient per-row buffers use the internal
heap.

## Options that matter

| Option | Effect |
|---|---|
| `target_w/h` + `fit` | Output box. `Contain` preserves aspect; `Stretch` is exact. **Decode at the on-screen size and display 1:1** — letting LVGL rescale ruins the dither. |
| `max_src_pixels` | Pre-allocation guard. Rejects oversized sources (`TooLarge`) before any full-res work; also drives JPEG's decode-time downscale. |
| `levels` | 2 → EPD `FAST`; 16 → EPD `QUALITY` (the EPD's two modes). L8 packs `level*255/(N-1)`, so 16 levels land exactly in the byte's high nibble. |
| `dither` | `None`, `Bayer2/4/8` (stateless, frame-stable — good for EPD), or error diffusion (`FloydSteinberg` default, `Atkinson/Sierra/JJN/Stucki`; `serpentine`). |
| `luma` / `in_gamma` / `out_gamma` | Rec709 (default) / Rec601 / average / custom; sRGB / power / linear in; sRGB / power / **`EpdLut`** out (16-entry measured-reflectance curve for panel calibration). |
| `invert` | Flips black/white. Default off (L8 255 = white, matching the panel). |
| `out` | `L8` (default, 1:1 to EPD + LVGL `L8`), `I4` (16-color palette), `I1` (1bpp). The app adapter only wraps `L8` today; `I4`/`I1` need an LVGL palette prepended. |

## Decoders (`src/png.cpp`, `src/jpeg.cpp`, `src/inflate.cpp`)

Both are `Decoder`s (a `RowSource` + `open(InputStream&, const Options&)`) behind
`make_decoder()`, selected by `sniff()`.

**PNG** — chunk parse (IHDR/PLTE/tRNS/IDAT/IEND) → 5-filter unfilter → per-row
color normalization. Streams IDAT through a **pull-based in-tree `inflate`**
(stored/fixed/dynamic DEFLATE, 32 KiB window), so it decodes one scanline at a
time. Huffman symbols decode through a 9-bit fast table (`Huff::fast`, built in
`construct`); only codes longer than 9 bits fall back to a bit-by-bit walk. Supports 8-bit gray/RGB/RGBA, 1/2/4/8-bit gray and palette, `tRNS`
composited over white. Unsupported (returns from `open`): 16-bit channels,
interlaced.

**JPEG** — baseline (SOF0), 8-bit, Huffman. Marker parse → MCU-row band decode →
Huffman/dequant/IDCT/YCbCr→RGB. Huffman uses the same 9-bit fast-table scheme as
inflate (built in `parse_dht`; MSB-first, so no bit reversal), over a 32-bit
MSB-first bit buffer. **Downscales while decoding**: a
1/1..1/8 factor is chosen in `setup()` from `target_w/h` and `max_src_pixels`;
each 8×8 block is reduced to (8/S)×(8/S) — **DC-only at 1/8**, a jidctfst-style
integer **AAN IDCT** then box-reduce at 1/2 and 1/4, full AAN IDCT at 1/1. AC-free
blocks take a **flat-block fast path** (skip the IDCT, fill with the DC value —
a big win for the smooth regions of a name card). The AAN scale factors are
folded into the dequant table (`aan_qt_`, built in `setup()`), so the IDCT applies
dequant + scaling in one multiply. Supports grayscale + YCbCr with sampling
factors ≤ 2×2 (4:4:4 / 4:2:2 / 4:4:0 / 4:2:0) and restart intervals. Unsupported:
progressive, 16-bit, arithmetic, CMYK.

Both decoders pull **one byte at a time** from the `InputStream`. The base class
batches the underlying source into a 32 KiB heap window (`raw_read` is called per
refill, not per byte), so per-byte decoding doesn't hit a `fread` per byte — which
is very slow on FAT-over-SDSPI on the device. The window is heap-allocated (too
big for the LVGL task stack); `InputStream::ok()` reports an allocation failure.

## Status codes

`Ok`, `OpenFailed` (file), `UnsupportedFormat` (container/feature), `DecodeError`
(malformed), `Truncated` (stream ended early — also what a mid-decode error
surfaces as, since `RowSource::next_row` is boolean), `TooLarge`, `OutOfMemory`,
`BadArgument`.

## App integration

`NameCardScreen::build()` decodes at `lv_display_get_{horizontal,vertical}_resolution`
with `Fit::Contain` + `levels=16`, owns the `imgproc::Image`, and wraps it into an
`lv_image_dsc_t` via `app/lv_image_adapter.hpp` (L8 → `LV_COLOR_FORMAT_L8`). The
Image must outlive the descriptor and the `lv_image`. EPD `QUALITY_FULL` is set in
`onAppear()`.

## Profiling (`IMGPROC_PROFILE`)

`decode_*` prints a one-line per-decode stage breakdown via `printf` (device: idf
monitor; simulator: terminal). On by default; the host unit tests build with
`-DIMGPROC_PROFILE=0` to stay quiet (define it to 0 anywhere to compile the timing
out). Timing source is `esp_timer_get_time()` on device, `std::chrono` on host
(`src/profile.{hpp,cpp}`).

```
[imgproc] 540x540  total=7860us  decode=1562us (compute=1169us io=393us,11 calls,40KB)  color+down=330us  dither=5780us
```

- **decode** — `RowSource::next_row` (inflate / JPEG IDCT+Huffman), inclusive of
  **io** (the raw SD/file reads); `compute = decode − io`.
- **color+down** — linearize + luma + box downscale.
- **dither** — finalize + dither + pack.

Use it to decide what to optimize before touching code — the host and device
splits differ, so measure on hardware.

### Hot-path integerization

The first device profile was dominated by `double` math (`double` is software-
emulated on the ESP32-S3 — no double-precision FPU). Two passes removed it:

- **Horizontal box weights are precomputed once** (`build_hbox`, integer Q16
  weights) instead of recomputed with `double` per source row, so `hreduce` is an
  integer multiply-accumulate in the hot loop. Only the vertical geometry keeps a
  few `double`s per *source row* (O(rows), negligible).
- **Dither is integer** (`src/dither.cpp`): quantize is a 256-entry LUT, error
  diffusion carries fixed-point (`<<8`) error in `int32` buffers, Bayer thresholds
  are integers — no per-pixel `float`.
- **No per-pixel divide in finalize/dither**: the box-average divide
  (`vacc/vwsum`) is a per-row reciprocal multiply, and power-of-two error-diffusion
  divisors (FS=16 / Atkinson=8 / Sierra=32) use a shift.

### Two-stage parallelism (`IMGPROC_PARALLEL`)

The entropy bitstream is serial (can't be split across cores), but decode and the
rest of the pipeline can overlap. A **producer task** (`producer_task`) pulls source rows via `RowSource::next_row`
(decode + I/O) into a 6-slot ring; the **consumer** (the calling thread, the
LVGL task on the device) drains the ring through color → downscale → dither →
pack. The producer is pinned with `xTaskCreatePinnedToCore` to
`1 - xPortGetCoreID()` — the core *not* running the consumer — so the two genuinely
run in parallel; pinning both to one core just time-slices it (no speedup, and the
wall-clock stage timings inflate). Two counting semaphores (free / filled) bound the ring; a binary `done_sem`
joins before teardown. Wall-clock then approaches `max(decode, rest)` instead of
their sum (the profile shows `total < decode + transform` as proof).

Each `Prof` field has a single writer — decode/io/entropy/idct/post on the
producer, transform/dither on the consumer — so the shared struct needs no lock.

The speedup is bounded by the shared **PSRAM bus**: the output buffer is in PSRAM
(consumer writes), so to keep the producer off that bus the JPEG band is allocated
in **internal RAM** (`img_alloc_internal`, PSRAM fallback for very wide images) and
the SD window is 32 KiB. Even so, expect well under 2× — the workload is memory-
bandwidth-bound, and for a big PNG the consumer is already fully hidden behind a
long serial decode (`total ≈ decode`), so the remaining lever there is the decoder
itself, not more parallelism.
On by default; the host unit tests build `-DIMGPROC_PARALLEL=0` (no FreeRTOS),
which keeps the pipeline single-threaded and host-testable. FreeRTOS comes from
ESP-IDF on device and from `idf_compat` (pthread-backed) in the simulator.

## Known optimization opportunities (not yet done)

- JPEG 1/2 and 1/4 run the full AAN IDCT then box-reduce; a true reduced IDCT
  (4×4 / 2×2 directly) would cut CPU further. 1/8 and AC-free blocks already skip it.
- JPEG YCbCr assembly (`post`) is still per-pixel; PNG `unfilter` is already
  branch-hoisted per scanline. PNG `inflate` pulls input a block at a time
  (`ByteSource::refill`), not a virtual call per byte.
- LVGL's bundled lodepng/tjpgd and the device `espressif__zlib` managed dependency
  are no longer used by NameCard and can be dropped once nothing else needs them.

## Testing

Host unit tests (`test/run.sh`, g++, no ESP-IDF) cover the allocator, stream,
sniff/size-guard, the full pipeline against synthetic `RowSource`s (luma ordering,
linear-vs-perceptual averaging, error-diffusion mean preservation, pack formats),
and both decoders against real fixtures. PNG fixtures are generated with the
stdlib zlib (`gen_fixtures.py`), JPEG fixtures with libjpeg (`gen_jpeg.c`);
regenerate per the header comment in each. End-to-end display is checked headless
via `simulator/verify/namecard.txt`. See [`testing.md`](testing.md).
