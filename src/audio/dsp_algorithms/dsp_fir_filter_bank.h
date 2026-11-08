#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "async_io/sync_primitives/mutex_wrapper.h"
#include <future>

namespace XER::Audio::DSP {

class FIRFilterBank {
public:
	std::string GetMemoryStats() const;
	FIRFilterBank();
	~FIRFilterBank();

	void Clear();
	bool AddFilter(const std::vector<float>& taps);
	size_t GetFilterCount() const;

	bool ProcessSample(float input, std::vector<float>& outputs);
	std::future<bool> ProcessSampleAsync(float input, std::vector<float>& outputs);
	std::string GetReport() const;

private:
	mutable XER::AsyncIO::IO::Sync::MutexWrapper mutex_;
	std::vector<std::vector<float>> filters_;
	std::vector<std::vector<float>> states_;
};

}  // namespace XER::Audio::DSP
