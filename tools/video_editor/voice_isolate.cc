// voice_isolate.cc — GTCRN ONNX denoise pipeline. See voice_isolate.h
// for the feature contract; assets/models/README.md for the model
// provenance. The tensor contract below mirrors the sherpa-onnx
// OfflineSpeechDenoiserGtcrnImpl usage of the same file:
//   STFT  : n_fft=512, hop=256, win=512, sqrt symmetric Hann,
//           center=True with reflect padding, unnormalized
//   input : mix [1,257,1,2] (interleaved re/im, one frame per Run)
//           + conv_cache [2,1,16,16,33] + tra_cache [2,3,1,1,16]
//           + inter_cache [2,1,33,16] (zero-initialized, then the
//           previous frame's *_out tensors verbatim)
//   output: enh [1,257,1,2] + the three *_out caches
//   ISTFT : same sqrt-Hann at synthesis, OLA normalized by the sum of
//           window^2, center padding trimmed back off.
// The only deviation from sherpa's framing is that the tail is
// zero-extended to a whole hop so no input samples are dropped —
// their pipeline accepts the small tail loss, an editor effect must
// return exactly `frames` samples.
#include "voice_isolate.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <mutex>
#include <vector>

#include "audio/audio_core/audio_sample_rate_converter.h"
#include "content/ai/onnx_session/onnx_session.h"
#include "lab/ai_ops.h"

namespace XER::VideoEditor::VoiceIsolate {
namespace {

namespace Onnx = XER::AI::Onnx;
namespace Core = XER::Audio::Core;

constexpr float kPi = 3.14159265358979323846f;
constexpr int kFft = 512;          // n_fft == window length
constexpr int kHop = 256;          // hop_length
constexpr int kBins = kFft / 2 + 1;  // 257
constexpr int kPad = kFft / 2;     // center pad each side

// ── session cache (lab/ai_ops.cc pattern) ───────────────────────────
// One lazily-loaded session for the single staged model, guarded by a
// mutex that also serializes Run — the Onnx::Session contract is
// one-session-per-thread / borrowed output views, so all inference
// here happens under the lock.
std::mutex g_session_mutex;
std::unique_ptr<Onnx::Session> g_session;
std::string g_loaded_path;

Onnx::Session* GetSessionLocked(const std::string& path,
                                std::string* error_out) {
    if (g_session != nullptr && g_loaded_path == path) {
        return g_session.get();
    }
    auto session = std::make_unique<Onnx::Session>();
    std::string err;
    if (!session->Load(path, "", &err)) {
        if (error_out != nullptr) {
            *error_out = "gtcrn model load failed: " +
                         (err.empty() ? std::string("rejected by every "
                                                    "ONNX backend")
                                      : err);
        }
        return nullptr;
    }
    Onnx::Session* raw = session.get();
    g_session = std::move(session);
    g_loaded_path = path;
    return raw;
}

// ── minimal radix-2 complex FFT (512 pts; the dsp_algorithms
// FFTEngine's non-MKL/FFTW path is a naive O(N^2) DFT — unusable per
// frame, so this self-contained transform is deliberate) ────────────
using Cx = std::complex<float>;

void Fft(Cx* a, int n, bool inverse) {
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = 2.0f * kPi / static_cast<float>(len) *
                          (inverse ? 1.0f : -1.0f);
        const Cx wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            Cx w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j) {
                const Cx u = a[i + j];
                const Cx v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse) {
        const float inv = 1.0f / static_cast<float>(n);
        for (int i = 0; i < n; ++i) a[i] *= inv;
    }
}

// sqrt of the symmetric Hann window — analysis AND synthesis both
// apply it, so the effective OLA weight is the plain Hann (COLA).
std::vector<float> MakeWindow() {
    std::vector<float> w(kFft);
    for (int i = 0; i < kFft; ++i) {
        w[i] = std::sqrt(
            0.5f - 0.5f * std::cos(2.0f * kPi * i / (kFft - 1)));
    }
    return w;
}

// One-shot mono resample through the audio-core converter (HIGH =
// windowed sinc — offline path, quality over speed).
bool Resample(const std::vector<float>& in, int src_rate, int dst_rate,
              std::vector<float>* out, std::string* error_out) {
    if (src_rate <= 0 || dst_rate <= 0) {
        if (error_out) *error_out = "invalid sample rate";
        return false;
    }
    if (src_rate == dst_rate) {
        *out = in;
        return true;
    }
    Core::AudioSampleRateConverter conv(Core::ResampleQuality::HIGH);
    if (!conv.Initialize(src_rate, dst_rate)) {
        if (error_out) *error_out = "sample-rate converter init failed";
        return false;
    }
    const std::size_t cap = static_cast<std::size_t>(std::ceil(
                                static_cast<double>(in.size()) *
                                conv.GetRatio())) +
                            8;
    out->assign(cap, 0.0f);
    std::size_t produced = 0;
    if (!conv.Convert(in.data(), in.size(), out->data(), produced)) {
        if (error_out) *error_out = "sample-rate conversion failed";
        return false;
    }
    out->resize(produced);
    return true;
}

const Onnx::TensorView* FindOut(const std::vector<Onnx::TensorView>& o,
                                const char* name) {
    for (const Onnx::TensorView& t : o) {
        if (t.name == name) return &t;
    }
    return nullptr;
}

// Denoises one mono channel at the model rate. `x16` is 16 kHz float
// PCM; the enhanced result lands in `y16` with exactly x16.size()
// samples. Runs under g_session_mutex.
bool Denoise16k(const std::vector<float>& x16, Onnx::Session* session,
                std::vector<float>* y16, std::string* error_out) {
    const int n = static_cast<int>(x16.size());
    if (n <= 0) {
        y16->clear();
        return true;
    }
    // Centered reflect padding (knf Stft pad_mode "reflect"), falling
    // back to edge replication when the input is shorter than a pad
    // arm. The right arm additionally extends past the 256-sample
    // pad so the frame count covers every input sample (hop-aligned
    // tail — the model's conv caches make it causal-cheap).
    const int n2 = (n + kHop - 1) / kHop * kHop;  // >= n, hop-aligned
    const int rp = kPad + (n2 - n);               // right pad amount
    const bool reflect = n > kPad + 1;
    std::vector<float> padded(static_cast<std::size_t>(kPad) + n + rp,
                              0.0f);
    for (int i = 0; i < kPad; ++i) {
        padded[i] = reflect ? x16[kPad - i] : x16[0];
    }
    std::copy(x16.begin(), x16.end(), padded.begin() + kPad);
    for (int i = 0; i < rp; ++i) {
        const int ri = n - 2 - i;   // mirror around the last sample
        padded[kPad + n + i] = ri >= 0
                                   ? x16[ri]
                                   : (reflect ? x16[0] : x16[n - 1]);
    }

    const int total = static_cast<int>(padded.size());
    const int num_frames = 1 + (total - kFft) / kHop;

    const std::vector<float> win = MakeWindow();
    std::vector<Cx> spec(kFft);
    std::vector<float> mix(static_cast<std::size_t>(kBins) * 2);

    // Enhanced spectra, stored contiguously per frame for the ISTFT.
    std::vector<float> enh(
        static_cast<std::size_t>(num_frames) * kBins * 2, 0.0f);

    // Streaming caches — zero-init once, carried across frames.
    std::vector<float> conv_cache(2ull * 1 * 16 * 16 * 33, 0.0f);
    std::vector<float> tra_cache(2ull * 3 * 1 * 1 * 16, 0.0f);
    std::vector<float> inter_cache(2ull * 1 * 33 * 16, 0.0f);

    for (int t = 0; t < num_frames; ++t) {
        const float* fp = padded.data() + static_cast<std::size_t>(t) * kHop;
        for (int i = 0; i < kFft; ++i) {
            spec[i] = Cx(fp[i] * win[i], 0.0f);
        }
        Fft(spec.data(), kFft, false);
        for (int k = 0; k < kBins; ++k) {
            mix[2 * k] = spec[k].real();
            mix[2 * k + 1] = spec[k].imag();
        }

        Onnx::Input in_mix, in_conv, in_tra, in_inter;
        in_mix.name = "mix";
        in_mix.dims = {1, kBins, 1, 2};
        in_mix.data = mix.data();
        in_conv.name = "conv_cache";
        in_conv.dims = {2, 1, 16, 16, 33};
        in_conv.data = conv_cache.data();
        in_tra.name = "tra_cache";
        in_tra.dims = {2, 3, 1, 1, 16};
        in_tra.data = tra_cache.data();
        in_inter.name = "inter_cache";
        in_inter.dims = {2, 1, 33, 16};
        in_inter.data = inter_cache.data();

        std::vector<Onnx::TensorView> outs;
        std::string err;
        if (!session->Run({in_mix, in_conv, in_tra, in_inter}, &outs,
                          &err)) {
            if (error_out != nullptr) {
                *error_out = "gtcrn inference failed (frame " +
                             std::to_string(t) + "): " + err;
            }
            return false;
        }
        const Onnx::TensorView* o_enh = FindOut(outs, "enh");
        const Onnx::TensorView* o_conv = FindOut(outs, "conv_cache_out");
        const Onnx::TensorView* o_tra = FindOut(outs, "tra_cache_out");
        const Onnx::TensorView* o_int = FindOut(outs, "inter_cache_out");
        if (o_enh == nullptr || o_conv == nullptr || o_tra == nullptr ||
            o_int == nullptr || o_enh->ElementCount() != mix.size() ||
            o_conv->ElementCount() != conv_cache.size() ||
            o_tra->ElementCount() != tra_cache.size() ||
            o_int->ElementCount() != inter_cache.size()) {
            if (error_out != nullptr) {
                *error_out =
                    "gtcrn output contract mismatch (expected enh "
                    "[1,257,1,2] + 3 caches)";
            }
            return false;
        }
        std::memcpy(enh.data() + static_cast<std::size_t>(t) * kBins * 2,
                    o_enh->data, mix.size() * sizeof(float));
        std::memcpy(conv_cache.data(), o_conv->data,
                    conv_cache.size() * sizeof(float));
        std::memcpy(tra_cache.data(), o_tra->data,
                    tra_cache.size() * sizeof(float));
        std::memcpy(inter_cache.data(), o_int->data,
                    inter_cache.size() * sizeof(float));
    }

    // ISTFT: windowed inverse frames, OLA, divide by OLA of window^2,
    // then strip the center pad. Output length = n2, trimmed to n.
    const int out_len = kFft + (num_frames - 1) * kHop;
    std::vector<float> ola(static_cast<std::size_t>(out_len), 0.0f);
    std::vector<float> wsum(static_cast<std::size_t>(out_len), 0.0f);
    for (int t = 0; t < num_frames; ++t) {
        const float* fr = enh.data() + static_cast<std::size_t>(t) * kBins * 2;
        spec[0] = Cx(fr[0], 0.0f);
        spec[kBins - 1] = Cx(fr[2 * (kBins - 1)], 0.0f);
        for (int k = 1; k < kBins - 1; ++k) {
            spec[k] = Cx(fr[2 * k], fr[2 * k + 1]);
            spec[kFft - k] = Cx(fr[2 * k], -fr[2 * k + 1]);
        }
        Fft(spec.data(), kFft, true);
        const int base = t * kHop;
        for (int i = 0; i < kFft; ++i) {
            ola[base + i] += spec[i].real() * win[i];
            wsum[base + i] += win[i] * win[i];
        }
    }
    y16->assign(static_cast<std::size_t>(n2), 0.0f);
    for (int i = 0; i < n2; ++i) {
        const int idx = kPad + i;
        const float d = wsum[idx];
        (*y16)[i] = d > 1e-8f ? ola[idx] / d : 0.0f;
    }
    y16->resize(x16.size());
    return true;
}

}  // namespace

std::string ModelPath() { return Lab::FindAiModel(kModelFile); }

bool Available() {
    return !ModelPath().empty() && !Onnx::ActiveBackendId().empty();
}

bool ProcessBuffer(float* samples, std::int32_t frames,
                   std::int32_t channels, std::int32_t sample_rate,
                   float amount, std::string* error_out) {
    if (samples == nullptr || frames <= 0 || channels <= 0 ||
        sample_rate <= 0) {
        if (error_out) *error_out = "invalid audio buffer";
        return false;
    }
    const float a = std::clamp(amount, 0.0f, 1.0f);
    if (a <= 0.0f) {
        return true;   // amount 0 = verified pass-through no-op
    }
    const std::string model = ModelPath();
    if (model.empty()) {
        if (error_out) {
            *error_out = Lab::AiModelHint(kModelFile);
        }
        return false;
    }

    std::lock_guard lock(g_session_mutex);
    Onnx::Session* session = GetSessionLocked(model, error_out);
    if (session == nullptr) return false;

    std::vector<float> mono(static_cast<std::size_t>(frames));
    std::vector<float> x16, y16, wet;
    for (std::int32_t c = 0; c < channels; ++c) {
        for (std::int32_t i = 0; i < frames; ++i) {
            mono[i] = samples[static_cast<std::size_t>(i) * channels + c];
        }
        std::string err;
        if (!Resample(mono, sample_rate, kModelRate, &x16, &err)) {
            if (error_out) *error_out = err;
            return false;
        }
        if (!Denoise16k(x16, session, &y16, &err)) {
            if (error_out) *error_out = err;
            return false;
        }
        if (!Resample(y16, kModelRate, sample_rate, &wet, &err)) {
            if (error_out) *error_out = err;
            return false;
        }
        // Resampling returns ~frames samples (+-2); hold the last
        // sample across any short tail so the blend never clicks.
        if (wet.size() < mono.size() && !wet.empty()) {
            wet.resize(mono.size(), wet.back());
        }
        for (std::int32_t i = 0; i < frames; ++i) {
            const float w = i < static_cast<std::int32_t>(wet.size())
                                ? wet[i]
                                : 0.0f;
            float& s = samples[static_cast<std::size_t>(i) * channels + c];
            s = s * (1.0f - a) + w * a;
        }
    }
    return true;
}

}  // namespace XER::VideoEditor::VoiceIsolate
