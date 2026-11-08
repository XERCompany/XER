#include "anal_rms_peak_detector.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace XER::Audio::AnalysisAI {

RMSPeakDetector::RMSPeakDetector()
	: buffer_pool_(65536, 2),
	  rms_(0.0f),
	  peak_(0.0f) {
	memory_tracker_.Enable();
	perf_counter_.Enable();
}

RMSPeakDetector::~RMSPeakDetector() = default;

bool RMSPeakDetector::Analyze(const float* audio, size_t frame_count, int channels) {
	if (audio == nullptr || frame_count == 0 || channels <= 0) {
		return false;
	}

	perf_counter_.StartCounter("rms_peak_analyze");

	const size_t sample_count = frame_count * static_cast<size_t>(channels);
	double sum_squares = 0.0;
	peak_ = 0.0f;
	window_history_.clear();

	auto buffer = buffer_pool_.AcquireBuffer();
	if (buffer != nullptr && !buffer->data.empty()) {
		memory_tracker_.TrackAllocation(buffer->data.data(), buffer->data.size(), "rms_peak_buffer");
		const size_t copy_bytes = std::min(buffer->data.size(), sample_count * sizeof(float));
		std::memcpy(buffer->data.data(), audio, copy_bytes);
	}

	constexpr size_t window_length = 256;
	for (size_t index = 0; index < sample_count; ++index) {
		const float sample = audio[index];
		sum_squares += static_cast<double>(sample) * static_cast<double>(sample);
		peak_ = std::max(peak_, std::abs(sample));

		if ((index + 1) % window_length == 0) {
			const size_t begin = index + 1 - window_length;
			double window_square = 0.0;
			for (size_t window_index = begin; window_index <= index; ++window_index) {
				const double value = audio[window_index];
				window_square += value * value;
			}
			window_history_.push_back(static_cast<float>(std::sqrt(window_square / window_length)));
		}
	}

	rms_ = static_cast<float>(std::sqrt(sum_squares / static_cast<double>(sample_count)));

	if (buffer != nullptr && !buffer->data.empty()) {
		memory_tracker_.TrackDeallocation(buffer->data.data());
		buffer_pool_.ReleaseBuffer(buffer);
	}

	perf_counter_.StopCounter("rms_peak_analyze");
	return true;
}

void RMSPeakDetector::Reset() {
	window_history_.clear();
	rms_ = 0.0f;
	peak_ = 0.0f;
}

float RMSPeakDetector::GetCrestFactor() const {
	return rms_ > 0.0f ? peak_ / rms_ : 0.0f;
}

float RMSPeakDetector::GetPeakDBFS() const {
	return 20.0f * static_cast<float>(std::log10(std::max(peak_, 1e-9f)));
}

std::string RMSPeakDetector::GetReport() const {
	return "RMSPeak: rms=" + std::to_string(rms_) +
		", peak=" + std::to_string(peak_) +
		", crest=" + std::to_string(GetCrestFactor()) +
		", dbfs=" + std::to_string(GetPeakDBFS());
}

}  // namespace AIToolsXPro::Audio::AnalysisAI
