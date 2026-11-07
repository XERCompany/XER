#include "audio_fft_analyzer.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace XER::Engine::Audio::AnalysisAI {

const float PI = 3.14159265359f;

AudioFFTAnalyzer::AudioFFTAnalyzer(size_t fft_size)
    : fft_size_(fft_size), fft_engine_(fft_size), total_power_(0.0f), rms_energy_(0.0f) {
    fft_output_.resize(fft_size);
    magnitude_spectrum_.resize(fft_size / 2);
    phase_spectrum_.resize(fft_size / 2);
    GenerateWindow();
}

AudioFFTAnalyzer::~AudioFFTAnalyzer() {}

bool AudioFFTAnalyzer::ComputeFFT(const float* input, size_t frame_count) {
    if (!input || frame_count != fft_size_) return false;

    std::vector<float> time_domain(fft_size_, 0.0f);
    for (size_t i = 0; i < fft_size_; ++i) {
        time_domain[i] = input[i] * window_[i];
    }

    if (!fft_engine_.Forward(time_domain.data(), time_domain.size())) {
        return false;
    }

    fft_output_ = fft_engine_.GetSpectrum();

    ComputeMagnitudePhase();
    return true;
}

bool AudioFFTAnalyzer::AnalyzeSpectrum(const float* input, size_t frame_count) {
    if (!ComputeFFT(input, frame_count)) return false;

    // Calculate power and energy
    total_power_ = 0.0f;
    for (float mag : magnitude_spectrum_) {
        total_power_ += mag * mag;
    }
    total_power_ /= magnitude_spectrum_.size();

    // Calculate RMS energy
    float sum_sq = 0.0f;
    for (size_t i = 0; i < frame_count; ++i) {
        sum_sq += input[i] * input[i];
    }
    rms_energy_ = std::sqrt(sum_sq / frame_count);

    return true;
}

float AudioFFTAnalyzer::GetDominantFrequency(int sample_rate) const {
    auto it = std::max_element(magnitude_spectrum_.begin(), magnitude_spectrum_.end());
    size_t bin = std::distance(magnitude_spectrum_.begin(), it);
    return (bin * sample_rate) / static_cast<float>(fft_size_);
}

std::vector<float> AudioFFTAnalyzer::GetFrequencyBands() const {
    // Return magnitude spectrum as frequency bands
    return magnitude_spectrum_;
}

void AudioFFTAnalyzer::GenerateWindow() {
    window_ = XER::Engine::Audio::DSP::WindowingFunctions::GenerateHann(fft_size_);
}

void AudioFFTAnalyzer::ComputeMagnitudePhase() {
    for (size_t i = 0; i < magnitude_spectrum_.size(); ++i) {
        magnitude_spectrum_[i] = std::abs(fft_output_[i]);
        phase_spectrum_[i] = std::arg(fft_output_[i]);
    }
}

std::string AudioFFTAnalyzer::GetAnalysisReport() const {
    std::string report = "FFT Analysis Report:\n";
    report += "Total Power: " + std::to_string(total_power_) + "\n";
    report += "RMS Energy: " + std::to_string(rms_energy_) + "\n";
    report += "Spectra Size: " + std::to_string(magnitude_spectrum_.size()) + "\n";
    return report;
}

}  // namespace XER::Engine::Audio::AnalysisAI
