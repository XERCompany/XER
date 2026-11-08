#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace XER::ModelsBuilder::HardwareBinding {

struct OpenVinoConfig {
	bool enable_openvino = true;
	std::string device = "CPU";
	uint32_t num_streams = 1U;
	bool enable_fp16 = false;
};

class OpenVinoConfigurator {
 public:
	void SetConfig(const OpenVinoConfig& config);
	const OpenVinoConfig& GetConfig() const { return config_; }

	bool IsAvailable() const;
	std::unordered_map<std::string, std::string> BuildProperties() const;

 private:
	OpenVinoConfig config_;
};

}  // namespace XER::ModelsBuilder::HardwareBinding

