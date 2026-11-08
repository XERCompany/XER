#pragma once

#include "reader_base.h"

#include <memory>
#include <string>

namespace XER::ModelsBuilder::Reader {

class ReaderManager {
 public:
	ReaderManager();

	LoadResult Load(const std::string& filepath);
	std::unique_ptr<ReaderBase> CreateReader(const std::string& filepath) const;

 private:
	static std::string ExtractExtension(const std::string& filepath);
};

}  // namespace XER::ModelsBuilder::Reader

