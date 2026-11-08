#pragma once

#include "reader_base.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace XER::ModelsBuilder::Reader {

class ReaderRegistry {
 public:
	using ReaderFactory = std::function<std::unique_ptr<ReaderBase>()>;

	static ReaderRegistry& GetInstance();

	void Register(const std::string& extension, ReaderFactory factory);
	std::unique_ptr<ReaderBase> Create(const std::string& extension) const;
	bool Has(const std::string& extension) const;

 private:
	ReaderRegistry() = default;

	std::unordered_map<std::string, ReaderFactory> factories_;
};

}  // namespace XER::ModelsBuilder::Reader

