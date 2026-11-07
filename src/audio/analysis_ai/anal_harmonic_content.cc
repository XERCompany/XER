#include "anal_harmonic_content.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace XER::Engine::Audio::AnalysisAI {

HarmonicContentAnalyzer::HarmonicContentAnalyzer(size_t fft_size)
	: fft_size_(fft_size),
	  fft_analyzer_(fft_size),
	  pitch_detector_(),
	  fundamental_frequency_(0.0f),
	  harmonic_ratio_(0.0f),
	  inharmonicity_(0.0f) {}

HarmonicContentAnalyzer::~HarmonicContentAnalyzer() = default;

bool HarmonicContentAnalyzer::Analyze(const float* audio, size_t frame_count, int sample_rate) {
	if (audio == nullptr || frame_count != fft_size_ || sample_rate <= 0) {
		return false;
	}

	if (!fft_analyzer_.AnalyzeSpectrum(audio, frame_count)) {
		return false;
	}

	const PitchInfo pitch_info = pitch_detector_.DetectPitch(audio, frame_count);
	fundamental_frequency_ = pitch_info.frequency;
	harmonic_series_.clear();
	harmonic_ratio_ = 0.0f;
	inharmonicity_ = 0.0f;

	if (fundamental_frequency_ <= 0.0f) {
		return true;
	}

	const std::vector<float>& magnitude = fft_analyzer_.GetMagnitudeSpectrum();
	const float total_energy = std::accumulate(magnitude.begin(), magnitude.end(), 0.0f);
	if (total_energy <= 0.0f) {
		return true;
	}

	float harmonic_energy = 0.0f;
	float accumulated_inharmonicity = 0.0f;
	size_t used_harmonics = 0;

	for (size_t harmonic = 1; harmonic <= 8; ++harmonic) {
		const float target_frequency = fundamental_frequency_ * static_cast<float>(harmonic);
		if (target_frequency >= sample_rate * 0.5f) {
			break;
		}

		harmonic_series_.push_back(target_frequency);
		const size_t bin = FrequencyToBin(target_frequency, sample_rate);
		if (bin >= magnitude.size()) {
			continue;
		}

		const size_t begin = bin > 0 ? bin - 1 : 0;
		const size_t end = std::min(bin + 1, magnitude.size() - 1);
		for (size_t index = begin; index <= end; ++index) {
			harmonic_energy += magnitude[index];
		}

		const float detected_frequency = static_cast<float>(bin) * static_cast<float>(sample_rate) /
			static_cast<float>(fft_size_);
		accumulated_inharmonicity += std::abs(detected_frequency - target_frequency) /
			std::max(target_frequency, 1.0f);
		++used_harmonics;
	}

	harmonic_ratio_ = harmonic_energy / total_energy;
	inharmonicity_ = used_harmonics > 0 ? accumulated_inharmonicity / static_cast<float>(used_harmonics) : 0.0f;
	return true;
}

std::string HarmonicContentAnalyzer::GetSummary() const {
	const uint32_t harmonic_bits = static_cast<uint32_t>(std::max(harmonic_ratio_, 0.0f) * 1000.0f);
	return "HarmonicContent: f0=" + std::to_string(fundamental_frequency_) +
		", ratio=" + std::to_string(harmonic_ratio_) +
		", inharmonicity=" + std::to_string(inharmonicity_) +
		", bits=" + XER::AsyncIO::IO::LogDebug::DumpHelper::BitDump(harmonic_bits);
}

size_t HarmonicContentAnalyzer::FrequencyToBin(float frequency, int sample_rate) const {
	if (frequency <= 0.0f || sample_rate <= 0) {
		return 0;
	}

	const float bin = frequency * static_cast<float>(fft_size_) / static_cast<float>(sample_rate);
	return static_cast<size_t>(std::max(bin, 0.0f));
}

}  // namespace AIToolsXPro::Audio::AnalysisAI
