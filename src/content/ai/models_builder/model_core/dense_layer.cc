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

std::string DenseLayer::Describe() const {
    std::ostringstream stream;
    stream << layer_name_
           << " type=Dense"
           << " units=" << units_
           << " activation=" << static_cast<int>(activation_)
           << " input_units=" << input_units_
           << " initialized=" << (initialized_ ? "true" : "false");
    return stream.str();
}

} // namespace XER::ModelsBuilder::Core
