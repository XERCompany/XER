#include "custom_effect_list.h"

#include "custom_effect_algorithms.h"

namespace XER::Audio::FX {

std::vector<CustomEffectNode> CreateCustomEffectNodeList(const std::vector<std::string>& algorithm_names) {
	std::vector<CustomEffectNode> nodes;
	nodes.reserve(algorithm_names.size());
	for (const auto& algorithm_name : algorithm_names) {
		nodes.push_back(CreateDefaultCustomEffectNode(algorithm_name));
	}
	return nodes;
}

}  // namespace XER::Audio::FX
