#pragma once

#include "darknet_version_v3_v4.h"
#include "../../models_builder/model_core/layer.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace XER::ModelsBuilder::Reader::Darknet {

/// @brief Loads binary weights from a Darknet .weights file
///
/// After parsing with DarknetVersionV3V4, reads floating-point weights
/// in the order Darknet stores them: bn_biases, bn_weights, bn_means,
/// bn_variances, biases, weights.
class DarknetWeightsLoader {
 public:
  /// Load weights from file
  /// @param filepath Path to .weights binary
  bool LoadFile(const std::string& filepath);

  /// Parse from memory buffer
  bool LoadBuffer(const uint8_t* data, size_t size);

  /// Read `count` floats from the current cursor position
  /// @param count Number of float32 values to read
  /// @return Float vector; empty on failure
  std::vector<float> ReadFloats(size_t count);

  /// Get version info parsed from header
  const DarknetVersion& GetVersion() const { return version_checker_.GetVersion(); }

  /// Returns true when all weights have been consumed
  bool IsExhausted() const { return cursor_ >= data_.size(); }

  /// Current byte position in weight stream
  size_t GetCursor() const { return cursor_; }

  /// Total weight data size (bytes after header)
  size_t GetWeightBytes() const;

 private:
  DarknetVersionV3V4    version_checker_;
  std::vector<uint8_t>  data_;     ///< Full file bytes
  size_t                cursor_{0}; ///< Current read position in data_
};

}  // namespace XER::ModelsBuilder::Reader::Darknet
