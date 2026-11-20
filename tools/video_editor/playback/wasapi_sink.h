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
// Format negotiation (shared mode): the audio engine converts PCM
// sample SIZE only — the client stream must match the device's mix
// RATE and channel count unless the stream is opened with
// AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM (Win10 1709+). The sink therefore
// negotiates in three stages:
//   1. direct: s16 stereo then float32 stereo at the requested rate —
//      zero-copy Write on the common 48 kHz endpoint;
//   2. AUTOCONVERTPCM: the same two formats with the engine's
//      SRC/channel-matrix flag when the endpoint's mix rate differs
//      (e.g. 44.1 kHz onboard audio, Bluetooth headsets);
//   3. closest-match: adopt the format IsFormatSupported suggested
//      (the device mix format) and resample/remap client-side in
//      Write — covers pre-1709 Windows and odd endpoints.
// Without any of this the sink used to reject outright whenever the
// mix rate wasn't the requested 48 kHz, leaving previews silent.
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
    std::string LastError() const override { return last_error_; }

    // Opens the device eagerly at `sample_rate` instead of lazily on
    // the first Write. Idempotent: re-Init at the same rate while open
    // is a no-op; a different rate re-opens the endpoint.
    bool Init(int sample_rate, std::string* error_out = nullptr);
    bool IsOpen() const { return open_; }

private:
    enum class OutKind { kS16, kS24, kS32, kF32 };

    bool Open(int sample_rate, std::string* error_out);
    // Initialize/SetEventHandle/GetBufferSize/GetService/Start for one
    // candidate format. Returns false with *init_hr set when
    // Initialize rejects the format (negotiation may try another
    // candidate); a failure after a successful Initialize is fatal —
    // Fail() runs and *fatal_out is set so the caller aborts Open.
    bool Activate(const WAVEFORMATEX& fmt, DWORD stream_flags,
                  HRESULT* init_hr, bool* fatal_out,
                  std::string* error_out);
    void Close();
    bool Fail(const char* what, long hr, std::string* error_out);
    bool TryReopen();   // one device-loss recovery attempt
    // Shared write-path skeleton: returns writable device frames
    // (0 = wait/retry — event fired or reconnect consumed), -1 on
    // unrecoverable failure. `reconnects` bounds TryReopen attempts
    // per Write call.
    long WaitForSpace(int* reconnects);
    // Write() inner loop for the converted path: resamples `src`
    // (s16 stereo, `count` frames at `sample_rate`) to the negotiated
    // device rate/channels/sample-kind while pacing on the buffer.
    bool WriteConverted(const std::int16_t* src, int count,
                        int sample_rate);
    // Writes one stereo float pair into `dst` as `channels` samples of
    // the negotiated kind. `sample` is the dst frame base.
    void StoreFrame(std::uint8_t* dst, float l, float r) const;

    IMMDeviceEnumerator* enumerator_ = nullptr;
    IMMDevice* device_ = nullptr;
    IAudioClient* audio_client_ = nullptr;
    IAudioRenderClient* render_client_ = nullptr;
    void* buffer_event_ = nullptr;     // HANDLE, owned
    unsigned int buffer_frames_ = 0;   // device buffer size in frames
    int rate_ = 0;                     // client (Write) rate in Hz
    int device_rate_ = 0;              // negotiated stream rate (Hz)
    int device_channels_ = 2;          // negotiated stream channels
    OutKind out_kind_ = OutKind::kS16; // negotiated stream sample kind
    bool device_float_ = false;        // out_kind_ == kF32
    // Client-side resample state (closest-match fallback only):
    // `pos` is the fractional source index of the next output frame in
    // units of the CURRENT Write call's input block; `prev` carries
    // the previous block's last frame for boundary interpolation.
    double resample_pos_ = 0.0;
    std::int16_t resample_prev_l_ = 0;
    std::int16_t resample_prev_r_ = 0;
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
