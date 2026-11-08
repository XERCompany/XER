#include "custom_effect_eq_lite.h"

#include "custom_effect_eq_engine.h"

namespace XER::Audio::FX {

CustomEffectNode CreateCustomEffectLiteEqNode(float gain_db) {
	CustomEffectNode node = CreateCustomEffectParametricEqNode(2200.0f, 0.8f, gain_db, 0.65f);
	node.label = "eq_lite";
	return node;
}

}  // namespace XER::Audio::FX
