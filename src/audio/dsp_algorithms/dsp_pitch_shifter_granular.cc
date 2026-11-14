#include "dsp_pitch_shifter_granular.h"

#include <algorithm>
#include <cmath>

namespace XER::Audio::DSP {

GranularPitchShifter::GranularPitchShifter()
	: sample_rate_(44100.0f),
	  pitch_ratio_(1.0f),
	  smoothed_pitch_ratio_(1.0f),
	  grain_size_(256),
	  max_delay_samples_(0),
	  write_index_(0) {
	perf_counter_.Enable();
}

GranularPitchShifter::~GranularPitchShifter() = default;

bool GranularPitchShifter::Initialize(float sample_rate, size_t grain_size, size_t max_delay_samples) {
	if (sample_rate <= 0.0f || grain_size == 0 || max_delay_samples == 0) {
		return false;
	}

	sample_rate_ = sample_rate;
	grain_size_ = grain_size;
	max_delay_samples_ = std::max(max_delay_samples, grain_size_ * 2);
	write_index_ = 0;
	smoothed_pitch_ratio_ = pitch_ratio_;
	grain_window_ = WindowingFunctions::GenerateHann(grain_size_);
	delay_buffer_.assign(max_delay_samples_, 0.0f);
	const double phase_step = static_cast<double>(grain_size_) / static_cast<double>(grains_.size());
	const double spread = std::min(24.0, std::max(2.0, static_cast<double>(grain_size_) * 0.055));
	const double offsets[] = {-0.45, 0.18, 0.62, -0.12};
	for (size_t index = 0; index < grains_.size(); ++index) {
		grains_[index].position = phase_step * static_cast<double>(index);
		grains_[index].delay_offset_samples = offsets[index] * spread;
	}
	return !delay_buffer_.empty();
}

void GranularPitchShifter::SetPitchRatio(float ratio) {
	pitch_ratio_ = std::clamp(ratio, 0.5f, 2.0f);
}

bool GranularPitchShifter::ProcessBlock(const float* input, size_t frame_count, float* output) {
	if (input == nullptr || output == nullptr || frame_count == 0 || delay_buffer_.empty() || grain_window_.empty()) {
		return false;
	}

	perf_counter_.StartCounter("granular_pitch_shift");
	const double usable_delay = static_cast<double>(max_delay_samples_ > 2 ? max_delay_samples_ - 2 : 1);
	const double grain_size = static_cast<double>(grain_size_);
	const double smoothing = std::clamp(64.0 / std::max(1.0f, sample_rate_), 0.00025, 0.006);

	for (size_t i = 0; i < frame_count; ++i) {
		delay_buffer_[write_index_] = input[i];
		smoothed_pitch_ratio_ += static_cast<float>((pitch_ratio_ - smoothed_pitch_ratio_) * smoothing);
		const double ratio = static_cast<double>(smoothed_pitch_ratio_);
		const double pitch_delta = std::abs(1.0 - ratio) * grain_size;
		const double minimum_base_delay = pitch_delta + std::max(24.0, grain_size * 0.45);
		const double maximum_base_delay = std::max(minimum_base_delay, usable_delay - pitch_delta - 4.0);
		const double natural_base_delay = grain_size * 1.55 + pitch_delta;
		const double base_delay = std::clamp(natural_base_delay, minimum_base_delay, maximum_base_delay);
		const double delay_slope = 1.0 - ratio;

		float mixed = 0.0f;
		float weight_sum = 0.0f;
		for (GrainState& grain : grains_) {
			const size_t window_index = std::min(
				grain_window_.size() - 1,
				static_cast<size_t>(grain.position) % grain_window_.size());
			const float window = grain_window_[window_index];
			const double delay = base_delay + delay_slope * grain.position + grain.delay_offset_samples;
			mixed += ReadDelayedSample(delay) * window;
			weight_sum += window;
			AdvanceGrain(&grain);
		}

		const float shifted = weight_sum > 1.0e-6f ? mixed / weight_sum : input[i];
		const float neutral_blend = std::clamp(1.0f - std::abs(smoothed_pitch_ratio_ - 1.0f) * 36.0f, 0.0f, 1.0f);
		output[i] = shifted * (1.0f - neutral_blend) + input[i] * neutral_blend;
		write_index_ = (write_index_ + 1) % delay_buffer_.size();
	}

	perf_counter_.StopCounter("granular_pitch_shift");
	return true;
}

void GranularPitchShifter::Reset() {
	write_index_ = 0;
	smoothed_pitch_ratio_ = pitch_ratio_;
	std::fill(delay_buffer_.begin(), delay_buffer_.end(), 0.0f);
	const double phase_step = static_cast<double>(grain_size_) / static_cast<double>(grains_.size());
	for (size_t index = 0; index < grains_.size(); ++index) {
		grains_[index].position = phase_step * static_cast<double>(index);
	}
}

std::string GranularPitchShifter::GetReport() const {
	return "GranularPitchShifter: sr=" + std::to_string(sample_rate_) +
		", ratio=" + std::to_string(pitch_ratio_) +
		", smooth=" + std::to_string(smoothed_pitch_ratio_) +
		", grain=" + std::to_string(grain_size_) +
		", delay=" + std::to_string(max_delay_samples_) +
		", grains=4";
}

float GranularPitchShifter::ReadDelayedSample(double delay_samples) const {
	if (delay_buffer_.empty()) {
		return 0.0f;
	}

	const double clamped_delay = std::clamp(delay_samples, 1.0, static_cast<double>(delay_buffer_.size() - 2));
	double read_position = static_cast<double>(write_index_) - clamped_delay;
	while (read_position < 0.0) {
		read_position += static_cast<double>(delay_buffer_.size());
	}
	while (read_position >= static_cast<double>(delay_buffer_.size())) {
		read_position -= static_cast<double>(delay_buffer_.size());
	}

	const size_t index_a = static_cast<size_t>(read_position);
	const size_t index_b = (index_a + 1) % delay_buffer_.size();
	const float fraction = static_cast<float>(read_position - std::floor(read_position));
	return delay_buffer_[index_a] + (delay_buffer_[index_b] - delay_buffer_[index_a]) * fraction;
}

void GranularPitchShifter::AdvanceGrain(GrainState* grain) {
	if (grain == nullptr) {
		return;
	}

	grain->position += 1.0;
	while (grain->position >= static_cast<double>(grain_size_)) {
		grain->position -= static_cast<double>(grain_size_);
	}
}

}  // namespace XER::Audio::DSP
