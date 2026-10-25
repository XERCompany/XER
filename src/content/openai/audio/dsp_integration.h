#pragma once

#include "../audio/audio_transcriber.h"
#include "../../../audio/dsp_algorithms/dsp_fft_engine.h"
#include "../../../audio/dsp_algorithms/dsp_stft_processor.h"
#include "../../../audio/dsp_algorithms/dsp_windowing_functions.h"
#include <memory>
#include <vector>
#include <complex>

namespace Content::OpenAI::Audio::DSP {

/**
 * Audio feature extraction using XER DSP algorithms
 */
class AudioFeatureExtractor {
public:
    AudioFeatureExtractor();
    ~AudioFeatureExtractor() = default;

    bool Initialize(int fft_size = 1024);
    bool IsInitialized() const;

    // Spectral features
    std::vector<float> ExtractMFCC(const AudioBuffer& buffer, int num_coefficients = 13);
    std::vector<float> ExtractSpectralCentroid(const AudioBuffer& buffer);
    std::vector<float> ExtractSpectralRolloff(const AudioBuffer& buffer);
    std::vector<float> ExtractSpectralBandwidth(const AudioBuffer& buffer);
    std::vector<float> ExtractSpectralContrast(const AudioBuffer& buffer);

    // Time-domain features
    std::vector<float> ExtractZeroCrossingRate(const AudioBuffer& buffer);
    std::vector<float> ExtractRMS(const AudioBuffer& buffer);
    std::vector<float> ExtractEnergy(const AudioBuffer& buffer);

    // Chroma features
    std::vector<float> ExtractChroma(const AudioBuffer& buffer);

    // Full feature vector
    std::vector<float> ExtractFullFeatureVector(const AudioBuffer& buffer);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Audio preprocessing using XER DSP algorithms
 */
class AudioPreprocessor {
public:
    AudioPreprocessor();
    ~AudioPreprocessor() = default;

    bool Initialize();
    bool IsInitialized() const;

    // Preprocessing operations
    AudioBuffer Normalize(const AudioBuffer& buffer);
    AudioBuffer RemoveSilence(const AudioBuffer& buffer, float threshold = 0.01f);
    AudioBuffer ApplyWindow(const AudioBuffer& buffer, const std::string& window_type = "hann");
    AudioBuffer ApplyNoiseReduction(const AudioBuffer& buffer);
    AudioBuffer ApplyHighPassFilter(const AudioBuffer& buffer, float cutoff_freq = 80.0f);
    AudioBuffer ApplyLowPassFilter(const AudioBuffer& buffer, float cutoff_freq = 8000.0f);

    // Resampling
    AudioBuffer Resample(const AudioBuffer& buffer, int target_sample_rate);

    // Channel operations
    AudioBuffer ConvertToMono(const AudioBuffer& buffer);
    AudioBuffer ApplyStereoPanning(const AudioBuffer& buffer, float pan = 0.0f);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Voice Activity Detection using DSP
 */
class VoiceActivityDetector {
public:
    VoiceActivityDetector();
    ~VoiceActivityDetector() = default;

    bool Initialize();
    bool IsInitialized() const;

    // VAD detection
    std::vector<bool> DetectVoiceActivity(const AudioBuffer& buffer);
    std::vector<std::pair<float, float>> DetectVoiceSegments(const AudioBuffer& buffer);

    // Configuration
    void SetSensitivity(float sensitivity);
    void SetFrameSize(int frame_size_ms);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Speaker diarization using DSP features
 */
class SpeakerDiarization {
public:
    SpeakerDiarization();
    ~SpeakerDiarization() = default;

    bool Initialize(int num_speakers = 2);
    bool IsInitialized() const;

    // Diarization
    std::vector<int> ClassifySpeakers(const AudioBuffer& buffer);
    std::vector<TranscriptionSegment> DiarizeWithTimestamps(const AudioBuffer& buffer);

    // Configuration
    void SetNumSpeakers(int num_speakers);
    void SetMinSegmentDuration(float duration_seconds);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Spectral visualization for UI
 */
class SpectralVisualizer {
public:
    SpectralVisualizer();
    ~SpectralVisualizer() = default;

    bool Initialize(int fft_size = 1024);
    bool IsInitialized() const;

    // Generate spectrogram data
    std::vector<std::vector<float>> GenerateSpectrogram(const AudioBuffer& buffer);
    std::vector<float> GetSpectrum(const AudioBuffer& frame);

    // Mel spectrogram
    std::vector<std::vector<float>> GenerateMelSpectrogram(const AudioBuffer& buffer, int num_mel_bands = 128);

    // Chromagram
    std::vector<std::vector<float>> GenerateChromagram(const AudioBuffer& buffer);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace Content::OpenAI::Audio::DSP
