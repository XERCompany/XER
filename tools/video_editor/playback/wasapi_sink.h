// WASAPI shared-mode render sink for the VideoEditor preview engine —
// the real Windows implementation of IAudioSink (audio_output.h). The
// TU is compiled on every platform (the core CMake glob pulls in all of
// playback/*.cc unconditionally) but the class only exists under
// _WIN32; elsewhere MakeWasapiSink()/MakePlatformAudioSink() return
// nullptr + an error string.
//
// Pattern mirrors src/content/video/video_audio_player.cc (IMMDevice
// -> IAudioClient shared mode -> IAudioRenderClient) but stays small
// and self-contained: no VideoSession, no internal thread, no queue —
// Write() pushes straight into the device buffer and paces the pump by
// blocking on the WASAPI buffer event only while the buffer is full.
//
// Rate contract: the sink negotiates the device format at Init() or, if
// Init was never called, on the first Write() using that call's
// sample_rate. The engine feeds a constant mixer rate per session; if a
// later Write arrives with a different rate the sink re-opens the
// device at the new rate once (cheap, happens at most on session
// boundaries).
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "audio_output.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <mmdeviceapi.h>
#include <audioclient.h>
#endif

namespace XER::VideoEditor {

#ifdef _WIN32
// Push-model WASAPI sink. Opens the default render endpoint in shared
// mode with an event callback (~30 ms buffer); accepts the IAudioSink
// contract of interleaved s16 stereo at the negotiated rate.
//
// Format negotiation: the sink first requests PCM s16 stereo at the
// requested rate — the shared-mode audio engine converts to the device
// mix format for any sane rate. If the endpoint rejects that it retries
// with float32 stereo; if both fail it reports a clean error. When the
// engine hands us float32 the Write path does the trivial s16->f32
// convert; s16 writes go straight through.
//
// Write() pacing: blocks on the buffer event only when the device
// buffer is full — that is the pump's free A/V-sync throttle. On device
// loss (AUDCLNT_E_DEVICE_INVALIDATED et al.) Write attempts ONE
// reconnection to the (possibly new) default endpoint, then returns
// false so the engine sheds the chunk.
//
// Threading: Write() must be called from a single thread (the engine
// worker). The destructor may run on any thread after the engine's
// Shutdown() guarantees no Write is in flight; the WASAPI/COM objects
// used here are free-threaded (audioses.dll registers them Both).
class WasapiSink final : public IAudioSink {
public:
    WasapiSink();
    ~WasapiSink() override;   // Stop()s the stream, releases all COM
    WasapiSink(const WasapiSink&) = delete;
    WasapiSink& operator=(const WasapiSink&) = delete;

    bool Write(const std::int16_t* interleaved_samples,
               int frame_count, int sample_rate) override;

    // Opens the device eagerly at `sample_rate` instead of lazily on
    // the first Write. Idempotent: re-Init at the same rate while open
    // is a no-op; a different rate re-opens the endpoint.
    bool Init(int sample_rate, std::string* error_out = nullptr);
    bool IsOpen() const { return open_; }
    std::string LastError() const { return last_error_; }

private:
    bool Open(int sample_rate, std::string* error_out);
    void Close();
    bool Fail(const char* what, long hr, std::string* error_out);
    bool TryReopen();   // one device-loss recovery attempt

    IMMDeviceEnumerator* enumerator_ = nullptr;
    IMMDevice* device_ = nullptr;
    IAudioClient* audio_client_ = nullptr;
    IAudioRenderClient* render_client_ = nullptr;
    void* buffer_event_ = nullptr;     // HANDLE, owned
    unsigned int buffer_frames_ = 0;   // device buffer size in frames
    int rate_ = 0;                     // negotiated client rate (Hz)
    bool device_float_ = false;        // client format is f32 vs s16
    bool open_ = false;
    bool com_init_ = false;            // we hold a CoInitialize count
    unsigned long com_thread_ = 0;     // thread that owns that count
    std::string last_error_;
};
#endif  // _WIN32

// Convenience factory for callers that want the WASAPI sink directly.
// sample_rate > 0 eagerly inits the device (nullptr + *error_out on
// failure); sample_rate <= 0 defers negotiation to the first Write.
// Always nullptr on non-Windows.
std::unique_ptr<IAudioSink> MakeWasapiSink(int sample_rate,
                                           std::string* error_out = nullptr);

}  // namespace XER::VideoEditor
