#include "metadata_extractor.h"

#include <sstream>

namespace XER::ModelsBuilder::Reader::Schema {

ModelMetadata MetadataExtractor::Extract(const Core::Model& model,
                                          const std::string& format) {
  ModelMetadata meta;
  meta.name        = model.GetModelName();
  meta.format      = format;
  meta.layer_count = model.GetLayerCount();
  return meta;
}

std::string MetadataExtractor::ToJsonString(const ModelMetadata& meta) {
  std::ostringstream oss;
  oss << "{\n"
      << "  \"name\": \"" << meta.name << "\",\n"
      << "  \"format\": \"" << meta.format << "\",\n"
      << "  \"layer_count\": " << meta.layer_count << ",\n"
      << "  \"param_count\": " << meta.param_count << "\n"
      << "}";
  return oss.str();
}

}  // namespace XER::ModelsBuilder::Reader::Schema
