#include "dsp_windowing_functions.h"

#include <algorithm>
#include <cmath>

namespace XER::Audio::DSP {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

std::vector<float> WindowingFunctions::GenerateHann(size_t size) {
	std::vector<float> window(size, 0.0f);
	if (size == 0) {
		return window;
	}

	for (size_t index = 0; index < size; ++index) {
		window[index] = 0.5f * (1.0f - std::cos(2.0f * kPi * index / (size - 1)));
	}
	return window;
}

std::vector<float> WindowingFunctions::GenerateSqrtHann(size_t size) {
	std::vector<float> window = GenerateHann(size);
	for (float& value : window) {
		value = std::sqrt(std::max(value, 0.0f));
	}
	return window;
}

std::vector<float> WindowingFunctions::GenerateHamming(size_t size) {
	std::vector<float> window(size, 0.0f);
	if (size == 0) {
		return window;
	}

	for (size_t index = 0; index < size; ++index) {
		window[index] = 0.54f - 0.46f * std::cos(2.0f * kPi * index / (size - 1));
	}
	return window;
}

std::vector<float> WindowingFunctions::GenerateBlackman(size_t size) {
	std::vector<float> window(size, 0.0f);
	if (size == 0) {
		return window;
	}

	for (size_t index = 0; index < size; ++index) {
		const float phase = static_cast<float>(index) / static_cast<float>(size - 1);
		window[index] = 0.42f - 0.5f * std::cos(2.0f * kPi * phase) + 0.08f * std::cos(4.0f * kPi * phase);
	}
	return window;
}

void WindowingFunctions::ApplyWindow(const std::vector<float>& window, float* samples, size_t size) {
	if (samples == nullptr || window.size() < size) {
		return;
	}

	for (size_t index = 0; index < size; ++index) {
		samples[index] *= window[index];
	}
}

std::string WindowingFunctions::DescribeWindow(const std::vector<float>& window) {
	if (window.empty()) {
		return "Window: empty";
	}

	const float min_value = *std::min_element(window.begin(), window.end());
	const float max_value = *std::max_element(window.begin(), window.end());
	const uint32_t encoded_span = static_cast<uint32_t>((max_value - min_value) * 1000.0f);

	return "Window: size=" + std::to_string(window.size()) +
		", min=" + std::to_string(min_value) +
		", max=" + std::to_string(max_value) +
		", span_bits=" + XER::AsyncIO::IO::LogDebug::DumpHelper::BitDump(encoded_span);
}

}  // namespace XER::Audio::DSP
