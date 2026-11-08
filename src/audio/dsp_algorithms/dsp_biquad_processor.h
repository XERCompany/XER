#pragma once

#include <string>

#include "async_io/log_and_debug/io_perf_counter.h"

namespace XER::Audio::DSP {

enum class BiquadType {
	LowPass,
	HighPass,
	BandPass,
	Notch
};

class BiquadProcessor {
public:
	BiquadProcessor();
	BiquadProcessor(const BiquadProcessor& other);
	BiquadProcessor(BiquadProcessor&& other) noexcept;
	~BiquadProcessor();

	BiquadProcessor& operator=(const BiquadProcessor& other);
	BiquadProcessor& operator=(BiquadProcessor&& other) noexcept;

	bool Configure(BiquadType type, float sample_rate, float frequency, float q);
	float ProcessSample(float sample);
	void Reset();

	std::string GetReport() const;

private:
	BiquadType type_;
	float sample_rate_;
	float frequency_;
	float q_;
	float b0_;
	float b1_;
	float b2_;
	float a1_;
	float a2_;
	float x1_;
	float x2_;
	float y1_;
	float y2_;
	XER::AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;

	bool ComputeCoefficients();
};

}  // namespace XER::Audio::DSP
