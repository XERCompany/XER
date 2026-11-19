#include "shared_memory_channel.h"

#include <algorithm>
#include <cstring>

#ifdef _WIN32
#include <windows.h>

#include "helper/string_utils.h"

namespace XER::Cache {
namespace detail {

// One direction of the channel. The ring's byte storage lives at
// (SmcSharedLayout + sizeof(SmcSharedLayout) + storage_offset).
struct SmcRing {
    volatile long capacity;
    volatile long storage_offset;
    volatile long long write_index;
    volatile long long read_index;
};

struct SmcSharedLayout {
    volatile long magic;
    volatile long version;
    volatile long closed;
    volatile long capacity_bytes;
    SmcRing a2b;
    SmcRing b2a;
};

}  // namespace detail
namespace {

using detail::SmcRing;
using detail::SmcSharedLayout;

constexpr uint32_t kChannelMagic = 0x584D4356;  // 'XMCV'
constexpr uint32_t kChannelVersion = 1;

std::wstring ObjName(const wchar_t* prefix, const std::string& name) {
    std::wstring w = prefix + Helper::String::Utf8ToWide(name);
    for (auto& ch : w) {
        if (ch == L'\\' || ch == L'/') {
            ch = L'_';
        }
    }
    return w;
}

uint8_t* RingData(SmcSharedLayout* s, const SmcRing* r) {
    return reinterpret_cast<uint8_t*>(s) + sizeof(SmcSharedLayout) +
           static_cast<uint32_t>(r->storage_offset);
}

// Messages: [u32 length][payload]; length 0xFFFFFFFF marks a wrap so
// the reader skips to offset 0.
void WriteRing(SmcSharedLayout* s, SmcRing* r, const void* data,
               uint32_t bytes) {
    const uint32_t cap = static_cast<uint32_t>(r->capacity);
    uint8_t* ring = RingData(s, r);
    const uint64_t w = static_cast<uint64_t>(r->write_index);
    uint32_t pos = static_cast<uint32_t>(w % cap);
    const uint32_t record = bytes + 4;
    if (pos + record + 4 > cap) {
        const uint32_t wrap = 0xFFFFFFFFu;
        if (pos + 4 <= cap) {
            std::memcpy(ring + pos, &wrap, 4);
        } else {
            std::memcpy(ring + pos, &wrap, cap - pos);
            std::memcpy(ring,
                        reinterpret_cast<const uint8_t*>(&wrap) +
                            (cap - pos),
                        4 - (cap - pos));
        }
        InterlockedAdd64(&r->write_index, cap - pos);
        pos = 0;
    }
    std::memcpy(ring + pos, &bytes, 4);
    pos += 4;
    const uint32_t first = std::min(bytes, cap - pos);
    std::memcpy(ring + pos, data, first);
    if (first < bytes) {
        std::memcpy(ring,
                    static_cast<const uint8_t*>(data) + first,
                    bytes - first);
    }
    InterlockedAdd64(&r->write_index, record);
}

// Returns message size or -1 when nothing complete is buffered.
int64_t ReadRing(SmcSharedLayout* s, SmcRing* r,
                 std::vector<uint8_t>* out) {
    const uint32_t cap = static_cast<uint32_t>(r->capacity);
    uint8_t* ring = RingData(s, r);
    uint64_t rd = static_cast<uint64_t>(
        InterlockedCompareExchange64(&r->read_index, 0, 0));
    const uint64_t wr = static_cast<uint64_t>(
        InterlockedCompareExchange64(&r->write_index, 0, 0));
    if (wr - rd < 4) {
        return -1;
    }
    uint32_t pos = static_cast<uint32_t>(rd % cap);
    uint32_t len = 0;
    if (pos + 4 <= cap) {
        std::memcpy(&len, ring + pos, 4);
    } else {
        std::memcpy(&len, ring + pos, cap - pos);
        std::memcpy(reinterpret_cast<uint8_t*>(&len) + (cap - pos), ring,
                    4 - (cap - pos));
    }
    if (len == 0xFFFFFFFFu) {
        InterlockedAdd64(&r->read_index, cap - pos);
        rd += cap - pos;
        pos = 0;
        if (wr - rd < 4) {
            return -1;
        }
        std::memcpy(&len, ring, 4);
    }
    if (wr - rd < 4 + len) {
        return -1;
    }
    InterlockedAdd64(&r->read_index, 4);
    pos = static_cast<uint32_t>((rd + 4) % cap);
    out->resize(len);
    const uint32_t first = std::min(len, cap - pos);
    std::memcpy(out->data(), ring + pos, first);
    if (first < len) {
        std::memcpy(out->data() + first, ring, len - first);
    }
    InterlockedAdd64(&r->read_index, len);
    return static_cast<int64_t>(len);
}

uint64_t RingSpace(const SmcRing* r) {
    const uint64_t w = static_cast<uint64_t>(
        InterlockedCompareExchange64(
            const_cast<volatile long long*>(&r->write_index), 0, 0));
    const uint64_t rd = static_cast<uint64_t>(
        InterlockedCompareExchange64(
            const_cast<volatile long long*>(&r->read_index), 0, 0));
    return static_cast<uint64_t>(r->capacity) - (w - rd);
}

}  // namespace

SharedMemoryChannel::~SharedMemoryChannel() {
    if (host_) {
        SignalClose();
    }
    if (shared_ != nullptr) {
        UnmapViewOfFile(shared_);
    }
    for (HANDLE h : {mapping_, in_event_, out_event_, in_space_,
                     out_space_}) {
        if (h != nullptr) {
            CloseHandle(h);
        }
    }
}

std::unique_ptr<SharedMemoryChannel> SharedMemoryChannel::Create(
    const std::string& name, uint32_t capacity_bytes,
    std::string* error_out) {
    if (name.empty() || capacity_bytes < 256) {
        if (error_out) *error_out = "invalid channel config";
        return nullptr;
    }
    const size_t bytes =
        sizeof(SmcSharedLayout) + 2 * static_cast<size_t>(capacity_bytes);
    HANDLE mapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
        static_cast<DWORD>(bytes),
        ObjName(L"Local\\XER.SMC.", name).c_str());
    if (mapping == nullptr) {
        if (error_out) *error_out = "CreateFileMapping failed";
        return nullptr;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mapping);
        if (error_out) *error_out = "channel already exists: " + name;
        return nullptr;
    }
    auto* shared = static_cast<SmcSharedLayout*>(MapViewOfFile(
        mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes));
    if (shared == nullptr) {
        CloseHandle(mapping);
        if (error_out) *error_out = "MapViewOfFile failed";
        return nullptr;
    }
    std::memset(shared, 0, sizeof(SmcSharedLayout));
    shared->magic = kChannelMagic;
    shared->version = kChannelVersion;
    shared->capacity_bytes = static_cast<long>(capacity_bytes);
    shared->a2b.capacity = static_cast<long>(capacity_bytes);
    shared->a2b.storage_offset = 0;
    shared->b2a.capacity = static_cast<long>(capacity_bytes);
    shared->b2a.storage_offset = static_cast<long>(capacity_bytes);

    auto ch = std::unique_ptr<SharedMemoryChannel>(
        new SharedMemoryChannel());
    ch->name_ = name;
    ch->host_ = true;
    ch->mapping_ = mapping;
    ch->shared_ = shared;
    ch->out_ring_ = &shared->a2b;
    ch->in_ring_ = &shared->b2a;
    ch->in_event_ = CreateEventW(
        nullptr, TRUE, FALSE,
        ObjName(L"Local\\XER.SMC.A2B.", name).c_str());
    ch->out_event_ = CreateEventW(
        nullptr, TRUE, FALSE,
        ObjName(L"Local\\XER.SMC.B2A.", name).c_str());
    ch->in_space_ = CreateEventW(
        nullptr, TRUE, TRUE,
        ObjName(L"Local\\XER.SMC.A2BS.", name).c_str());
    ch->out_space_ = CreateEventW(
        nullptr, TRUE, TRUE,
        ObjName(L"Local\\XER.SMC.B2AS.", name).c_str());
    return ch;
}

std::unique_ptr<SharedMemoryChannel> SharedMemoryChannel::Open(
    const std::string& name, std::string* error_out) {
    HANDLE mapping = OpenFileMappingW(
        FILE_MAP_ALL_ACCESS, FALSE,
        ObjName(L"Local\\XER.SMC.", name).c_str());
    if (mapping == nullptr) {
        if (error_out) *error_out = "channel not found: " + name;
        return nullptr;
    }
    auto* shared = static_cast<SmcSharedLayout*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
    if (shared == nullptr ||
        static_cast<uint32_t>(InterlockedCompareExchange(&shared->magic,
                                                         0, 0)) !=
            kChannelMagic) {
        if (shared) UnmapViewOfFile(shared);
        CloseHandle(mapping);
        if (error_out) *error_out = "not a shared channel: " + name;
        return nullptr;
    }
    auto ch = std::unique_ptr<SharedMemoryChannel>(
        new SharedMemoryChannel());
    ch->name_ = name;
    ch->mapping_ = mapping;
    ch->shared_ = shared;
    // Client writes B->A and reads A->B — mirror of the host.
    ch->out_ring_ = &shared->b2a;
    ch->in_ring_ = &shared->a2b;
    ch->in_event_ = OpenEventW(
        EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE,
        ObjName(L"Local\\XER.SMC.B2A.", name).c_str());
    ch->out_event_ = OpenEventW(
        EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE,
        ObjName(L"Local\\XER.SMC.A2B.", name).c_str());
    ch->in_space_ = OpenEventW(
        EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE,
        ObjName(L"Local\\XER.SMC.B2AS.", name).c_str());
    ch->out_space_ = OpenEventW(
        EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE,
        ObjName(L"Local\\XER.SMC.A2BS.", name).c_str());
    return ch;
}

bool SharedMemoryChannel::Send(const void* data, uint32_t bytes,
                               uint32_t timeout_ms) {
    if (shared_ == nullptr || out_ring_ == nullptr ||
        (bytes > 0 && data == nullptr)) {
        return false;
    }
    const uint64_t cap = static_cast<uint64_t>(out_ring_->capacity);
    const uint64_t need = bytes + 8;  // header + worst-case wrap pad
    if (need >= cap) {
        return false;
    }
    const uint64_t deadline =
        timeout_ms == INFINITE ? UINT64_MAX
                               : GetTickCount64() + timeout_ms;
    while (RingSpace(out_ring_) < need) {
        if (IsClosed() || GetTickCount64() >= deadline) {
            return false;
        }
        if (out_space_ != nullptr) {
            WaitForSingleObject(out_space_, 10);
        }
    }
    WriteRing(shared_, out_ring_, data, bytes);
    if (out_event_ != nullptr) {
        SetEvent(out_event_);
    }
    return true;
}

bool SharedMemoryChannel::TryRecv(std::vector<uint8_t>* out) {
    if (shared_ == nullptr || in_ring_ == nullptr || out == nullptr) {
        return false;
    }
    const int64_t n = ReadRing(shared_, in_ring_, out);
    if (n < 0) {
        return false;
    }
    if (in_space_ != nullptr) {
        SetEvent(in_space_);
    }
    return true;
}

bool SharedMemoryChannel::Recv(std::vector<uint8_t>* out,
                               uint32_t timeout_ms) {
    const uint64_t deadline =
        timeout_ms == INFINITE ? UINT64_MAX
                               : GetTickCount64() + timeout_ms;
    while (true) {
        if (TryRecv(out)) {
            return true;
        }
        if (IsClosed() || GetTickCount64() >= deadline) {
            return false;
        }
        if (in_event_ != nullptr) {
            ResetEvent(in_event_);
            if (TryRecv(out)) {
                if (in_space_ != nullptr) {
                    SetEvent(in_space_);
                }
                return true;
            }
            WaitForSingleObject(in_event_, 10);
        } else {
            Sleep(1);
        }
    }
}

uint32_t SharedMemoryChannel::InboundAvailable() const {
    if (shared_ == nullptr || in_ring_ == nullptr) {
        return 0;
    }
    const int64_t w = InterlockedCompareExchange64(
        const_cast<volatile long long*>(&in_ring_->write_index), 0, 0);
    const int64_t r = InterlockedCompareExchange64(
        const_cast<volatile long long*>(&in_ring_->read_index), 0, 0);
    return static_cast<uint32_t>(std::max<int64_t>(0, w - r));
}

void SharedMemoryChannel::SignalClose() {
    if (shared_ != nullptr) {
        InterlockedExchange(
            const_cast<volatile long*>(&shared_->closed), 1);
    }
    for (HANDLE h : {in_event_, out_event_, in_space_, out_space_}) {
        if (h != nullptr) {
            SetEvent(h);
        }
    }
}

bool SharedMemoryChannel::IsClosed() const {
    return shared_ == nullptr ||
           InterlockedCompareExchange(
               const_cast<volatile long*>(&shared_->closed), 0, 0) != 0;
}

}  // namespace XER::Cache

#else

namespace XER::Cache {
namespace detail {
struct SmcRing {};
struct SmcSharedLayout {};
}  // namespace detail

SharedMemoryChannel::~SharedMemoryChannel() = default;

std::unique_ptr<SharedMemoryChannel> SharedMemoryChannel::Create(
    const std::string&, uint32_t, std::string* e) {
    if (e) *e = "shared memory channels require Windows";
    return nullptr;
}
std::unique_ptr<SharedMemoryChannel> SharedMemoryChannel::Open(
    const std::string&, std::string* e) {
    if (e) *e = "shared memory channels require Windows";
    return nullptr;
}
bool SharedMemoryChannel::Send(const void*, uint32_t, uint32_t) {
    return false;
}
bool SharedMemoryChannel::TryRecv(std::vector<uint8_t>*) { return false; }
bool SharedMemoryChannel::Recv(std::vector<uint8_t>*, uint32_t) {
    return false;
}
uint32_t SharedMemoryChannel::InboundAvailable() const { return 0; }
void SharedMemoryChannel::SignalClose() {}
bool SharedMemoryChannel::IsClosed() const { return true; }

}  // namespace XER::Cache

#endif
