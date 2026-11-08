#include "anal_beat_tracker.h"

#include <algorithm>
#include <numeric>

namespace XER::Audio::AnalysisAI {

BeatTracker::BeatTracker(size_t window_size, size_t hop_size)
	: window_size_(window_size),
	  hop_size_(hop_size),
	  mutex_("beat_tracker"),
	  fft_analyzer_(window_size),
	  estimated_bpm_(0.0) {
	perf_counter_.Enable();
}

BeatTracker::~BeatTracker() = default;

bool BeatTracker::Analyze(const float* audio, size_t frame_count, int sample_rate) {
	if (audio == nullptr || frame_count < window_size_ || sample_rate <= 0) {
		return false;
	}

 	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	perf_counter_.StartCounter("beat_analysis");

	beat_events_.clear();
	energy_curve_.clear();

	for (size_t offset = 0; offset + window_size_ <= frame_count; offset += hop_size_) {
		const float energy = ComputeWindowEnergy(audio, offset);
		energy_curve_.push_back(energy);

		const size_t history_begin = energy_curve_.size() > 8 ? energy_curve_.size() - 8 : 0;
		const auto begin_it = energy_curve_.begin() + static_cast<std::ptrdiff_t>(history_begin);
		const float local_average = std::accumulate(begin_it, energy_curve_.end(), 0.0f) /
			static_cast<float>(energy_curve_.size() - history_begin);

		if (energy_curve_.size() < 3) {
			continue;
		}

		const float previous = energy_curve_[energy_curve_.size() - 2];
		const bool is_beat = energy > local_average * 1.35f && energy > previous * 1.1f;
		if (!is_beat) {
			continue;
		}

		fft_analyzer_.AnalyzeSpectrum(audio + offset, window_size_);
		const float strength = energy + fft_analyzer_.GetTotalPower() * 0.05f;
		beat_events_.push_back(BeatEvent{offset, static_cast<double>(offset) / sample_rate, strength});
	}

	UpdateTempoEstimate(sample_rate);
	perf_counter_.StopCounter("beat_analysis");
	return true;
}

void BeatTracker::Reset() {
	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	beat_events_.clear();
	energy_curve_.clear();
	estimated_bpm_ = 0.0;
}

float BeatTracker::GetAverageStrength() const {
	if (beat_events_.empty()) {
		return 0.0f;
	}

	float total_strength = 0.0f;
	for (const BeatEvent& beat : beat_events_) {
		total_strength += beat.strength;
	}

	return total_strength / static_cast<float>(beat_events_.size());
}

std::string BeatTracker::GetReport() const {
	return "BeatTracker: beats=" + std::to_string(beat_events_.size()) +
		", bpm=" + std::to_string(estimated_bpm_) +
		", avg_strength=" + std::to_string(GetAverageStrength());
}

float BeatTracker::ComputeWindowEnergy(const float* audio, size_t offset) const {
	float energy = 0.0f;
	for (size_t index = 0; index < window_size_; ++index) {
		const float sample = audio[offset + index];
		energy += sample * sample;
	}
	return energy / static_cast<float>(window_size_);
}

void BeatTracker::UpdateTempoEstimate(int sample_rate) {
	if (beat_events_.size() < 2 || sample_rate <= 0) {
		estimated_bpm_ = 0.0;
		return;
	}

	std::vector<double> intervals;
	intervals.reserve(beat_events_.size() - 1);
	for (size_t index = 1; index < beat_events_.size(); ++index) {
		const size_t delta_frames = beat_events_[index].frame_index - beat_events_[index - 1].frame_index;
		intervals.push_back(static_cast<double>(delta_frames) / sample_rate);
	}

	std::sort(intervals.begin(), intervals.end());
	const size_t median_index = intervals.size() / 2;
	const double median_interval = intervals.size() % 2 == 0
		? (intervals[median_index - 1] + intervals[median_index]) * 0.5
		: intervals[median_index];
	estimated_bpm_ = median_interval > 0.0 ? 60.0 / median_interval : 0.0;
	while (estimated_bpm_ > 220.0) {
		estimated_bpm_ *= 0.5;
	}
	while (estimated_bpm_ > 0.0 && estimated_bpm_ < 60.0) {
		estimated_bpm_ *= 2.0;
	}
}

}  // namespace AIToolsXPro::Audio::AnalysisAI
