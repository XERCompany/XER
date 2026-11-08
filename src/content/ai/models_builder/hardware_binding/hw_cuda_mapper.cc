#include "hw_cuda_mapper.h"

#include "../model_core/layer.h"
#include "../utility/ai_runtime_features.h"
#include "../utility/mb_logger.h"

#include <algorithm>

namespace XER::ModelsBuilder::HardwareBinding {

namespace {

std::string LayerKernelName(Core::LayerType layer_type) {
	switch (layer_type) {
		case Core::LayerType::Dense:
			return "kernel_dense_gemm";
		case Core::LayerType::Conv2D:
			return "kernel_conv2d";
		case Core::LayerType::MaxPool:
			return "kernel_maxpool";
		case Core::LayerType::Activation:
			return "kernel_activation";
		case Core::LayerType::Dropout:
			return "kernel_dropout";
		case Core::LayerType::BatchNorm:
			return "kernel_batchnorm";
		case Core::LayerType::Flatten:
			return "kernel_flatten";
		case Core::LayerType::LSTM:
			return "kernel_lstm";
		default:
			return "kernel_generic";
	}
}

}  // namespace

void CudaMapper::SetConfig(const CudaMapperConfig& config) {
	config_ = config;
	config_.streams = std::max<uint32_t>(1U, config_.streams);
}

bool CudaMapper::IsAvailable() const {
	const Utility::ExternalLibraryAvailability libs = Utility::DetectExternalLibraries();
	return libs.has_cuda;
}

CudaKernelPlan CudaMapper::BuildPlan(const Core::Model& model) const {
	CudaKernelPlan plan;
	const Utility::ExternalLibraryAvailability libs = Utility::DetectExternalLibraries();

	if (!config_.enable_cuda || !libs.has_cuda) {
		Utility::ModelBuilderLogger::GetInstance().Info(
				"CudaMapper: CUDA is unavailable, returning disabled plan.");
		return plan;
	}

	plan.enabled = true;
	plan.stream_count = std::max<uint32_t>(1U, config_.streams);
	plan.use_cutlass = config_.enable_cutlass && libs.has_cutlass;
	plan.workspace_bytes = config_.preferred_workspace_mb * 1024U * 1024U;

	for (const auto& layer : model.GetLayers()) {
		if (layer == nullptr) {
			continue;
		}
		plan.layer_kernels.push_back(LayerKernelName(layer->GetLayerType()));
	}

	Utility::ModelBuilderLogger::GetInstance().Info(
			"CudaMapper: built CUDA plan for " + std::to_string(plan.layer_kernels.size()) +
			" layers, streams=" + std::to_string(plan.stream_count) +
			", cutlass=" + std::string(plan.use_cutlass ? "on" : "off"));
	return plan;
}

}  // namespace XER::ModelsBuilder::HardwareBinding

