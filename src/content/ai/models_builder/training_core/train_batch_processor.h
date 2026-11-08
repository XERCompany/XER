#pragma once

#include <functional>
#include <vector>

namespace XER::ModelsBuilder::Training {

/// @brief Assembles mini-batches from a flat data vector
class TrainBatchProcessor {
 public:
  explicit TrainBatchProcessor(int batch_size);

  /// Process data in batches; callback receives (batch_x, batch_y, step)
  void Process(const std::vector<float>& x, const std::vector<float>& y,
               int features,
               std::function<void(const std::vector<float>&,
                                   const std::vector<float>&, int)> callback);

 private:
  int batch_size_;
};

}  // namespace XER::ModelsBuilder::Training
