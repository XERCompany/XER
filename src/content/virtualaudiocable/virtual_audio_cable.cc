#include "virtualaudiocable/virtual_audio_cable.h"

#include <algorithm>
#include <cstring>

#ifdef _WIN32
#include <windows.h>

#include "helper/string_utils.h"

namespace XER::Audio::Core {
namespace {

constexpr uint32_t kCableMagic = 0x58434156;  // 'XVAC'
constexpr uint32_t kCableVersion = 1;
constexpr uint32_t kRegistrySlots = 64;
constexpr size_t kRegistryName = 64;

// Backslashes are legal in kernel object names (Local\ is a namespace
// prefix) but a cable name containing one would collide with that
// syntax — sanitize only the caller-controlled portion.
std::wstring SanitizeCableName(std::wstring w) {
    for (auto& ch : w) {
        if (ch == L'\\' || ch == L'/') {
            ch = L'_';
        }
    }
    return w;
}

std::wstring WideName(const wchar_t* prefix, const std::string& name) {
    return prefix + SanitizeCableName(Helper::String::Utf8ToWide(name));
}

std::wstring WideName(const wchar_t* prefix, const wchar_t* name) {
    return prefix + SanitizeCableName(name);
}

struct RegistrySlot {
    wchar_t name[kRegistryName];
    uint32_t pid;
    uint32_t active;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t capacity_frames;
};

struct RegistryMap {
    RegistrySlot slots[kRegistrySlots];
};

bool PidAlive(uint32_t pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h == nullptr) {
        return false;
    }
    DWORD code = STILL_ACTIVE;
    const bool ok = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return ok;
}

}  // namespace

// Shared-memory cable layout: fixed header followed by an interleaved
// float32 ring. Indices are monotonic frame counters; ring position is
// index % capacity. SPSC: one writer owns write_index, one reader owns
// read_index; cross-thread visibility uses Interlocked ops.
struct VirtualAudioCable::SharedLayout {
    volatile long magic;
    volatile long version;
    volatile long channels;
    volatile long sample_rate;
    volatile long capacity_frames;
    volatile long closed;
    volatile long long write_index;
    volatile long long read_index;
    // float samples[capacity * channels] follows.
};

namespace {

float* RingSamples(VirtualAudioCable::SharedLayout* s) {
    return reinterpret_cast<float*>(s + 1);
}

void CopyToRing(float* ring, uint32_t capacity, uint32_t channels,
                uint64_t write_index, const float* src, uint32_t frames) {
    const uint32_t pos = static_cast<uint32_t>(write_index % capacity);
    const uint32_t first = std::min(frames, capacity - pos);
    std::memcpy(ring + static_cast<size_t>(pos) * channels, src,
                static_cast<size_t>(first) * channels * sizeof(float));
    if (first < frames) {
        std::memcpy(ring, src + static_cast<size_t>(first) * channels,
                    static_cast<size_t>(frames - first) * channels *
                        sizeof(float));
    }
}

void CopyFromRing(float* dst, const float* ring, uint32_t capacity,
                  uint32_t channels, uint64_t read_index, uint32_t frames) {
    const uint32_t pos = static_cast<uint32_t>(read_index % capacity);
    const uint32_t first = std::min(frames, capacity - pos);
    std::memcpy(dst, ring + static_cast<size_t>(pos) * channels,
                static_cast<size_t>(first) * channels * sizeof(float));
    if (first < frames) {
        std::memcpy(dst + static_cast<size_t>(first) * channels, ring,
                    static_cast<size_t>(frames - first) * channels *
                        sizeof(float));
    }
}

}  // namespace

VirtualAudioCable::~VirtualAudioCable() {
    if (host_ && shared_ != nullptr) {
        SignalClose();
    }
    if (registry_view_ != nullptr) {
        // Unregister from the cable directory (view still held, so the
        // object is alive and the clear is visible to later Enumerate
        // calls before the last handle closes).
        HANDLE mtx = CreateMutexW(
            nullptr, FALSE, WideName(L"Local\\XER.VAC.Registry.", L"mutex").c_str());
        if (mtx != nullptr) {
            WaitForSingleObject(mtx, 2000);
            auto* map = static_cast<RegistryMap*>(registry_view_);
            for (auto& slot : map->slots) {
                if (slot.active != 0 &&
                    slot.pid == GetCurrentProcessId() &&
                    WideName(L"", config_.name) == slot.name) {
                    InterlockedExchange(
                        reinterpret_cast<volatile long*>(&slot.active),
                        0);
                }
            }
            ReleaseMutex(mtx);
            CloseHandle(mtx);
        }
        UnmapViewOfFile(registry_view_);
    }
    if (registry_map_ != nullptr) {
        CloseHandle(registry_map_);
    }
    if (shared_ != nullptr) {
        UnmapViewOfFile(shared_);
    }
    if (mapping_ != nullptr) {
        CloseHandle(mapping_);
    }
    if (data_event_ != nullptr) {
        CloseHandle(data_event_);
    }
    if (space_event_ != nullptr) {
        CloseHandle(space_event_);
    }
}

std::unique_ptr<VirtualAudioCable> VirtualAudioCable::Create(
    const VirtualCableConfig& config, std::string* error_out) {
    if (config.name.empty() || config.name.size() >= kRegistryName ||
        config.channels == 0 || config.channels > 64 ||
        config.sample_rate < 4000 || config.sample_rate > 384000 ||
        config.buffer_ms == 0) {
        if (error_out) *error_out = "invalid cable configuration";
        return nullptr;
    }
    auto cable = std::unique_ptr<VirtualAudioCable>(new VirtualAudioCable());
    cable->config_ = config;
    cable->host_ = true;
    cable->can_write_ = true;
    cable->can_read_ = true;

    const uint32_t capacity =
        std::max<uint32_t>(64, config.sample_rate * config.buffer_ms / 1000);
    const size_t bytes =
        sizeof(SharedLayout) +
        static_cast<size_t>(capacity) * config.channels * sizeof(float);

    HANDLE mapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
        static_cast<DWORD>(bytes),
        WideName(L"Local\\XER.VAC.", config.name).c_str());
    if (mapping == nullptr) {
        if (error_out) *error_out = "CreateFileMapping failed";
        return nullptr;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mapping);
        if (error_out) *error_out = "cable already exists: " + config.name;
        return nullptr;
    }
    auto* shared = static_cast<SharedLayout*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes));
    if (shared == nullptr) {
        CloseHandle(mapping);
        if (error_out) *error_out = "MapViewOfFile failed";
        return nullptr;
    }
    std::memset(shared, 0, sizeof(SharedLayout));
    InterlockedExchange(const_cast<volatile long*>(&shared->magic),
                        static_cast<long>(kCableMagic));
    InterlockedExchange(const_cast<volatile long*>(&shared->version),
                        static_cast<long>(kCableVersion));
    InterlockedExchange(const_cast<volatile long*>(&shared->channels),
                        static_cast<long>(config.channels));
    InterlockedExchange(const_cast<volatile long*>(&shared->sample_rate),
                        static_cast<long>(config.sample_rate));
    InterlockedExchange(
        const_cast<volatile long*>(&shared->capacity_frames),
        static_cast<long>(capacity));
    cable->mapping_ = mapping;
    cable->shared_ = shared;

    cable->data_event_ = CreateEventW(
        nullptr, TRUE, FALSE,
        WideName(L"Local\\XER.VAC.Data.", config.name).c_str());
    cable->space_event_ = CreateEventW(
        nullptr, TRUE, TRUE,  // space starts signaled
        WideName(L"Local\\XER.VAC.Space.", config.name).c_str());
    if (cable->data_event_ == nullptr || cable->space_event_ == nullptr) {
        if (error_out) *error_out = "CreateEvent failed";
        return nullptr;
    }

    // Register in the shared cable directory. The host keeps the
    // mapping and its view open for the cable's lifetime — closing them
    // would destroy the object and erase the entry for everyone.
    HANDLE mtx = CreateMutexW(
        nullptr, FALSE, WideName(L"Local\\XER.VAC.Registry.", L"mutex").c_str());
    HANDLE reg = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
        sizeof(RegistryMap),
        WideName(L"Local\\XER.VAC.", L"Registry").c_str());
    if (mtx != nullptr && reg != nullptr) {
        WaitForSingleObject(mtx, 2000);
        auto* map = static_cast<RegistryMap*>(MapViewOfFile(
            reg, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(RegistryMap)));
        if (map != nullptr) {
            // Reuse a dead slot before a fresh one.
            RegistrySlot* slot = nullptr;
            for (auto& s : map->slots) {
                if (s.active != 0 && !PidAlive(s.pid)) {
                    InterlockedExchange(
                        reinterpret_cast<volatile long*>(&s.active), 0);
                }
                if (slot == nullptr && s.active == 0) {
                    slot = &s;
                }
            }
            if (slot != nullptr) {
                std::wstring wname = WideName(L"", config.name);
                std::wcsncpy(slot->name, wname.c_str(), kRegistryName - 1);
                slot->name[kRegistryName - 1] = L'\0';
                slot->pid = GetCurrentProcessId();
                slot->sample_rate = config.sample_rate;
                slot->channels = config.channels;
                slot->capacity_frames = capacity;
                InterlockedExchange(
                    reinterpret_cast<volatile long*>(&slot->active), 1);
            }
            cable->registry_view_ = map;
            cable->registry_map_ = reg;
        } else {
            CloseHandle(reg);
        }
        ReleaseMutex(mtx);
        CloseHandle(mtx);
    } else {
        if (mtx != nullptr) {
            CloseHandle(mtx);
        }
        if (reg != nullptr) {
            CloseHandle(reg);
        }
    }
    return cable;
}

std::unique_ptr<VirtualAudioCable> VirtualAudioCable::OpenImpl(
    const std::string& name, bool for_write, std::string* error_out) {
    if (name.empty()) {
        if (error_out) *error_out = "empty cable name";
        return nullptr;
    }
    HANDLE mapping = OpenFileMappingW(
        FILE_MAP_ALL_ACCESS, FALSE,
        WideName(L"Local\\XER.VAC.", name).c_str());
    if (mapping == nullptr) {
        if (error_out) *error_out = "cable not found: " + name;
        return nullptr;
    }
    // Size 0 maps the entire object: header plus the ring storage.
    auto* shared = static_cast<VirtualAudioCable::SharedLayout*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
    if (shared == nullptr) {
        CloseHandle(mapping);
        if (error_out) *error_out = "MapViewOfFile failed";
        return nullptr;
    }
    if (static_cast<uint32_t>(InterlockedCompareExchange(
            const_cast<volatile long*>(&shared->magic), 0, 0)) !=
        kCableMagic) {
        UnmapViewOfFile(shared);
        CloseHandle(mapping);
        if (error_out) *error_out = "not a virtual audio cable: " + name;
        return nullptr;
    }
    auto cable = std::unique_ptr<VirtualAudioCable>(new VirtualAudioCable());
    cable->config_.name = name;
    cable->config_.sample_rate =
        static_cast<uint32_t>(shared->sample_rate);
    cable->config_.channels =
        static_cast<uint32_t>(shared->channels);
    cable->config_.buffer_ms = 0;
    cable->mapping_ = mapping;
    cable->shared_ = shared;
    cable->can_read_ = !for_write;
    cable->can_write_ = for_write;
    cable->data_event_ = OpenEventW(
        EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE,
        WideName(L"Local\\XER.VAC.Data.", name).c_str());
    cable->space_event_ = OpenEventW(
        EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE,
        WideName(L"Local\\XER.VAC.Space.", name).c_str());
    return cable;
}

std::unique_ptr<VirtualAudioCable> VirtualAudioCable::OpenRead(
    const std::string& name, std::string* error_out) {
    return OpenImpl(name, /*for_write=*/false, error_out);
}

std::unique_ptr<VirtualAudioCable> VirtualAudioCable::OpenWrite(
    const std::string& name, std::string* error_out) {
    return OpenImpl(name, /*for_write=*/true, error_out);
}

std::vector<VirtualCableInfo> VirtualAudioCable::Enumerate() {
    std::vector<VirtualCableInfo> out;
    HANDLE reg = OpenFileMappingW(
        FILE_MAP_READ, FALSE, WideName(L"Local\\XER.VAC.", L"Registry").c_str());
    if (reg == nullptr) {
        return out;
    }
    auto* map = static_cast<const RegistryMap*>(
        MapViewOfFile(reg, FILE_MAP_READ, 0, 0, sizeof(RegistryMap)));
    if (map == nullptr) {
        CloseHandle(reg);
        return out;
    }
    for (const auto& s : map->slots) {
        if (s.active == 0 || s.name[0] == L'\0') {
            continue;
        }
        VirtualCableInfo info;
        info.name = Helper::String::WideToUtf8(s.name);
        info.sample_rate = s.sample_rate;
        info.channels = s.channels;
        info.capacity_frames = s.capacity_frames;
        info.host_pid = s.pid;
        info.host_alive = PidAlive(s.pid);
        if (info.host_alive) {
            out.push_back(std::move(info));
        }
    }
    UnmapViewOfFile(const_cast<RegistryMap*>(map));
    CloseHandle(reg);
    return out;
}

uint32_t VirtualAudioCable::Write(const float* interleaved, uint32_t frames,
                                  uint32_t timeout_ms) {
    if (!can_write_ || shared_ == nullptr || interleaved == nullptr) {
        return 0;
    }
    const uint32_t capacity =
        static_cast<uint32_t>(shared_->capacity_frames);
    const uint32_t channels =
        static_cast<uint32_t>(shared_->channels);
    const uint64_t deadline =
        timeout_ms == INFINITE ? UINT64_MAX
                               : GetTickCount64() + timeout_ms;
    uint32_t written = 0;
    while (written < frames) {
        if (InterlockedCompareExchange(
                const_cast<volatile long*>(&shared_->closed), 0, 0) != 0) {
            break;
        }
        const uint64_t w = static_cast<uint64_t>(InterlockedCompareExchange64(
            const_cast<volatile long long*>(&shared_->write_index), 0, 0));
        const uint64_t r = static_cast<uint64_t>(InterlockedCompareExchange64(
            const_cast<volatile long long*>(&shared_->read_index), 0, 0));
        const uint64_t space = capacity - (w - r);
        if (space > 0) {
            const uint32_t chunk =
                static_cast<uint32_t>(
                    std::min<uint64_t>(space, frames - written));
            CopyToRing(RingSamples(shared_), capacity, channels, w,
                       interleaved +
                           static_cast<size_t>(written) * channels,
                       chunk);
            InterlockedExchange64(
                const_cast<volatile long long*>(&shared_->write_index),
                static_cast<long long>(w + chunk));
            written += chunk;
            if (data_event_ != nullptr) {
                SetEvent(data_event_);
            }
            continue;
        }
        if (GetTickCount64() >= deadline) {
            break;
        }
        const DWORD wait_ms = timeout_ms == INFINITE
                                  ? INFINITE
                                  : static_cast<DWORD>(
                                        std::min<uint64_t>(
                                            10,
                                            deadline - GetTickCount64()));
        if (space_event_ != nullptr) {
            // Recheck under the event to avoid a lost wakeup: the reader
            // sets it after every successful Read.
            WaitForSingleObject(space_event_, wait_ms);
        }
    }
    return written;
}

uint32_t VirtualAudioCable::Read(float* interleaved, uint32_t frames,
                                 uint32_t timeout_ms) {
    if (!can_read_ || shared_ == nullptr || interleaved == nullptr) {
        return 0;
    }
    const uint32_t capacity =
        static_cast<uint32_t>(shared_->capacity_frames);
    const uint32_t channels =
        static_cast<uint32_t>(shared_->channels);
    const uint64_t deadline =
        timeout_ms == INFINITE ? UINT64_MAX
                               : GetTickCount64() + timeout_ms;
    uint32_t read = 0;
    while (read < frames) {
        if (InterlockedCompareExchange(
                const_cast<volatile long*>(&shared_->closed), 0, 0) != 0) {
            break;
        }
        const uint64_t w = static_cast<uint64_t>(InterlockedCompareExchange64(
            const_cast<volatile long long*>(&shared_->write_index), 0, 0));
        const uint64_t r = static_cast<uint64_t>(InterlockedCompareExchange64(
            const_cast<volatile long long*>(&shared_->read_index), 0, 0));
        const uint64_t avail = w - r;
        if (avail > 0) {
            const uint32_t chunk =
                static_cast<uint32_t>(
                    std::min<uint64_t>(avail, frames - read));
            CopyFromRing(interleaved + static_cast<size_t>(read) * channels,
                         RingSamples(shared_), capacity, channels, r, chunk);
            InterlockedExchange64(
                const_cast<volatile long long*>(&shared_->read_index),
                static_cast<long long>(r + chunk));
            read += chunk;
            if (space_event_ != nullptr) {
                SetEvent(space_event_);
            }
            continue;
        }
        if (GetTickCount64() >= deadline) {
            break;
        }
        if (data_event_ != nullptr) {
            ResetEvent(data_event_);
            // If the writer produced between our check and the reset,
            // re-arm so we do not sleep through available data.
            if (static_cast<uint64_t>(InterlockedCompareExchange64(
                    const_cast<volatile long long*>(&shared_->write_index),
                    0, 0)) != r) {
                SetEvent(data_event_);
                continue;
            }
            const DWORD wait_ms =
                timeout_ms == INFINITE
                    ? INFINITE
                    : static_cast<DWORD>(std::min<uint64_t>(
                          10, deadline - GetTickCount64()));
            WaitForSingleObject(data_event_, wait_ms);
        }
    }
    return read;
}

uint32_t VirtualAudioCable::Available() const {
    if (shared_ == nullptr) {
        return 0;
    }
    const int64_t w = InterlockedCompareExchange64(
        const_cast<volatile long long*>(&shared_->write_index), 0, 0);
    const int64_t r = InterlockedCompareExchange64(
        const_cast<volatile long long*>(&shared_->read_index), 0, 0);
    return static_cast<uint32_t>(std::max<int64_t>(0, w - r));
}

void VirtualAudioCable::SignalClose() {
    if (shared_ != nullptr) {
        InterlockedExchange(const_cast<volatile long*>(&shared_->closed), 1);
    }
    if (data_event_ != nullptr) {
        SetEvent(data_event_);
    }
    if (space_event_ != nullptr) {
        SetEvent(space_event_);
    }
}

bool VirtualAudioCable::IsClosed() const {
    return shared_ == nullptr ||
           InterlockedCompareExchange(
               const_cast<volatile long*>(&shared_->closed), 0, 0) != 0;
}

}  // namespace XER::Audio::Core

#else  // !_WIN32 — portable stub: cables exist only inside this process.

namespace XER::Audio::Core {

struct VirtualAudioCable::SharedLayout {};

VirtualAudioCable::~VirtualAudioCable() = default;

std::unique_ptr<VirtualAudioCable> VirtualAudioCable::Create(
    const VirtualCableConfig& config, std::string* error_out) {
    if (error_out) *error_out = "virtual audio cables require Windows";
    return nullptr;
}
std::unique_ptr<VirtualAudioCable> VirtualAudioCable::OpenRead(
    const std::string&, std::string* error_out) {
    if (error_out) *error_out = "virtual audio cables require Windows";
    return nullptr;
}
std::unique_ptr<VirtualAudioCable> VirtualAudioCable::OpenWrite(
    const std::string&, std::string* error_out) {
    if (error_out) *error_out = "virtual audio cables require Windows";
    return nullptr;
}
std::vector<VirtualCableInfo> VirtualAudioCable::Enumerate() { return {}; }
uint32_t VirtualAudioCable::Write(const float*, uint32_t, uint32_t) { return 0; }
uint32_t VirtualAudioCable::Read(float*, uint32_t, uint32_t) { return 0; }
uint32_t VirtualAudioCable::Available() const { return 0; }
void VirtualAudioCable::SignalClose() {}
bool VirtualAudioCable::IsClosed() const { return true; }

}  // namespace XER::Audio::Core

#endif
