// See wasapi_sink.h — WASAPI shared-mode render sink (Windows only).

#include "wasapi_sink.h"

#include <cstring>

#ifdef _WIN32
#include <algorithm>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

namespace {

// ~30 ms of device buffering: short enough for editor-preview latency,
// long enough that a ~21 ms (1024-frame @ 48 kHz) mixer chunk fits per
// Write.
// 200 ms device buffer — enough cushion that a slow compositor tick
// (heavy effects, first-frame decode) drains buffered audio instead of
// underrunning. The pump tops the buffer up with a ~120 ms lead, so
// Write() almost never blocks on a full buffer and never stalls video.
constexpr REFERENCE_TIME kBufferDuration100ns = 2000000;
// Cap for one WaitForSingleObject on the buffer event. The event is
// signaled every device period while the stream runs, so a timeout
// means the endpoint stalled — shed the chunk instead of hanging the
// engine worker.
constexpr DWORD kBufferWaitMs = 1000;
constexpr int kMaxReconnectAttempts = 1;

}  // namespace
#endif  // _WIN32

namespace XER::VideoEditor {

#ifdef _WIN32

WasapiSink::WasapiSink() = default;

WasapiSink::~WasapiSink() { Close(); }

bool WasapiSink::Init(int sample_rate, std::string* error_out) {
    if (open_ && sample_rate == rate_) return true;
    return Open(sample_rate, error_out);
}

bool WasapiSink::Fail(const char* what, long hr, std::string* error_out) {
    last_error_ = std::string("WasapiSink: ") + what +
                  " failed (hr=" +
                  std::to_string(static_cast<unsigned long>(hr)) + ")";
    if (error_out != nullptr) *error_out = last_error_;
    Close();
    return false;
}

bool WasapiSink::Open(int sample_rate, std::string* error_out) {
    Close();
    if (sample_rate <= 0) {
        last_error_ = "WasapiSink: invalid sample rate";
        if (error_out != nullptr) *error_out = last_error_;
        return false;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        return Fail("CoInitializeEx", hr, error_out);
    }
    // S_OK and S_FALSE both hold an init count we must release; only do
    // it on the same thread (COM init is per-thread).
    com_init_ = true;
    com_thread_ = GetCurrentThreadId();

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                          CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                          reinterpret_cast<void**>(&enumerator_));
    if (FAILED(hr)) {
        return Fail("CoCreateInstance(MMDeviceEnumerator)", hr, error_out);
    }

    hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
    if (FAILED(hr)) {
        return Fail("GetDefaultAudioEndpoint", hr, error_out);
    }

    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(&audio_client_));
    if (FAILED(hr)) {
        return Fail("IMMDevice::Activate(IAudioClient)", hr, error_out);
    }

    // Format negotiation: plain WAVEFORMATEX only — s16 stereo first
    // (zero-copy Write path), float32 stereo as fallback. The shared-
    // mode engine converts either to the device mix format for any
    // reasonable rate, so no resampler lives here.
    WAVEFORMATEX fmt{};
    bool use_float = false;
    {
        WAVEFORMATEX pcm16{};
        pcm16.wFormatTag = WAVE_FORMAT_PCM;
        pcm16.nChannels = 2;
        pcm16.nSamplesPerSec = static_cast<DWORD>(sample_rate);
        pcm16.wBitsPerSample = 16;
        pcm16.nBlockAlign = static_cast<WORD>(
            pcm16.nChannels * pcm16.wBitsPerSample / 8);
        pcm16.nAvgBytesPerSec =
            pcm16.nSamplesPerSec * pcm16.nBlockAlign;
        WAVEFORMATEX* closest = nullptr;
        hr = audio_client_->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,
                                              &pcm16, &closest);
        if (closest != nullptr) CoTaskMemFree(closest);
        if (hr == S_OK) {
            fmt = pcm16;
        } else {
            WAVEFORMATEX f32{};
            f32.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
            f32.nChannels = 2;
            f32.nSamplesPerSec = static_cast<DWORD>(sample_rate);
            f32.wBitsPerSample = 32;
            f32.nBlockAlign = 8;
            f32.nAvgBytesPerSec = static_cast<DWORD>(sample_rate) * 8;
            closest = nullptr;
            hr = audio_client_->IsFormatSupported(
                AUDCLNT_SHAREMODE_SHARED, &f32, &closest);
            if (closest != nullptr) CoTaskMemFree(closest);
            if (hr != S_OK) {
                return Fail("format negotiation (s16 + f32 rejected)",
                            hr, error_out);
            }
            fmt = f32;
            use_float = true;
        }
    }

    buffer_event_ = CreateEventW(nullptr, /*bManualReset=*/FALSE,
                                 /*bInitialState=*/FALSE, nullptr);
    if (buffer_event_ == nullptr) {
        return Fail("CreateEvent",
                    HRESULT_FROM_WIN32(GetLastError()), error_out);
    }

    hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                   AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                   kBufferDuration100ns, 0, &fmt,
                                   nullptr);
    if (FAILED(hr)) return Fail("IAudioClient::Initialize", hr, error_out);

    hr = audio_client_->SetEventHandle(
        static_cast<HANDLE>(buffer_event_));
    if (FAILED(hr)) return Fail("SetEventHandle", hr, error_out);

    hr = audio_client_->GetBufferSize(&buffer_frames_);
    if (FAILED(hr) || buffer_frames_ == 0) {
        return Fail("GetBufferSize", hr, error_out);
    }

    hr = audio_client_->GetService(
        __uuidof(IAudioRenderClient),
        reinterpret_cast<void**>(&render_client_));
    if (FAILED(hr)) return Fail("GetService(IAudioRenderClient)", hr,
                                error_out);

    hr = audio_client_->Start();
    if (FAILED(hr)) return Fail("IAudioClient::Start", hr, error_out);

    rate_ = sample_rate;
    device_float_ = use_float;
    open_ = true;
    last_error_.clear();
    return true;
}

void WasapiSink::Close() {
    if (audio_client_ != nullptr) audio_client_->Stop();
    if (render_client_ != nullptr) {
        render_client_->Release();
        render_client_ = nullptr;
    }
    if (audio_client_ != nullptr) {
        audio_client_->Release();
        audio_client_ = nullptr;
    }
    if (device_ != nullptr) {
        device_->Release();
        device_ = nullptr;
    }
    if (enumerator_ != nullptr) {
        enumerator_->Release();
        enumerator_ = nullptr;
    }
    if (buffer_event_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(buffer_event_));
        buffer_event_ = nullptr;
    }
    buffer_frames_ = 0;
    open_ = false;
    // Release our COM init count only on the thread that took it —
    // a stray CoUninitialize on another thread would pop its count.
    if (com_init_) {
        if (GetCurrentThreadId() == com_thread_) CoUninitialize();
        com_init_ = false;
    }
}

bool WasapiSink::TryReopen() {
    // Device loss: the endpoint may have changed (headphone pull,
    // driver restart) — a full re-open grabs the CURRENT default.
    const int rate = rate_;
    Close();
    std::string err;
    if (!Open(rate, &err)) {
        last_error_ = "WasapiSink: reconnect failed: " + err;
        return false;
    }
    return true;
}

bool WasapiSink::Write(const std::int16_t* interleaved_samples,
                       int frame_count, int sample_rate) {
    if (frame_count <= 0 || interleaved_samples == nullptr) return true;

    // Lazy init on first Write fixes the negotiated rate; a later Write
    // at a different rate re-opens once (see header contract).
    if (!open_ || sample_rate != rate_) {
        if (!Open(sample_rate, nullptr)) return false;
    }

    const std::int16_t* src = interleaved_samples;
    int left = frame_count;
    int reconnects = 0;

    while (left > 0) {
        UINT32 padding = 0;
        HRESULT hr = audio_client_->GetCurrentPadding(&padding);
        if (FAILED(hr)) {
            if (++reconnects > kMaxReconnectAttempts || !TryReopen()) {
                return false;
            }
            continue;
        }
        const UINT32 available = padding < buffer_frames_
                                     ? buffer_frames_ - padding
                                     : 0;
        if (available == 0) {
            // Buffer full — block until the engine drains a period.
            // This is the pump's pacing throttle.
            const DWORD wait =
                WaitForSingleObject(buffer_event_, kBufferWaitMs);
            if (wait == WAIT_OBJECT_0) continue;
            if (wait == WAIT_TIMEOUT) {
                last_error_ = "WasapiSink: buffer wait timed out";
                return false;
            }
            // WAIT_FAILED/abandoned — treat as device loss.
            if (++reconnects > kMaxReconnectAttempts || !TryReopen()) {
                return false;
            }
            continue;
        }

        const UINT32 n =
            (std::min)(available, static_cast<UINT32>(left));
        BYTE* dst = nullptr;
        hr = render_client_->GetBuffer(n, &dst);
        if (FAILED(hr) || dst == nullptr) {
            if (++reconnects > kMaxReconnectAttempts || !TryReopen()) {
                return false;
            }
            continue;
        }
        if (device_float_) {
            float* f = reinterpret_cast<float*>(dst);
            const UINT32 count = n * 2;  // stereo samples per frame
            for (UINT32 i = 0; i < count; ++i) {
                f[i] = static_cast<float>(src[i]) / 32768.0f;
            }
        } else {
            std::memcpy(dst, src, n * 2 * sizeof(std::int16_t));
        }
        render_client_->ReleaseBuffer(n, 0);
        src += n * 2;
        left -= static_cast<int>(n);
    }
    return true;
}

#endif  // _WIN32

std::unique_ptr<IAudioSink> MakeWasapiSink(int sample_rate,
                                           std::string* error_out) {
#ifdef _WIN32
    auto sink = std::make_unique<WasapiSink>();
    if (sample_rate > 0 && !sink->Init(sample_rate, error_out)) {
        return nullptr;
    }
    return sink;
#else
    (void)sample_rate;
    if (error_out != nullptr) {
        *error_out = "WasapiSink is only available on Windows";
    }
    return nullptr;
#endif
}

}  // namespace XER::VideoEditor
