#pragma once

#include <cstddef>
#include <string>

#include "async_io/log_and_debug/io_perf_counter.h"
#include "audio/dsp_algorithms/dsp_delay_line_circular.h"

namespace XER::Engine::Audio::FX {

// Amplitude modulation: output = input * (1 - depth/2 + depth/2 * lfo).
class ModTremolo {
public:
	ModTremolo();
	~ModTremolo();

	bool Initialize(float sample_rate);
	void SetRateHz(float rate_hz);
	void SetDepth(float depth);
	void SetMix(float mix);
	bool ProcessBlock(const float* input, size_t frame_count, float* output);
	std::string GetReport() const;

private:
	float sample_rate_ = 44100.0f;
	float rate_hz_ = 4.5f;
	float depth_ = 0.5f;
	float mix_ = 1.0f;
	float lfo_phase_ = 0.0f;
	XER::AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;
};

// Pitch modulation via a fractional-delay vibrato (no pitch shifter needed).
class ModVibrato {
public:
	ModVibrato();
	~ModVibrato();

	bool Initialize(float sample_rate, size_t max_delay_samples);
	void SetRateHz(float rate_hz);
	void SetDepthMs(float depth_ms);
	void SetMix(float mix);
	bool ProcessBlock(const float* input, size_t frame_count, float* output);
	std::string GetReport() const;

private:
	float sample_rate_ = 44100.0f;
	float rate_hz_ = 5.0f;
	float depth_ms_ = 4.0f;
	float mix_ = 1.0f;
	float lfo_phase_ = 0.0f;
	XER::Engine::Audio::DSP::CircularDelayLine delay_line_;
	XER::AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;
};

// Ring modulation: input multiplied by a sine carrier, blended by mix.
class ModRingMod {
public:
	ModRingMod();
	~ModRingMod();

	bool Initialize(float sample_rate);
	void SetFrequencyHz(float frequency_hz);
	void SetMix(float mix);
	bool ProcessBlock(const float* input, size_t frame_count, float* output);
	std::string GetReport() const;

private:
	float sample_rate_ = 44100.0f;
	float frequency_hz_ = 80.0f;
	float mix_ = 1.0f;
	float phase_ = 0.0f;
	XER::AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;
};

}  // namespace XER::Engine::Audio::FX
