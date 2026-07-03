/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "L8View.hpp"
#include <cstddef>
#include <cstdint>

// CardStore: a card persisted in a dedicated 2MB flash partition, managed as a
// single custom binary layout (no filesystem). Image blobs are raw L8 shown
// straight from memory-mapped flash (MMIO) with no RAM copy. Two instances:
// mycard() (the imported My Card) and lastcard() (the sleep-entry cache of the
// displayed card: PDF + decoded display image).
//
// The partition is NOT mapped wholesale: on the original ESP32 the ~4MB DROM
// mmap window is mostly taken by .rodata, so a permanent 2MB map starves it and
// hangs the board. Instead the header is read into a small RAM cache, the PDF is
// read by copy, and only the image blob being displayed is mmap'd on demand via
// a MappedImage handle that unmaps on destruction (see docs/mycard.md).
//
// Layout (offsets from the partition start):
//   0x0000  Header slots (see below), within the first 4KB sector
//   0x1000  Blobs, each sector-aligned, in write order
//
// Power-fail safety: a Writer erases the header sector, writes blobs, and
// publishes with commit() — which appends the header to the NEXT free 128-byte
// slot of the header sector (program-only, no re-erase). commit() may be called
// after each blob, so an interrupted write keeps every previously committed
// blob readable (the reader uses the last valid slot). An interrupted first
// commit leaves no valid slot -> available() == false. abort() only undoes a
// transaction that has not committed yet.
namespace cardstore {

enum BlobId : uint8_t {
    BLOB_PDF = 0,
    BLOB_DISPLAY,
    BLOB_PREVIEW,
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
    // `label` is the partition name; on the simulator the backing file is
    // `sim_env` (when set) or `sim_default`.
    Store(const char *label, const char *sim_env, const char *sim_default);

    // Find the partition + read the header into a RAM cache. Idempotent; does not
    // require a valid card and never maps the bulk of the partition.
    bool mount();
    // True iff a complete, CRC-valid card header is cached.
    bool available() const { return hdr_valid_; }

    const Header *header() const { return hdr_valid_ ? &hdr_ : nullptr; }
    uint32_t      blob_len(BlobId id) const;

    // Copy a blob into a caller buffer (for the PDF + small metadata). `len` is
    // clamped to the blob length; returns false if unavailable or read failed.
    bool read_blob(BlobId id, void *buf, uint32_t len);

    // Copy a sub-range [offset, offset+len) of a blob; len clamped to what remains.
    // Returns bytes read (0 if unavailable or offset past the blob).
    uint32_t read_blob_range(BlobId id, uint32_t offset, void *buf, uint32_t len);

    // Map one stored L8 image on demand. Empty MappedImage unless the card is
    // available and the blob is FMT_L8. Keep the handle alive while displaying.
    MappedImage map_image(BlobId id);

    // Replaces the stored card: erase -> (write_blob... commit)*. Callers must
    // hold no MappedImage over a blob being rewritten (drop them first);
    // commit() publishes the blobs written so far and refreshes the header cache.
    class Writer {
    public:
        explicit Writer(Store &store) : store_(store) {}
        ~Writer();

        bool begin();
        // `meta` carries w/h/stride/levels/format for images; nullptr -> FMT_RAW.
        bool write_blob(BlobId id, const void *data, uint32_t len, const Blob *meta = nullptr);
        bool commit();
        void abort();  // only before the first commit

    private:
        Store   &store_;
        Header   hdr_{};
        uint32_t cursor_ = 0;
        uint32_t slot_ = 0;
        bool     active_ = false;
    };

private:
    bool st_find();
    bool st_read(uint32_t off, void *buf, uint32_t len);
    bool st_erase(uint32_t off, uint32_t len);
    bool st_write(uint32_t off, const void *data, uint32_t len);
    bool st_map_blob(uint32_t off, uint32_t len, const uint8_t **ptr, uintptr_t *handle);
    void reload_header();

    const char *label_;
    const char *sim_env_;
    const char *sim_default_;
    Header hdr_{};
    bool   hdr_valid_ = false;
#ifdef ESP_PLATFORM
    const void *part_ = nullptr;   // esp_partition_t (kept void: no esp header here)
#else
    uint8_t *base_ = nullptr;
    int      fd_   = -1;
#endif
};

Store &mycard();
Store &lastcard();

}  // namespace cardstore
