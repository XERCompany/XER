#include "custom_effect_list.h"

#include "custom_effect_algorithms.h"

namespace XER::Engine::Audio::FX {

std::vector<CustomEffectNode> CreateCustomEffectNodeList(const std::vector<std::string>& algorithm_names) {
	std::vector<CustomEffectNode> nodes;
	nodes.reserve(algorithm_names.size());
	for (const auto& algorithm_name : algorithm_names) {
		nodes.push_back(CreateDefaultCustomEffectNode(algorithm_name));
	}
	return nodes;
}

}  // namespace XER::Engine::Audio::FX
