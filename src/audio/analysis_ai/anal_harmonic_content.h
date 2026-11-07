#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "async_io/log_and_debug/io_dump_helper.h"

#include "audio_fft_analyzer.h"
#include "audio_pitch_detector.h"

namespace XER::Engine::Audio::AnalysisAI {

class HarmonicContentAnalyzer {
public:
	explicit HarmonicContentAnalyzer(size_t fft_size = 2048);
	~HarmonicContentAnalyzer();

	bool Analyze(const float* audio, size_t frame_count, int sample_rate);

	float GetFundamentalFrequency() const { return fundamental_frequency_; }
	float GetHarmonicRatio() const { return harmonic_ratio_; }
	float GetInharmonicity() const { return inharmonicity_; }
	const std::vector<float>& GetHarmonicSeries() const { return harmonic_series_; }
	std::string GetSummary() const;

private:
	size_t fft_size_;
	AudioFFTAnalyzer fft_analyzer_;
	AudioPitchDetector pitch_detector_;
	float fundamental_frequency_;
	float harmonic_ratio_;
	float inharmonicity_;
	std::vector<float> harmonic_series_;

	size_t FrequencyToBin(float frequency, int sample_rate) const;
};

}  // namespace AIToolsXPro::Audio::AnalysisAI
