/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "CardStore.hpp"
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

namespace cardstore {

namespace {

constexpr uint32_t kSector     = 4096;
constexpr uint32_t kPartSize   = 2u * 1024 * 1024;
constexpr uint32_t kDataBase   = kSector;  // blobs start after the header sector
constexpr uint32_t kSlotStride = 128;      // header slots within the header sector
constexpr uint32_t kSlotCount  = kSector / kSlotStride;

uint32_t align_up(uint32_t x, uint32_t a) { return (x + a - 1) & ~(a - 1); }

uint32_t header_payload_crc(const Header *h) {
    const uint8_t *p = reinterpret_cast<const uint8_t *>(h) + offsetof(Header, blobs);
    return nckpdf::crc32(p, sizeof(Header) - offsetof(Header, blobs));
}

bool header_valid(const Header *h) {
    return h->magic == kMagic && h->version == kVersion && h->blob_count == BLOB_COUNT &&
           header_payload_crc(h) == h->header_crc;
}

}  // namespace

// ---- Flash backend ----------------------------------------------------------
// st_read copies bytes out (header/PDF); st_erase/st_write mutate flash;
// st_map_blob maps a single blob for MMIO display. Device = esp_partition (no
// wholesale mmap); simulator = a host file mmap'd once.

#ifdef ESP_PLATFORM

static void st_unmap_blob(uintptr_t handle) {
    if (handle) esp_partition_munmap(static_cast<esp_partition_mmap_handle_t>(handle));
}
bool Store::st_find() {
    if (part_) return true;
    part_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label_);
    return part_ != nullptr;
}
bool Store::st_read(uint32_t off, void *buf, uint32_t len) {
    return esp_partition_read(static_cast<const esp_partition_t *>(part_), off, buf, len) == ESP_OK;
}
bool Store::st_erase(uint32_t off, uint32_t len) {
    return esp_partition_erase_range(static_cast<const esp_partition_t *>(part_), off,
                                     align_up(len, kSector)) == ESP_OK;
}
bool Store::st_write(uint32_t off, const void *data, uint32_t len) {
    // Non-encrypted partitions accept unaligned length; write exactly `len`.
    return esp_partition_write(static_cast<const esp_partition_t *>(part_), off, data, len) == ESP_OK;
}
bool Store::st_map_blob(uint32_t off, uint32_t len, const uint8_t **ptr, uintptr_t *handle) {
    const void *p = nullptr;
    esp_partition_mmap_handle_t h = 0;
    if (esp_partition_mmap(static_cast<const esp_partition_t *>(part_), off, len,
                           ESP_PARTITION_MMAP_DATA, &p, &h) != ESP_OK) return false;
    *ptr = static_cast<const uint8_t *>(p);
    *handle = static_cast<uintptr_t>(h);
    return true;
}

#else  // simulator: a 2MB backing file, mmap'd once (host has no DROM limit)

static void st_unmap_blob(uintptr_t) {}

bool Store::st_find() {
    if (base_) return true;
    const char *env = sim_env_ ? getenv(sim_env_) : nullptr;
    const char *path = env ? env : sim_default_;
    fd_ = open(path, O_RDWR | O_CREAT, 0644);
    if (fd_ < 0) return false;
    struct stat st;
    if (fstat(fd_, &st) == 0 && static_cast<uint32_t>(st.st_size) < kPartSize) {
        ftruncate(fd_, kPartSize);  // new file: mirror erased flash (0xFF) below
        void *m = mmap(nullptr, kPartSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (m != MAP_FAILED) {
            memset(m, 0xFF, kPartSize);
            msync(m, kPartSize, MS_SYNC);
            munmap(m, kPartSize);
        }
    }
    void *m = mmap(nullptr, kPartSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (m == MAP_FAILED) { close(fd_); fd_ = -1; return false; }
    base_ = static_cast<uint8_t *>(m);
    return true;
}
bool Store::st_read(uint32_t off, void *buf, uint32_t len) {
    if (!base_) return false;
    memcpy(buf, base_ + off, len);
    return true;
}
bool Store::st_erase(uint32_t off, uint32_t len) {
    memset(base_ + off, 0xFF, align_up(len, kSector));
    msync(base_ + off, align_up(len, kSector), MS_SYNC);
    return true;
}
bool Store::st_write(uint32_t off, const void *data, uint32_t len) {
    memcpy(base_ + off, data, len);
    msync(base_ + off, align_up(len, kSector), MS_SYNC);
    return true;
}
bool Store::st_map_blob(uint32_t off, uint32_t len, const uint8_t **ptr, uintptr_t *handle) {
    (void)len;
    if (!base_) return false;
    *ptr = base_ + off;
    *handle = 0;  // view into the persistent host mapping; nothing to unmap
    return true;
}

#endif

// The last valid slot wins: commit() appends, so later slots supersede earlier.
void Store::reload_header() {
    hdr_valid_ = false;
    Header h;
    for (uint32_t s = 0; s < kSlotCount; s++) {
        if (!st_read(s * kSlotStride, &h, sizeof(h))) return;
        if (h.magic == 0xFFFFFFFFu) break;  // erased: no further slots written
        if (!header_valid(&h)) continue;
        hdr_ = h;
        hdr_valid_ = true;
    }
}

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

Store::Store(const char *label, const char *sim_env, const char *sim_default)
    : label_(label), sim_env_(sim_env), sim_default_(sim_default) {
    (void)label_;
    (void)sim_env_;
    (void)sim_default_;
}

bool Store::mount() {
    if (!st_find()) return false;
    reload_header();
    return true;
}

uint32_t Store::blob_len(BlobId id) const {
    return (hdr_valid_ && id < BLOB_COUNT) ? hdr_.blobs[id].length : 0;
}

bool Store::read_blob(BlobId id, void *buf, uint32_t len) {
    if (!hdr_valid_ || id >= BLOB_COUNT) return false;
    const Blob &b = hdr_.blobs[id];
    if (!b.length) return false;
    return st_read(b.offset, buf, len < b.length ? len : b.length);
}

uint32_t Store::read_blob_range(BlobId id, uint32_t offset, void *buf, uint32_t len) {
    if (!hdr_valid_ || id >= BLOB_COUNT) return 0;
    const Blob &b = hdr_.blobs[id];
    if (offset >= b.length) return 0;
    uint32_t n = b.length - offset;
    if (n > len) n = len;
    return st_read(b.offset + offset, buf, n) ? n : 0;
}

MappedImage Store::map_image(BlobId id) {
    MappedImage m;
    if (!hdr_valid_ || id >= BLOB_COUNT) return m;
    const Blob &b = hdr_.blobs[id];
    if (b.format != FMT_L8 || !b.length) return m;
    const uint8_t *p = nullptr;
    if (!st_map_blob(b.offset, b.length, &p, &m.handle_)) return m;
    m.view_ = L8View{p, b.w, b.h, b.stride, b.levels};
    return m;
}

// ---- Writer -----------------------------------------------------------------

bool Store::Writer::begin() {
    if (active_) return false;
    if (!store_.st_find()) return false;
    store_.hdr_valid_ = false;                        // header about to be invalidated
    if (!store_.st_erase(0, kSector)) return false;   // wipe the header sector first -> magic gone
    hdr_ = Header{};
    cursor_ = kDataBase;
    slot_ = 0;
    active_ = true;
    return true;
}

bool Store::Writer::write_blob(BlobId id, const void *data, uint32_t len, const Blob *meta) {
    if (!active_ || id >= BLOB_COUNT || !len) return false;
    uint32_t off = cursor_;  // sector-aligned
    if (off + len > kPartSize) return false;
    if (!store_.st_erase(off, len)) return false;
    if (!store_.st_write(off, data, len)) return false;

    Blob &b = hdr_.blobs[id];
    if (meta) b = *meta; else b = Blob{};
    b.offset = off;
    b.length = len;
    b.crc = nckpdf::crc32(static_cast<const uint8_t *>(data), len);

    cursor_ = align_up(off + len, kSector);
    return true;
}

bool Store::Writer::commit() {
    if (!active_ || slot_ >= kSlotCount) return false;
    hdr_.magic = kMagic;
    hdr_.version = kVersion;
    hdr_.blob_count = BLOB_COUNT;
    hdr_.header_crc = header_payload_crc(&hdr_);
    bool ok = store_.st_write(slot_ * kSlotStride, &hdr_, sizeof(hdr_));
    slot_++;
    store_.reload_header();  // refresh the RAM cache from the freshly written header
    return ok;
}

void Store::Writer::abort() {
    if (!active_ || slot_ > 0) return;  // committed slots cannot be taken back
    active_ = false;
    store_.reload_header();  // header sector is erased -> available() == false
}

Store::Writer::~Writer() {
    if (active_ && slot_ == 0) abort();
}

// ---- Instances ---------------------------------------------------------------

Store &mycard() {
    static Store s("mycard", "SIMULATOR_MYCARD_PATH", "simulator/mycard.img");
    return s;
}

Store &lastcard() {
    static Store s("lastcard", "SIMULATOR_LASTCARD_PATH", "simulator/lastcard.img");
    return s;
}

}  // namespace cardstore
