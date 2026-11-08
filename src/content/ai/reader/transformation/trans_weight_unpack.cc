#include "trans_weight_unpack.h"

#include <fp16.h>
#include <cstring>

namespace XER::ModelsBuilder::Reader::Transform {

std::shared_ptr<ML::Tensors::Tensor> TransWeightUnpack::Unpack(
    const WeightBlob& blob) {
  size_t n = 1;
  for (size_t d : blob.shape) n *= d;

  std::vector<float> floats;
  floats.reserve(n);

  switch (blob.dtype) {
    case WeightBlob::DType::Float32: {
      floats.resize(n);
      std::memcpy(floats.data(), blob.raw_data.data(), n * sizeof(float));
      break;
    }
    case WeightBlob::DType::Float16: {
      const uint16_t* fp16_data =
          reinterpret_cast<const uint16_t*>(blob.raw_data.data());
      for (size_t i = 0; i < n; ++i)
        floats.push_back(fp16_ieee_to_fp32_value(fp16_data[i]));
      break;
    }
    case WeightBlob::DType::Int8: {
      const int8_t* i8 =
          reinterpret_cast<const int8_t*>(blob.raw_data.data());
      for (size_t i = 0; i < n; ++i)
        floats.push_back(blob.scale *
                         (static_cast<float>(i8[i]) -
                          static_cast<float>(blob.zero_point)));
      break;
    }
  }

  // Build Eigen matrix and wrap in Tensor
  size_t rows = blob.shape.empty() ? 1 : blob.shape[0];
  size_t cols = n / rows;
  Eigen::MatrixXf mat(static_cast<Eigen::Index>(rows),
                      static_cast<Eigen::Index>(cols));
  for (size_t i = 0; i < rows; ++i)
    for (size_t j = 0; j < cols; ++j)
      mat(static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(j)) =
          floats[i * cols + j];

  return std::make_shared<ML::Tensors::Tensor>(mat);
}

std::unordered_map<std::string, std::shared_ptr<ML::Tensors::Tensor>>
TransWeightUnpack::UnpackAll(
    const std::unordered_map<std::string, WeightBlob>& blobs) {
  std::unordered_map<std::string, std::shared_ptr<ML::Tensors::Tensor>> out;
  for (auto& [name, blob] : blobs) out[name] = Unpack(blob);
  return out;
}

}  // namespace XER::ModelsBuilder::Reader::Transform
