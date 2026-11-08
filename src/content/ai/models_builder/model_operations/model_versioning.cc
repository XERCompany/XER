#include "model_versioning.h"

#include <charconv>

namespace XER::ModelsBuilder::Operations {

std::string ModelVersioning::ToString(const ModelVersion& version) {
  return std::to_string(version.major) + "." +
         std::to_string(version.minor) + "." +
         std::to_string(version.patch);
}

ModelVersion ModelVersioning::Parse(const std::string& version_text) {
  ModelVersion version;

  uint32_t values[3] = {1U, 0U, 0U};
  size_t start = 0U;
  for (size_t index = 0U; index < 3U; ++index) {
    const size_t dot = version_text.find('.', start);
    const size_t end = (dot == std::string::npos) ? version_text.size() : dot;

    const char* begin_ptr = version_text.data() + start;
    const char* end_ptr = version_text.data() + end;
    (void)std::from_chars(begin_ptr, end_ptr, values[index]);

    if (dot == std::string::npos) {
      break;
    }
    start = dot + 1U;
  }

  version.major = values[0];
  version.minor = values[1];
  version.patch = values[2];
  return version;
}

ModelVersion ModelVersioning::BumpMajor(const ModelVersion& version) {
  return ModelVersion{version.major + 1U, 0U, 0U};
}

ModelVersion ModelVersioning::BumpMinor(const ModelVersion& version) {
  return ModelVersion{version.major, version.minor + 1U, 0U};
}

ModelVersion ModelVersioning::BumpPatch(const ModelVersion& version) {
  return ModelVersion{version.major, version.minor, version.patch + 1U};
}

std::string ModelVersioning::AttachVersionToModelName(const std::string& model_name,
                                                      const ModelVersion& version) {
  return model_name + "@" + ToString(version);
}

}  // namespace XER::ModelsBuilder::Operations
