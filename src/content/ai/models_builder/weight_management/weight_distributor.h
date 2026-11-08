#pragma once

#include "weight_initialization.h"
#include "../model_core/model.h"

#include <cstdint>
#include <string>
#include <vector>

namespace XER::ModelsBuilder::Weights {

struct DistributionPlan {
    size_t layer_index = 0;
    std::string layer_name;
    std::string tensor_name;
    size_t param_count = 0;
    bool assigned = false;
};

class WeightDistributor {
public:
    // Apply a flat list of tensors to matching model layers in order.
    // Returns number of layers successfully assigned.
    size_t Distribute(Core::Model& model,
                      const std::vector<WeightTensor>& tensors) const;

    // Build an assignment plan without actually mutating model (dry run).
    std::vector<DistributionPlan> PlanDistribution(
        const Core::Model& model,
        const std::vector<WeightTensor>& tensors) const;

    // Extract all assignable weight tensors from model for inspection.
    std::vector<WeightTensor> Collect(const Core::Model& model) const;
};

}  // namespace XER::ModelsBuilder::Weights
