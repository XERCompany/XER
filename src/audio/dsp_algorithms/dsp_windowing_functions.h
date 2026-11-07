#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "async_io/log_and_debug/io_dump_helper.h"

namespace XER::Engine::Audio::DSP {

class WindowingFunctions {
public:
	static std::vector<float> GenerateHann(size_t size);
	static std::vector<float> GenerateSqrtHann(size_t size);
	static std::vector<float> GenerateHamming(size_t size);
	static std::vector<float> GenerateBlackman(size_t size);

	static void ApplyWindow(const std::vector<float>& window, float* samples, size_t size);
	static std::string DescribeWindow(const std::vector<float>& window);
};

}  // namespace XER::Engine::Audio::DSP
