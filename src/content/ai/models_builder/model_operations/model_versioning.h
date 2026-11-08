#pragma once

#include <cstdint>
#include <string>

namespace XER::ModelsBuilder::Operations {

struct ModelVersion {
	uint32_t major = 1U;
	uint32_t minor = 0U;
	uint32_t patch = 0U;
};

class ModelVersioning {
 public:
	static std::string ToString(const ModelVersion& version);
	static ModelVersion Parse(const std::string& version_text);

	static ModelVersion BumpMajor(const ModelVersion& version);
	static ModelVersion BumpMinor(const ModelVersion& version);
	static ModelVersion BumpPatch(const ModelVersion& version);

	static std::string AttachVersionToModelName(const std::string& model_name, const ModelVersion& version);
};

}  // namespace XER::ModelsBuilder::Operations

