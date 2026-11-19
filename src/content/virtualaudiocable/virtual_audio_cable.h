#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace XER::Audio::Core {

// Settings for a single virtual audio cable. A cable is a named,
// cross-process PCM endpoint: any process on the machine can open it by
// name and exchange interleaved float32 audio through a shared-memory
// ring buffer.
struct VirtualCableConfig {
    std::string name;
    uint32_t sample_rate = 48000;
    uint32_t channels = 2;
    // Ring capacity in milliseconds of audio.
    uint32_t buffer_ms = 250;
};

struct VirtualCableInfo {
    std::string name;
    uint32_t sample_rate = 0;
    uint32_t channels = 0;
    uint32_t capacity_frames = 0;
    uint32_t host_pid = 0;
    bool host_alive = false;
};

// One endpoint of a virtual cable. The creator owns the shared memory
// (the cable disappears when the last handle closes); readers and
// writers open it by name from other processes. Single producer /
// single consumer per cable.
//
//   Host app : VirtualAudioCable::Create(cfg)   -> Write() or Read()
//   Client   : VirtualAudioCable::OpenWrite()   -> Write()
//              VirtualAudioCable::OpenRead()    -> Read()
class VirtualAudioCable {
public:
    ~VirtualAudioCable();
    VirtualAudioCable(const VirtualAudioCable&) = delete;
    VirtualAudioCable& operator=(const VirtualAudioCable&) = delete;

    // Host side: creates the named cable and registers it in the shared
    // cable registry so clients can discover it via Enumerate().
    static std::unique_ptr<VirtualAudioCable> Create(
        const VirtualCableConfig& config, std::string* error_out = nullptr);

    // Client sides: attach to an existing cable.
    static std::unique_ptr<VirtualAudioCable> OpenRead(
        const std::string& name, std::string* error_out = nullptr);
    static std::unique_ptr<VirtualAudioCable> OpenWrite(
        const std::string& name, std::string* error_out = nullptr);

    // All live cables (stale registry entries from dead hosts are
    // filtered out).
    static std::vector<VirtualCableInfo> Enumerate();

    // Interleaved float32 PCM, `frames` frames * config().channels.
    // timeout_ms: wait for space/data; 0 = non-blocking, ~INFINITE
    // allowed via (uint32_t)-1. Returns frames actually moved.
    uint32_t Write(const float* interleaved, uint32_t frames,
                   uint32_t timeout_ms = 100);
    uint32_t Read(float* interleaved, uint32_t frames,
                  uint32_t timeout_ms = 100);

    // Frames buffered right now (reader side view).
    uint32_t Available() const;

    // Signal all waiters to wake and return 0 — the host calls this on
    // shutdown so clients stop cleanly instead of timing out.
    void SignalClose();
    bool IsClosed() const;

    const VirtualCableConfig& config() const { return config_; }
    bool is_host() const { return host_; }

    // Opaque shared-memory layout; defined in the .cc.
    struct SharedLayout;

private:
    VirtualAudioCable() = default;

    static std::unique_ptr<VirtualAudioCable> OpenImpl(
        const std::string& name, bool for_write,
        std::string* error_out);

    VirtualCableConfig config_;
    bool host_ = false;
    bool can_write_ = false;
    bool can_read_ = false;

    void* mapping_ = nullptr;   // HANDLE
    SharedLayout* shared_ = nullptr;
    void* data_event_ = nullptr;    // HANDLE, set when data available
    void* space_event_ = nullptr;   // HANDLE, set when space available
    // Host only: the registry mapping must stay open for the cable's
    // lifetime or the directory object is destroyed with the handle.
    void* registry_map_ = nullptr;  // HANDLE
    void* registry_view_ = nullptr; // MapViewOfFile result
};

}  // namespace XER::Audio::Core
