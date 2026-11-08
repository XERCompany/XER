#pragma once

#include <cstddef>
#include <string>

#include "async_io/sync_primitives/mutex_wrapper.h"
#include "audio/dsp_algorithms/dsp_convolution_engine.h"

namespace XER::Audio::FX {

class ReverbConvolution {
public:
	ReverbConvolution();
	~ReverbConvolution();

	bool LoadImpulseResponse(const float* ir, size_t ir_size);
	void SetMix(float mix);
	bool ProcessBlock(const float* input, size_t frame_count, float* output);

	std::string GetReport() const;

private:
	float mix_;
	XER::AsyncIO::IO::Sync::MutexWrapper mutex_;
	XER::Audio::DSP::ConvolutionEngine convolution_;
};

}  // namespace XER::Audio::FX
