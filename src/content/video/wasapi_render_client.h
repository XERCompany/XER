#pragma once

// Shared WASAPI shared-mode render client: the IMMDevice ->
// IAudioClient -> IAudioRenderClient plumbing that VideoAudioPlayer
// (src/content/video) and the VideoEditor preview sink
// (tools/video_editor/playback/wasapi_sink.cc) used to open by hand —
// the editor's copy was a self-confessed duplicate.
//
// The class owns only the device-side lifecycle: COM apartment
// balance, the enumerator/endpoint/client/render-client chain, the
// optional buffer event, and Start/Stop/Reset/Close ordering. It does
// NOT own a consumption model — callers keep their own pacing:
//   * VideoAudioPlayer pulls device-rate float32 chunks from its
//     play_queue_ on a dedicated render thread (no event, polled
//     padding);
//   * the editor's WasapiSink pushes s16 stereo from the engine worker
//     and paces on the buffer event when the device buffer is full.
//
// COM init contract (the editor's hardened semantics, now shared):
// OpenDevice records the apartment count only when our own
// CoInitializeEx succeeded; Close releases it only on the thread that
// took it — RPC_E_CHANGED_MODE means another component owns the
// apartment and must never be balanced.
//
// Windows-only: the class exists solely under _WIN32; consumers guard
// their members/uses the same way the old code did.

#include <cstdint>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <mmdeviceapi.h>
#include <audioclient.h>
#endif

namespace XER::Content::Video {

#ifdef _WIN32

class WasapiRenderClient {
public:
    WasapiRenderClient() = default;
    ~WasapiRenderClient();   // Close()
    WasapiRenderClient(const WasapiRenderClient&) = delete;
    WasapiRenderClient& operator=(const WasapiRenderClient&) = delete;

    // Stage 1 — COM init + MMDeviceEnumerator + the DEFAULT render
    // endpoint (eRender/eConsole) + Activate(IAudioClient). Closes any
    // previously opened chain first.
    bool OpenDevice(std::string* error_out = nullptr);

    // Stage 2 — IAudioClient::Initialize(AUDCLNT_SHAREMODE_SHARED,
    // stream_flags | EVENTCALLBACK, buffer_100ns, 0, fmt, nullptr),
    // then SetEventHandle (only when event_handle != nullptr),
    // GetBufferSize into buffer_frames() and GetService for the
    // IAudioRenderClient.
    //
    // Failure contract for format-negotiation loops: when
    // IAudioClient::Initialize itself rejects `fmt`, *rejected_hr
    // receives the HRESULT and the client stays uninitialized so the
    // caller can try the next candidate. Any failure AFTER a
    // successful Initialize is fatal — the whole chain is torn down
    // internally and the reason lands in error_out/LastError(). (A
    // retried Initialize on an initialized client returns
    // AUDCLNT_E_ALREADY_INITIALIZED, which is why post-init failures
    // cannot be "try another format".)
    bool Initialize(const WAVEFORMATEX& fmt, DWORD stream_flags,
                    REFERENCE_TIME buffer_100ns, HANDLE event_handle,
                    HRESULT* rejected_hr = nullptr,
                    std::string* error_out = nullptr);

    // IAudioClient::Start — false + error string on failure.
    bool Start(std::string* error_out = nullptr);
    void Stop();   // client->Stop() when open — Pause()
    void Reset();  // client->Reset() when open — seek: drop queued audio
    void Close();  // releases the chain; balances the COM init count
                   // only on the thread that took it

    // Raw accessors for the consumers' pacing loops (GetCurrentPadding,
    // GetBuffer/ReleaseBuffer, IsFormatSupported, GetMixFormat, ...).
    // The pointers stay valid until Close().
    IAudioClient* client() const { return audio_client_; }
    IAudioRenderClient* render_client() const { return render_client_; }
    // GetBufferSize captured by a successful Initialize (0 before).
    UINT32 buffer_frames() const { return buffer_frames_; }
    bool IsDeviceOpen() const { return audio_client_ != nullptr; }
    const std::string& LastError() const { return last_error_; }

    // Convenience for callers that immediately free the result with
    // CoTaskMemFree — nullptr on failure.
    WAVEFORMATEX* MixFormat() const {
        WAVEFORMATEX* fmt = nullptr;
        if (audio_client_ == nullptr ||
            FAILED(audio_client_->GetMixFormat(&fmt))) {
            return nullptr;
        }
        return fmt;
    }

private:
    // Records "<what> failed (hr=0x........)" into last_error_ /
    // *error_out and returns false. Does NOT tear down — the caller
    // decides whether the failure is fatal (Close) or recoverable
    // (format negotiation continues).
    bool Fail(const char* what, HRESULT hr, std::string* error_out);

    IMMDeviceEnumerator* enumerator_ = nullptr;
    IMMDevice* device_ = nullptr;
    IAudioClient* audio_client_ = nullptr;
    IAudioRenderClient* render_client_ = nullptr;
    UINT32 buffer_frames_ = 0;
    // True only when OUR CoInitializeEx took (or deepened) the
    // apartment reference — RPC_E_CHANGED_MODE holds no count.
    bool com_init_ = false;
    unsigned long com_thread_ = 0;   // thread that owns com_init_
    std::string last_error_;
};

#endif  // _WIN32

}  // namespace XER::Content::Video
