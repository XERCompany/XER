#include "custom_effect_processor.h"

#include <algorithm>

namespace XER::Audio::FX {

bool CustomEffectProcessor::Initialize(const CustomEffectNode& node, float sample_rate, size_t max_block_size, std::string* error_out) {
	frame_offset_ = 0;
	return wrapper_.Initialize(node, sample_rate, max_block_size, error_out);
}

bool CustomEffectProcessor::ProcessBlock(const float* input, size_t frame_count, float* output, std::string* error_out) {
	if (!wrapper_.ProcessBlock(input, frame_count, frame_offset_, output, error_out)) {
		return false;
	}
	frame_offset_ += frame_count;
	return true;
}

bool CustomEffectProcessor::ProcessBuffer(
	const std::vector<float>& input,
	size_t block_size,
	std::vector<float>* output,
	std::string* error_out,
	bool reset_frame_offset) {
	if (output == nullptr || block_size == 0) {
		if (error_out != nullptr) {
			*error_out = "custom effect processor output target is invalid";
		}
		return false;
	}
	output->assign(input.size(), 0.0f);
	std::vector<float> scratch(block_size, 0.0f);
	if (reset_frame_offset) {
		ResetFrameOffset();
	}
	for (size_t cursor = 0; cursor < input.size(); cursor += block_size) {
		const size_t frames = std::min(block_size, input.size() - cursor);
		if (!ProcessBlock(input.data() + static_cast<std::ptrdiff_t>(cursor), frames, scratch.data(), error_out)) {
			return false;
		}
		std::copy(scratch.begin(), scratch.begin() + static_cast<std::ptrdiff_t>(frames), output->begin() + static_cast<std::ptrdiff_t>(cursor));
	}
	return true;
}

void CustomEffectProcessor::ResetFrameOffset() {
	frame_offset_ = 0;
}

std::string CustomEffectProcessor::GetReport() const {
	return wrapper_.GetReport();
}

}  // namespace XER::Audio::FX
