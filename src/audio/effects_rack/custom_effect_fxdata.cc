#include "custom_effect_fxdata.h"

namespace XER::Audio::FX {

float GetCustomEffectParameterValue(
	const std::vector<CustomEffectParameter>& parameters,
	const std::string& name,
	float default_value) {
	for (const auto& parameter : parameters) {
		if (parameter.name == name) {
			return parameter.value;
		}
	}
	return default_value;
}

void SetCustomEffectParameterValue(
	std::vector<CustomEffectParameter>* parameters,
	const std::string& name,
	float value) {
	if (parameters == nullptr) {
		return;
	}
	for (auto& parameter : *parameters) {
		if (parameter.name == name) {
			parameter.value = value;
			return;
		}
	}
	parameters->push_back(CustomEffectParameter{name, value});
}

}  // namespace XER::Audio::FX
