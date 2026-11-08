#pragma once

#include "../../models_builder/model_core/model.h"
#include "../../ml/tensors/tensor.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace XER::ModelsBuilder::Reader::Transform {

/// @brief Unpacks and dequantizes weight tensors stored in compressed formats
///
/// Supports: int8 symmetric/asymmetric, fp16 → fp32 conversion via fp16.h
class TransWeightUnpack {
 public:
  struct WeightBlob {
    std::vector<uint8_t> raw_data;
    std::vector<size_t>  shape;
    float                scale{1.0f};
    int32_t              zero_point{0};
    enum class DType { Float32, Float16, Int8 } dtype{DType::Float32};
  };

  /// Unpack a WeightBlob to a float32 Tensor
  static std::shared_ptr<ML::Tensors::Tensor> Unpack(
      const WeightBlob& blob);

  /// Unpack all registered blobs and store in a map
  std::unordered_map<std::string, std::shared_ptr<ML::Tensors::Tensor>>
  UnpackAll(const std::unordered_map<std::string, WeightBlob>& blobs);
};

}  // namespace XER::ModelsBuilder::Reader::Transform
