#include "hw_tensorrt_converter.h"

#include "../model_core/layer.h"
#include "../utility/ai_runtime_features.h"
#include "../utility/mb_logger.h"

#include <fstream>

namespace XER::ModelsBuilder::HardwareBinding {

void TensorRtConverter::SetConfig(const TensorRtConfig& config) {
	config_ = config;
}

bool TensorRtConverter::IsAvailable() const {
	const Utility::ExternalLibraryAvailability libs = Utility::DetectExternalLibraries();
	return libs.has_cuda && libs.has_cudnn;
}

bool TensorRtConverter::CanConvert(const Core::Model& model) const {
	if (!config_.enable_tensorrt || !IsAvailable()) {
		return false;
	}

	for (const auto& layer : model.GetLayers()) {
		if (layer == nullptr) {
			continue;
		}
		const Core::LayerType type = layer->GetLayerType();
		if (type != Core::LayerType::Dense &&
				type != Core::LayerType::Conv2D &&
				type != Core::LayerType::Activation &&
				type != Core::LayerType::MaxPool &&
				type != Core::LayerType::Flatten) {
			return false;
		}
	}
	return true;
}

std::string TensorRtConverter::BuildEngineDescriptor(const Core::Model& model) const {
	return std::string("tensorrt_engine{") +
				 "model=\"" + model.GetModelName() + "\"" +
				 ",layer_count=" + std::to_string(model.GetLayerCount()) +
				 ",fp16=" + (config_.use_fp16 ? "true" : "false") +
				 ",int8=" + (config_.use_int8 ? "true" : "false") +
				 ",workspace_mb=" + std::to_string(config_.max_workspace_mb) +
				 ",opt_batch=" + std::to_string(config_.optimal_batch_size) +
				 ",available=" + (IsAvailable() ? "true" : "false") +
				 "}";
}

bool TensorRtConverter::Convert(const Core::Model& model, const std::string& output_path) const {
	if (!CanConvert(model)) {
		Utility::ModelBuilderLogger::GetInstance().Warning(
				"TensorRtConverter: conversion skipped (unsupported layers or unavailable runtime).");
		return false;
	}

	std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
	if (!out.is_open()) {
		Utility::ModelBuilderLogger::GetInstance().Error(
				"TensorRtConverter: cannot open output file: " + output_path);
		return false;
	}

	out << BuildEngineDescriptor(model) << "\n";
	if (!out.good()) {
		Utility::ModelBuilderLogger::GetInstance().Error(
				"TensorRtConverter: write failure: " + output_path);
		return false;
	}

	Utility::ModelBuilderLogger::GetInstance().Info(
			"TensorRtConverter: generated engine descriptor for model " + model.GetModelName());
	return true;
}

}  // namespace XER::ModelsBuilder::HardwareBinding

