#include "hw_openvino_config.h"

#include "../utility/ai_runtime_features.h"

#include <algorithm>

namespace XER::ModelsBuilder::HardwareBinding {

void OpenVinoConfigurator::SetConfig(const OpenVinoConfig& config) {
	config_ = config;
	config_.num_streams = std::max<uint32_t>(1U, config_.num_streams);
	if (config_.device.empty()) {
		config_.device = "CPU";
	}
}

bool OpenVinoConfigurator::IsAvailable() const {
	const Utility::ExternalLibraryAvailability libs = Utility::DetectExternalLibraries();
	return libs.has_openvino;
}

std::unordered_map<std::string, std::string> OpenVinoConfigurator::BuildProperties() const {
	std::unordered_map<std::string, std::string> properties;
	properties.emplace("device", config_.device);
	properties.emplace("num_streams", std::to_string(config_.num_streams));
	properties.emplace("inference_precision", config_.enable_fp16 ? "FP16" : "FP32");
	properties.emplace("runtime_available", IsAvailable() ? "true" : "false");
	properties.emplace("enabled", config_.enable_openvino ? "true" : "false");
	return properties;
}

}  // namespace XER::ModelsBuilder::HardwareBinding

