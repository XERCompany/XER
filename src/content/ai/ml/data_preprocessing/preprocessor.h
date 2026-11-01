#pragma once

#include "../data_types/tensor.h"
#include <memory>
#include <string>

namespace Engine::MLData::Preprocessing {

class Preprocessor {
public:
    virtual ~Preprocessor() = default;
    
    virtual std::shared_ptr<Types::Tensor> Process(const Types::Tensor& input) = 0;
    virtual std::string GetProcessorName() const = 0;
};

} // namespace Engine::MLData::Preprocessing
