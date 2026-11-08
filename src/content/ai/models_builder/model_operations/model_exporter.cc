#include "model_exporter.h"

#include "../hardware_binding/hw_onnx_exporter.h"
#include "../model_serialization/serializer.h"

namespace XER::ModelsBuilder::Operations {

bool ModelExporter::ExportNative(const Core::Model& model, const std::string& output_path) const {
	return Serialization::ModelSerializer::GetInstance().SaveModel(output_path, model);
}

bool ModelExporter::ExportOnnxLike(const Core::Model& model, const std::string& output_path) const {
	HardwareBinding::OnnxExporter exporter;
	return exporter.ExportModel(model, output_path);
}

}  // namespace XER::ModelsBuilder::Operations

