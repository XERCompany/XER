
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace XER::Engine::Audio::DSP {

class CircularDelayLine {
public:
	CircularDelayLine();
	~CircularDelayLine();

	bool Initialize(size_t max_delay_samples);
	void SetDelaySamples(size_t delay_samples);
	void Reset();

	float Process(float sample);
	// Fractional-delay read with linear interpolation — eliminates the
	// zipper/aliasing artifacts of integer-sample modulation delays.
	float ProcessFractional(float sample, float delay_samples);
	size_t GetMaxDelaySamples() const { return max_delay_samples_; }
	std::string GetReport() const;

private:
	size_t max_delay_samples_;
	size_t delay_samples_;
	size_t write_index_;
	std::vector<float> buffer_;
};

}  // namespace XER::Engine::Audio::DSP
