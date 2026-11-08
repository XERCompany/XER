#pragma once

#include "../../models_builder/model_core/model.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace XER::ModelsBuilder::Reader {

struct LoadResult {
	std::shared_ptr<Core::Model> model;
	bool success = false;
	std::string error;
};

class ReaderBase {
 public:
	virtual ~ReaderBase() = default;

	virtual LoadResult Load(const std::string& filepath) = 0;
	virtual bool CanLoad(const std::string& filepath) const = 0;
	virtual std::string GetFormatName() const = 0;
	virtual std::string GetFileExtension() const = 0;

 protected:
	static bool ReadFile(const std::string& filepath, std::vector<uint8_t>& out_bytes);
};

}  // namespace XER::ModelsBuilder::Reader

