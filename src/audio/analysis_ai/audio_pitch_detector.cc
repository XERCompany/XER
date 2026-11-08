#include "audio_pitch_detector.h"

#include <cmath>
#include <algorithm>
#include <map>

namespace XER::Audio::AnalysisAI {

const float A4_FREQUENCY = 440.0f;

AudioPitchDetector::AudioPitchDetector(int sample_rate)
    : sample_rate_(sample_rate), last_confidence_(0.0f),
      autocorr_buffer_(sample_rate / 2) {}

AudioPitchDetector::~AudioPitchDetector() {}

PitchInfo AudioPitchDetector::DetectPitch(const float* audio, size_t frame_count) {
    PitchInfo info{0.0f, 0.0f, 0};
    
    info.frequency = GetFundamentalFrequency(audio, frame_count);
    info.confidence = AutocorrelationPeak(audio, frame_count);
    last_confidence_ = info.confidence;
    
    if (info.frequency > 0) {
        info.semitone_offset = static_cast<int>(
            12.0f * std::log2(info.frequency / A4_FREQUENCY) + 0.5f);
    }
    
    return info;
}

bool AudioPitchDetector::IsPitched(const float* audio, size_t frame_count) const {
    return GetConfidence() > 0.5f;
}

float AudioPitchDetector::GetFundamentalFrequency(const float* audio, size_t frame_count) {
    float max_period = 0.0f;
    float max_corr = 0.0f;
    
    size_t min_period = sample_rate_ / 500;  // Max 500 Hz
    size_t max_lag = std::min(frame_count / 2, autocorr_buffer_.size());
    
    for (size_t lag = min_period; lag < max_lag; ++lag) {
        float corr = 0.0f;
        
        for (size_t i = 0; i + lag < frame_count; ++i) {
            corr += audio[i] * audio[i + lag];
        }
        
        if (corr > max_corr) {
            max_corr = corr;
            max_period = lag;
        }
    }
    
    if (max_period > 0) {
        return sample_rate_ / max_period;
    }
    
    return 0.0f;
}

std::vector<float> AudioPitchDetector::GetHarmonics(const float* audio, size_t frame_count) const {
    std::vector<float> harmonics(5, 0.0f);
    // float fundamental = GetFundamentalFrequency(audio, frame_count);
    
    // if (fundamental > 0) {
    //     for (int i = 0; i < 5; ++i) {
    //         harmonics[i] = fundamental * (i + 1);
    //     }
    // }
    
    return harmonics;
}

std::string AudioPitchDetector::FrequencyToNote(float frequency) const {
    if (frequency <= 0) return "Unknown";
    
    float semitones = 12.0f * std::log2(frequency / A4_FREQUENCY);
    int note_index = static_cast<int>(semitones + 0.5f) + 57;  // A4 = 57
    
    static const std::vector<std::string> notes{
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    
    int octave = note_index / 12;
    int note = note_index % 12;
    
    return notes[note] + std::to_string(octave);
}

float AudioPitchDetector::NoteToFrequency(const std::string& note) {
    static const std::map<std::string, float> note_map{
        {"C4", 261.63f}, {"D4", 293.66f}, {"E4", 329.63f}, {"F4", 349.23f},
        {"G4", 392.00f}, {"A4", 440.00f}, {"B4", 493.88f}, {"C5", 523.25f}
    };
    
    auto it = note_map.find(note);
    return (it != note_map.end()) ? it->second : 0.0f;
}

float AudioPitchDetector::AutocorrelationPeak(const float* audio, size_t frame_count) const {
    float norm = 0.0f;
    
    for (size_t i = 0; i < frame_count; ++i) {
        norm += audio[i] * audio[i];
    }
    
    if (norm < 1e-12f) return 0.0f;
    
    return std::sqrt(norm / frame_count);
}

}  // namespace XER::Audio::AnalysisAI
