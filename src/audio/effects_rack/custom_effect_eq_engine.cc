#include "custom_effect_eq_engine.h"

#include "custom_effect_fxdata.h"

namespace XER::Audio::FX {

CustomEffectNode CreateCustomEffectParametricEqNode(float frequency_hz, float q, float gain_db, float mix) {
	CustomEffectNode node;
	node.label = "parametric_eq";
	node.backend = CustomEffectBackend::kBuiltinAlgorithm;
	node.algorithm = CustomEffectAlgorithm::kParametricEq;
	SetCustomEffectParameterValue(&node.parameters, "frequency_hz", frequency_hz);
	SetCustomEffectParameterValue(&node.parameters, "q", q);
	SetCustomEffectParameterValue(&node.parameters, "gain_db", gain_db);
	node.mix_curve.push_back(CustomEffectCurvePoint{0u, mix});
	return node;
}

}  // namespace XER::Audio::FX
