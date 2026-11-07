#include "anal_pitch_estimator.h"

namespace XER::Engine::Audio::AnalysisAI {

PitchEstimator::PitchEstimator(int sample_rate)
	: sample_rate_(sample_rate),
	  mutex_("pitch_estimator"),
	  detector_(sample_rate),
	  last_pitch_{0.0f, 0.0f, 0} {
	perf_counter_.Enable();
}

PitchEstimator::~PitchEstimator() = default;

bool PitchEstimator::Estimate(const float* audio, size_t frame_count) {
	if (audio == nullptr || frame_count == 0) {
		return false;
	}

	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	perf_counter_.StartCounter("pitch_estimate");

	last_pitch_ = detector_.DetectPitch(audio, frame_count);
	last_harmonics_ = detector_.GetHarmonics(audio, frame_count);

	perf_counter_.StopCounter("pitch_estimate");
	return true;
}

void PitchEstimator::Reset() {
	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	last_pitch_ = {0.0f, 0.0f, 0};
	last_harmonics_.clear();
}

std::string PitchEstimator::GetLastNote() const {
	return detector_.FrequencyToNote(last_pitch_.frequency);
}

std::string PitchEstimator::GetReport() const {
	return "PitchEstimator: f0=" + std::to_string(last_pitch_.frequency) +
		", conf=" + std::to_string(last_pitch_.confidence) +
		", note=" + GetLastNote();
}

}  // namespace XER::Engine::Audio::AnalysisAI
