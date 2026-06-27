/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "L8View.hpp"
#include <cstddef>
#include <cstdint>

// MyCardStore: the persisted "My Card" living in a dedicated 2MB flash
// partition ("mycard"), managed as a single custom binary layout (no
// filesystem). The display/preview/name caches are stored as raw L8 and can be
// shown straight from memory-mapped flash (MMIO) with no RAM copy.
//
// The partition is NOT mapped wholesale: on the original ESP32 the ~4MB DROM
// mmap window is mostly taken by .rodata, so a permanent 2MB map starves it and
// hangs the board. Instead the header is read into a small RAM cache, the PDF is
// read by copy, and only the image blob being displayed is mmap'd on demand via
// a MappedImage handle that unmaps on destruction (see docs/mycard.md).
//
// Layout (offsets from the partition start):
//   0x0000  Header (magic + per-blob index), within the first 4KB sector
//   0x1000  Pdf      (the full .mnc.pdf, byte copy)
//   ...     Display  (540x960 L8)   sector-aligned
//   ...     Preview  (169x300 L8)   sector-aligned
//   ...     Name     (32px L8)      sector-aligned
//
// Power-fail safety: a Writer erases only the sectors it touches, writes the
// blobs, and writes the magic header LAST. An interrupted import leaves the
// magic invalid -> available() == false (the old card is already gone, but a
// half-written card is never mistaken for valid).
namespace mycard {

enum BlobId : uint8_t {
    BLOB_PDF = 0,
    BLOB_DISPLAY,
    BLOB_PREVIEW,
    BLOB_NAME,
    BLOB_COUNT,
};

enum BlobFormat : uint8_t { FMT_RAW = 0, FMT_L8 = 1 };

struct Blob {
    uint32_t offset;   // bytes from partition start (sector-aligned)
    uint32_t length;   // payload length
    uint32_t crc;      // crc32 of the payload
    uint16_t w, h;     // images only (0 otherwise)
    uint32_t stride;   // bytes per row (images only)
    uint8_t  levels;   // gray levels (images only)
    uint8_t  format;   // BlobFormat
    uint8_t  pad[2];
};

constexpr uint32_t kMagic = 0x4D4B434E;  // 'NCKM'
constexpr uint16_t kVersion = 1;

struct Header {
    uint32_t magic;
    uint16_t version;
    uint16_t blob_count;
    uint32_t header_crc;  // crc32 over the bytes following this field (the blob index)
    Blob     blobs[BLOB_COUNT];
};
static_assert(sizeof(Header) % 4 == 0, "flash writes must be 4-byte aligned");

// A single image blob mapped from flash for display (MMIO). Hold this while an
// lv_image references view(); destroying it unmaps. Move-only.
class MappedImage {
public:
    MappedImage() = default;
    ~MappedImage();
    MappedImage(MappedImage &&o) noexcept;
    MappedImage &operator=(MappedImage &&o) noexcept;
    MappedImage(const MappedImage &) = delete;
    MappedImage &operator=(const MappedImage &) = delete;

    const L8View &view() const { return view_; }
    bool valid() const { return view_.valid(); }

private:
    friend class Store;
    void release();

    L8View    view_;
    uintptr_t handle_ = 0;  // device: esp_partition_mmap_handle_t; simulator: unused
};

class Store {
public:
    // Find the partition + read the header into a RAM cache. Idempotent; does not
    // require a valid card and never maps the bulk of the partition.
    static bool mount();
    // True iff a complete, CRC-valid card header is cached.
    static bool available();

    static const Header *header();  // cached header (RAM), nullptr if unmounted
    static uint32_t      blob_len(BlobId id);

    // Copy a blob into a caller buffer (for the PDF + small metadata). `len` is
    // clamped to the blob length; returns false if unavailable or read failed.
    static bool read_blob(BlobId id, void *buf, uint32_t len);

    // Map one stored L8 image on demand. Empty MappedImage unless the card is
    // available and the blob is FMT_L8. Keep the handle alive while displaying.
    static MappedImage map_image(BlobId id);

    // Replaces the stored card. erase -> write_blob... -> commit, in that order.
    // Callers must hold no MappedImage over a blob being rewritten (drop them
    // first); commit() refreshes the header cache.
    class Writer {
    public:
        Writer() = default;
        ~Writer();

        bool begin();
        // `meta` carries w/h/stride/levels/format for images; nullptr -> FMT_RAW.
        bool write_blob(BlobId id, const void *data, uint32_t len, const Blob *meta = nullptr);
        bool commit();
        void abort();

    private:
        Header   hdr_{};
        uint32_t cursor_ = 0;
        bool     active_ = false;
    };
};

}  // namespace mycard
