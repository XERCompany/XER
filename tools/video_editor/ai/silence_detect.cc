// Implementation of ai/silence_detect.h — windowed RMS over
// ExtractMonoPcm output plus the LoudnessMeterLUFS pass.
#include "ai/silence_detect.h"

#include <algorithm>
#include <cmath>

#include "ai/audio_pcm.h"
#include "audio/analysis_ai/anal_loudness_meter_lufs.h"

namespace XER::VideoEditor::Ai {
namespace {

constexpr float kDbFloor = -120.0f;

float LinearToDb(double v) {
    return v > 1e-9
               ? static_cast<float>(20.0 * std::log10(v))
               : kDbFloor;
}

}  // namespace

bool DetectSilences(const std::string& utf8_path,
                    const SilenceDetectParams& params,
                    bool with_envelope, SilenceResult* out,
                    std::string* error_out) {
    if (out == nullptr) return false;
    out->silences.clear();
    out->envelope.clear();
    out->duration_seconds = 0.0;
    out->threshold_db = params.threshold_db;

    // 16 kHz mono: plenty for a level envelope, 4x cheaper than 48k.
    Pcm pcm;
    if (!ExtractMonoPcm(utf8_path, 16000, params.max_seconds, &pcm,
                        error_out)) {
        return false;
    }
    if (pcm.samples.empty() || pcm.sample_rate <= 0) {
        if (error_out) *error_out = "no decodable audio";
        return false;
    }
    out->duration_seconds = pcm.duration_seconds;

    const double window_ms = std::clamp(params.window_ms, 10.0, 500.0);
    const int win = std::max(
        1, static_cast<int>(pcm.sample_rate * window_ms / 1000.0));
    const int count =
        static_cast<int>(pcm.samples.size() / static_cast<size_t>(win));
    if (count <= 0) {
        if (error_out) *error_out = "audio shorter than one window";
        return false;
    }

    // Envelope pass — one RMS/peak pair per non-overlapping window.
    out->envelope.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const float* p =
            pcm.samples.data() + static_cast<size_t>(i) * win;
        double sum_sq = 0.0;
        float peak = 0.0f;
        for (int j = 0; j < win; ++j) {
            const float s = std::fabs(p[j]);
            peak = std::max(peak, s);
            sum_sq += static_cast<double>(p[j]) * p[j];
        }
        AudioEnvelopeWindow w;
        w.t = static_cast<double>(i) * win / pcm.sample_rate;
        w.rms_db = LinearToDb(std::sqrt(sum_sq / win));
        w.peak_db = LinearToDb(peak);
        out->envelope.push_back(w);
    }

    // Merge consecutive below-threshold windows into spans; drop
    // spans shorter than min_silence.
    const double step = static_cast<double>(win) / pcm.sample_rate;
    const float thr = static_cast<float>(params.threshold_db);
    int run_start = -1;
    double run_sum = 0.0;
    int run_len = 0;
    const auto flush = [&](int end_index) {
        if (run_start < 0) return;
        SilenceSpan span;
        span.t0 = run_start * step;
        span.t1 = end_index * step;
        span.rms_db =
            run_len > 0 ? LinearToDb(run_sum / run_len) : kDbFloor;
        if (span.t1 - span.t0 >= params.min_silence) {
            out->silences.push_back(span);
        }
        run_start = -1;
        run_sum = 0.0;
        run_len = 0;
    };
    for (int i = 0; i < count; ++i) {
        const auto& w = out->envelope[static_cast<size_t>(i)];
        if (w.rms_db < thr) {
            if (run_start < 0) {
                run_start = i;
                run_sum = 0.0;
                run_len = 0;
            }
            run_sum += std::pow(10.0, w.rms_db / 20.0);
            ++run_len;
        } else {
            flush(i);
        }
    }
    flush(count);

    if (!with_envelope) out->envelope.clear();
    return true;
}

bool MeasureLoudness(const std::string& utf8_path, double max_seconds,
                     LoudnessResult* out, std::string* error_out) {
    if (out == nullptr) return false;
    out->integrated_lufs = 0.0;
    out->true_peak_dbfs = -120.0;
    out->duration_seconds = 0.0;

    Pcm pcm;
    // Keep the source rate — K-weighted metering is rate-aware.
    if (!ExtractMonoPcm(utf8_path, 0, max_seconds, &pcm, error_out)) {
        return false;
    }
    if (pcm.samples.empty()) {
        if (error_out) *error_out = "no decodable audio";
        return false;
    }
    out->duration_seconds = pcm.duration_seconds;

    XER::Audio::AnalysisAI::LoudnessMeterLUFS meter;
    constexpr size_t kBlock = 4096;
    for (size_t off = 0; off < pcm.samples.size(); off += kBlock) {
        const size_t n =
            std::min(kBlock, pcm.samples.size() - off);
        meter.ProcessBlock(pcm.samples.data() + off, n, 1);
    }
    out->integrated_lufs = meter.GetIntegratedLUFS();
    out->true_peak_dbfs = meter.GetTruePeakDBFS();
    return true;
}

}  // namespace XER::VideoEditor::Ai
