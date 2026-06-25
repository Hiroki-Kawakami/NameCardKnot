/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

// image_processor: decode JPEG/PNG (from file or buffer), downscale while
// decoding, convert RGB/YUV -> grayscale with perceptual luminance + gamma,
// and dither to 2- or 16-level for the EPD panel. LVGL-free: the output is a
// plain grayscale/packed buffer; the app wraps it into an lv_image_dsc_t.
namespace imgproc {

enum class Status {
    Ok,
    OpenFailed,         // file could not be opened / read
    UnsupportedFormat,  // unknown container, or unsupported feature (e.g. progressive)
    DecodeError,        // malformed / corrupt stream
    Truncated,          // stream ended early
    TooLarge,           // source exceeds Options::max_src_pixels (rejected before alloc)
    OutOfMemory,
    BadArgument,
    Cancelled,          // aborted via Progress::cancel
};

const char *status_str(Status s);

// ---- Color / geometry -------------------------------------------------------

enum class Fit {
    Contain,  // scale to fit inside target box, preserve aspect (default)
    Stretch,  // scale to exactly target_w x target_h
};

enum class Luma {
    Rec709,   // sRGB primaries (0.2126, 0.7152, 0.0722)  [default]
    Rec601,   // matches JPEG YCbCr (0.299, 0.587, 0.114)
    Average,  // (R + G + B) / 3
    Custom,   // use Options::luma_custom
};

// Electro-optical transfer of the *input* pixels (decode -> linear light).
enum class GammaIn {
    Srgb,    // sRGB EOTF (default)
    Power,   // pure power curve, exponent = Options::in_power
    Linear,  // already linear, no conversion
};

// Maps linear luminance -> perceptual gray that gets quantized/dithered.
enum class GammaOut {
    Srgb,    // sRGB OETF (default)
    Power,   // pure power curve, exponent = Options::out_power
    EpdLut,  // use Options::epd_lut16 (measured panel reflectance per level)
};

// ---- Dithering --------------------------------------------------------------

enum class Dither {
    None,            // nearest-level quantize
    Bayer2,          // ordered, stateless, frame-stable (good for EPD)
    Bayer4,
    Bayer8,
    FloydSteinberg,  // error diffusion (richer tones)  [default]
    Atkinson,
    Sierra,
    JJN,             // Jarvis-Judice-Ninke
    Stucki,
};

// ---- Output -----------------------------------------------------------------

// L8 stores gray in the high nibble so 16 levels map 1:1 onto the EPD panel
// (level*17 -> high nibble == level). I4/I1 are packed palette formats.
enum class OutFormat {
    L8,  // 8 bpp grayscale (default)
    I4,  // 4 bpp, 16-entry palette
    I1,  // 1 bpp, 2-entry palette
};

struct Options {
    // Geometry. Set target to the on-screen size: dithering must happen at the
    // final display resolution (do not let LVGL rescale the result).
    uint16_t target_w = 0;  // 0 = source size, clamped by max_src_pixels
    uint16_t target_h = 0;
    Fit      fit = Fit::Contain;
    uint32_t max_src_pixels = 8u * 1024 * 1024;  // pre-alloc guard

    // Color
    Luma     luma = Luma::Rec709;
    float    luma_custom[3] = {0.2126f, 0.7152f, 0.0722f};
    GammaIn  in_gamma = GammaIn::Srgb;
    float    in_power = 2.2f;
    GammaOut out_gamma = GammaOut::Srgb;
    float    out_power = 2.2f;
    const uint8_t *epd_lut16 = nullptr;  // 16 entries, used when out_gamma == EpdLut
    bool     linear_downsample = true;   // average in linear light (correct, costlier)
    bool     invert = false;             // flip black/white

    // Dithering
    Dither  dither = Dither::FloydSteinberg;
    uint8_t levels = 16;        // 16 -> QUALITY, 2 -> FAST (any N >= 2 allowed)
    bool    serpentine = true;  // boustrophedon scan for error diffusion

    // Output
    OutFormat out = OutFormat::L8;
    uint32_t  alloc_caps = 0;   // device heap caps for the output buffer (0 = default)
};

// Owns `data`. Move-only.
struct Image {
    uint8_t  *data = nullptr;
    uint16_t  w = 0;
    uint16_t  h = 0;
    OutFormat format = OutFormat::L8;
    size_t    stride = 0;   // bytes per row
    uint8_t   levels = 16;  // distinct gray levels actually produced

    Image() = default;
    ~Image();
    Image(Image &&other) noexcept;
    Image &operator=(Image &&other) noexcept;
    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;

    void reset();  // free + clear
};

// Optional progress + cancellation channel. The decoder writes done/total; the
// caller writes cancel. Each field has a single writer (no locking needed). LVGL-
// free: the app polls the atomics from its own UI loop.
struct Progress {
    std::atomic<int>  done{0};         // finalized output rows (decoder writes)
    std::atomic<int>  total{0};        // output rows; set once when known (decoder writes)
    std::atomic<bool> cancel{false};   // set to request an abort (caller writes)
};

// ---- Entry points -----------------------------------------------------------

Status decode_file(const char *path, const Options &opts, Image &out, Progress *prog = nullptr);
Status decode_buffer(const void *data, size_t len, const Options &opts, Image &out,
                     Progress *prog = nullptr);

// ---- Async ------------------------------------------------------------------

// Background decode job. Poll progress_pct()/state() from the UI loop; on
// State::Ok call take_image(). cancel() requests an abort (the caller should keep
// its modal up until state() becomes Cancelled). Refcounted via shared_ptr, and
// the worker holds its own ref, so the job stays alive until the decode finishes
// even if the app drops its handle.
class DecodeJob {
public:
    enum class State { Running, Ok, Failed, Cancelled };

    DecodeJob(const char *path, const Options &opts);  // internal: use decode_file_async

    int    progress_pct() const;
    State  state() const { return state_.load(); }
    Status status() const { return status_.load(); }
    Image  take_image();  // move the result out (after State::Ok, on the UI thread)
    void   cancel() { prog_.cancel.store(true); }

    void run();  // internal: runs the decode synchronously on the worker

private:
    Progress prog_;
    std::atomic<State>  state_{State::Running};
    std::atomic<Status> status_{Status::Ok};
    Image image_;
    std::string path_;
    Options opts_;
};

// Starts a background decode (a FreeRTOS task) and returns the job immediately;
// runs synchronously when built without FreeRTOS (host unit tests). task_priority
// / task_core default to -1 = the caller's priority and the core not running it.
std::shared_ptr<DecodeJob> decode_file_async(const char *path, const Options &opts,
                                             int task_priority = -1, int task_core = -1);

}  // namespace imgproc
