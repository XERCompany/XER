#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "async_io/sync_primitives/mutex_wrapper.h"
#include "dsp_biquad_processor.h"

namespace XER::Audio::DSP {

class IIRFilterBank {
public:
	IIRFilterBank();
	~IIRFilterBank();

	void Clear();
	bool AddBiquad(BiquadType type, float sample_rate, float frequency, float q);
	size_t GetFilterCount() const { return filters_.size(); }

	bool ProcessSample(float input, std::vector<float>& outputs);
	std::string GetReport() const;

private:
	XER::AsyncIO::IO::Sync::MutexWrapper mutex_;
	std::vector<BiquadProcessor> filters_;
};

}  // namespace XER::Audio::DSP
