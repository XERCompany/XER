#include "layer.h"

#include <sstream>

namespace XER::ModelsBuilder::Core {

Layer::Layer(LayerType type) : layer_type_(type) {
    layer_name_ = "Layer_" + std::to_string(static_cast<int>(type));
}

bool Layer::ValidateInputShape(const std::vector<uint32_t>& input_shape) const {
    if (input_shape.empty()) {
        return false;
    }

    for (const uint32_t dimension : input_shape) {
        if (dimension == 0U) {
            return false;
        }
    }

    return true;
}

std::vector<uint32_t> Layer::ComputeOutputShape(const std::vector<uint32_t>& input_shape) {
    output_shape_ = input_shape;
    return output_shape_;
}

std::string Layer::Describe() const {
    std::ostringstream stream;
    stream << layer_name_ << " type=" << static_cast<int>(layer_type_) << " output_shape=[";
    for (size_t index = 0; index < output_shape_.size(); ++index) {
        stream << output_shape_[index];
        if (index + 1U < output_shape_.size()) {
            stream << ", ";
        }
    }
    stream << "]";
    return stream.str();
}

} // namespace XER::ModelsBuilder::Core
