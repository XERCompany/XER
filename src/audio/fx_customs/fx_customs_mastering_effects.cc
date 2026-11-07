#include "fx_customs_mastering_effects.h"

#include "audio/fx_customs/fx_customs_effect_utils.h"

namespace XER::Engine::Audio::FX::Customs {

bool BuildFxCustomMasteringEffectPackage(const std::string& normalized_name, CustomEffectPackage* package_out) {
	if (package_out == nullptr) {
		return false;
	}

	if (normalized_name == "pulteclowend") {
		CustomEffectNode sub = MakeFxNode("parametric_eq", "pultec_low_sub_bloom", 0u, 0.86f);
		SetFxParam(&sub, "frequency_hz", 58.0f);
		SetFxParam(&sub, "q", 0.54f);
		SetFxParam(&sub, "gain_db", 3.8f);
		CustomEffectNode atten = MakeFxNode("parametric_eq", "pultec_low_box_control", 1u, 0.68f);
		SetFxParam(&atten, "frequency_hz", 185.0f);
		SetFxParam(&atten, "q", 0.82f);
		SetFxParam(&atten, "gain_db", -1.6f);
		CustomEffectNode tube = MakeFxNode("tube", "pultec_low_transformer", 2u, 0.22f);
		SetFxParam(&tube, "drive", 1.58f);
		SetFxParam(&tube, "output_gain", 0.94f);
		CustomEffectNode limiter = MakeFxNode("limiter", "pultec_low_peak_guard", 3u, 1.0f);
		SetFxParam(&limiter, "ceiling_db", -0.8f);
		*package_out = MakeFxPresetPackage("PultecLowEnd", {sub, atten, tube, limiter});
		return true;
	}

	if (normalized_name == "sslbuscomp") {
		CustomEffectNode comp = MakeFxNode("compressor", "ssl_bus_vca_glue", 0u, 0.68f);
		SetFxParam(&comp, "threshold_db", -13.5f);
		SetFxParam(&comp, "ratio", 2.35f);
		CustomEffectNode low = MakeFxNode("parametric_eq", "ssl_bus_low_tighten", 1u, 0.55f);
		SetFxParam(&low, "frequency_hz", 105.0f);
		SetFxParam(&low, "q", 0.74f);
		SetFxParam(&low, "gain_db", 1.1f);
		CustomEffectNode presence = MakeFxNode("parametric_eq", "ssl_bus_presence_lift", 2u, 0.48f);
		SetFxParam(&presence, "frequency_hz", 5200.0f);
		SetFxParam(&presence, "q", 0.70f);
		SetFxParam(&presence, "gain_db", 1.2f);
		CustomEffectNode limiter = MakeFxNode("limiter", "ssl_bus_true_peak_guard", 3u, 1.0f);
		SetFxParam(&limiter, "ceiling_db", -0.7f);
		*package_out = MakeFxPresetPackage("SSLBusComp", {comp, low, presence, limiter});
		return true;
	}

	if (normalized_name == "transientpunch") {
		CustomEffectNode expander = MakeFxNode("expander", "transient_punch_front_expander", 0u, 0.78f);
		SetFxParam(&expander, "threshold_db", -31.0f);
		SetFxParam(&expander, "ratio", 1.85f);
		SetFxParam(&expander, "range_db", 8.0f);
		CustomEffectNode attack = MakeFxNode("parametric_eq", "transient_punch_click_focus", 1u, 0.62f);
		SetFxParam(&attack, "frequency_hz", 2800.0f);
		SetFxParam(&attack, "q", 0.96f);
		SetFxParam(&attack, "gain_db", 2.0f);
		CustomEffectNode body = MakeFxNode("parametric_eq", "transient_punch_body", 2u, 0.58f);
		SetFxParam(&body, "frequency_hz", 120.0f);
		SetFxParam(&body, "q", 0.72f);
		SetFxParam(&body, "gain_db", 1.8f);
		CustomEffectNode limiter = MakeFxNode("limiter", "transient_punch_limiter", 3u, 1.0f);
		SetFxParam(&limiter, "ceiling_db", -0.9f);
		*package_out = MakeFxPresetPackage("TransientPunch", {expander, attack, body, limiter});
		return true;
	}

	if (normalized_name == "exciterair") {
		CustomEffectNode silk = MakeFxNode("parametric_eq", "exciter_air_silk", 0u, 0.72f);
		SetFxParam(&silk, "frequency_hz", 7600.0f);
		SetFxParam(&silk, "q", 0.70f);
		SetFxParam(&silk, "gain_db", 2.2f);
		CustomEffectNode tube = MakeFxNode("tube", "exciter_air_harmonics", 1u, 0.26f);
		SetFxParam(&tube, "drive", 1.72f);
		SetFxParam(&tube, "output_gain", 0.92f);
		CustomEffectNode air = MakeFxNode("parametric_eq", "exciter_air_open_top", 2u, 0.64f);
		SetFxParam(&air, "frequency_hz", 13200.0f);
		SetFxParam(&air, "q", 0.48f);
		SetFxParam(&air, "gain_db", 3.6f);
		CustomEffectNode deharsh = MakeFxNode("parametric_eq", "exciter_air_deharsh", 3u, 0.52f);
		SetFxParam(&deharsh, "frequency_hz", 3200.0f);
		SetFxParam(&deharsh, "q", 1.15f);
		SetFxParam(&deharsh, "gain_db", -0.9f);
		*package_out = MakeFxPresetPackage("ExciterAir", {silk, tube, air, deharsh});
		return true;
	}

	return false;
}

}  // namespace XER::Engine::Audio::FX::Customs
