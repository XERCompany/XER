#include "serializer.h"

#include "../model_core/dense_layer.h"
#include "../model_core/layer.h"
#include "../utility/mb_error_handler.h"
#include "../utility/mb_logger.h"

#include <cereal/archives/json.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/utility.hpp>
#include <cereal/types/vector.hpp>

#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace XER::ModelsBuilder::Serialization {

namespace {

using XER::ModelsBuilder::Core::ActivationType;
using XER::ModelsBuilder::Core::DenseLayer;
using XER::ModelsBuilder::Core::Layer;
using XER::ModelsBuilder::Core::LayerType;
using XER::ModelsBuilder::Core::Model;
using XER::ModelsBuilder::Core::ModelType;
using XER::ModelsBuilder::Utility::ModelBuilderErrorCode;
using XER::ModelsBuilder::Utility::ModelBuilderErrorHandler;
using XER::ModelsBuilder::Utility::ModelBuilderLogger;

struct SerializedLayer {
    std::string name;
    uint32_t layer_type;
    uint32_t activation_type;
    uint32_t units;
    std::vector<uint32_t> output_shape;

    template <class Archive>
    void serialize(Archive& archive) {
        archive(cereal::make_nvp("name", name),
                cereal::make_nvp("layer_type", layer_type),
                cereal::make_nvp("activation_type", activation_type),
                cereal::make_nvp("units", units),
                cereal::make_nvp("output_shape", output_shape));
    }
};

struct SerializedModel {
    std::string model_name;
    uint32_t model_type;
    bool is_compiled;
    std::vector<uint32_t> input_shape;
    std::vector<uint32_t> output_shape;
    std::vector<SerializedLayer> layers;

    template <class Archive>
    void serialize(Archive& archive) {
        archive(cereal::make_nvp("model_name", model_name),
                cereal::make_nvp("model_type", model_type),
                cereal::make_nvp("is_compiled", is_compiled),
                cereal::make_nvp("input_shape", input_shape),
                cereal::make_nvp("output_shape", output_shape),
                cereal::make_nvp("layers", layers));
    }
};

SerializedLayer SerializeLayer(const std::shared_ptr<Layer>& layer) {
    SerializedLayer dto{};
    dto.name = layer->GetLayerName();
    dto.layer_type = static_cast<uint32_t>(layer->GetLayerType());
    dto.output_shape = layer->GetOutputShape();
    dto.activation_type = static_cast<uint32_t>(ActivationType::Linear);
    dto.units = 0U;

    if (const std::shared_ptr<DenseLayer> dense = std::dynamic_pointer_cast<DenseLayer>(layer)) {
        dto.units = dense->GetUnits();
        dto.activation_type = static_cast<uint32_t>(dense->GetActivation());
    }

    return dto;
}

std::shared_ptr<Layer> DeserializeLayer(const SerializedLayer& dto) {
    switch (static_cast<LayerType>(dto.layer_type)) {
        case LayerType::Dense: {
            std::shared_ptr<DenseLayer> dense = std::make_shared<DenseLayer>(dto.units);
            dense->SetLayerName(dto.name);
            dense->SetActivation(static_cast<ActivationType>(dto.activation_type));
            return dense;
        }
        default:
            return nullptr;
    }
}

SerializedModel SerializeModel(const Model& model) {
    SerializedModel dto{};
    dto.model_name = model.GetModelName();
    dto.model_type = static_cast<uint32_t>(model.GetModelType());
    dto.is_compiled = model.IsCompiled();
    dto.input_shape = model.GetInputShape();
    dto.output_shape = model.GetOutputShape();

    for (const std::shared_ptr<Layer>& layer : model.GetLayers()) {
        dto.layers.push_back(SerializeLayer(layer));
    }

    return dto;
}

bool WriteArchive(const std::string& filepath, const SerializedModel& dto) {
    std::ofstream output(filepath, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    cereal::JSONOutputArchive archive(output);
    archive(cereal::make_nvp("model", dto));
    return true;
}

bool ReadArchive(const std::string& filepath, SerializedModel& dto) {
    std::ifstream input(filepath, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }

    cereal::JSONInputArchive archive(input);
    archive(cereal::make_nvp("model", dto));
    return true;
}

} // namespace

ModelSerializer& ModelSerializer::GetInstance() {
    static ModelSerializer instance;
    return instance;
}

bool ModelSerializer::SaveModel(const std::string& filepath, const Core::Model& model) {
    try {
        const SerializedModel dto = SerializeModel(model);
        if (!WriteArchive(filepath, dto)) {
            ModelBuilderErrorHandler::GetInstance().ReportError(
                ModelBuilderErrorCode::IoFailure,
                "Failed to open model file for writing: " + filepath,
                "ModelsBuilder::Serialization::ModelSerializer::SaveModel");
            return false;
        }

        ModelBuilderLogger::GetInstance().Info("Saved model to " + filepath);
        return true;
    } catch (const std::exception& exception) {
        ModelBuilderErrorHandler::GetInstance().ReportError(
            ModelBuilderErrorCode::SerializationFailed,
            exception.what(),
            "ModelsBuilder::Serialization::ModelSerializer::SaveModel");
        return false;
    }
}

std::shared_ptr<Core::Model> ModelSerializer::LoadModel(const std::string& filepath) {
    try {
        SerializedModel dto{};
        if (!ReadArchive(filepath, dto)) {
            ModelBuilderErrorHandler::GetInstance().ReportError(
                ModelBuilderErrorCode::IoFailure,
                "Failed to open model file for reading: " + filepath,
                "ModelsBuilder::Serialization::ModelSerializer::LoadModel");
            return nullptr;
        }

        std::shared_ptr<Model> model = std::make_shared<Model>(dto.model_name);
        model->SetModelType(static_cast<ModelType>(dto.model_type));
        model->ClearLayers();

        for (const SerializedLayer& layer_dto : dto.layers) {
            std::shared_ptr<Layer> layer = DeserializeLayer(layer_dto);
            if (!layer) {
                std::ostringstream message;
                message << "Unsupported layer type during load: " << layer_dto.layer_type;
                ModelBuilderErrorHandler::GetInstance().ReportError(
                    ModelBuilderErrorCode::SerializationFailed,
                    message.str(),
                    "ModelsBuilder::Serialization::ModelSerializer::LoadModel");
                return nullptr;
            }
            model->AddLayer(layer);
        }

        if (!dto.input_shape.empty() && !model->Build(dto.input_shape)) {
            return nullptr;
        }

        if (dto.is_compiled && !model->Compile()) {
            return nullptr;
        }

        ModelBuilderLogger::GetInstance().Info("Loaded model from " + filepath);
        return model;
    } catch (const std::exception& exception) {
        ModelBuilderErrorHandler::GetInstance().ReportError(
            ModelBuilderErrorCode::SerializationFailed,
            exception.what(),
            "ModelsBuilder::Serialization::ModelSerializer::LoadModel");
        return nullptr;
    }
}

bool ModelSerializer::SaveModelState(const std::string& filepath, const Core::Model& model) {
    return SaveModel(filepath, model);
}

bool ModelSerializer::LoadModelState(const std::string& filepath, Core::Model& model) {
    std::shared_ptr<Core::Model> loaded_model = LoadModel(filepath);
    if (!loaded_model) {
        return false;
    }

    model = *loaded_model;
    return true;
}

} // namespace XER::ModelsBuilder::Serialization
