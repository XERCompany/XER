#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include <future>

#include "async_io/async_buffer_pool.h"
#include "async_io/log_and_debug/io_perf_counter.h"

namespace XER::Audio::DSP {

class ConvolutionEngine {
public:
	std::string GetMemoryStats() const;
	ConvolutionEngine();
	~ConvolutionEngine();

	bool SetImpulseResponse(const float* ir, size_t ir_size);
	bool ProcessBlock(const float* input, size_t frame_count, float* output);
	void Reset();
	std::future<bool> ProcessBlockAsync(const float* input, size_t frame_count, float* output);

	size_t GetImpulseSize() const { return impulse_response_.size(); }
	std::string GetReport() const;

private:
	std::vector<float> impulse_response_;
	std::vector<float> history_;
	size_t history_cursor_ = 0;
	XER::AsyncIO::IO::Async::AsyncBufferPool buffer_pool_;
	XER::AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;
};

}  // namespace XER::Audio::DSP
