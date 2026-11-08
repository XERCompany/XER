#include "layer_factory.h"

namespace XER::ModelsBuilder::Builder {

LayerFactory& LayerFactory::GetInstance() {
    static LayerFactory instance;
    return instance;
}

std::shared_ptr<Core::Layer> LayerFactory::CreateLayer(Core::LayerType type) {
    return std::make_shared<Core::Layer>(type);
}

std::shared_ptr<Core::Layer> LayerFactory::CreateLayer(const std::string& layer_name) {
    auto it = creators_.find(layer_name);
    if (it != creators_.end()) {
        return it->second();
    }
    return nullptr;
}

void LayerFactory::RegisterLayer(const std::string& name, std::function<std::shared_ptr<Core::Layer>()> creator) {
    creators_[name] = creator;
}

} // namespace XER::ModelsBuilder::Builder
