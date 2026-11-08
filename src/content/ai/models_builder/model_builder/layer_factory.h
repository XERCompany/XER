#pragma once

#include "../model_core/layer.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>

namespace XER::ModelsBuilder::Builder {

class LayerFactory {
public:
    static LayerFactory& GetInstance();
    
    std::shared_ptr<Core::Layer> CreateLayer(Core::LayerType type);
    std::shared_ptr<Core::Layer> CreateLayer(const std::string& layer_name);
    
    void RegisterLayer(const std::string& name, std::function<std::shared_ptr<Core::Layer>()> creator);

private:
    LayerFactory() = default;
    std::unordered_map<std::string, std::function<std::shared_ptr<Core::Layer>()>> creators_;
};

} // namespace XER::ModelsBuilder::Builder
