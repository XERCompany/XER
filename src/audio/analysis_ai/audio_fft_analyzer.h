#pragma once

#include "audio/dsp_algorithms/dsp_fft_engine.h"
#include "audio/dsp_algorithms/dsp_windowing_functions.h"

#include <vector>
#include <complex>
#include <string>
 

namespace XER::Engine::Audio::AnalysisAI {

using Complex = std::complex<float>;

class AudioFFTAnalyzer {
public:
    explicit AudioFFTAnalyzer(size_t fft_size = 2048);
    ~AudioFFTAnalyzer();

    // FFT operations
    bool ComputeFFT(const float* input, size_t frame_count);
    const std::vector<Complex>& GetFFTOutput() const { return fft_output_; }
    const std::vector<float>& GetMagnitudeSpectrum() const { return magnitude_spectrum_; }
    const std::vector<float>& GetPhaseSpectrum() const { return phase_spectrum_; }

    // Spectral analysis
    bool AnalyzeSpectrum(const float* input, size_t frame_count);
    float GetDominantFrequency(int sample_rate) const;
    std::vector<float> GetFrequencyBands() const;

    // Power and energy
    float GetTotalPower() const { return total_power_; }
    float GetRMSEnergy() const { return rms_energy_; }

    // Cross-module integration
    std::string GetAnalysisReport() const;

private:
    size_t fft_size_;
    std::vector<Complex> fft_output_;
    std::vector<float> magnitude_spectrum_;
    std::vector<float> phase_spectrum_;
    std::vector<float> window_;
    XER::Engine::Audio::DSP::FFTEngine fft_engine_;
    
    float total_power_;
    float rms_energy_;

    void GenerateWindow();
    void ComputeMagnitudePhase();
};

}  // namespace XER::Engine::Audio::AnalysisAI
