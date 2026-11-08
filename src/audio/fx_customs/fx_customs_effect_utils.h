#pragma once

#include <string>
#include <utility>
#include <vector>

#include "audio/effects_rack/custom_effect_algorithms.h"
#include "audio/effects_rack/custom_effect_fxdata.h"

namespace XER::Audio::FX::Customs {

inline CustomEffectNode MakeFxNode(const std::string& algorithm_name, const std::string& label, uint32_t stage_index, float mix = 1.0f) {
	CustomEffectNode node = CreateDefaultCustomEffectNode(algorithm_name);
	node.label = label;
	node.stage_index = stage_index;
	node.mix_curve = {CustomEffectCurvePoint{0u, mix}};
	return node;
}

inline void SetFxParam(CustomEffectNode* node, const std::string& name, float value) {
	SetCustomEffectParameterValue(&node->parameters, name, value);
}

inline CustomEffectPackage MakeFxPresetPackage(const std::string& label, std::vector<CustomEffectNode> nodes) {
	CustomEffectPackage package;
	package.label = label;
	package.render_config.block_size = 1024u;
	package.render_config.enable_multicore_render = true;
	package.nodes = std::move(nodes);
	return package;
}

}  // namespace XER::Audio::FX::Customs
