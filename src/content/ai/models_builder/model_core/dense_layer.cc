#include "dense_layer.h"

#include <sstream>

namespace XER::ModelsBuilder::Core {

DenseLayer::DenseLayer(uint32_t units) 
    : Layer(LayerType::Dense), units_(units), activation_(ActivationType::ReLU), input_units_(0U), initialized_(false) {
    layer_name_ = "Dense_" + std::to_string(units_);
}

bool DenseLayer::ValidateInputShape(const std::vector<uint32_t>& input_shape) const {
    return Layer::ValidateInputShape(input_shape) && !input_shape.empty() && units_ > 0U;
}

std::vector<uint32_t> DenseLayer::ComputeOutputShape(const std::vector<uint32_t>& input_shape) {
    if (!ValidateInputShape(input_shape)) {
        output_shape_.clear();
        return output_shape_;
    }

    input_units_ = input_shape.back();
    output_shape_ = {units_};
    return output_shape_;
}

bool DenseLayer::Initialize() {
    initialized_ = units_ > 0U;
    return initialized_;
}

void DenseLayer::SetWeights(std::vector<float> weights, std::vector<float> bias) {
    // Keep parameters only when they match the declared fan-in/fan-out once
    // the input shape is known; before Build() any size is accepted and
    // validated again at inference time.
    if (!weights.empty() && input_units_ > 0U &&
        weights.size() != static_cast<size_t>(units_) * input_units_) {
        weights.clear();
        bias.clear();
        return;
    }
    if (!bias.empty() && bias.size() != units_) {
        bias.clear();
    }
    weights_ = std::move(weights);
    bias_ = std::move(bias);
}

std::string DenseLayer::Describe() const {
    std::ostringstream stream;
    stream << layer_name_
           << " type=Dense"
           << " units=" << units_
           << " activation=" << static_cast<int>(activation_)
           << " input_units=" << input_units_
           << " initialized=" << (initialized_ ? "true" : "false")
           << " has_weights=" << (HasWeights() ? "true" : "false");
    return stream.str();
}

} // namespace XER::ModelsBuilder::Core
