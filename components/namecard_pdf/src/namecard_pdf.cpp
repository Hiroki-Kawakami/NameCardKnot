/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "namecard_pdf.hpp"

#include <cstdio>
#include <cstring>

namespace nckpdf {

namespace {

constexpr size_t kFooterHeaderSize = 12;
constexpr size_t kFooterEntrySize = 16;
constexpr size_t kFooterTrailerSize = 24;
constexpr size_t kGlyphsHeaderSize = 16;
constexpr size_t kGlyphsEntrySize = 16;

inline uint16_t rd16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Payload byte range [offset, offset+length) must lie inside a `total`-byte file.
inline bool in_bounds(uint32_t offset, uint32_t length, uint64_t total) {
    return static_cast<uint64_t>(offset) + length <= total;
}

}  // namespace

const char *status_str(Status s) {
    switch (s) {
        case Status::Ok: return "Ok";
        case Status::NotNckPdf: return "NotNckPdf";
        case Status::VersionUnsupported: return "VersionUnsupported";
        case Status::Truncated: return "Truncated";
        case Status::CrcMismatch: return "CrcMismatch";
        case Status::OpenFailed: return "OpenFailed";
        case Status::IoError: return "IoError";
        case Status::BadArgument: return "BadArgument";
    }
    return "?";
}

uint32_t crc32(const uint8_t *data, size_t len) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        init = true;
    }
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < len; i++) c = table[(c ^ data[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

void rle_decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t nbits) {
    memset(out, 0, (nbits + 7) / 8);
    size_t bit = 0;
    size_t i = 0;
    uint8_t cur = 0;
    while (i < in_len && bit < nbits) {
        uint32_t run = 0;
        uint32_t shift = 0;
        uint8_t b;
        do {
            b = in[i++];
            run |= static_cast<uint32_t>(b & 0x7f) << shift;
            shift += 7;
        } while ((b & 0x80) && i < in_len);
        if (cur) {
            for (uint32_t k = 0; k < run && bit < nbits; k++, bit++) out[bit >> 3] |= 0x80 >> (bit & 7);
        } else {
            bit += run;
        }
        cur ^= 1;
    }
}

const Asset *Card::find(AssetType t) const {
    for (const auto &a : assets)
        if (a.type == static_cast<uint8_t>(t)) return &a;
    return nullptr;
}

std::vector<const Asset *> Card::all(AssetType t) const {
    std::vector<const Asset *> out;
    for (const auto &a : assets)
        if (a.type == static_cast<uint8_t>(t)) out.push_back(&a);
    return out;
}

// Validate + read the footer header/index out of a `len`-byte region pointed to
// by `data` (the full file image). On success fills out.assets/base_total_length.
static Status read_footer(const uint8_t *data, size_t len, Card &out) {
    if (len < kFooterTrailerSize) return Status::NotNckPdf;
    const uint8_t *trailer = data + len - kFooterTrailerSize;
    if (memcmp(data + len - 8, "NCKEND01", 8) != 0) return Status::NotNckPdf;

    const uint32_t base_total = rd32(trailer);
    const uint32_t foff = rd32(trailer + 4);
    const uint32_t fcrc = rd32(trailer + 8);
    const size_t t_start = len - kFooterTrailerSize;

    if (static_cast<uint64_t>(foff) + kFooterHeaderSize > t_start) return Status::Truncated;
    if (memcmp(data + foff, "NCK1", 4) != 0) return Status::NotNckPdf;
    if (rd16(data + foff + 4) != 1) return Status::VersionUnsupported;
    const uint16_t count = rd16(data + foff + 8);

    const uint64_t index_end = static_cast<uint64_t>(foff) + kFooterHeaderSize + static_cast<uint64_t>(count) * kFooterEntrySize;
    if (index_end != t_start) return Status::Truncated;
    if (crc32(data + foff, static_cast<size_t>(index_end - foff)) != fcrc) return Status::CrcMismatch;

    out.assets.clear();
    out.base_total_length = base_total;
    const uint8_t *e = data + foff + kFooterHeaderSize;
    for (uint16_t i = 0; i < count; i++, e += kFooterEntrySize) {
        Asset a;
        a.type = e[0];
        a.flags = e[1];
        a.subtype = e[2];
        a.offset = rd32(e + 4);
        a.length = rd32(e + 8);
        a.crc = rd32(e + 12);
        out.assets.push_back(a);
    }
    return Status::Ok;
}

Status parse_buffer(const uint8_t *data, size_t len, Card &out) {
    if (!data) return Status::BadArgument;
    Status st = read_footer(data, len, out);
    if (st != Status::Ok) return st;

    for (const auto &a : out.assets) {
        if (!in_bounds(a.offset, a.length, len)) return Status::Truncated;
        if (crc32(data + a.offset, a.length) != a.crc) return Status::CrcMismatch;
        const char *p = reinterpret_cast<const char *>(data + a.offset);
        switch (static_cast<AssetType>(a.type)) {
            case AssetType::Name: out.name.assign(p, a.length); break;
            case AssetType::Url: out.url.assign(p, a.length); break;
            case AssetType::Message: out.message.assign(p, a.length); break;
            default: break;
        }
    }
    return Status::Ok;
}

Status parse_name_glyphs(const uint8_t *blob, size_t len, GlyphSet &out) {
    if (!blob) return Status::BadArgument;
    if (len < kGlyphsHeaderSize || memcmp(blob, "NGLY", 4) != 0) return Status::NotNckPdf;
    if (blob[4] != 1) return Status::VersionUnsupported;

    out.glyph_px = rd16(blob + 8);
    out.base_line = rd16(blob + 10);
    out.line_height = rd16(blob + 12);
    const uint16_t count = rd16(blob + 14);

    const uint64_t table_end = kGlyphsHeaderSize + static_cast<uint64_t>(count) * kGlyphsEntrySize;
    if (table_end > len) return Status::Truncated;
    const size_t data_start = static_cast<size_t>(table_end);

    out.glyphs.clear();
    for (uint16_t i = 0; i < count; i++) {
        const uint8_t *g = blob + kGlyphsHeaderSize + i * kGlyphsEntrySize;
        Glyph gl;
        gl.codepoint = rd32(g);
        gl.adv_w = rd16(g + 4);
        gl.box_w = g[6];
        gl.box_h = g[7];
        gl.ofs_x = static_cast<int8_t>(g[8]);
        gl.ofs_y = static_cast<int8_t>(g[9]);
        const uint32_t doff = rd32(g + 10);
        const uint16_t dlen = rd16(g + 14);
        if (static_cast<uint64_t>(data_start) + doff + dlen > len) return Status::Truncated;
        gl.rle = blob + data_start + doff;
        gl.rle_len = dlen;
        out.glyphs.push_back(gl);
    }
    return Status::Ok;
}

// ---- File-backed helpers ----------------------------------------------------

namespace {

struct File {
    FILE *f = nullptr;
    explicit File(const char *path, const char *mode) { f = fopen(path, mode); }
    ~File() {
        if (f) fclose(f);
    }
    File(const File &) = delete;
    File &operator=(const File &) = delete;
};

// Read the trailer (last 24 bytes) of an open file; returns size and fields.
Status read_trailer(FILE *f, long &fsize, uint32_t &base_total, uint32_t &foff, uint32_t &fcrc) {
    if (fseek(f, 0, SEEK_END) != 0) return Status::IoError;
    fsize = ftell(f);
    if (fsize < static_cast<long>(kFooterTrailerSize)) return Status::NotNckPdf;
    uint8_t tr[kFooterTrailerSize];
    if (fseek(f, fsize - static_cast<long>(kFooterTrailerSize), SEEK_SET) != 0) return Status::IoError;
    if (fread(tr, 1, sizeof tr, f) != sizeof tr) return Status::IoError;
    if (memcmp(tr + 16, "NCKEND01", 8) != 0) return Status::NotNckPdf;
    base_total = rd32(tr);
    foff = rd32(tr + 4);
    fcrc = rd32(tr + 8);
    return Status::Ok;
}

}  // namespace

Status open_file(const char *path, Card &out) {
    File file(path, "rb");
    if (!file.f) return Status::OpenFailed;
    FILE *f = file.f;

    long fsize;
    uint32_t base_total, foff, fcrc;
    Status st = read_trailer(f, fsize, base_total, foff, fcrc);
    if (st != Status::Ok) return st;

    const size_t t_start = static_cast<size_t>(fsize) - kFooterTrailerSize;
    if (static_cast<uint64_t>(foff) + kFooterHeaderSize > t_start) return Status::Truncated;

    // Read the footer region [foff, t_start) and validate via the buffer path.
    const size_t flen = t_start - foff;
    std::vector<uint8_t> footer(flen);
    if (fseek(f, foff, SEEK_SET) != 0) return Status::IoError;
    if (fread(footer.data(), 1, flen, f) != flen) return Status::IoError;
    if (memcmp(footer.data(), "NCK1", 4) != 0) return Status::NotNckPdf;
    if (rd16(footer.data() + 4) != 1) return Status::VersionUnsupported;
    const uint16_t count = rd16(footer.data() + 8);
    if (kFooterHeaderSize + static_cast<size_t>(count) * kFooterEntrySize != flen) return Status::Truncated;
    if (crc32(footer.data(), flen) != fcrc) return Status::CrcMismatch;

    out.assets.clear();
    out.base_total_length = base_total;
    out.name.clear();
    out.url.clear();
    out.message.clear();
    const uint8_t *e = footer.data() + kFooterHeaderSize;
    for (uint16_t i = 0; i < count; i++, e += kFooterEntrySize) {
        Asset a;
        a.type = e[0];
        a.flags = e[1];
        a.subtype = e[2];
        a.offset = rd32(e + 4);
        a.length = rd32(e + 8);
        a.crc = rd32(e + 12);
        if (!in_bounds(a.offset, a.length, static_cast<uint64_t>(fsize))) return Status::Truncated;
        out.assets.push_back(a);
    }

    // Read the (small) string payloads now; image payloads stay as offsets.
    for (const auto &a : out.assets) {
        std::string *dst = nullptr;
        switch (static_cast<AssetType>(a.type)) {
            case AssetType::Name: dst = &out.name; break;
            case AssetType::Url: dst = &out.url; break;
            case AssetType::Message: dst = &out.message; break;
            default: continue;
        }
        dst->resize(a.length);
        if (a.length) {
            if (fseek(f, a.offset, SEEK_SET) != 0) return Status::IoError;
            if (fread(&(*dst)[0], 1, a.length, f) != a.length) return Status::IoError;
            if (crc32(reinterpret_cast<const uint8_t *>(dst->data()), a.length) != a.crc)
                return Status::CrcMismatch;
        }
    }
    return Status::Ok;
}

Status read_asset(const char *path, const Asset &a, uint8_t *buf, size_t buflen) {
    if (!buf || buflen < a.length) return Status::BadArgument;
    File file(path, "rb");
    if (!file.f) return Status::OpenFailed;
    if (fseek(file.f, a.offset, SEEK_SET) != 0) return Status::IoError;
    if (a.length && fread(buf, 1, a.length, file.f) != a.length) return Status::IoError;
    if (crc32(buf, a.length) != a.crc) return Status::CrcMismatch;
    return Status::Ok;
}

Status write_share_pdf(const char *src_path, const char *dst_path) {
    File src(src_path, "rb");
    if (!src.f) return Status::OpenFailed;

    long fsize;
    uint32_t base_total, foff, fcrc;
    Status st = read_trailer(src.f, fsize, base_total, foff, fcrc);
    if (st != Status::Ok) return st;

    // base_total 0 => the source is already share-only; copy it whole.
    const size_t copy_len = base_total ? base_total : static_cast<size_t>(fsize);
    if (copy_len > static_cast<size_t>(fsize)) return Status::Truncated;

    File dst(dst_path, "wb");
    if (!dst.f) return Status::OpenFailed;
    if (fseek(src.f, 0, SEEK_SET) != 0) return Status::IoError;

    uint8_t buf[4096];
    size_t remaining = copy_len;
    while (remaining) {
        const size_t want = remaining < sizeof buf ? remaining : sizeof buf;
        const size_t got = fread(buf, 1, want, src.f);
        if (got != want) return Status::IoError;
        if (fwrite(buf, 1, got, dst.f) != got) return Status::IoError;
        remaining -= got;
    }
    return Status::Ok;
}

}  // namespace nckpdf
