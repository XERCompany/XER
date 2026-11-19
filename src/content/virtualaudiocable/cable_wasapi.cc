#include "virtualaudiocable/cable_wasapi.h"

#include <atomic>
#include <cstring>
#include <thread>

#ifdef _WIN32

#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <windows.h>

#include "helper/string_utils.h"

namespace XER::Audio::Core {
namespace {

std::string HrText(HRESULT hr) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "hr=0x%08X", static_cast<unsigned>(hr));
    return buf;
}

// Shared-mode WASAPI clients produce the endpoint engine format, which
// on modern Windows is float32 interleaved; anything else is declined
// (no conversion is performed — the cable layer is float32).
bool IsFloatFormat(const WAVEFORMATEX* f) {
    if (f == nullptr) {
        return false;
    }
    if (f->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return f->wBitsPerSample == 32;
    }
    if (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ex =
            reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f);
        return ex->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT &&
               f->wBitsPerSample == 32;
    }
    return false;
}

template <typename T>
struct ComRelease {
    void operator()(T* p) const { if (p) p->Release(); }
};
template <typename T>
using ComPtrT = std::unique_ptr<T, ComRelease<T>>;

ComPtrT<IMMDevice> OpenEndpoint(const WasapiEndpoint& endpoint, EDataFlow flow,
                                IMMDeviceEnumerator** enumerator_out,
                                std::string* error_out) {
    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr)) {
        if (error_out) *error_out = "MMDeviceEnumerator: " + HrText(hr);
        return {};
    }
    *enumerator_out = enumerator;
    IMMDevice* device = nullptr;
    if (endpoint.device_id.empty()) {
        hr = enumerator->GetDefaultAudioEndpoint(
            flow, eConsole, &device);
    } else {
        hr = enumerator->GetDevice(endpoint.device_id.c_str(), &device);
    }
    if (FAILED(hr) || device == nullptr) {
        if (error_out) *error_out = "endpoint open: " + HrText(hr);
        return {};
    }
    return ComPtrT<IMMDevice>{device};
}

struct ComInit {
    ComInit() { hr_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
    ~ComInit() { if (SUCCEEDED(hr_)) CoUninitialize(); }
    bool ok() const { return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE; }
    HRESULT hr_ = S_OK;
};

}  // namespace

// ---------------- device enumeration -----------------------------

std::vector<WasapiDeviceInfo> EnumerateWasapiDevices(
    std::string* error_out) {
    std::vector<WasapiDeviceInfo> out;
    ComInit com;
    if (!com.ok()) {
        if (error_out) *error_out = "COM init failed";
        return out;
    }
    ComPtrT<IMMDeviceEnumerator> enumerator;
    IMMDeviceEnumerator* raw_enum = nullptr;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&raw_enum));
    if (FAILED(hr)) {
        if (error_out) *error_out = "MMDeviceEnumerator: " + HrText(hr);
        return out;
    }
    enumerator.reset(raw_enum);
    for (EDataFlow flow : {eRender, eCapture}) {
        IMMDevice* def = nullptr;
        LPWSTR def_id = nullptr;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(
                flow, eConsole, &def)) && def != nullptr) {
            def->GetId(&def_id);
            def->Release();
        }
        ComPtrT<IMMDeviceCollection> coll;
        IMMDeviceCollection* raw_coll = nullptr;
        hr = enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE,
                                          &raw_coll);
        if (FAILED(hr)) {
            continue;
        }
        coll.reset(raw_coll);
        UINT count = 0;
        coll->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            IMMDevice* device = nullptr;
            if (FAILED(coll->Item(i, &device)) || device == nullptr) {
                continue;
            }
            WasapiDeviceInfo info;
            info.render = (flow == eRender);
            info.capture = (flow == eCapture);
            LPWSTR id = nullptr;
            device->GetId(&id);
            if (id != nullptr) {
                info.id = id;
                info.is_default = def_id != nullptr &&
                                  wcscmp(id, def_id) == 0;
                CoTaskMemFree(id);
            }
            IPropertyStore* props = nullptr;
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props)) &&
                props != nullptr) {
                PROPVARIANT v;
                PropVariantInit(&v);
                if (SUCCEEDED(props->GetValue(
                        PKEY_Device_FriendlyName, &v)) &&
                    v.vt == VT_LPWSTR) {
                    info.name =
                        Helper::String::WideToUtf8(v.pwszVal);
                }
                PropVariantClear(&v);
                props->Release();
            }
            device->Release();
            out.push_back(std::move(info));
        }
        if (def_id != nullptr) {
            CoTaskMemFree(def_id);
        }
    }
    return out;
}

// ---------------- WasapiRenderSink ---------------------------------

struct WasapiRenderSink::Impl {
    FillFn fill;
    std::thread thread;
    std::atomic<bool> stop{false};
    std::atomic<bool> running{false};
    uint32_t rate = 0;
    uint32_t channels = 0;
};

WasapiRenderSink::WasapiRenderSink() = default;
WasapiRenderSink::~WasapiRenderSink() { Stop(); }

bool WasapiRenderSink::Start(const WasapiEndpoint& endpoint, FillFn fill,
                             std::string* error_out) {
    if (impl_ && impl_->running.load()) {
        if (error_out) *error_out = "already running";
        return false;
    }
    impl_ = std::make_unique<Impl>();
    impl_->fill = std::move(fill);
    impl_->stop.store(false);
    impl_->running.store(false);

    WasapiEndpoint ep = endpoint;
    std::string err;
    std::atomic<bool> started{false};
    std::string thread_err;
    impl_->thread = std::thread([this, ep, &started, &thread_err]() {
        ComInit com;
        if (!com.ok()) {
            thread_err = "COM init failed";
            started.store(true);
            return;
        }
        IMMDeviceEnumerator* enumerator = nullptr;
        ComPtrT<IMMDevice> device =
            OpenEndpoint(ep, eRender, &enumerator, &thread_err);
        ComPtrT<IMMDeviceEnumerator> enum_guard;
        enum_guard.reset(enumerator);
        if (!device) {
            started.store(true);
            return;
        }
        IAudioClient* raw_client = nullptr;
        device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                         reinterpret_cast<void**>(&raw_client));
        ComPtrT<IAudioClient> client;
        client.reset(raw_client);
        if (!client) {
            thread_err = "device Activate failed";
            started.store(true);
            return;
        }
        WAVEFORMATEX* mix = nullptr;
        client->GetMixFormat(&mix);
        if (!IsFloatFormat(mix)) {
            if (mix) CoTaskMemFree(mix);
            thread_err = "endpoint is not float32";
            started.store(true);
            return;
        }
        impl_->rate = mix->nSamplesPerSec;
        impl_->channels = mix->nChannels;
        HANDLE wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        REFERENCE_TIME period = 0;
        client->GetDevicePeriod(nullptr, &period);
        if (period == 0) {
            period = 100000;  // 10 ms fallback
        }
        HRESULT hr = client->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            period * 2, 0, mix, nullptr);
        CoTaskMemFree(mix);
        if (FAILED(hr)) {
            CloseHandle(wake);
            thread_err = "client Initialize: " + HrText(hr);
            started.store(true);
            return;
        }
        client->SetEventHandle(wake);
        UINT32 buffer_frames = 0;
        client->GetBufferSize(&buffer_frames);
        IAudioRenderClient* raw_render = nullptr;
        client->GetService(__uuidof(IAudioRenderClient),
                           reinterpret_cast<void**>(&raw_render));
        ComPtrT<IAudioRenderClient> render;
        render.reset(raw_render);
        if (!render) {
            CloseHandle(wake);
            thread_err = "IAudioRenderClient failed";
            started.store(true);
            return;
        }
        // Prime the buffer so the first callback has data to play.
        {
            BYTE* data = nullptr;
            if (SUCCEEDED(render->GetBuffer(buffer_frames, &data)) &&
                data != nullptr) {
                impl_->fill(reinterpret_cast<float*>(data),
                            buffer_frames);
                render->ReleaseBuffer(buffer_frames, 0);
            }
        }
        impl_->running.store(true);
        started.store(true);
        client->Start();
        std::vector<float> zero;
        while (!impl_->stop.load()) {
            if (WaitForSingleObject(wake, 2000) == WAIT_TIMEOUT) {
                continue;
            }
            UINT32 pad = 0;
            client->GetCurrentPadding(&pad);
            UINT32 avail = buffer_frames - pad;
            while (avail > 0) {
                BYTE* data = nullptr;
                if (FAILED(render->GetBuffer(avail, &data)) ||
                    data == nullptr) {
                    break;
                }
                impl_->fill(reinterpret_cast<float*>(data), avail);
                render->ReleaseBuffer(avail, 0);
                client->GetCurrentPadding(&pad);
                avail = buffer_frames - pad;
            }
        }
        client->Stop();
        impl_->running.store(false);
        CloseHandle(wake);
    });
    while (!started.load()) {
        Sleep(1);
    }
    if (!impl_->running.load()) {
        impl_->stop.store(true);
        if (impl_->thread.joinable()) {
            impl_->thread.join();
        }
        impl_.reset();
        if (error_out) {
            *error_out = thread_err.empty() ? "render start failed"
                                            : thread_err;
        }
        return false;
    }
    return true;
}

void WasapiRenderSink::Stop() {
    if (impl_) {
        impl_->stop.store(true);
        if (impl_->thread.joinable()) {
            impl_->thread.join();
        }
        impl_.reset();
    }
}
bool WasapiRenderSink::running() const {
    return impl_ && impl_->running.load();
}
uint32_t WasapiRenderSink::sample_rate() const {
    return impl_ ? impl_->rate : 0;
}
uint32_t WasapiRenderSink::channels() const {
    return impl_ ? impl_->channels : 0;
}

// ---------------- WasapiCaptureSource ------------------------------

struct WasapiCaptureSource::Impl {
    PushFn push;
    std::thread thread;
    std::atomic<bool> stop{false};
    std::atomic<bool> running{false};
    uint32_t rate = 0;
    uint32_t channels = 0;
};

WasapiCaptureSource::WasapiCaptureSource() = default;
WasapiCaptureSource::~WasapiCaptureSource() { Stop(); }

bool WasapiCaptureSource::Start(const WasapiEndpoint& endpoint,
                                bool loopback, PushFn push,
                                std::string* error_out) {
    if (impl_ && impl_->running.load()) {
        if (error_out) *error_out = "already running";
        return false;
    }
    impl_ = std::make_unique<Impl>();
    impl_->push = std::move(push);
    impl_->stop.store(false);
    impl_->running.store(false);
    std::atomic<bool> started{false};
    std::string thread_err;
    impl_->thread = std::thread([this, endpoint, loopback, &started,
                                 &thread_err]() {
        ComInit com;
        if (!com.ok()) {
            thread_err = "COM init failed";
            started.store(true);
            return;
        }
        IMMDeviceEnumerator* enumerator = nullptr;
        ComPtrT<IMMDevice> device = OpenEndpoint(
            endpoint, loopback ? eRender : eCapture, &enumerator,
            &thread_err);
        ComPtrT<IMMDeviceEnumerator> enum_guard;
        enum_guard.reset(enumerator);
        if (!device) {
            started.store(true);
            return;
        }
        IAudioClient* raw_client = nullptr;
        device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                         reinterpret_cast<void**>(&raw_client));
        ComPtrT<IAudioClient> client;
        client.reset(raw_client);
        if (!client) {
            thread_err = "device Activate failed";
            started.store(true);
            return;
        }
        WAVEFORMATEX* mix = nullptr;
        client->GetMixFormat(&mix);
        if (!IsFloatFormat(mix)) {
            if (mix) CoTaskMemFree(mix);
            thread_err = "endpoint is not float32";
            started.store(true);
            return;
        }
        impl_->rate = mix->nSamplesPerSec;
        impl_->channels = mix->nChannels;
        HANDLE wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
        if (loopback) {
            flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
        }
        HRESULT hr = client->Initialize(
            AUDCLNT_SHAREMODE_SHARED, flags, 300000, 0, mix, nullptr);
        CoTaskMemFree(mix);
        if (FAILED(hr)) {
            CloseHandle(wake);
            thread_err = "client Initialize: " + HrText(hr);
            started.store(true);
            return;
        }
        client->SetEventHandle(wake);
        IAudioCaptureClient* raw_capture = nullptr;
        client->GetService(__uuidof(IAudioCaptureClient),
                           reinterpret_cast<void**>(&raw_capture));
        ComPtrT<IAudioCaptureClient> capture;
        capture.reset(raw_capture);
        if (!capture) {
            CloseHandle(wake);
            thread_err = "IAudioCaptureClient failed";
            started.store(true);
            return;
        }
        impl_->running.store(true);
        started.store(true);
        client->Start();
        while (!impl_->stop.load()) {
            if (WaitForSingleObject(wake, 2000) == WAIT_TIMEOUT) {
                continue;
            }
            for (;;) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags2 = 0;
                UINT64 pos = 0, qpc = 0;
                HRESULT hr2 = capture->GetBuffer(
                    &data, &frames, &flags2, &pos, &qpc);
                if (FAILED(hr2) || frames == 0) {
                    break;
                }
                if ((flags2 & AUDCLNT_BUFFERFLAGS_SILENT) == 0 &&
                    data != nullptr) {
                    impl_->push(reinterpret_cast<const float*>(data),
                                frames);
                } else {
                    // Silence flag: still emit zeros so downstream
                    // timing stays continuous.
                    std::vector<float> zeros(
                        static_cast<size_t>(frames) * impl_->channels,
                        0.0f);
                    impl_->push(zeros.data(), frames);
                }
                capture->ReleaseBuffer(frames);
            }
        }
        client->Stop();
        impl_->running.store(false);
        CloseHandle(wake);
    });
    while (!started.load()) {
        Sleep(1);
    }
    if (!impl_->running.load()) {
        impl_->stop.store(true);
        if (impl_->thread.joinable()) {
            impl_->thread.join();
        }
        impl_.reset();
        if (error_out) {
            *error_out = thread_err.empty() ? "capture start failed"
                                            : thread_err;
        }
        return false;
    }
    return true;
}

void WasapiCaptureSource::Stop() {
    if (impl_) {
        impl_->stop.store(true);
        if (impl_->thread.joinable()) {
            impl_->thread.join();
        }
        impl_.reset();
    }
}
bool WasapiCaptureSource::running() const {
    return impl_ && impl_->running.load();
}
uint32_t WasapiCaptureSource::sample_rate() const {
    return impl_ ? impl_->rate : 0;
}
uint32_t WasapiCaptureSource::channels() const {
    return impl_ ? impl_->channels : 0;
}

}  // namespace XER::Audio::Core

#else  // !_WIN32

namespace XER::Audio::Core {

std::vector<WasapiDeviceInfo> EnumerateWasapiDevices(
    std::string* error_out) {
    if (error_out) *error_out = "WASAPI requires Windows";
    return {};
}
struct WasapiRenderSink::Impl {};
WasapiRenderSink::WasapiRenderSink() = default;
WasapiRenderSink::~WasapiRenderSink() = default;
bool WasapiRenderSink::Start(const WasapiEndpoint&, FillFn,
                             std::string* e) {
    if (e) *e = "WASAPI requires Windows";
    return false;
}
void WasapiRenderSink::Stop() {}
bool WasapiRenderSink::running() const { return false; }
uint32_t WasapiRenderSink::sample_rate() const { return 0; }
uint32_t WasapiRenderSink::channels() const { return 0; }

struct WasapiCaptureSource::Impl {};
WasapiCaptureSource::WasapiCaptureSource() = default;
WasapiCaptureSource::~WasapiCaptureSource() = default;
bool WasapiCaptureSource::Start(const WasapiEndpoint&, bool, PushFn,
                                std::string* e) {
    if (e) *e = "WASAPI requires Windows";
    return false;
}
void WasapiCaptureSource::Stop() {}
bool WasapiCaptureSource::running() const { return false; }
uint32_t WasapiCaptureSource::sample_rate() const { return 0; }
uint32_t WasapiCaptureSource::channels() const { return 0; }

}  // namespace XER::Audio::Core

#endif
