#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "async_io/log_and_debug/io_perf_counter.h"
#include "async_io/sync_primitives/mutex_wrapper.h"

#include "audio_pitch_detector.h"

namespace XER::Audio::AnalysisAI {

class PitchEstimator {
public:
	explicit PitchEstimator(int sample_rate = 44100);
	~PitchEstimator();

	bool Estimate(const float* audio, size_t frame_count);
	void Reset();

	const PitchInfo& GetLastPitch() const { return last_pitch_; }
	std::vector<float> GetLastHarmonics() const { return last_harmonics_; }
	std::string GetLastNote() const;
	std::string GetReport() const;

private:
	int sample_rate_;
	XER::AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;
	XER::AsyncIO::IO::Sync::MutexWrapper mutex_;
	AudioPitchDetector detector_;
	PitchInfo last_pitch_;
	std::vector<float> last_harmonics_;
};

}  // namespace XER::Audio::AnalysisAI
