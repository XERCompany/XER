#include "fx_customs_vintage_effects.h"

#include "audio/fx_customs/fx_customs_effect_utils.h"

namespace XER::Audio::FX::Customs {

bool BuildFxCustomVintageEffectPackage(const std::string& normalized_name, CustomEffectPackage* package_out) {
	if (package_out == nullptr) {
		return false;
	}

	if (normalized_name == "tapemachine") {
		CustomEffectNode head_bump = MakeFxNode("parametric_eq", "tape_machine_head_bump", 0u, 0.76f);
		SetFxParam(&head_bump, "frequency_hz", 72.0f);
		SetFxParam(&head_bump, "q", 0.64f);
		SetFxParam(&head_bump, "gain_db", 2.4f);
		CustomEffectNode preamp = MakeFxNode("tube", "tape_machine_record_amp", 1u, 0.38f);
		SetFxParam(&preamp, "drive", 2.05f);
		SetFxParam(&preamp, "output_gain", 0.88f);
		CustomEffectNode flutter = MakeFxNode("flanger", "tape_machine_wow_flutter", 2u, 0.16f);
		SetFxParam(&flutter, "rate_hz", 0.13f);
		SetFxParam(&flutter, "depth_samples", 5.0f);
		SetFxParam(&flutter, "feedback", 0.08f);
		CustomEffectNode top_loss = MakeFxNode("parametric_eq", "tape_machine_head_loss", 3u, 0.58f);
		SetFxParam(&top_loss, "frequency_hz", 11800.0f);
		SetFxParam(&top_loss, "q", 0.54f);
		SetFxParam(&top_loss, "gain_db", -1.7f);
		CustomEffectNode glue = MakeFxNode("compressor", "tape_machine_soft_knee", 4u, 0.42f);
		SetFxParam(&glue, "threshold_db", -16.0f);
		SetFxParam(&glue, "ratio", 1.8f);
		*package_out = MakeFxPresetPackage("TapeMachine", {head_bump, preamp, flutter, top_loss, glue});
		return true;
	}

	if (normalized_name == "vinylmaster") {
		CustomEffectNode rumble_control = MakeFxNode("parametric_eq", "vinyl_master_rumble_control", 0u, 0.72f);
		SetFxParam(&rumble_control, "frequency_hz", 42.0f);
		SetFxParam(&rumble_control, "q", 0.80f);
		SetFxParam(&rumble_control, "gain_db", -1.8f);
		CustomEffectNode mid = MakeFxNode("parametric_eq", "vinyl_master_mid_forward", 1u, 0.64f);
		SetFxParam(&mid, "frequency_hz", 1250.0f);
		SetFxParam(&mid, "q", 0.92f);
		SetFxParam(&mid, "gain_db", 1.8f);
		CustomEffectNode cutter = MakeFxNode("tube", "vinyl_master_cutter_amp", 2u, 0.26f);
		SetFxParam(&cutter, "drive", 1.62f);
		SetFxParam(&cutter, "output_gain", 0.92f);
		CustomEffectNode converter = MakeFxNode("bitcrush", "vinyl_master_converter_texture", 3u, 0.12f);
		SetFxParam(&converter, "bit_depth", 14.0f);
		SetFxParam(&converter, "downsample_factor", 1.0f);
		CustomEffectNode limiter = MakeFxNode("limiter", "vinyl_master_limiter", 4u, 1.0f);
		SetFxParam(&limiter, "ceiling_db", -1.2f);
		*package_out = MakeFxPresetPackage("VinylMaster", {rumble_control, mid, cutter, converter, limiter});
		return true;
	}

	if (normalized_name == "springtank") {
		CustomEffectNode drive = MakeFxNode("tube", "spring_tank_driver", 0u, 0.30f);
		SetFxParam(&drive, "drive", 1.90f);
		SetFxParam(&drive, "output_gain", 0.88f);
		CustomEffectNode splash = MakeFxNode("delay", "spring_tank_initial_splash", 1u, 0.18f);
		SetFxParam(&splash, "delay_samples", 420.0f);
		SetFxParam(&splash, "feedback", 0.18f);
		CustomEffectNode spring = MakeFxNode("reverb_algorithmic", "spring_tank_coils", 2u, 0.42f);
		SetFxParam(&spring, "room_size", 0.58f);
		SetFxParam(&spring, "damping", 0.62f);
		CustomEffectNode resonance = MakeFxNode("phaser", "spring_tank_metal_resonance", 3u, 0.20f);
		SetFxParam(&resonance, "rate_hz", 0.12f);
		SetFxParam(&resonance, "depth", 0.42f);
		SetFxParam(&resonance, "feedback", 0.24f);
		CustomEffectNode tone = MakeFxNode("parametric_eq", "spring_tank_bright_drip", 4u, 0.58f);
		SetFxParam(&tone, "frequency_hz", 3800.0f);
		SetFxParam(&tone, "q", 0.92f);
		SetFxParam(&tone, "gain_db", 2.4f);
		*package_out = MakeFxPresetPackage("SpringTank", {drive, splash, spring, resonance, tone});
		return true;
	}

	return false;
}

}  // namespace XER::Audio::FX::Customs
