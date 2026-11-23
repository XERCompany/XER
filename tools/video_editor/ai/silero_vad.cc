// Implementation of ai/silero_vad.h — the Silero VAD streaming
// wrapper on the shared ONNX session. The model is a top-level
// If(Equal(sr, 16000)) selecting the 16 kHz subgraph, so `sr` rides
// the int64 input binding (Onnx::Input::data_i64) and everything
// else is f32. The state tensor is carried verbatim like GTCRN's
// caches in voice_isolate.cc — zero-init once, previous stateN after.
#include "ai/silero_vad.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "content/ai/onnx_session/onnx_session.h"
#include "lab/ai_ops.h"

namespace XER::VideoEditor::Ai {
namespace {

namespace Onnx = XER::AI::Onnx;

constexpr std::size_t kStateElems = 2ull * 1 * 128;

const Onnx::TensorView* FindOut(const std::vector<Onnx::TensorView>& o,
                                const char* name) {
    for (const Onnx::TensorView& t : o) {
        if (t.name == name) return &t;
    }
    return nullptr;
}

}  // namespace

std::string SileroVadModel() { return Lab::FindAiModel(kSileroVadFile); }

bool SileroVadAvailable() {
    return !SileroVadModel().empty() &&
           !Onnx::ActiveBackendId().empty();
}

SileroVad::SileroVad() = default;
SileroVad::~SileroVad() = default;

bool SileroVad::Load(const std::string& model_path,
                     std::string* error_out) {
    session_.reset();
    if (model_path.empty()) {
        if (error_out) *error_out = Lab::AiModelHint(kSileroVadFile);
        return false;
    }
    auto session = std::make_unique<Onnx::Session>();
    std::string err;
    if (!session->Load(model_path, "", &err)) {
        if (error_out) {
            *error_out = "silero_vad load failed: " +
                         (err.empty()
                              ? std::string("rejected by every ONNX "
                                            "backend")
                              : err);
        }
        return false;
    }
    // `sr`'s declared rank varies between exports (scalar [] on the
    // onnx-community fp32 file, [1] on some re-exports) — feed the
    // declared shape so ORT's strict shape check never sees a rank
    // mismatch. Anything bigger than a single value is a contract
    // break the first Run would report anyway.
    sr_dims_.assign(1, 1);
    const auto& names = session->InputNames();
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i] == "sr") {
            const std::vector<std::int64_t> d = session->InputDims(i);
            std::size_t count = 1;
            bool ok = true;
            for (const std::int64_t dim : d) {
                if (dim <= 0 || dim > 4) {
                    ok = false;
                    break;
                }
                count *= static_cast<std::size_t>(dim);
            }
            if (ok && count == 1) {
                sr_dims_ = d;   // scalar [] or singleton dims
            }
            break;
        }
    }
    session_ = std::move(session);
    Reset();
    return true;
}

bool SileroVad::IsLoaded() const {
    return session_ != nullptr && session_->IsLoaded();
}

void SileroVad::Reset() { state_.assign(kStateElems, 0.0f); }

bool SileroVad::ProcessWindow(const float* samples, float* prob_out,
                              std::string* error_out) {
    if (!IsLoaded()) {
        if (error_out) *error_out = "silero session is not loaded";
        return false;
    }
    if (samples == nullptr || prob_out == nullptr) {
        if (error_out) *error_out = "null VAD in/out";
        return false;
    }
    Onnx::Input in_audio, in_state, in_sr;
    in_audio.name = "input";
    in_audio.dims = {1, kVadWindowSamples};
    in_audio.data = samples;
    in_state.name = "state";
    in_state.dims = {2, 1, 128};
    in_state.data = state_.data();
    in_sr.name = "sr";
    in_sr.dims = sr_dims_;
    in_sr.data_i64 = &sr_;

    std::vector<Onnx::TensorView> outs;
    std::string err;
    if (!session_->Run({in_audio, in_state, in_sr}, &outs, &err)) {
        if (error_out) {
            *error_out = "silero inference failed: " + err;
        }
        return false;
    }
    const Onnx::TensorView* o_prob = FindOut(outs, "output");
    const Onnx::TensorView* o_state = FindOut(outs, "stateN");
    if (o_prob == nullptr || o_state == nullptr ||
        o_prob->data == nullptr || o_state->data == nullptr ||
        o_prob->ElementCount() < 1 ||
        o_state->ElementCount() != kStateElems) {
        if (error_out) {
            *error_out =
                "silero output contract mismatch (expected output "
                "[B,1] + stateN [2,B,128])";
        }
        return false;
    }
    *prob_out = o_prob->data[0];
    std::memcpy(state_.data(), o_state->data,
                kStateElems * sizeof(float));
    return true;
}

bool DetectSpeech(SileroVad& vad, const float* samples,
                  std::size_t n, const VadSpeechParams& params,
                  std::vector<SpeechSpan>* spans_out,
                  std::vector<float>* probs_out,
                  std::string* error_out) {
    if (spans_out != nullptr) spans_out->clear();
    if (probs_out != nullptr) probs_out->clear();
    if (samples == nullptr || n == 0) {
        if (error_out) *error_out = "no audio for VAD";
        return false;
    }
    if (!vad.IsLoaded()) {
        if (error_out) *error_out = "silero session is not loaded";
        return false;
    }
    const float on = std::clamp(params.speech_on, 0.0f, 1.0f);
    const float off =
        std::clamp(std::min(params.speech_off, on), 0.0f, 1.0f);
    const double window_s = static_cast<double>(kVadWindowSamples) /
                            kVadSampleRate;

    vad.Reset();
    const std::size_t windows =
        (n + kVadWindowSamples - 1) / kVadWindowSamples;
    std::vector<float> probs(windows);
    std::vector<float> chunk(kVadWindowSamples, 0.0f);
    for (std::size_t w = 0; w < windows; ++w) {
        const std::size_t off_s = w * kVadWindowSamples;
        const std::size_t take =
            std::min<std::size_t>(kVadWindowSamples, n - off_s);
        const float* block = samples + off_s;
        if (take < kVadWindowSamples) {
            std::fill(chunk.begin(), chunk.end(), 0.0f);
            std::memcpy(chunk.data(), block, take * sizeof(float));
            block = chunk.data();
        }
        std::string err;
        if (!vad.ProcessWindow(block, &probs[w], &err)) {
            if (error_out) *error_out = err;
            return false;
        }
    }
    if (probs_out != nullptr) *probs_out = probs;

    // Hysteresis walk: prob >= speech_on opens a run, prob <=
    // speech_off closes it; between the two the current state holds.
    std::vector<SpeechSpan> raw;
    int run_start = -1;
    double run_sum = 0.0;
    int run_len = 0;
    const auto flush = [&](std::size_t end_index) {
        if (run_start < 0) return;
        SpeechSpan s;
        s.t0 = static_cast<double>(run_start) * window_s;
        s.t1 = static_cast<double>(end_index) * window_s;
        s.mean_prob = run_len > 0
                          ? static_cast<float>(run_sum / run_len)
                          : 0.0f;
        raw.push_back(s);
        run_start = -1;
        run_sum = 0.0;
        run_len = 0;
    };
    bool in_speech = false;
    for (std::size_t i = 0; i < windows; ++i) {
        const float p = probs[i];
        if (!in_speech) {
            if (p >= on) {
                in_speech = true;
                run_start = static_cast<int>(i);
                run_sum = p;
                run_len = 1;
            }
        } else {
            if (p <= off) {
                in_speech = false;
                flush(i);
            } else {
                run_sum += p;
                ++run_len;
            }
        }
    }
    flush(windows);

    // Span shaping: truncate the padded tail, merge runs split by a
    // sub-min_silence gap, then drop sub-min_speech runs.
    const double dur_s = static_cast<double>(n) / kVadSampleRate;
    std::vector<SpeechSpan> merged;
    for (const SpeechSpan& s : raw) {
        SpeechSpan c = s;
        c.t0 = std::min(c.t0, dur_s);
        c.t1 = std::min(c.t1, dur_s);
        if (!merged.empty() &&
            c.t0 - merged.back().t1 < params.min_silence) {
            merged.back().t1 = c.t1;
            merged.back().mean_prob = std::max(merged.back().mean_prob,
                                               c.mean_prob);
            continue;
        }
        merged.push_back(c);
    }
    if (spans_out != nullptr) {
        for (const SpeechSpan& s : merged) {
            if (s.t1 - s.t0 >= params.min_speech) {
                spans_out->push_back(s);
            }
        }
    }
    return true;
}

}  // namespace XER::VideoEditor::Ai
