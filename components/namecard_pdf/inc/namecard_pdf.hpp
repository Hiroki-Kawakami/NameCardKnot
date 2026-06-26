/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// namecard_pdf: read the NameCardKnot container PDF produced by the editor
// (TypeScript). Extract the name/url/message strings and the embedded JPEGs,
// and write a "share-only" PDF (display image stripped) by truncation. The
// PDF body is never parsed: only the appended NCK footer (an index of
// {type,offset,length,crc}) is read. LVGL-free — the name_glyphs blob is
// returned as raw data; the app wraps it into an lv_font. See
// docs/namecard_pdf.md.
namespace nckpdf {

enum class Status {
    Ok,
    NotNckPdf,           // missing/bad trailer or footer magic
    VersionUnsupported,  // footer/glyphs version newer than this parser
    Truncated,           // offsets/lengths run past the buffer/file
    CrcMismatch,         // footer or asset CRC failed
    OpenFailed,          // file could not be opened
    IoError,             // read/write failed
    BadArgument,
};

const char *status_str(Status s);

enum class AssetType : uint8_t {
    Name = 0,
    Url = 1,
    Message = 2,
    DisplayJpeg = 3,
    ShareJpeg = 4,
    NameGlyphs = 5,
};

enum class AssetSubtype : uint8_t { Jpeg = 0, Png = 1 };

constexpr uint8_t kFlagStripOnShare = 0x01;

// One footer index entry: a byte range of the file holding the payload.
struct Asset {
    uint8_t type = 0;
    uint8_t flags = 0;
    uint8_t subtype = 0;
    uint32_t offset = 0;
    uint32_t length = 0;
    uint32_t crc = 0;
};

struct Card {
    std::string name;     // decoded UTF-8
    std::string url;      // decoded UTF-8
    std::string message;  // decoded UTF-8
    std::vector<Asset> assets;
    uint32_t base_total_length = 0;  // share-only truncation point (0 = already minimal)

    const Asset *find(AssetType t) const;            // first asset of type, or nullptr
    std::vector<const Asset *> all(AssetType t) const;
};

// ---- Parsing ----------------------------------------------------------------

// Parse a full in-memory file image. Validates the footer CRC and every
// asset CRC, and decodes the name/url/message strings.
Status parse_buffer(const uint8_t *data, size_t len, Card &out);

// Parse straight from an SD/host file via seeks (footer + strings only; image
// payloads stay as offset/length for read_asset). Does not load the whole file.
Status open_file(const char *path, Card &out);

// Read an asset's raw bytes into a caller buffer (buflen must be >= a.length).
Status read_asset(const char *path, const Asset &a, uint8_t *buf, size_t buflen);

// ---- Share-only PDF ---------------------------------------------------------

// Write a share-only PDF (display image removed) by copying [0,
// base_total_length) of src to dst. If base_total_length is 0 the source is
// already share-only and the whole file is copied.
Status write_share_pdf(const char *src_path, const char *dst_path);

// ---- name_glyphs blob (LVGL-free) ------------------------------------------

// One embedded glyph. `rle`/`rle_len` point into the blob; expand with
// rle_decode (nbits = box_w*box_h).
struct Glyph {
    uint32_t codepoint = 0;
    uint16_t adv_w = 0;  // advance width in 1/16 px (8.4 fixed)
    uint8_t box_w = 0;
    uint8_t box_h = 0;
    int8_t ofs_x = 0;
    int8_t ofs_y = 0;
    const uint8_t *rle = nullptr;
    uint16_t rle_len = 0;
};

struct GlyphSet {
    uint16_t glyph_px = 0;
    uint16_t base_line = 0;
    uint16_t line_height = 0;
    std::vector<Glyph> glyphs;  // sorted by codepoint
};

// Parse a name_glyphs blob (the bytes of the NameGlyphs asset). Glyph `rle`
// pointers reference `blob`, so it must outlive `out`.
Status parse_name_glyphs(const uint8_t *blob, size_t len, GlyphSet &out);

// Expand bit-run RLE into `out` (ceil(nbits/8) bytes, MSB-first). `out` is
// cleared first. Mirrors editor/src/lib/namecard-pdf/rle.ts.
void rle_decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t nbits);

// zlib CRC32 (poly 0xEDB88320). Exposed for tests.
uint32_t crc32(const uint8_t *data, size_t len);

}  // namespace nckpdf
