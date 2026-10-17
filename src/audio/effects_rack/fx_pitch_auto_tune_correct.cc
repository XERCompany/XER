#include "fx_pitch_auto_tune_correct.h"

#include <algorithm>
#include <cmath>

namespace Engine::Audio::FX {

namespace {

constexpr float kA4Hz = 440.0f;
constexpr float kMinPitchHz = 70.0f;
constexpr float kMaxPitchHz = 1200.0f;
constexpr float kMinConfidence = 0.18f;

float ClampUnit(float value) {
	return std::clamp(value, 0.0f, 1.0f);
}

int NormalizeSemitone(int semitone) {
	int result = semitone % 12;
	if (result < 0) {
		result += 12;
	}
	return result;
}

}  // namespace

AutoTuneCorrectPitch::AutoTuneCorrectPitch() = default;
AutoTuneCorrectPitch::~AutoTuneCorrectPitch() = default;

bool AutoTuneCorrectPitch::Initialize(float sample_rate, size_t max_block_size) {
	if (sample_rate <= 0.0f || max_block_size == 0) {
		return false;
	}
	sample_rate_ = sample_rate;
	max_block_size_ = max_block_size;
	smoothed_ratio_ = 1.0f;
	last_target_midi_ = 69.0f;
	last_pitch_hz_ = 0.0f;
	last_confidence_ = 0.0f;
	pitch_buffer_.assign(max_block_size_, 0.0f);
	tone_buffer_.assign(max_block_size_, 0.0f);
	dynamics_buffer_.assign(max_block_size_, 0.0f);
	if (!pitch_shifter_.Initialize(sample_rate_, 384u, std::max<size_t>(4096u, max_block_size_ * 4))) {
		return false;
	}
	if (!presence_eq_.Initialize(sample_rate_, 1u)) {
		return false;
	}
	if (!presence_eq_.SetBand(0u, ParametricBand{3600.0f, 0.82f, presence_db_, true})) {
		return false;
	}
	compressor_.SetThresholdDb(-20.0f);
	compressor_.SetRatio(2.4f);
	compressor_.SetAttackMs(6.0f);
	compressor_.SetReleaseMs(84.0f);
	limiter_.SetCeilingDb(-0.8f);
	limiter_.SetRelease(0.002f);
	return true;
}

void AutoTuneCorrectPitch::SetCorrectionAmount(float amount) {
	correction_amount_ = ClampUnit(amount);
}

void AutoTuneCorrectPitch::SetCorrectionSpeed(float speed) {
	correction_speed_ = std::clamp(speed, 0.01f, 1.0f);
}

void AutoTuneCorrectPitch::SetRetuneSpeedMs(float milliseconds) {
	retune_speed_ms_ = std::clamp(milliseconds, 0.0f, 250.0f);
}

void AutoTuneCorrectPitch::SetHumanize(float humanize) {
	humanize_ = ClampUnit(humanize);
}

void AutoTuneCorrectPitch::SetScaleRoot(float semitone) {
	scale_root_ = NormalizeSemitone(static_cast<int>(std::round(semitone)));
}

void AutoTuneCorrectPitch::SetScaleMode(float mode) {
	scale_mode_ = std::clamp(static_cast<int>(std::round(mode)), 0, 3);
}

void AutoTuneCorrectPitch::SetPresenceDb(float gain_db) {
	presence_db_ = std::clamp(gain_db, -6.0f, 6.0f);
	if (max_block_size_ > 0) {
		presence_eq_.SetBand(0u, ParametricBand{3600.0f, 0.82f, presence_db_, true});
	}
}

void AutoTuneCorrectPitch::SetWetMix(float mix) {
	wet_mix_ = ClampUnit(mix);
}

bool AutoTuneCorrectPitch::ProcessBlock(const float* input, size_t frame_count, float* output) {
	if (input == nullptr || output == nullptr || frame_count == 0 || frame_count > max_block_size_) {
		return false;
	}

	const PitchEstimate pitch = EstimatePitch(input, frame_count);
	last_pitch_hz_ = pitch.frequency_hz;
	last_confidence_ = pitch.confidence;
	const float target_ratio = ComputeCorrectionRatio(pitch);
	const float confidence_gate = pitch.confidence >= kMinConfidence ? ClampUnit((pitch.confidence - kMinConfidence) / 0.42f) : 0.0f;
	const float block_ms = 1000.0f * static_cast<float>(frame_count) / std::max(1.0f, sample_rate_);
	const float retune_alpha = retune_speed_ms_ <= 0.01f ? 1.0f : 1.0f - std::exp(-block_ms / retune_speed_ms_);
	const float smoothing = std::clamp(retune_alpha * correction_speed_ * (0.22f + 0.78f * confidence_gate), 0.0025f, 1.0f);
	smoothed_ratio_ += (target_ratio - smoothed_ratio_) * smoothing;
	smoothed_ratio_ = std::clamp(smoothed_ratio_, 0.943874f, 1.059463f);

	pitch_shifter_.SetPitchRatio(smoothed_ratio_);
	if (!pitch_shifter_.ProcessBlock(input, frame_count, pitch_buffer_.data())) {
		return false;
	}
	presence_eq_.SetBand(0u, ParametricBand{3600.0f, 0.82f, presence_db_, true});
	if (!presence_eq_.ProcessBlock(pitch_buffer_.data(), frame_count, tone_buffer_.data())) {
		return false;
	}
	if (!compressor_.ProcessBlock(tone_buffer_.data(), frame_count, dynamics_buffer_.data())) {
		return false;
	}
	if (!limiter_.ProcessBlock(dynamics_buffer_.data(), frame_count, pitch_buffer_.data())) {
		return false;
	}

	const float effective_mix = std::clamp(wet_mix_ * (0.35f + 0.65f * confidence_gate), 0.0f, 1.0f);
	for (size_t index = 0; index < frame_count; ++index) {
		output[index] = input[index] + (pitch_buffer_[index] - input[index]) * effective_mix;
	}
	return true;
}

void AutoTuneCorrectPitch::Reset() {
	smoothed_ratio_ = 1.0f;
	last_pitch_hz_ = 0.0f;
	last_confidence_ = 0.0f;
	last_target_midi_ = 69.0f;
	pitch_shifter_.Reset();
}

std::string AutoTuneCorrectPitch::GetReport() const {
	return "AutoTuneCorrectPitch: pitch=" + std::to_string(last_pitch_hz_) +
		", target_midi=" + std::to_string(last_target_midi_) +
		", confidence=" + std::to_string(last_confidence_) +
		", ratio=" + std::to_string(smoothed_ratio_) +
		", amount=" + std::to_string(correction_amount_) +
		", retune_ms=" + std::to_string(retune_speed_ms_) +
		", humanize=" + std::to_string(humanize_) +
		", root=" + std::to_string(scale_root_) +
		", scale=" + std::to_string(scale_mode_);
}

AutoTuneCorrectPitch::PitchEstimate AutoTuneCorrectPitch::EstimatePitch(const float* input, size_t frame_count) const {
	PitchEstimate estimate;
	if (input == nullptr || frame_count < 64 || sample_rate_ <= 0.0f) {
		return estimate;
	}

	float mean = 0.0f;
	for (size_t index = 0; index < frame_count; ++index) {
		mean += input[index];
	}
	mean /= static_cast<float>(frame_count);

	const size_t min_lag = std::max<size_t>(1u, static_cast<size_t>(sample_rate_ / kMaxPitchHz));
	const size_t max_lag = std::min(frame_count / 2u, static_cast<size_t>(sample_rate_ / kMinPitchHz));
	if (max_lag <= min_lag + 2u) {
		return estimate;
	}

	float best_score = 0.0f;
	size_t best_lag = min_lag;
	for (size_t lag = min_lag; lag <= max_lag; ++lag) {
		double correlation = 0.0;
		double energy_a = 0.0;
		double energy_b = 0.0;
		const size_t compare_count = frame_count - lag;
		for (size_t index = 0; index < compare_count; ++index) {
			const float a = input[index] - mean;
			const float b = input[index + lag] - mean;
			correlation += static_cast<double>(a) * static_cast<double>(b);
			energy_a += static_cast<double>(a) * static_cast<double>(a);
			energy_b += static_cast<double>(b) * static_cast<double>(b);
		}
		const double norm = std::sqrt(energy_a * energy_b) + 1.0e-12;
		const float score = static_cast<float>(correlation / norm);
		if (score > best_score) {
			best_score = score;
			best_lag = lag;
		}
	}

	if (best_score <= 0.0f) {
		return estimate;
	}
	estimate.frequency_hz = sample_rate_ / static_cast<float>(best_lag);
	estimate.confidence = ClampUnit(best_score);
	return estimate;
}

float AutoTuneCorrectPitch::ResolveTargetMidi(float pitch_midi) const {
	if (scale_mode_ == 0) {
		return std::round(pitch_midi);
	}

	float best_midi = std::round(pitch_midi);
	float best_distance = 128.0f;
	const int center = static_cast<int>(std::round(pitch_midi));
	for (int candidate = center - 12; candidate <= center + 12; ++candidate) {
		const int degree = NormalizeSemitone(candidate - scale_root_);
		if (!IsScaleDegreeAllowed(degree)) {
			continue;
		}
		const float distance = std::abs(static_cast<float>(candidate) - pitch_midi);
		if (distance < best_distance) {
			best_distance = distance;
			best_midi = static_cast<float>(candidate);
		}
	}
	return best_midi;
}

bool AutoTuneCorrectPitch::IsScaleDegreeAllowed(int degree) const {
	const int normalized = NormalizeSemitone(degree);
	if (scale_mode_ == 1) {
		return normalized == 0 || normalized == 2 || normalized == 4 || normalized == 5 || normalized == 7 || normalized == 9 || normalized == 11;
	}
	if (scale_mode_ == 2) {
		return normalized == 0 || normalized == 2 || normalized == 3 || normalized == 5 || normalized == 7 || normalized == 8 || normalized == 10;
	}
	if (scale_mode_ == 3) {
		return normalized == 0 || normalized == 2 || normalized == 3 || normalized == 5 || normalized == 7 || normalized == 8 || normalized == 11;
	}
	return true;
}

float AutoTuneCorrectPitch::ComputeCorrectionRatio(const PitchEstimate& pitch) {
	if (pitch.frequency_hz <= 0.0f || pitch.confidence < kMinConfidence) {
		return 1.0f;
	}
	const float pitch_midi = 69.0f + 12.0f * std::log2(pitch.frequency_hz / kA4Hz);
	const float target_midi = ResolveTargetMidi(pitch_midi);
	last_target_midi_ = target_midi;
	const float semitone_error = target_midi - pitch_midi;
	const float deadband = humanize_ * 0.18f;
	const float error_abs = std::abs(semitone_error);
	const float corrected_error = error_abs <= deadband ? 0.0f : std::copysign(error_abs - deadband, semitone_error);
	const float confidence_weight = ClampUnit((pitch.confidence - kMinConfidence) / 0.42f);
	const float amount = correction_amount_ * confidence_weight;
	const float correction_semitones = std::clamp(corrected_error * amount, -1.0f, 1.0f);
	return std::clamp(std::pow(2.0f, correction_semitones / 12.0f), 0.943874f, 1.059463f);
}

}  // namespace Engine::Audio::FX