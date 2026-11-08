#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include <future>

#include "async_io/sync_primitives/mutex_wrapper.h"

namespace XER::Audio::FX {

class ModPhaser {
public:
	std::string GetMemoryStats() const;
	ModPhaser();
	~ModPhaser();

	bool Initialize(float sample_rate, size_t stages = 4);
	void SetRateHz(float rate_hz);
	void SetDepth(float depth);
	void SetFeedback(float feedback);
	void SetMix(float mix);
	bool ProcessBlock(const float* input, size_t frame_count, float* output);
	std::future<bool> ProcessBlockAsync(const float* input, size_t frame_count, float* output);

	std::string GetReport() const;

private:
	float sample_rate_;
	size_t stages_;
	float rate_hz_;
	float depth_;
	float feedback_;
	float mix_;
	float lfo_phase_;
	float feedback_state_;
	std::vector<float> ap_state_;
	XER::AsyncIO::IO::Sync::MutexWrapper mutex_;
};

}  // namespace XER::Audio::FX
