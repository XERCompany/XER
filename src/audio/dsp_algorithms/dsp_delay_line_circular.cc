#include "dsp_delay_line_circular.h"

#include <algorithm>

namespace XER::Engine::Audio::DSP {

CircularDelayLine::CircularDelayLine()
	: max_delay_samples_(0),
	  delay_samples_(0),
	  write_index_(0) {}

CircularDelayLine::~CircularDelayLine() = default;

bool CircularDelayLine::Initialize(size_t max_delay_samples) {
	if (max_delay_samples == 0) {
		return false;
	}

	max_delay_samples_ = max_delay_samples;
	delay_samples_ = std::min<size_t>(delay_samples_, max_delay_samples_);
	write_index_ = 0;
	buffer_.assign(max_delay_samples_, 0.0f);
	return true;
}

void CircularDelayLine::SetDelaySamples(size_t delay_samples) {
	delay_samples_ = std::min(delay_samples, max_delay_samples_ > 0 ? max_delay_samples_ - 1 : 0);
}

void CircularDelayLine::Reset() {
	std::fill(buffer_.begin(), buffer_.end(), 0.0f);
	write_index_ = 0;
}

float CircularDelayLine::Process(float sample) {
	if (buffer_.empty()) {
		return sample;
	}

	size_t read_index = 0;
	if (write_index_ >= delay_samples_) {
		read_index = write_index_ - delay_samples_;
	} else {
		read_index = buffer_.size() + write_index_ - delay_samples_;
	}

	const float delayed = buffer_[read_index];
	buffer_[write_index_] = sample;
	write_index_ = (write_index_ + 1) % buffer_.size();

	return delayed;
}

float CircularDelayLine::ProcessFractional(float sample, float delay_samples) {
	if (buffer_.empty()) {
		return 0.0f;
	}

	const float max_delay = static_cast<float>(buffer_.size() - 1);
	const float delay = std::clamp(delay_samples, 0.0f, max_delay);

	// Read position trails the write index by `delay` samples.
	float pos = static_cast<float>(write_index_) - delay;
	if (pos < 0.0f) {
		pos += static_cast<float>(buffer_.size());
	}
	std::size_t i0 = static_cast<std::size_t>(pos);
	if (i0 >= buffer_.size()) {
		i0 = buffer_.size() - 1;
	}
	const float frac = pos - static_cast<float>(i0);
	const std::size_t i1 = (i0 + 1) % buffer_.size();
	const float delayed = buffer_[i0] * (1.0f - frac) + buffer_[i1] * frac;

	buffer_[write_index_] = sample;
	write_index_ = (write_index_ + 1) % buffer_.size();
	return delayed;
}

std::string CircularDelayLine::GetReport() const {
	return "DelayLine: max=" + std::to_string(max_delay_samples_) +
		", delay=" + std::to_string(delay_samples_);
}

}  // namespace XER::Engine::Audio::DSP
