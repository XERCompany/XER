#include "model_builder.h"

namespace XER::ModelsBuilder::Builder {

SequentialModelBuilder::SequentialModelBuilder() 
    : model_(std::make_shared<XER::ModelsBuilder::Core::Model>()) {
}

SequentialModelBuilder& SequentialModelBuilder::WithName(const std::string& name) {
    model_ = std::make_shared<XER::ModelsBuilder::Core::Model>(name);
    return *this;
}

SequentialModelBuilder& SequentialModelBuilder::AddDenseLayer(uint32_t units) {
    auto layer = std::make_shared<XER::ModelsBuilder::Core::DenseLayer>(units);
    model_->AddLayer(layer);
    return *this;
}

SequentialModelBuilder& SequentialModelBuilder::AddConvLayer(uint32_t filters, uint32_t kernel_size) {
    return *this;
}

SequentialModelBuilder& SequentialModelBuilder::AddDropout(float rate) {
    return *this;
}

std::shared_ptr<Core::Model> SequentialModelBuilder::Build() {
    model_->Compile();
    return model_;
}

} // namespace XER::ModelsBuilder::Builder
