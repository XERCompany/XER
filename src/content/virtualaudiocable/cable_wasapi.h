#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace XER::Audio::Core {

// Endpoint selection for the WASAPI helpers. An empty device_id uses
// the default console endpoint; a specific id is the device instance
// path reported by IMMDevice::GetId (see XERVac --devices).
struct WasapiEndpoint {
    std::wstring device_id;
};

struct WasapiDeviceInfo {
    std::wstring id;
    std::string name;
    bool render = false;
    bool capture = false;
    bool is_default = false;
};

// Lists render and capture endpoints for configs and --devices.
std::vector<WasapiDeviceInfo> EnumerateWasapiDevices(std::string* error_out = nullptr);

// Shared-mode render sink: owns an event-driven WASAPI client on a
// render endpoint and pulls interleaved float32 blocks through the fill
// callback on its own thread. The callback runs on the audio thread —
// no allocations, no blocking.
class WasapiRenderSink {
public:
    using FillFn =
        std::function<void(float* interleaved, uint32_t frames)>;

    WasapiRenderSink();
    ~WasapiRenderSink();
    WasapiRenderSink(const WasapiRenderSink&) = delete;
    WasapiRenderSink& operator=(const WasapiRenderSink&) = delete;

    bool Start(const WasapiEndpoint& endpoint, FillFn fill,
               std::string* error_out = nullptr);
    void Stop();
    bool running() const;

    uint32_t sample_rate() const;
    uint32_t channels() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Shared-mode capture source: captures either a capture endpoint
// (microphone/line-in) or a render endpoint in loopback mode (what the
// device is currently playing) and pushes interleaved float32 blocks
// through the callback on its own thread.
class WasapiCaptureSource {
public:
    using PushFn = std::function<void(const float* interleaved,
                                      uint32_t frames)>;

    WasapiCaptureSource();
    ~WasapiCaptureSource();
    WasapiCaptureSource(const WasapiCaptureSource&) = delete;
    WasapiCaptureSource& operator=(const WasapiCaptureSource&) = delete;

    // loopback=true: endpoint must be a render device; captures its mix.
    bool Start(const WasapiEndpoint& endpoint, bool loopback, PushFn push,
               std::string* error_out = nullptr);
    void Stop();
    bool running() const;

    uint32_t sample_rate() const;
    uint32_t channels() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace XER::Audio::Core
