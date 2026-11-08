#pragma once

#include "../core_loaders/reader_base.h"

namespace XER::ModelsBuilder::Reader::Framework {

class OpenVinoIrReader : public ReaderBase {
 public:
  LoadResult Load(const std::string& filepath) override;
  bool CanLoad(const std::string& filepath) const override;
  std::string GetFormatName() const override { return "OpenVINO IR"; }
  std::string GetFileExtension() const override { return ".xml"; }
};

}  // namespace XER::ModelsBuilder::Reader::Framework
