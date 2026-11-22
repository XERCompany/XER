// See wasapi_render_client.h — shared WASAPI shared-mode render client
// plumbing (IMMDevice -> IAudioClient -> IAudioRenderClient) used by
// VideoAudioPlayer and the VideoEditor preview sink. Windows-only TU:
// everything sits under _WIN32 like the consumers' own code.

#include "wasapi_render_client.h"

#ifdef _WIN32

#include <cstdio>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace XER::Content::Video {

WasapiRenderClient::~WasapiRenderClient() { Close(); }

bool WasapiRenderClient::Fail(const char* what, HRESULT hr,
                              std::string* error_out) {
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "WasapiRenderClient: %s failed (hr=0x%08lX)", what,
                  static_cast<unsigned long>(hr));
    last_error_ = buf;
    if (error_out != nullptr) *error_out = last_error_;
    return false;
}

bool WasapiRenderClient::OpenDevice(std::string* error_out) {
    Close();

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // RPC_E_CHANGED_MODE: the calling thread already joined a
    // different apartment — an existing STA init still satisfies us
    // (the WASAPI render objects are free-threaded), but we hold no
    // init count to balance in Close().
    if (hr == RPC_E_CHANGED_MODE) {
        hr = S_OK;
    }
    if (FAILED(hr)) {
        return Fail("CoInitializeEx", hr, error_out);
    }
    // S_OK and S_FALSE both hold an init count we must release; only
    // do it on the same thread (COM init is per-thread).
    if (hr == S_OK || hr == S_FALSE) {
        com_init_ = true;
        com_thread_ = GetCurrentThreadId();
    }

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
    return true;
}

bool WasapiRenderClient::Initialize(const WAVEFORMATEX& fmt,
                                    DWORD stream_flags,
                                    REFERENCE_TIME buffer_100ns,
                                    HANDLE event_handle,
                                    HRESULT* rejected_hr,
                                    std::string* error_out) {
    if (audio_client_ == nullptr) {
        last_error_ = "WasapiRenderClient: Initialize before OpenDevice";
        if (error_out != nullptr) *error_out = last_error_;
        return false;
    }
    DWORD flags = stream_flags;
    if (event_handle != nullptr) {
        flags |= AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    }
    HRESULT hr = audio_client_->Initialize(
        AUDCLNT_SHAREMODE_SHARED, flags, buffer_100ns, 0, &fmt,
        nullptr);
    if (FAILED(hr)) {
        // Format rejected — the client is NOT initialized and the next
        // candidate may still Initialize cleanly. Not fatal: the chain
        // stays open for the negotiation loop.
        if (rejected_hr != nullptr) *rejected_hr = hr;
        return false;
    }

    // Anything below is post-Initialize: the client is now initialized
    // and cannot be re-initialized, so a failure is fatal — tear the
    // whole chain down rather than leaving a half-open device.
    if (event_handle != nullptr) {
        hr = audio_client_->SetEventHandle(event_handle);
        if (FAILED(hr)) {
            const bool r = Fail("SetEventHandle", hr, error_out);
            Close();
            return r;
        }
    }
    buffer_frames_ = 0;
    hr = audio_client_->GetBufferSize(&buffer_frames_);
    if (FAILED(hr) || buffer_frames_ == 0) {
        const bool r = Fail("GetBufferSize",
                            FAILED(hr) ? hr : E_FAIL, error_out);
        Close();
        return r;
    }
    hr = audio_client_->GetService(
        __uuidof(IAudioRenderClient),
        reinterpret_cast<void**>(&render_client_));
    if (FAILED(hr)) {
        const bool r = Fail("GetService(IAudioRenderClient)", hr,
                            error_out);
        Close();
        return r;
    }
    return true;
}

bool WasapiRenderClient::Start(std::string* error_out) {
    if (audio_client_ == nullptr) {
        last_error_ = "WasapiRenderClient: Start before OpenDevice";
        if (error_out != nullptr) *error_out = last_error_;
        return false;
    }
    const HRESULT hr = audio_client_->Start();
    if (FAILED(hr)) {
        return Fail("IAudioClient::Start", hr, error_out);
    }
    return true;
}

void WasapiRenderClient::Stop() {
    if (audio_client_ != nullptr) {
        audio_client_->Stop();
    }
}

void WasapiRenderClient::Reset() {
    if (audio_client_ != nullptr) {
        // Reset needs a stopped stream; on a never-started one it just
        // returns AUDCLNT_E_NOT_STOPPED — harmless either way.
        audio_client_->Reset();
    }
}

void WasapiRenderClient::Close() {
    if (audio_client_ != nullptr) {
        audio_client_->Stop();
    }
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
    buffer_frames_ = 0;
    // Release our COM init count only on the thread that took it —
    // CoUninitialize on a different thread would pop THAT thread's
    // count (COM init is per-thread). A cross-thread Close therefore
    // KEEPS com_init_ set instead of clearing it: a later Close() on
    // the owning thread still balances the count, and if the last
    // Close runs elsewhere the count is released with the owning
    // thread's apartment teardown; leaking it would be strictly worse
    // because clearing the flag made it unrecoverable (bug-audit
    // finding carried over from the editor's copy).
    if (com_init_ && GetCurrentThreadId() == com_thread_) {
        CoUninitialize();
        com_init_ = false;
    }
}

}  // namespace XER::Content::Video

#endif  // _WIN32
