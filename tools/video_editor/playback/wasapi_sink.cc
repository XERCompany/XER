// See wasapi_sink.h — WASAPI shared-mode render sink (Windows only).

#include "wasapi_sink.h"

#include <cstring>

#ifdef _WIN32
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

namespace {

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

// WAVEFORMATEX tags (kept local — mmreg.h arrives via windows.h).
constexpr WORD kTagPcm = WAVE_FORMAT_PCM;            // 0x0001
constexpr WORD kTagFloat = WAVE_FORMAT_IEEE_FLOAT;   // 0x0003
constexpr WORD kTagExtensible = WAVE_FORMAT_EXTENSIBLE;  // 0xFFFE

// KSDATAFORMAT_SUBTYPE_PCM / _IEEE_FLOAT — defined inline instead of
// pulling in ksmedia.h (and its ksguid dependency) for two GUIDs.
const GUID kSubPcm = {0x00000001, 0x0000, 0x0010,
                      {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
const GUID kSubFloat = {0x00000003, 0x0000, 0x0010,
                        {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

// Byte count of a WAVEFORMATEX(+EXTENSIBLE) blob — sizeof(WAVEFORMATEX)
// already includes the cbSize field.
std::size_t WaveFormatBytes(const WAVEFORMATEX& fmt) {
    return sizeof(WAVEFORMATEX) + fmt.cbSize;
}

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

    // Shared endpoint plumbing (src/content/video/wasapi_render_client)
    // — COM init + enumerator + default render endpoint + IAudioClient.
    // Its apartment handling carries the RPC_E_CHANGED_MODE semantics
    // this path needs: the editor's UI thread is STA (the canvas/D2D
    // init calls CoInitializeEx(COINIT_APARTMENTTHREADED) on it), an
    // existing STA init still satisfies the free-threaded WASAPI
    // objects, and only a real init count gets balanced in Close().
    if (!wasapi_.OpenDevice(error_out)) {
        last_error_ = wasapi_.LastError();
        if (error_out != nullptr) *error_out = last_error_;
        return false;
    }

    buffer_event_ = CreateEventW(nullptr, /*bManualReset=*/FALSE,
                                 /*bInitialState=*/FALSE, nullptr);
    if (buffer_event_ == nullptr) {
        return Fail("CreateEvent",
                    HRESULT_FROM_WIN32(GetLastError()), error_out);
    }

    HRESULT hr = S_OK;

    // Negotiation stage 1 — direct stream support at the requested
    // rate: s16 stereo (zero-copy Write), then float32 stereo. The
    // shared-mode engine converts sample SIZE but not rate/channel
    // count, so this only succeeds when the device mix rate matches.
    // The closest-match blob (usually the mix format) is kept for the
    // stage-3 fallback.
    WAVEFORMATEX pcm16{};
    pcm16.wFormatTag = kTagPcm;
    pcm16.nChannels = 2;
    pcm16.nSamplesPerSec = static_cast<DWORD>(sample_rate);
    pcm16.wBitsPerSample = 16;
    pcm16.nBlockAlign =
        static_cast<WORD>(pcm16.nChannels * pcm16.wBitsPerSample / 8);
    pcm16.nAvgBytesPerSec = pcm16.nSamplesPerSec * pcm16.nBlockAlign;
    WAVEFORMATEX f32{};
    f32.wFormatTag = kTagFloat;
    f32.nChannels = 2;
    f32.nSamplesPerSec = static_cast<DWORD>(sample_rate);
    f32.wBitsPerSample = 32;
    f32.nBlockAlign = 8;
    f32.nAvgBytesPerSec = static_cast<DWORD>(sample_rate) * 8;

    std::vector<std::uint8_t> closest_buf;
    const WAVEFORMATEX* direct = nullptr;
    for (const WAVEFORMATEX* cand : {&pcm16, &f32}) {
        WAVEFORMATEX* closest = nullptr;
        hr = wasapi_.client()->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,
                                                 cand, &closest);
        if (hr == S_OK) {
            if (closest != nullptr) CoTaskMemFree(closest);
            direct = cand;
            break;
        }
        // S_FALSE: not usable directly but the engine suggests the
        // closest supported format — stash it (usually the mix format).
        if (hr == S_FALSE && closest != nullptr &&
            closest_buf.empty()) {
            closest_buf.assign(
                reinterpret_cast<const std::uint8_t*>(closest),
                reinterpret_cast<const std::uint8_t*>(closest) +
                    WaveFormatBytes(*closest));
        }
        if (closest != nullptr) CoTaskMemFree(closest);
    }

    HRESULT init_hr = S_OK;
    bool fatal = false;
    if (direct != nullptr) {
        if (!Activate(*direct, 0, &init_hr, &fatal, error_out)) {
            // Initialize failed even though IsFormatSupported liked
            // the format — a deeper problem, not a negotiation one.
            if (!fatal) {
                Fail("IAudioClient::Initialize", init_hr, error_out);
            }
            return false;
        }
        rate_ = sample_rate;
        device_rate_ = sample_rate;
        device_channels_ = 2;
        out_kind_ = direct == &f32 ? OutKind::kF32 : OutKind::kS16;
        device_float_ = out_kind_ == OutKind::kF32;
        open_ = true;
        last_error_.clear();
        return true;
    }

    // Negotiation stage 2 — AUTOCONVERTPCM (Windows 10 1709+): the
    // engine inserts an SRC + channel matrixer so our 2ch stream at
    // `sample_rate` plays on an endpoint mixing at any other rate.
    for (const WAVEFORMATEX* cand : {&pcm16, &f32}) {
        fatal = false;
        if (!Activate(*cand,
                      AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                          AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                      &init_hr, &fatal, error_out)) {
            if (fatal) return false;
            continue;
        }
        rate_ = sample_rate;
        device_rate_ = sample_rate;
        device_channels_ = 2;
        out_kind_ = cand == &f32 ? OutKind::kF32 : OutKind::kS16;
        device_float_ = out_kind_ == OutKind::kF32;
        open_ = true;
        last_error_.clear();
        return true;
    }

    // Negotiation stage 3 — adopt the closest-match mix format and
    // convert client-side in Write() (resample + channel remap +
    // sample-kind convert). Covers pre-1709 Windows and endpoints
    // without a conversion APO.
    if (!closest_buf.empty()) {
        const auto& cf =
            *reinterpret_cast<const WAVEFORMATEX*>(closest_buf.data());
        OutKind kind = OutKind::kS16;
        bool kind_ok = false;
        WORD tag = cf.wFormatTag;
        WORD bits = cf.wBitsPerSample;
        if (tag == kTagExtensible && cf.cbSize >= 22) {
            const auto* ext =
                reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(&cf);
            if (IsEqualGUID(ext->SubFormat, kSubFloat)) {
                kind = OutKind::kF32;
                kind_ok = ext->Format.wBitsPerSample == 32;
            } else if (IsEqualGUID(ext->SubFormat, kSubPcm)) {
                bits = ext->Format.wBitsPerSample;
                tag = kTagPcm;
            }
        }
        if (!kind_ok) {
            if (tag == kTagFloat && bits == 32) {
                kind = OutKind::kF32;
                kind_ok = true;
            } else if (tag == kTagPcm &&
                       (bits == 16 || bits == 24 || bits == 32)) {
                kind = bits == 16   ? OutKind::kS16
                       : bits == 24 ? OutKind::kS24
                                    : OutKind::kS32;
                kind_ok = true;
            }
        }
        fatal = false;
        if (kind_ok && cf.nChannels >= 1 && cf.nChannels <= 32 &&
            cf.nSamplesPerSec > 0 &&
            Activate(cf, 0, &init_hr, &fatal, error_out)) {
            rate_ = sample_rate;
            device_rate_ = static_cast<int>(cf.nSamplesPerSec);
            device_channels_ = static_cast<int>(cf.nChannels);
            out_kind_ = kind;
            device_float_ = kind == OutKind::kF32;
            resample_pos_ = 0.0;
            resample_prev_l_ = 0;
            resample_prev_r_ = 0;
            open_ = true;
            last_error_.clear();
            return true;
        }
        if (fatal) return false;
    }

    return Fail("format negotiation (no usable shared-mode format)",
                init_hr, error_out);
}

// Runs the shared-client Initialize (Initialize + event hookup +
// buffer query + IAudioRenderClient service) + Start for `fmt`.
// `stream_flags` are extra flags for the AUTOCONVERTPCM stage — the
// shared client ORs in EVENTCALLBACK itself because we pass it our
// buffer event. Returns false with *init_hr set to the rejection
// HRESULT when IAudioClient::Initialize declines the format — the
// caller may try another candidate. A failure AFTER a successful
// Initialize is fatal: the shared client has already torn the chain
// down (a retried Initialize would return AUDCLNT_E_ALREADY_INITIALIZED)
// and *fatal_out is set so the caller aborts Open rather than
// negotiating further.
bool WasapiSink::Activate(const WAVEFORMATEX& fmt, DWORD stream_flags,
                          HRESULT* init_hr, bool* fatal_out,
                          std::string* error_out) {
    *init_hr = S_OK;
    if (!wasapi_.Initialize(fmt, stream_flags, kBufferDuration100ns,
                           static_cast<HANDLE>(buffer_event_),
                           init_hr, error_out)) {
        if (*init_hr != S_OK) {
            return false;   // format rejected — try the next candidate
        }
        // Post-Initialize failure: the shared client already tore the
        // chain down; surface its reason and mark fatal.
        last_error_ = wasapi_.LastError();
        if (error_out != nullptr) *error_out = last_error_;
        *fatal_out = true;
        return false;
    }
    if (wasapi_.Start(error_out)) {
        return true;
    }
    // Start() failure is fatal too — a stopped-but-initialized client
    // cannot be re-negotiated either.
    last_error_ = wasapi_.LastError();
    if (error_out != nullptr) *error_out = last_error_;
    *fatal_out = true;
    Close();
    return false;
}

void WasapiSink::Close() {
    wasapi_.Close();
    if (buffer_event_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(buffer_event_));
        buffer_event_ = nullptr;
    }
    device_rate_ = 0;
    device_channels_ = 2;
    resample_pos_ = 0.0;
    resample_prev_l_ = 0;
    resample_prev_r_ = 0;
    open_ = false;
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

// Writes `l`/`r` (float, unclamped) as one output frame of
// `device_channels_` samples in the negotiated sample kind: ch0 = L
// (mono endpoints get the (L+R)/2 fold), ch1 = R, any extra channels
// are zeroed — standard layouts order FL/FR first.
void WasapiSink::StoreFrame(std::uint8_t* dst, float l, float r) const {
    const int channels = device_channels_;
    for (int c = 0; c < channels; ++c) {
        float v = 0.0f;
        if (c == 0) {
            v = channels == 1 ? (l + r) * 0.5f : l;
        } else if (c == 1) {
            v = r;
        }
        switch (out_kind_) {
            case OutKind::kF32: {
                float out;
                std::memcpy(&out, &v, sizeof(out));
                std::memcpy(dst, &out, sizeof(out));
                dst += sizeof(float);
                break;
            }
            case OutKind::kS16: {
                const std::int16_t s = static_cast<std::int16_t>(
                    std::lrint(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
                std::memcpy(dst, &s, sizeof(s));
                dst += sizeof(std::int16_t);
                break;
            }
            case OutKind::kS24: {
                const std::int32_t s = static_cast<std::int32_t>(
                    std::lrint(std::clamp(v, -1.0f, 1.0f) * 8388607.0f));
                dst[0] = static_cast<std::uint8_t>(s & 0xff);
                dst[1] = static_cast<std::uint8_t>((s >> 8) & 0xff);
                dst[2] = static_cast<std::uint8_t>((s >> 16) & 0xff);
                dst += 3;
                break;
            }
            case OutKind::kS32: {
                const std::int32_t s = static_cast<std::int32_t>(
                    std::lrint(std::clamp(v, -1.0f, 1.0f) *
                               2147483647.0f));
                std::memcpy(dst, &s, sizeof(s));
                dst += sizeof(std::int32_t);
                break;
            }
        }
    }
}

// Pacing skeleton shared by the fast and converted write paths:
// returns the number of writable device frames (0 = caller should
// retry/wait) after handling buffer-space waits and ONE reconnect
// attempt per call. Returns -1 on unrecoverable failure.
long WasapiSink::WaitForSpace(int* reconnects) {
    UINT32 padding = 0;
    HRESULT hr = wasapi_.client()->GetCurrentPadding(&padding);
    if (FAILED(hr)) {
        if (++*reconnects > kMaxReconnectAttempts || !TryReopen()) {
            return -1;
        }
        return 0;
    }
    const UINT32 available =
        padding < wasapi_.buffer_frames()
            ? wasapi_.buffer_frames() - padding
            : 0;
    if (available != 0) {
        return static_cast<long>(available);
    }
    // Buffer full — block until the engine drains a period. This is
    // the pump's pacing throttle.
    const DWORD wait = WaitForSingleObject(buffer_event_, kBufferWaitMs);
    if (wait == WAIT_OBJECT_0) {
        return 0;
    }
    if (wait == WAIT_TIMEOUT) {
        last_error_ = "WasapiSink: buffer wait timed out";
        return -1;
    }
    // WAIT_FAILED/abandoned — treat as device loss.
    if (++*reconnects > kMaxReconnectAttempts || !TryReopen()) {
        return -1;
    }
    return 0;
}

// Converted write path (closest-match fallback): linear-interpolating
// resample from the caller's rate to device_rate_ plus channel remap
// and sample-kind conversion. `resample_pos_` carries the fractional
// source position across calls so block boundaries stay click-free;
// index -1 addresses the previous call's last input frame.
bool WasapiSink::WriteConverted(const std::int16_t* src, int count,
                                int sample_rate) {
    double pos = resample_pos_;
    int reconnects = 0;
    while (pos < static_cast<double>(count)) {
        // Recomputed each iteration: a mid-write reconnect can
        // re-negotiate device_rate_/channels_/kind (e.g. the new
        // default endpoint has a different mix format).
        const double step =
            static_cast<double>(sample_rate) / device_rate_;
        const std::size_t frame_bytes =
            static_cast<std::size_t>(device_channels_) *
            (out_kind_ == OutKind::kS16 ? 2u
             : out_kind_ == OutKind::kS24 ? 3u
                                         : 4u);
        const long space = WaitForSpace(&reconnects);
        if (space < 0) {
            // The dropped chunk breaks continuity anyway — restart the
            // resampler cleanly at the next call rather than carrying
            // a position into a block that never arrives.
            resample_pos_ = 0.0;
            return false;
        }
        if (space == 0) {
            continue;
        }
        BYTE* dst = nullptr;
        HRESULT hr = wasapi_.render_client()->GetBuffer(
            static_cast<UINT32>(space), &dst);
        if (FAILED(hr) || dst == nullptr) {
            if (++reconnects > kMaxReconnectAttempts || !TryReopen()) {
                resample_pos_ = 0.0;
                return false;
            }
            continue;
        }
        UINT32 produced = 0;
        while (produced < static_cast<UINT32>(space) &&
               pos < static_cast<double>(count)) {
            const int i = static_cast<int>(std::floor(pos));
            const double t = pos - static_cast<double>(i);
            // a = input frame i (index -1 = previous block's tail);
            // b = input frame i+1 (past the block edge: hold the last
            // sample — a <=1-frame flat tail per Write is inaudible).
            const float a_l =
                (i < 0 ? resample_prev_l_ : src[i * 2]) / 32768.0f;
            const float a_r =
                (i < 0 ? resample_prev_r_ : src[i * 2 + 1]) / 32768.0f;
            const int j = i + 1;
            const int k = j < count ? j : count - 1;
            const float b_l = src[k * 2] / 32768.0f;
            const float b_r = src[k * 2 + 1] / 32768.0f;
            StoreFrame(dst + static_cast<std::size_t>(produced) *
                                 frame_bytes,
                       a_l + (b_l - a_l) * static_cast<float>(t),
                       a_r + (b_r - a_r) * static_cast<float>(t));
            pos += step;
            ++produced;
        }
        wasapi_.render_client()->ReleaseBuffer(produced, 0);
        if (produced == 0) {
            break;   // input exhausted mid-request
        }
    }
    resample_prev_l_ = src[(count - 1) * 2];
    resample_prev_r_ = src[(count - 1) * 2 + 1];
    resample_pos_ = pos - static_cast<double>(count);
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

    // Converted path: the negotiated stream isn't plain stereo at the
    // caller's rate — resample/remap per frame.
    if (device_rate_ != sample_rate || device_channels_ != 2 ||
        (out_kind_ != OutKind::kS16 && out_kind_ != OutKind::kF32)) {
        return WriteConverted(interleaved_samples, frame_count,
                              sample_rate);
    }

    const std::int16_t* src = interleaved_samples;
    int left = frame_count;
    int reconnects = 0;

    while (left > 0) {
        // A mid-write reconnect may have re-negotiated to a
        // non-direct format — hand the remainder to the converter.
        if (device_rate_ != sample_rate || device_channels_ != 2 ||
            (out_kind_ != OutKind::kS16 &&
             out_kind_ != OutKind::kF32)) {
            return WriteConverted(src, left, sample_rate);
        }
        const long space = WaitForSpace(&reconnects);
        if (space < 0) {
            return false;
        }
        if (space == 0) {
            continue;
        }
        const UINT32 n =
            (std::min)(static_cast<UINT32>(space),
                       static_cast<UINT32>(left));
        BYTE* dst = nullptr;
        HRESULT hr = wasapi_.render_client()->GetBuffer(n, &dst);
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
        wasapi_.render_client()->ReleaseBuffer(n, 0);
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
