#include "aug_gaussian_noise.h"

#include <algorithm>
#include <random>
#include <cmath>

// Optional integration: spectral noise shaping via AudioFFTAnalyzer
#if __has_include("audio/analysis_ai/audio_fft_analyzer.h")
#  include "audio/analysis_ai/audio_fft_analyzer.h"
#  define HAS_FFT_ANALYZER 1
#else
#  define HAS_FFT_ANALYZER 0
#endif

namespace XER::ML::Augmentation {

AugGaussianNoise::AugGaussianNoise(float std_dev, bool spectral_shape)
    : std_dev_(std_dev), spectral_shape_(spectral_shape) {}

bool AugGaussianNoise::Apply(ImageSample& sample) {
    if (!enabled_ || sample.pixels.empty()) return false;

    thread_local std::mt19937 rng{std::random_device{}()};
    std::normal_distribution<float> gauss(0.0f, std_dev_);

#if HAS_FFT_ANALYZER
    if (spectral_shape_ && sample.pixels.size() >= 64) {
        // Use AudioFFTAnalyzer to compute per-frequency noise shaping weights.
        XER::Audio::AnalysisAI::AudioFFTAnalyzer fft_analyzer(
            static_cast<size_t>(std::min(static_cast<int>(sample.pixels.size()), 2048)));
        fft_analyzer.ComputeFFT(sample.pixels.data(), sample.pixels.size());
        const auto& mag = fft_analyzer.GetMagnitudeSpectrum();
        float rms = fft_analyzer.GetRMSEnergy();
        float scale = (rms > 1e-6f) ? std_dev_ / rms : std_dev_;

        for (size_t i = 0; i < sample.pixels.size(); ++i) {
            float weight = (i < mag.size()) ? (1.0f + mag[i % mag.size()] * scale) : 1.0f;
            sample.pixels[i] = std::clamp(sample.pixels[i] + gauss(rng) * weight, 0.0f, 1.0f);
        }
        return true;
    }
#endif

    for (auto& p : sample.pixels)
        p = std::clamp(p + gauss(rng), 0.0f, 1.0f);
    return true;
}

}  // namespace XER::ML::Augmentation
