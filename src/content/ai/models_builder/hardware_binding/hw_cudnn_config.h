#pragma once

#include <cstddef>
#include <string>

namespace XER::ModelsBuilder::HardwareBinding {

struct CudnnConfig {
	bool enable_cudnn = true;
	bool deterministic = false;
	bool allow_tf32 = true;
	size_t workspace_limit_mb = 512U;
};

class CudnnConfigurator {
 public:
	void SetConfig(const CudnnConfig& config);
	const CudnnConfig& GetConfig() const { return config_; }

	bool IsAvailable() const;
	std::string BuildDescriptor() const;

 private:
	CudnnConfig config_;
};

}  // namespace XER::ModelsBuilder::HardwareBinding

