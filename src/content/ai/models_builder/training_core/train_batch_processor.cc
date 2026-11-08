#include "train_batch_processor.h"

namespace XER::ModelsBuilder::Training {

TrainBatchProcessor::TrainBatchProcessor(int batch_size)
    : batch_size_(batch_size) {}

void TrainBatchProcessor::Process(
    const std::vector<float>& x, const std::vector<float>& y, int features,
    std::function<void(const std::vector<float>&, const std::vector<float>&,
                        int)> callback) {
  size_t n_samples = x.size() / static_cast<size_t>(features);
  int step = 0;
  for (size_t offset = 0; offset < n_samples;
       offset += static_cast<size_t>(batch_size_), ++step) {
    size_t end = std::min(offset + static_cast<size_t>(batch_size_), n_samples);
    std::vector<float> bx(x.begin() + offset * features,
                           x.begin() + end * features);
    std::vector<float> by(y.begin() + offset, y.begin() + end);
    callback(bx, by, step);
  }
}

}  // namespace XER::ModelsBuilder::Training
