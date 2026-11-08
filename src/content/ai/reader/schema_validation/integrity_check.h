#pragma once

#include "../../models_builder/model_core/model.h"
#include <string>
#include <vector>

namespace XER::ModelsBuilder::Reader::Schema {

/// @brief Checks model file integrity (magic bytes, checksums)
class IntegrityCheck {
 public:
  /// Verify ONNX magic bytes at start of buffer
  static bool CheckOnnxMagic(const uint8_t* data, size_t size);

  /// Verify Darknet weights header magic
  static bool CheckDarknetMagic(const uint8_t* data, size_t size);

  /// Compute Adler-32 checksum of buffer
  static uint32_t Adler32(const uint8_t* data, size_t size);

  /// Verify model has expected structure post-load
  static bool VerifyModel(const Core::Model& model,
                           std::string& out_error);
};

}  // namespace XER::ModelsBuilder::Reader::Schema
