#include "tf_saved_model_parser.h"

#include "tf_pb_reader.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace XER::ModelsBuilder::Reader::Framework {

LoadResult TfSavedModelParser::Load(const std::string& filepath) {
  // Check if filepath is a directory containing saved_model.pb
  std::string pb = filepath + "/saved_model.pb";
  if (!fs::exists(pb))
    return {nullptr, false, "No saved_model.pb in: " + filepath};

  TfPbReader pb_reader;
  LoadResult pb_result = pb_reader.Load(pb);
  if (!pb_result.success) {
    return {nullptr, false, "Failed to parse saved_model.pb: " + pb_result.error};
  }
  return pb_result;
}

bool TfSavedModelParser::CanLoad(const std::string& filepath) const {
  std::error_code ec;
  if (!fs::is_directory(filepath, ec)) return false;
  std::string pb = filepath + "/saved_model.pb";
  return fs::exists(pb);
}

}  // namespace XER::ModelsBuilder::Reader::Framework
