#include "fx_customs_vocal_effects.h"

#include "audio/fx_customs/fx_customs_effect_utils.h"

namespace XER::Audio::FX::Customs {

bool BuildFxCustomVocalEffectPackage(const std::string& normalized_name, CustomEffectPackage* package_out) {
	if (package_out == nullptr) {
		return false;
	}

	if (normalized_name == "la2avocal") {
		CustomEffectNode gate = MakeFxNode("gate", "la2a_vocal_room_gate", 0u, 0.88f);
		SetFxParam(&gate, "threshold_db", -52.0f);
		SetFxParam(&gate, "hold_samples", 448.0f);
		CustomEffectNode leveler = MakeFxNode("compressor", "la2a_vocal_optical_leveler", 1u, 0.82f);
		SetFxParam(&leveler, "threshold_db", -24.0f);
		SetFxParam(&leveler, "ratio", 3.1f);
		CustomEffectNode body = MakeFxNode("parametric_eq", "la2a_vocal_chest", 2u, 0.54f);
		SetFxParam(&body, "frequency_hz", 210.0f);
		SetFxParam(&body, "q", 0.82f);
		SetFxParam(&body, "gain_db", 1.4f);
		CustomEffectNode presence = MakeFxNode("parametric_eq", "la2a_vocal_presence", 3u, 0.76f);
		SetFxParam(&presence, "frequency_hz", 4200.0f);
		SetFxParam(&presence, "q", 0.72f);
		SetFxParam(&presence, "gain_db", 2.4f);
		CustomEffectNode air = MakeFxNode("parametric_eq", "la2a_vocal_air", 4u, 0.66f);
		SetFxParam(&air, "frequency_hz", 11800.0f);
		SetFxParam(&air, "q", 0.48f);
		SetFxParam(&air, "gain_db", 2.8f);
		CustomEffectNode limiter = MakeFxNode("limiter", "la2a_vocal_peak_guard", 5u, 1.0f);
		SetFxParam(&limiter, "ceiling_db", -1.0f);
		*package_out = MakeFxPresetPackage("LA2AVocal", {gate, leveler, body, presence, air, limiter});
		return true;
	}

	if (normalized_name == "radioannouncer") {
		CustomEffectNode gate = MakeFxNode("gate", "radio_announcer_noise_floor", 0u, 1.0f);
		SetFxParam(&gate, "threshold_db", -46.0f);
		SetFxParam(&gate, "hold_samples", 512.0f);
		CustomEffectNode comp = MakeFxNode("compressor", "radio_announcer_dense_comp", 1u, 0.92f);
		SetFxParam(&comp, "threshold_db", -18.0f);
		SetFxParam(&comp, "ratio", 5.2f);
		CustomEffectNode low = MakeFxNode("parametric_eq", "radio_announcer_low_authority", 2u, 0.74f);
		SetFxParam(&low, "frequency_hz", 145.0f);
		SetFxParam(&low, "q", 0.70f);
		SetFxParam(&low, "gain_db", 2.6f);
		CustomEffectNode intelligibility = MakeFxNode("parametric_eq", "radio_announcer_intelligibility", 3u, 0.82f);
		SetFxParam(&intelligibility, "frequency_hz", 2800.0f);
		SetFxParam(&intelligibility, "q", 0.84f);
		SetFxParam(&intelligibility, "gain_db", 2.8f);
		CustomEffectNode tube = MakeFxNode("tube", "radio_announcer_transformer", 4u, 0.24f);
		SetFxParam(&tube, "drive", 1.78f);
		SetFxParam(&tube, "output_gain", 0.92f);
		CustomEffectNode limiter = MakeFxNode("limiter", "radio_announcer_limiter", 5u, 1.0f);
		SetFxParam(&limiter, "ceiling_db", -0.8f);
		*package_out = MakeFxPresetPackage("RadioAnnouncer", {gate, comp, low, intelligibility, tube, limiter});
		return true;
	}

	if (normalized_name == "silkydeesser") {
		CustomEffectNode detector = MakeFxNode("parametric_eq", "silky_deesser_detector_cut", 0u, 0.86f);
		SetFxParam(&detector, "frequency_hz", 6800.0f);
		SetFxParam(&detector, "q", 2.8f);
		SetFxParam(&detector, "gain_db", -4.8f);
		CustomEffectNode soft = MakeFxNode("compressor", "silky_deesser_soft_control", 1u, 0.58f);
		SetFxParam(&soft, "threshold_db", -27.0f);
		SetFxParam(&soft, "ratio", 3.8f);
		CustomEffectNode air = MakeFxNode("parametric_eq", "silky_deesser_air_restore", 2u, 0.42f);
		SetFxParam(&air, "frequency_hz", 12600.0f);
		SetFxParam(&air, "q", 0.52f);
		SetFxParam(&air, "gain_db", 1.6f);
		*package_out = MakeFxPresetPackage("SilkyDeEsser", {detector, soft, air});
		return true;
	}

	return false;
}

}  // namespace XER::Audio::FX::Customs
