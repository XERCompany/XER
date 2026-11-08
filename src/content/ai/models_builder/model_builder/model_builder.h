#pragma once

#include "../model_core/model.h"
#include "../model_core/dense_layer.h"
#include "layer_factory.h"
#include "../utility/mb_logger.h"
#include <memory>

namespace XER::ModelsBuilder::Builder {

class ModelBuilderBase {
public:
    virtual ~ModelBuilderBase() = default;
    
    virtual std::shared_ptr<XER::ModelsBuilder::Core::Model> Build() = 0;
};

class SequentialModelBuilder : public ModelBuilderBase {
public:
    SequentialModelBuilder();
    
    SequentialModelBuilder& WithName(const std::string& name);
    SequentialModelBuilder& AddDenseLayer(uint32_t units);
    SequentialModelBuilder& AddConvLayer(uint32_t filters, uint32_t kernel_size);
    SequentialModelBuilder& AddDropout(float rate);
    
    std::shared_ptr<XER::ModelsBuilder::Core::Model> Build() override;

private:
    std::shared_ptr<XER::ModelsBuilder::Core::Model> model_;
};

} // namespace XER::ModelsBuilder::Builder
