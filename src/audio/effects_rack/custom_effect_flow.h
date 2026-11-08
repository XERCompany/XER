#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "custom_effect_apply_multi.h"
#include "custom_effect_package.h"

namespace XER::Audio::FX {

class CustomEffectFlow {
public:
	bool Initialize(const CustomEffectPackage& package, float sample_rate, size_t max_block_size, std::string* error_out = nullptr);
	bool Process(const std::vector<float>& input, std::vector<float>* output, std::string* error_out = nullptr);
	// Live/streaming mode: processor frame offsets keep accumulating across
	// Process calls so LFO-driven effects (chorus, phaser, autotune, ...) do
	// not restart their phase on every audio block.
	void SetStreamingMode(bool enabled) { streaming_ = enabled; }
	bool IsStreamingMode() const { return streaming_; }
	std::string GetReport() const;
	const CustomEffectPackage& GetPackage() const { return package_; }
	const CustomEffectReport& GetLastReport() const { return last_report_; }

private:
	CustomEffectPackage package_;
	CustomEffectRenderConfig render_config_;
	std::vector<uint32_t> stage_indices_;
	std::deque<std::deque<CustomEffectProcessor>> stage_processors_;
	CustomEffectReport last_report_;
	bool streaming_ = false;
};

}  // namespace XER::Audio::FX
