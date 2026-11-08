#pragma once

#include <cstddef>
#include <string>

#include "custom_effect_node_wrapper.h"

namespace XER::Audio::FX {

class CustomEffectProcessor {
public:
	bool Initialize(const CustomEffectNode& node, float sample_rate, size_t max_block_size, std::string* error_out = nullptr);
	bool ProcessBlock(const float* input, size_t frame_count, float* output, std::string* error_out = nullptr);
	bool ProcessBuffer(const std::vector<float>& input, size_t block_size, std::vector<float>* output, std::string* error_out = nullptr, bool reset_frame_offset = true);
	void ResetFrameOffset();
	std::string GetReport() const;
	const CustomEffectNode& GetNode() const { return wrapper_.GetNode(); }

private:
	CustomEffectNodeWrapper wrapper_;
	size_t frame_offset_ = 0;
};

}  // namespace XER::Audio::FX
