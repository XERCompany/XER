#pragma once

#include "../model_core/model.h"
#include <memory>
#include <string>

namespace XER::ModelsBuilder::Serialization {

class ModelSerializer {
public:
    static ModelSerializer& GetInstance();
    
    bool SaveModel(const std::string& filepath, const Core::Model& model);
    std::shared_ptr<Core::Model> LoadModel(const std::string& filepath);
    
    bool SaveModelState(const std::string& filepath, const Core::Model& model);
    bool LoadModelState(const std::string& filepath, Core::Model& model);

private:
    ModelSerializer() = default;
};

} // namespace XER::ModelsBuilder::Serialization
