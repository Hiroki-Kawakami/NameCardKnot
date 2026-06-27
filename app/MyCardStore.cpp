/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "MyCardStore.hpp"
#include "namecard_pdf.hpp"  // nckpdf::crc32
#include <cstring>

#ifdef ESP_PLATFORM
#include "esp_partition.h"
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#endif

namespace mycard {

namespace {

constexpr uint32_t kSector   = 4096;
constexpr uint32_t kPartSize = 2u * 1024 * 1024;
constexpr uint32_t kDataBase = kSector;  // blobs start after the header sector

uint32_t align_up(uint32_t x, uint32_t a) { return (x + a - 1) & ~(a - 1); }

uint32_t header_payload_crc(const Header *h) {
    const uint8_t *p = reinterpret_cast<const uint8_t *>(h) + offsetof(Header, blobs);
    return nckpdf::crc32(p, sizeof(Header) - offsetof(Header, blobs));
}

// ---- Flash backend ----------------------------------------------------------
// st_map/st_unmap manage the read mapping (s_base); st_erase/st_write mutate
// flash (valid only while unmapped, on device). Offsets/lengths are pre-aligned
// by the Writer. Device = esp_partition + mmap; simulator = a host file mmap.

#ifdef ESP_PLATFORM

const esp_partition_t      *s_part   = nullptr;
esp_partition_mmap_handle_t s_handle = 0;
const uint8_t              *s_base   = nullptr;

bool st_find() {
    if (s_part) return true;
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "mycard");
    return s_part != nullptr;
}
bool st_map() {
    if (s_base) return true;
    const void *ptr = nullptr;
    if (esp_partition_mmap(s_part, 0, s_part->size, ESP_PARTITION_MMAP_DATA, &ptr, &s_handle) != ESP_OK)
        return false;
    s_base = static_cast<const uint8_t *>(ptr);
    return true;
}
void st_unmap() {
    if (!s_base) return;
    esp_partition_munmap(s_handle);
    s_base = nullptr;
    s_handle = 0;
}
bool st_erase(uint32_t off, uint32_t len) {
    return esp_partition_erase_range(s_part, off, align_up(len, kSector)) == ESP_OK;
}
bool st_write(uint32_t off, const void *data, uint32_t len) {
    // Non-encrypted partitions accept unaligned length; write exactly `len` so we
    // never read past the caller's buffer.
    return esp_partition_write(s_part, off, data, len) == ESP_OK;
}

#else  // simulator

uint8_t *s_base = nullptr;
int      s_fd   = -1;

const char *img_path() {
    const char *e = getenv("SIMULATOR_MYCARD_PATH");
    return e ? e : "simulator/mycard.img";
}
bool st_find() { return true; }
bool st_map() {
    if (s_base) return true;
    s_fd = open(img_path(), O_RDWR | O_CREAT, 0644);
    if (s_fd < 0) return false;
    struct stat st;
    if (fstat(s_fd, &st) == 0 && static_cast<uint32_t>(st.st_size) < kPartSize) {
        ftruncate(s_fd, kPartSize);  // new file: mirror erased flash (0xFF) below
        void *m = mmap(nullptr, kPartSize, PROT_READ | PROT_WRITE, MAP_SHARED, s_fd, 0);
        if (m != MAP_FAILED) {
            memset(m, 0xFF, kPartSize);
            msync(m, kPartSize, MS_SYNC);
            munmap(m, kPartSize);
        }
    }
    void *m = mmap(nullptr, kPartSize, PROT_READ | PROT_WRITE, MAP_SHARED, s_fd, 0);
    if (m == MAP_FAILED) { close(s_fd); s_fd = -1; return false; }
    s_base = static_cast<uint8_t *>(m);
    return true;
}
void st_unmap() {}  // MAP_SHARED stays valid across writes; no remap needed
bool st_erase(uint32_t off, uint32_t len) {
    memset(s_base + off, 0xFF, align_up(len, kSector));
    msync(s_base + off, align_up(len, kSector), MS_SYNC);
    return true;
}
bool st_write(uint32_t off, const void *data, uint32_t len) {
    memcpy(s_base + off, data, len);
    msync(s_base + off, align_up(len, kSector), MS_SYNC);
    return true;
}

#endif

}  // namespace

// ---- Read API ---------------------------------------------------------------

bool Store::mount() {
    if (s_base) return true;
    if (!st_find()) return false;
    return st_map();
}

const Header *Store::header() {
    return s_base ? reinterpret_cast<const Header *>(s_base) : nullptr;
}

bool Store::available() {
    const Header *h = header();
    if (!h || h->magic != kMagic || h->version != kVersion || h->blob_count != BLOB_COUNT)
        return false;
    return header_payload_crc(h) == h->header_crc;
}

const uint8_t *Store::blob(BlobId id) {
    const Header *h = header();
    if (!h || id >= BLOB_COUNT || !h->blobs[id].length) return nullptr;
    return s_base + h->blobs[id].offset;
}

uint32_t Store::blob_len(BlobId id) {
    const Header *h = header();
    return (h && id < BLOB_COUNT) ? h->blobs[id].length : 0;
}

L8View Store::image_view(BlobId id) {
    if (!available()) return {};
    const Blob &b = header()->blobs[id];
    const uint8_t *p = blob(id);
    if (!p || b.format != FMT_L8) return {};
    return L8View{p, b.w, b.h, b.stride, b.levels};
}

// ---- Writer -----------------------------------------------------------------

bool Store::Writer::begin() {
    if (active_) return false;
    if (!st_find()) return false;
    st_unmap();                                // invalidate the read mapping for the rewrite
    if (!st_erase(0, kSector)) return false;   // wipe the header sector first -> magic gone
    hdr_ = Header{};
    cursor_ = kDataBase;
    active_ = true;
    return true;
}

bool Store::Writer::write_blob(BlobId id, const void *data, uint32_t len, const Blob *meta) {
    if (!active_ || id >= BLOB_COUNT || !len) return false;
    uint32_t off = cursor_;  // sector-aligned
    if (off + len > kPartSize) return false;
    if (!st_erase(off, len)) return false;
    if (!st_write(off, data, len)) return false;

    Blob &b = hdr_.blobs[id];
    if (meta) b = *meta; else b = Blob{};
    b.offset = off;
    b.length = len;
    b.crc = nckpdf::crc32(static_cast<const uint8_t *>(data), len);

    cursor_ = align_up(off + len, kSector);
    return true;
}

bool Store::Writer::commit() {
    if (!active_) return false;
    hdr_.magic = kMagic;
    hdr_.version = kVersion;
    hdr_.blob_count = BLOB_COUNT;
    hdr_.header_crc = header_payload_crc(&hdr_);
    bool ok = st_write(0, &hdr_, sizeof(hdr_));
    active_ = false;
    st_map();  // re-establish the read mapping for the new card
    return ok;
}

void Store::Writer::abort() {
    if (!active_) return;
    active_ = false;
    st_map();  // header sector is erased -> available() == false
}

Store::Writer::~Writer() {
    if (active_) abort();
}

}  // namespace mycard
