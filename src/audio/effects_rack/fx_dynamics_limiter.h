#pragma once

#include <cstddef>
#include <string>

#include "async_io/sync_primitives/mutex_wrapper.h"

namespace XER::Engine::Audio::FX {

class DynamicsLimiter {
public:
	DynamicsLimiter();
	~DynamicsLimiter();

	void SetCeilingDb(float ceiling_db);
	void SetRelease(float release);
	bool ProcessBlock(const float* input, size_t frame_count, float* output);

	std::string GetReport() const;

private:
	XER::AsyncIO::IO::Sync::MutexWrapper mutex_;
	float ceiling_db_;
	float release_;
	float gain_;
};

}  // namespace XER::Engine::Audio::FX
