#include "custom_effect_invoke.h"

#include "custom_effect_fxdata.h"

namespace XER::Engine::Audio::FX {

bool InvokeCustomEffectParameter(
	CustomEffectPackage* package,
	const std::string& node_label,
	const std::string& parameter_name,
	float value) {
	if (package == nullptr) {
		return false;
	}
	for (auto& node : package->nodes) {
		if (node.label != node_label) {
			continue;
		}
		SetCustomEffectParameterValue(&node.parameters, parameter_name, value);
		return true;
	}
	return false;
}

}  // namespace XER::Engine::Audio::FX
