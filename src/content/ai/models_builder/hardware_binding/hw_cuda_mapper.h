#pragma once

#include "../model_core/model.h"

#include <cstddef>
#include <string>
#include <vector>

namespace XER::ModelsBuilder::HardwareBinding {

struct CudaMapperConfig {
	bool enable_cuda = true;
	bool enable_cutlass = true;
	size_t preferred_workspace_mb = 256U;
	uint32_t streams = 1U;
};

struct CudaKernelPlan {
	bool enabled = false;
	size_t workspace_bytes = 0U;
	uint32_t stream_count = 1U;
	bool use_cutlass = false;
	std::vector<std::string> layer_kernels;
};

class CudaMapper {
 public:
	void SetConfig(const CudaMapperConfig& config);
	const CudaMapperConfig& GetConfig() const { return config_; }

	bool IsAvailable() const;
	CudaKernelPlan BuildPlan(const Core::Model& model) const;

 private:
	CudaMapperConfig config_;
};

}  // namespace XER::ModelsBuilder::HardwareBinding

