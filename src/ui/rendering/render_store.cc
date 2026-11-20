#include "rendering/render_store.h"

#include <algorithm>
#include <atomic>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace XER::UI::Rendering {

namespace {
constexpr std::uint32_t kMagic = 0x58525354u;   // 'XRST'
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kMaxReadRetries = 64;
}  // namespace

RenderStore::~RenderStore() {
#ifdef _WIN32
    if (shared_ != nullptr) {
        UnmapViewOfFile(shared_);
    }
    if (mapping_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(mapping_));
    }
#else
    if (shared_ != nullptr) {
        munmap(shared_, sizeof(Header) + shared_->capacity);
    }
#endif
}

std::unique_ptr<RenderStore> RenderStore::Create(
    const std::string& name, std::uint32_t capacity_bytes,
    std::string* error_out) {
    const std::uint32_t total =
        static_cast<std::uint32_t>(sizeof(Header)) + capacity_bytes;
#ifdef _WIN32
    const std::wstring wname(name.begin(), name.end());
    HANDLE mapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, total,
        wname.c_str());
    if (mapping == nullptr) {
        if (error_out) *error_out = "CreateFileMapping failed";
        return nullptr;
    }
    void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (view == nullptr) {
        CloseHandle(mapping);
        if (error_out) *error_out = "MapViewOfFile failed";
        return nullptr;
    }
    auto store = std::unique_ptr<RenderStore>(new RenderStore());
    store->mapping_ = mapping;
    store->shared_ = static_cast<Header*>(view);
#else
    const int fd = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        if (error_out) *error_out = "shm_open(create) failed";
        return nullptr;
    }
    if (ftruncate(fd, total) != 0) {
        shm_unlink(name.c_str());
        close(fd);
        if (error_out) *error_out = "ftruncate failed";
        return nullptr;
    }
    void* view =
        mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (view == MAP_FAILED) {
        shm_unlink(name.c_str());
        if (error_out) *error_out = "mmap failed";
        return nullptr;
    }
    auto store = std::unique_ptr<RenderStore>(new RenderStore());
    store->shared_ = static_cast<Header*>(view);
#endif
    std::memset(store->shared_, 0, sizeof(Header));
    store->shared_->magic = kMagic;
    store->shared_->version = kVersion;
    store->shared_->capacity = capacity_bytes;
    store->shared_->alloc_top = 0;
    store->name_ = name;
    store->writer_ = true;
    return store;
}

std::unique_ptr<RenderStore> RenderStore::Open(
    const std::string& name, std::string* error_out) {
#ifdef _WIN32
    const std::wstring wname(name.begin(), name.end());
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE,
                                      wname.c_str());
    if (mapping == nullptr) {
        if (error_out) *error_out = "OpenFileMapping failed";
        return nullptr;
    }
    void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (view == nullptr) {
        CloseHandle(mapping);
        if (error_out) *error_out = "MapViewOfFile failed";
        return nullptr;
    }
    auto store = std::unique_ptr<RenderStore>(new RenderStore());
    store->mapping_ = mapping;
    store->shared_ = static_cast<Header*>(view);
#else
    const int fd = shm_open(name.c_str(), O_RDWR, 0600);
    if (fd < 0) {
        if (error_out) *error_out = "shm_open failed";
        return nullptr;
    }
    // Map just the header first to learn the capacity, then remap.
    void* probe = mmap(nullptr, sizeof(Header), PROT_READ | PROT_WRITE,
                       MAP_SHARED, fd, 0);
    if (probe == MAP_FAILED) {
        close(fd);
        if (error_out) *error_out = "mmap(header) failed";
        return nullptr;
    }
    const auto* hdr = static_cast<const Header*>(probe);
    const std::uint32_t total =
        sizeof(Header) + hdr->capacity;
    munmap(probe, sizeof(Header));
    void* view = mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_SHARED,
                      fd, 0);
    close(fd);
    if (view == MAP_FAILED) {
        if (error_out) *error_out = "mmap failed";
        return nullptr;
    }
    auto store = std::unique_ptr<RenderStore>(new RenderStore());
    store->shared_ = static_cast<Header*>(view);
#endif
    if (store->shared_->magic != kMagic ||
        store->shared_->version != kVersion) {
        if (error_out) *error_out = "store magic/version mismatch";
        return nullptr;
    }
    store->name_ = name;
    store->writer_ = false;
    return store;
}

RenderStore::Entry* RenderStore::Slot(std::uint32_t id) {
    return id < kMaxResources ? &shared_->entries[id] : nullptr;
}

const RenderStore::Entry* RenderStore::Slot(std::uint32_t id) const {
    return id < kMaxResources ? &shared_->entries[id] : nullptr;
}

void* RenderStore::BeginWrite(std::uint32_t id, Kind kind,
                              std::uint32_t width, std::uint32_t height,
                              std::uint32_t payload) {
    if (!writer_ || shared_ == nullptr) {
        return nullptr;
    }
    Entry* e = Slot(id);
    if (e == nullptr || payload == 0) {
        return nullptr;
    }
    // Grow-only arena: when the payload no longer fits the previous
    // allocation, bump the top and hand the slot a fresh extent.
    const bool needs_extent = e->kind == 0 || e->payload < payload;
    const std::uint32_t top = shared_->alloc_top;
    if (needs_extent && top + payload > shared_->capacity) {
        return nullptr;
    }
    // seqlock: flip seq odd BEFORE any Entry mutation — a racing Read()
    // that snapshots an even seq must never observe half-written state
    // (new offset + old payload/kind) and hand back a pointer into a
    // fresh, unwritten extent.
    e->seq = (e->seq + 1) | 1u;
    std::atomic_thread_fence(std::memory_order_release);
    if (needs_extent) {
        e->offset = top;
        shared_->alloc_top = top + payload;
    }
    e->kind = static_cast<std::uint32_t>(kind);
    e->width = width;
    e->height = height;
    e->payload = payload;
    return reinterpret_cast<std::uint8_t*>(shared_) + sizeof(Header) +
           e->offset;
}

void RenderStore::FinishWrite(std::uint32_t id) {
    if (!writer_ || shared_ == nullptr) {
        return;
    }
    Entry* e = Slot(id);
    if (e == nullptr || (e->seq & 1u) == 0) {
        return;
    }
    std::atomic_thread_fence(std::memory_order_release);
    e->seq += 1;  // odd -> even: payload is now stable for readers
}

void RenderStore::Free(std::uint32_t id) {
    if (!writer_ || shared_ == nullptr) {
        return;
    }
    Entry* e = Slot(id);
    if (e != nullptr) {
        e->seq |= 1u;
        e->kind = 0;
    }
}

bool RenderStore::Write(std::uint32_t id, Kind kind, std::uint32_t width,
                        std::uint32_t height, const void* data,
                        std::uint32_t bytes) {
    void* dst = BeginWrite(id, kind, width, height, bytes);
    if (dst == nullptr) {
        return false;
    }
    std::memcpy(dst, data, bytes);
    FinishWrite(id);
    return true;
}

RenderStore::View RenderStore::Read(std::uint32_t id) const {
    View v{};
    if (shared_ == nullptr) {
        return v;
    }
    const Entry* e = Slot(id);
    if (e == nullptr || e->kind == 0) {
        return v;
    }
    for (std::uint32_t retry = 0; retry < kMaxReadRetries; ++retry) {
        const std::uint32_t seq = e->seq;
        if ((seq & 1u) != 0) {
            continue;  // write in flight — spin briefly
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        if (e->seq == seq && e->kind != 0 &&
            e->offset + e->payload <= shared_->capacity) {
            v.info.kind = static_cast<Kind>(e->kind);
            v.info.width = e->width;
            v.info.height = e->height;
            v.info.payload = e->payload;
            v.info.seq = seq;
            v.data = reinterpret_cast<const std::uint8_t*>(shared_) +
                     sizeof(Header) + e->offset;
            return v;
        }
    }
    return v;
}

std::uint32_t RenderStore::Seq(std::uint32_t id) const {
    const Entry* e = shared_ ? Slot(id) : nullptr;
    return e ? e->seq : 0;
}

}  // namespace XER::UI::Rendering
