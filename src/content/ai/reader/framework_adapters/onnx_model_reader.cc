#include "onnx_model_reader.h"

#include "../onnx_stack/onnx_loader.h"

namespace XER::ModelsBuilder::Reader::Framework {

LoadResult OnnxModelReader::Load(const std::string& filepath) {
  try {
    auto model = Onnx::OnnxLoader::Load(filepath);
    return {model, model != nullptr, model ? "" : "ONNX loader returned null model"};
  } catch (const std::exception& ex) {
    return {nullptr, false, ex.what()};
  }
}

bool OnnxModelReader::CanLoad(const std::string& filepath) const {
  return filepath.size() >= 5 && filepath.substr(filepath.size() - 5) == ".onnx";
}

}  // namespace XER::ModelsBuilder::Reader::Framework
