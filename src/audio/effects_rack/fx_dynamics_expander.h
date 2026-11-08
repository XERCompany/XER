#pragma once

#include <cstddef>
#include <string>

#include "async_io/sync_primitives/mutex_wrapper.h"

namespace XER::Audio::FX {

class DynamicsExpander {
public:
	DynamicsExpander();
	~DynamicsExpander();

	void SetThresholdDb(float threshold_db);
	void SetRatio(float ratio);
	void SetRangeDb(float range_db);
	bool ProcessBlock(const float* input, size_t frame_count, float* output);

	std::string GetReport() const;

private:
	XER::AsyncIO::IO::Sync::MutexWrapper mutex_;
	float threshold_db_;
	float ratio_;
	float range_db_;
};

}  // namespace XER::Audio::FX
