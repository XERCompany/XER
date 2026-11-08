#pragma once

#include "../model_core/model.h"
#include <memory>
#include <vector>

namespace XER::ModelsBuilder::Inference {

class Predictor {
public:
    explicit Predictor(std::shared_ptr<Core::Model> model);
    
    std::vector<float> Predict(const std::vector<float>& input);
    std::vector<std::vector<float>> PredictBatch(const std::vector<std::vector<float>>& inputs);
    
    bool IsModelReady() const { return model_ != nullptr && model_->IsCompiled(); }

private:
    std::shared_ptr<Core::Model> model_;
};

} // namespace XER::ModelsBuilder::Inference
