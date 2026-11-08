#include "hw_onnx_exporter.h"

#include "../model_core/dense_layer.h"
#include "../model_core/layer.h"
#include "../utility/ai_runtime_features.h"
#include "../utility/mb_logger.h"

#include <fstream>
#include <sstream>

namespace XER::ModelsBuilder::HardwareBinding {

namespace {

std::string LayerTypeToOnnxOp(Core::LayerType type) {
	switch (type) {
		case Core::LayerType::Dense:
			return "Gemm";
		case Core::LayerType::Conv2D:
			return "Conv";
		case Core::LayerType::MaxPool:
			return "MaxPool";
		case Core::LayerType::Activation:
			return "Activation";
		case Core::LayerType::Dropout:
			return "Dropout";
		case Core::LayerType::BatchNorm:
			return "BatchNormalization";
		case Core::LayerType::Flatten:
			return "Flatten";
		case Core::LayerType::LSTM:
			return "LSTM";
		default:
			return "Identity";
	}
}

std::string ActivationToString(Core::ActivationType type) {
	switch (type) {
		case Core::ActivationType::ReLU:
			return "Relu";
		case Core::ActivationType::Sigmoid:
			return "Sigmoid";
		case Core::ActivationType::Tanh:
			return "Tanh";
		case Core::ActivationType::Linear:
			return "Linear";
		case Core::ActivationType::SoftMax:
			return "Softmax";
		default:
			return "Linear";
	}
}

}  // namespace

bool OnnxExporter::IsAvailable() const {
	const Utility::ExternalLibraryAvailability libs = Utility::DetectExternalLibraries();
	return libs.has_onnx;
}

bool OnnxExporter::ExportModel(const Core::Model& model, const std::string& output_path) const {
	std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
	if (!out.is_open()) {
		Utility::ModelBuilderLogger::GetInstance().Error(
				"OnnxExporter: failed to open output file: " + output_path);
		return false;
	}

	out << BuildOnnxLikeText(model);
	if (!out.good()) {
		Utility::ModelBuilderLogger::GetInstance().Error(
				"OnnxExporter: write failure: " + output_path);
		return false;
	}

	Utility::ModelBuilderLogger::GetInstance().Info(
			"OnnxExporter: exported model '" + model.GetModelName() + "' to " + output_path +
			(IsAvailable() ? " (native ONNX available)" : " (fallback text export)"));
	return true;
}

std::string OnnxExporter::BuildOnnxLikeText(const Core::Model& model) const {
	std::ostringstream stream;
	stream << "onnx_like_model {\n";
	stream << "  name: \"" << model.GetModelName() << "\"\n";
	stream << "  type: " << static_cast<uint32_t>(model.GetModelType()) << "\n";
	stream << "  compiled: " << (model.IsCompiled() ? "true" : "false") << "\n";
	stream << "  layer_count: " << model.GetLayerCount() << "\n";

	size_t index = 0U;
	for (const auto& layer : model.GetLayers()) {
		if (layer == nullptr) {
			continue;
		}
		stream << "  node {\n";
		stream << "    index: " << index++ << "\n";
		stream << "    name: \"" << layer->GetLayerName() << "\"\n";
		stream << "    op_type: \"" << LayerTypeToOnnxOp(layer->GetLayerType()) << "\"\n";

		const auto dense = std::dynamic_pointer_cast<Core::DenseLayer>(layer);
		if (dense != nullptr) {
			stream << "    units: " << dense->GetUnits() << "\n";
			stream << "    activation: \"" << ActivationToString(dense->GetActivation()) << "\"\n";
		}
		stream << "  }\n";
	}

	stream << "}\n";
	return stream.str();
}

}  // namespace XER::ModelsBuilder::HardwareBinding

