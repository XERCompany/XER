#pragma once

#include "../model_core/model.h"

#include <cstddef>
#include <string>

namespace XER::ModelsBuilder::HardwareBinding {

struct TensorRtConfig {
	bool enable_tensorrt = true;
	bool use_fp16 = true;
	bool use_int8 = false;
	size_t max_workspace_mb = 1024U;
	uint32_t optimal_batch_size = 1U;
};

class TensorRtConverter {
 public:
	void SetConfig(const TensorRtConfig& config);
	const TensorRtConfig& GetConfig() const { return config_; }

	bool IsAvailable() const;
	bool CanConvert(const Core::Model& model) const;
	std::string BuildEngineDescriptor(const Core::Model& model) const;
	bool Convert(const Core::Model& model, const std::string& output_path) const;

 private:
	TensorRtConfig config_;
};

}  // namespace XER::ModelsBuilder::HardwareBinding

