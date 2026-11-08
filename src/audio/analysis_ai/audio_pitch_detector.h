#pragma once

#include <vector>
#include <string>
#include <memory>
 

namespace XER::Audio::AnalysisAI {

struct PitchInfo {
    float frequency;
    float confidence;
    int semitone_offset;
};

class AudioPitchDetector {
public:
    explicit AudioPitchDetector(int sample_rate = 44100);
    ~AudioPitchDetector();

    // Pitch detection
    PitchInfo DetectPitch(const float* audio, size_t frame_count);
    bool IsPitched(const float* audio, size_t frame_count) const;

    // Fundamental frequency
    float GetFundamentalFrequency(const float* audio, size_t frame_count);
    std::vector<float> GetHarmonics(const float* audio, size_t frame_count) const;

    // Music note mapping
    std::string FrequencyToNote(float frequency) const;
    static float NoteToFrequency(const std::string& note);

    // Statistics
    float GetConfidence() const { return last_confidence_; }
    int GetSampleRate() const { return sample_rate_; }

private:
    int sample_rate_;
    float last_confidence_;
    std::vector<float> autocorr_buffer_;
    std::vector<float> spectral_buffer_;

    float AutocorrelationPeak(const float* audio, size_t frame_count) const;
};

}  // namespace XER::Audio::AnalysisAI
