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

Header s_hdr;        // RAM cache of the on-flash header (read at mount / commit)
bool   s_hdr_valid;  // s_hdr holds a structurally-checked header

uint32_t align_up(uint32_t x, uint32_t a) { return (x + a - 1) & ~(a - 1); }

uint32_t header_payload_crc(const Header *h) {
    const uint8_t *p = reinterpret_cast<const uint8_t *>(h) + offsetof(Header, blobs);
    return nckpdf::crc32(p, sizeof(Header) - offsetof(Header, blobs));
}

// ---- Flash backend ----------------------------------------------------------
// st_read copies bytes out (header/PDF); st_erase/st_write mutate flash;
// st_map_blob/st_unmap_blob map a single blob for MMIO display. Device =
// esp_partition (no wholesale mmap); simulator = a host file mmap'd once.

#ifdef ESP_PLATFORM

const esp_partition_t *s_part = nullptr;

bool st_find() {
    if (s_part) return true;
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "mycard");
    return s_part != nullptr;
}
bool st_read(uint32_t off, void *buf, uint32_t len) {
    return esp_partition_read(s_part, off, buf, len) == ESP_OK;
}
bool st_erase(uint32_t off, uint32_t len) {
    return esp_partition_erase_range(s_part, off, align_up(len, kSector)) == ESP_OK;
}
bool st_write(uint32_t off, const void *data, uint32_t len) {
    // Non-encrypted partitions accept unaligned length; write exactly `len`.
    return esp_partition_write(s_part, off, data, len) == ESP_OK;
}
bool st_map_blob(uint32_t off, uint32_t len, const uint8_t **ptr, uintptr_t *handle) {
    const void *p = nullptr;
    esp_partition_mmap_handle_t h = 0;
    if (esp_partition_mmap(s_part, off, len, ESP_PARTITION_MMAP_DATA, &p, &h) != ESP_OK) return false;
    *ptr = static_cast<const uint8_t *>(p);
    *handle = static_cast<uintptr_t>(h);
    return true;
}
void st_unmap_blob(uintptr_t handle) {
    if (handle) esp_partition_munmap(static_cast<esp_partition_mmap_handle_t>(handle));
}

#else  // simulator: a 2MB backing file, mmap'd once (host has no DROM limit)

uint8_t *s_base = nullptr;
int      s_fd   = -1;

const char *img_path() {
    const char *e = getenv("SIMULATOR_MYCARD_PATH");
    return e ? e : "simulator/mycard.img";
}
bool st_find() {
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
bool st_read(uint32_t off, void *buf, uint32_t len) {
    if (!s_base) return false;
    memcpy(buf, s_base + off, len);
    return true;
}
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
bool st_map_blob(uint32_t off, uint32_t len, const uint8_t **ptr, uintptr_t *handle) {
    (void)len;
    if (!s_base) return false;
    *ptr = s_base + off;
    *handle = 0;  // view into the persistent host mapping; nothing to unmap
    return true;
}
void st_unmap_blob(uintptr_t) {}

#endif

void reload_header() {
    s_hdr_valid = false;
    if (!st_read(0, &s_hdr, sizeof(s_hdr))) return;
    if (s_hdr.magic != kMagic || s_hdr.version != kVersion || s_hdr.blob_count != BLOB_COUNT) return;
    if (header_payload_crc(&s_hdr) != s_hdr.header_crc) return;
    s_hdr_valid = true;
}

}  // namespace

// ---- MappedImage ------------------------------------------------------------

void MappedImage::release() {
    st_unmap_blob(handle_);
    handle_ = 0;
    view_ = L8View{};
}
MappedImage::~MappedImage() { release(); }
MappedImage::MappedImage(MappedImage &&o) noexcept : view_(o.view_), handle_(o.handle_) {
    o.view_ = L8View{};
    o.handle_ = 0;
}
MappedImage &MappedImage::operator=(MappedImage &&o) noexcept {
    if (this != &o) {
        release();
        view_ = o.view_;
        handle_ = o.handle_;
        o.view_ = L8View{};
        o.handle_ = 0;
    }
    return *this;
}

// ---- Read API ---------------------------------------------------------------

bool Store::mount() {
    if (!st_find()) return false;
    reload_header();
    return true;
}

const Header *Store::header() { return s_hdr_valid ? &s_hdr : nullptr; }

bool Store::available() { return s_hdr_valid; }

uint32_t Store::blob_len(BlobId id) {
    return (s_hdr_valid && id < BLOB_COUNT) ? s_hdr.blobs[id].length : 0;
}

bool Store::read_blob(BlobId id, void *buf, uint32_t len) {
    if (!s_hdr_valid || id >= BLOB_COUNT) return false;
    const Blob &b = s_hdr.blobs[id];
    if (!b.length) return false;
    return st_read(b.offset, buf, len < b.length ? len : b.length);
}

uint32_t Store::read_blob_range(BlobId id, uint32_t offset, void *buf, uint32_t len) {
    if (!s_hdr_valid || id >= BLOB_COUNT) return 0;
    const Blob &b = s_hdr.blobs[id];
    if (offset >= b.length) return 0;
    uint32_t n = b.length - offset;
    if (n > len) n = len;
    return st_read(b.offset + offset, buf, n) ? n : 0;
}

MappedImage Store::map_image(BlobId id) {
    MappedImage m;
    if (!s_hdr_valid || id >= BLOB_COUNT) return m;
    const Blob &b = s_hdr.blobs[id];
    if (b.format != FMT_L8 || !b.length) return m;
    const uint8_t *p = nullptr;
    if (!st_map_blob(b.offset, b.length, &p, &m.handle_)) return m;
    m.view_ = L8View{p, b.w, b.h, b.stride, b.levels};
    return m;
}

// ---- Writer -----------------------------------------------------------------

bool Store::Writer::begin() {
    if (active_) return false;
    if (!st_find()) return false;
    s_hdr_valid = false;                        // header about to be invalidated
    if (!st_erase(0, kSector)) return false;    // wipe the header sector first -> magic gone
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
    reload_header();  // refresh the RAM cache from the freshly written header
    return ok;
}

void Store::Writer::abort() {
    if (!active_) return;
    active_ = false;
    reload_header();  // header sector is erased -> available() == false
}

Store::Writer::~Writer() {
    if (active_) abort();
}

}  // namespace mycard
