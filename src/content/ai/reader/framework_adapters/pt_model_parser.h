#pragma once

#include "../core_loaders/reader_base.h"
#include <string>

namespace XER::ModelsBuilder::Reader::Framework {

/// @brief Reads PyTorch TorchScript .pt / .pth model files
///
/// TorchScript archives are ZIP files containing serialized model + weights.
/// Full support requires libtorch; stubs provide format detection.
class PtModelParser : public ReaderBase {
 public:
  LoadResult Load(const std::string& filepath) override;
  bool CanLoad(const std::string& filepath) const override;
  std::string GetFormatName() const override { return "PyTorch"; }
  std::string GetFileExtension() const override { return ".pt"; }

 private:
  /// Check if file has ZIP magic (PK\x03\x04 or PK\x05\x06)
  static bool HasZipMagic(const std::vector<uint8_t>& data);
};

}  // namespace XER::ModelsBuilder::Reader::Framework
