#include "weight_distributor.h"

#include "../model_core/dense_layer.h"
#include "error_handler/err_monitor.h"

#include <sstream>
#include <algorithm>

namespace XER::ModelsBuilder::Weights {

size_t WeightDistributor::Distribute(Core::Model& model,
                                     const std::vector<WeightTensor>& tensors) const {
    if (tensors.empty()) return 0;

    size_t assigned = 0;
    size_t tensor_idx = 0;
    const auto& layers = model.GetLayers();

    for (size_t i = 0; i < layers.size() && tensor_idx < tensors.size(); ++i) {
        if (layers[i]->GetLayerType() != Core::LayerType::Dense) continue;

        // Count weight tensor (kernel) and optional bias tensor
        if (tensor_idx < tensors.size()) {
            ++tensor_idx;
            ++assigned;
        }
        if (tensor_idx < tensors.size()) {
            ++tensor_idx;  // bias
        }
    }

    if (assigned == 0) {
        XER::ErrorHandler::ReportDiagnostic(
            XER::ErrorHandler::MakeDiagnosticData("warning",
                "WeightDistributor", "No layers received weight tensors"));
    }

    return assigned;
}

std::vector<DistributionPlan> WeightDistributor::PlanDistribution(
    const Core::Model& model,
    const std::vector<WeightTensor>& tensors) const {

    std::vector<DistributionPlan> plan;
    const auto& layers = model.GetLayers();
    size_t tensor_idx = 0;

    for (size_t i = 0; i < layers.size() && tensor_idx < tensors.size(); ++i) {
        if (layers[i]->GetLayerType() != Core::LayerType::Dense) continue;
        auto* dense = dynamic_cast<const Core::DenseLayer*>(layers[i].get());

        if (tensor_idx < tensors.size()) {
            DistributionPlan entry;
            entry.layer_index = i;
            entry.layer_name = layers[i]->GetLayerName();
            entry.tensor_name = tensors[tensor_idx].name;
            entry.param_count = tensors[tensor_idx].values.size();
            entry.assigned = true;
            plan.push_back(entry);
            ++tensor_idx;
        }
        if (tensor_idx < tensors.size()) {
            DistributionPlan bias_entry;
            bias_entry.layer_index = i;
            bias_entry.layer_name = layers[i]->GetLayerName();
            bias_entry.tensor_name = tensors[tensor_idx].name;
            bias_entry.param_count = tensors[tensor_idx].values.size();
            bias_entry.assigned = true;
            plan.push_back(bias_entry);
            ++tensor_idx;
        }
        (void)dense;
    }

    return plan;
}

std::vector<WeightTensor> WeightDistributor::Collect(const Core::Model& model) const {
    std::vector<WeightTensor> result;
    const auto& layers = model.GetLayers();

    for (size_t i = 0; i < layers.size(); ++i) {
        if (layers[i]->GetLayerType() != Core::LayerType::Dense) continue;
        auto* dense = dynamic_cast<const Core::DenseLayer*>(layers[i].get());
        if (!dense) continue;

        // Build placeholder tensors using known shape from output
        const auto& out_shape = dense->GetOutputShape();
        if (out_shape.empty()) continue;

        WeightTensor wt;
        wt.name = dense->GetLayerName() + "_weights";
        wt.shape = out_shape;  // actual shape resolved by caller
        result.push_back(wt);

        WeightTensor bt;
        bt.name = dense->GetLayerName() + "_biases";
        bt.shape = {dense->GetUnits()};
        result.push_back(bt);
    }

    return result;
}

}  // namespace XER::ModelsBuilder::Weights
