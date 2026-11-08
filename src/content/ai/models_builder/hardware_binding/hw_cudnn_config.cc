#include "hw_cudnn_config.h"

#include "../utility/ai_runtime_features.h"

namespace XER::ModelsBuilder::HardwareBinding {

void CudnnConfigurator::SetConfig(const CudnnConfig& config) {
	config_ = config;
}

bool CudnnConfigurator::IsAvailable() const {
	const Utility::ExternalLibraryAvailability libs = Utility::DetectExternalLibraries();
	return libs.has_cudnn;
}

std::string CudnnConfigurator::BuildDescriptor() const {
	return std::string("cudnn{") +
				 "enabled=" + (config_.enable_cudnn ? "true" : "false") +
				 ",deterministic=" + (config_.deterministic ? "true" : "false") +
				 ",allow_tf32=" + (config_.allow_tf32 ? "true" : "false") +
				 ",workspace_mb=" + std::to_string(config_.workspace_limit_mb) +
				 ",runtime_available=" + (IsAvailable() ? "true" : "false") +
				 "}";
}

}  // namespace XER::ModelsBuilder::HardwareBinding

