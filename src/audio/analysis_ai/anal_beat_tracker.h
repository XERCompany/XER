#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "async_io/log_and_debug/io_perf_counter.h"
#include "async_io/sync_primitives/mutex_wrapper.h"

#include "audio_fft_analyzer.h"

namespace XER::Engine::Audio::AnalysisAI {

struct BeatEvent {
	size_t frame_index;
	double time_seconds;
	float strength;
};

class BeatTracker {
public:
	BeatTracker(size_t window_size = 1024, size_t hop_size = 512);
	~BeatTracker();

	bool Analyze(const float* audio, size_t frame_count, int sample_rate);
	void Reset();

	const std::vector<BeatEvent>& GetBeatEvents() const { return beat_events_; }
	double GetEstimatedBPM() const { return estimated_bpm_; }
	float GetAverageStrength() const;
	std::string GetReport() const;

private:
	size_t window_size_;
	size_t hop_size_;
	XER::AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;
	XER::AsyncIO::IO::Sync::MutexWrapper mutex_;
	AudioFFTAnalyzer fft_analyzer_;
	std::vector<BeatEvent> beat_events_;
	std::vector<float> energy_curve_;
	double estimated_bpm_;

	float ComputeWindowEnergy(const float* audio, size_t offset) const;
	void UpdateTempoEstimate(int sample_rate);
};

}  // namespace ENgine::Audio::AnalysisAI
