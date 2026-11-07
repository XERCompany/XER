#pragma once

#include <cstddef>
#include <string>
#include <future>
#include <array>

#include "async_io/log_and_debug/io_perf_counter.h"
#include "audio/dsp_algorithms/dsp_delay_line_circular.h"

namespace XER::Engine::Audio::FX {

class ReverbAlgorithmic {
public:
	std::string GetMemoryStats() const;
	ReverbAlgorithmic();
	~ReverbAlgorithmic();

	bool Initialize(float sample_rate, size_t max_delay_samples);
	void SetRoomSize(float room_size);
	void SetDamping(float damping);
	void SetMix(float mix);
	bool ProcessBlock(const float* input, size_t frame_count, float* output);
	std::future<bool> ProcessBlockAsync(const float* input, size_t frame_count, float* output);

	std::string GetReport() const;

private:
	float sample_rate_;
	float room_size_;
	float damping_;
	float mix_;
	std::array<XER::Engine::Audio::DSP::CircularDelayLine, 6> combs_;
	std::array<XER::Engine::Audio::DSP::CircularDelayLine, 3> allpasses_;
	std::array<XER::Engine::Audio::DSP::CircularDelayLine, 4> early_reflections_;
	std::array<float, 6> comb_damping_state_{};
	std::array<float, 3> allpass_state_{};
	XER::AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;
};

}  // namespace XER::Engine::Audio::FX
