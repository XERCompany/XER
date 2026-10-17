#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Engine::Audio::FX {

enum class CustomEffectBackend {
	kBuiltinAlgorithm,
	kClapPlugin,
};

enum class CustomEffectAlgorithm {
	kUnknown,
	kPassthrough,
	kCompressor,
	kExpander,
	kGate,
	kChorus,
	kFlanger,
	kPhaser,
	kParametricEq,
	kLimiter,
	kBitcrush,
	kTube,
	kReverbAlgorithmic,
	kPitchShift,
	kAutoTuneCorrectPitch,
	kDelay,
	kPlugin,
};

struct CustomEffectParameter {
	std::string name;
	float value = 0.0f;
};

struct CustomEffectCurvePoint {
	size_t frame_index = 0;
	float value = 0.0f;
};

struct CustomEffectNode {
	std::string label;
	CustomEffectBackend backend = CustomEffectBackend::kBuiltinAlgorithm;
	CustomEffectAlgorithm algorithm = CustomEffectAlgorithm::kUnknown;
	uint32_t stage_index = 0;
	std::string plugin_reference;
	std::vector<CustomEffectParameter> parameters;
	std::vector<CustomEffectCurvePoint> mix_curve;
	bool enabled = true;
};

struct CustomEffectRenderConfig {
	size_t block_size = 1024u;
	uint32_t worker_count = 0;
	bool enable_multicore_render = true;
};

struct CustomEffectPackage {
	std::string label;
	CustomEffectRenderConfig render_config;
	std::vector<CustomEffectNode> nodes;
};

struct CustomEffectReport {
	std::string label;
	size_t node_count = 0;
	size_t stage_count = 0;
	size_t channel_count = 1;
	size_t worker_count_used = 1;
	float peak = 0.0f;
	double rms = 0.0;
	std::vector<std::string> node_reports;
	std::vector<std::string> stage_reports;
};

std::string CustomEffectBackendToString(CustomEffectBackend backend);
bool ParseCustomEffectBackend(const std::string& text, CustomEffectBackend* backend_out);
std::string CustomEffectAlgorithmToString(CustomEffectAlgorithm algorithm);
bool ParseCustomEffectAlgorithm(const std::string& text, CustomEffectAlgorithm* algorithm_out);
std::string BuildCustomEffectReportText(const CustomEffectReport& report);

}  // namespace Engine::Audio::FX
