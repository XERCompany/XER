#include "model.h"

#include "../utility/mb_error_handler.h"
#include "../utility/mb_logger.h"
#include "../utility/ai_runtime_features.h"

#include <sstream>

namespace XER::ModelsBuilder::Core {

Model::Model(const std::string& name)
    : model_name_(name), model_type_(ModelType::Sequential), is_compiled_(false) {
}

void Model::AddLayer(std::shared_ptr<Layer> layer) {
    if (!layer) {
        Utility::ModelBuilderErrorHandler::GetInstance().ReportError(
            Utility::ModelBuilderErrorCode::InvalidArgument,
            "Attempted to add a null layer to the model.",
            "ModelsBuilder::Core::Model::AddLayer");
        return;
    }

    is_compiled_ = false;
    layers_.push_back(std::move(layer));
}

void Model::ClearLayers() {
    layers_.clear();
    input_shape_.clear();
    output_shape_.clear();
    is_compiled_ = false;
}

bool Model::Build(const std::vector<uint32_t>& input_shape) {
    if (input_shape.empty()) {
        Utility::ModelBuilderErrorHandler::GetInstance().ReportError(
            Utility::ModelBuilderErrorCode::InvalidShape,
            "Input shape cannot be empty.",
            "ModelsBuilder::Core::Model::Build");
        return false;
    }

    if (layers_.empty()) {
        Utility::ModelBuilderErrorHandler::GetInstance().ReportWarning(
            Utility::ModelBuilderErrorCode::ModelHasNoLayers,
            "Build requested for a model without layers.",
            "ModelsBuilder::Core::Model::Build");
        input_shape_ = input_shape;
        output_shape_ = input_shape;
        return true;
    }

    std::vector<uint32_t> current_shape = input_shape;
    for (auto& layer : layers_) {
        if (!layer->ValidateInputShape(current_shape)) {
            std::ostringstream message;
            message << "Layer '" << layer->GetLayerName() << "' rejected input shape.";
            Utility::ModelBuilderErrorHandler::GetInstance().ReportError(
                Utility::ModelBuilderErrorCode::InvalidShape,
                message.str(),
                "ModelsBuilder::Core::Model::Build");
            return false;
        }

        if (!layer->Initialize()) {
            std::ostringstream message;
            message << "Layer '" << layer->GetLayerName() << "' failed to initialize.";
            Utility::ModelBuilderErrorHandler::GetInstance().ReportError(
                Utility::ModelBuilderErrorCode::InitializationFailed,
                message.str(),
                "ModelsBuilder::Core::Model::Build");
            return false;
        }

        current_shape = layer->ComputeOutputShape(current_shape);
        if (current_shape.empty()) {
            std::ostringstream message;
            message << "Layer '" << layer->GetLayerName() << "' produced an empty output shape.";
            Utility::ModelBuilderErrorHandler::GetInstance().ReportError(
                Utility::ModelBuilderErrorCode::InvalidShape,
                message.str(),
                "ModelsBuilder::Core::Model::Build");
            return false;
        }

        Utility::ModelBuilderLogger::GetInstance().Debug(
            "Build step completed for layer: " + layer->Describe());
    }

    input_shape_ = input_shape;
    output_shape_ = current_shape;
    return true;
}

bool Model::Compile() {
    if (layers_.empty()) {
        Utility::ModelBuilderErrorHandler::GetInstance().ReportError(
            Utility::ModelBuilderErrorCode::CompilationFailed,
            "Cannot compile a model without layers.",
            "ModelsBuilder::Core::Model::Compile");
        return false;
    }

    if (output_shape_.empty()) {
        Utility::ModelBuilderErrorHandler::GetInstance().ReportError(
            Utility::ModelBuilderErrorCode::CompilationFailed,
            "Cannot compile before a successful build.",
            "ModelsBuilder::Core::Model::Compile");
        return false;
    }

    is_compiled_ = true;
    static const std::string runtime_banner = Utility::BuildRuntimeBanner();
    Utility::ModelBuilderLogger::GetInstance().Info(runtime_banner);
    Utility::ModelBuilderLogger::GetInstance().Info(
        "Compiled model '" + model_name_ + "' with " + std::to_string(layers_.size()) + " layers.");
    return true;
}

std::shared_ptr<Layer> Model::GetLayer(size_t index) const {
    if (index < layers_.size()) {
        return layers_[index];
    }
    return nullptr;
}

} // namespace XER::ModelsBuilder::Core
