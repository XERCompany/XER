#include "fx_customs_spatial_effects.h"

#include "audio/fx_customs/fx_customs_effect_utils.h"

namespace XER::Audio::FX::Customs {

bool BuildFxCustomSpatialEffectPackage(const std::string& normalized_name, CustomEffectPackage* package_out) {
	if (package_out == nullptr) {
		return false;
	}

	if (normalized_name == "dimensionchorus") {
		CustomEffectNode slow = MakeFxNode("chorus", "dimension_chorus_slow_bucket", 0u, 0.34f);
		SetFxParam(&slow, "rate_hz", 0.24f);
		SetFxParam(&slow, "depth_samples", 26.0f);
		CustomEffectNode wide = MakeFxNode("chorus", "dimension_chorus_wide_bucket", 1u, 0.30f);
		SetFxParam(&wide, "rate_hz", 0.41f);
		SetFxParam(&wide, "depth_samples", 18.0f);
		CustomEffectNode micro = MakeFxNode("delay", "dimension_chorus_micro_shift", 2u, 0.16f);
		SetFxParam(&micro, "delay_samples", 84.0f);
		SetFxParam(&micro, "feedback", 0.08f);
		CustomEffectNode tone = MakeFxNode("parametric_eq", "dimension_chorus_silk", 3u, 0.42f);
		SetFxParam(&tone, "frequency_hz", 7800.0f);
		SetFxParam(&tone, "q", 0.68f);
		SetFxParam(&tone, "gain_db", 1.4f);
		*package_out = MakeFxPresetPackage("DimensionChorus", {slow, wide, micro, tone});
		return true;
	}

	if (normalized_name == "spaceecho") {
		CustomEffectNode preamp = MakeFxNode("tube", "space_echo_tape_preamp", 0u, 0.34f);
		SetFxParam(&preamp, "drive", 2.15f);
		SetFxParam(&preamp, "output_gain", 0.86f);
		CustomEffectNode slap = MakeFxNode("delay", "space_echo_head_one", 1u, 0.28f);
		SetFxParam(&slap, "delay_samples", 640.0f);
		SetFxParam(&slap, "feedback", 0.32f);
		CustomEffectNode echo = MakeFxNode("delay", "space_echo_head_three", 2u, 0.34f);
		SetFxParam(&echo, "delay_samples", 1420.0f);
		SetFxParam(&echo, "feedback", 0.42f);
		CustomEffectNode darken = MakeFxNode("parametric_eq", "space_echo_tape_loss", 3u, 0.64f);
		SetFxParam(&darken, "frequency_hz", 6900.0f);
		SetFxParam(&darken, "q", 0.62f);
		SetFxParam(&darken, "gain_db", -2.6f);
		CustomEffectNode room = MakeFxNode("reverb_algorithmic", "space_echo_spring_room", 4u, 0.22f);
		SetFxParam(&room, "room_size", 0.62f);
		SetFxParam(&room, "damping", 0.46f);
		*package_out = MakeFxPresetPackage("SpaceEcho", {preamp, slap, echo, darken, room});
		return true;
	}

	if (normalized_name == "shimmerbloom") {
		CustomEffectNode pitch = MakeFxNode("pitch_shift", "shimmer_bloom_octave_hint", 0u, 0.24f);
		SetFxParam(&pitch, "pitch_ratio", 1.498307f);
		CustomEffectNode delay = MakeFxNode("delay", "shimmer_bloom_predelay", 1u, 0.22f);
		SetFxParam(&delay, "delay_samples", 760.0f);
		SetFxParam(&delay, "feedback", 0.24f);
		CustomEffectNode verb = MakeFxNode("reverb_algorithmic", "shimmer_bloom_large_hall", 2u, 0.56f);
		SetFxParam(&verb, "room_size", 0.92f);
		SetFxParam(&verb, "damping", 0.22f);
		CustomEffectNode air = MakeFxNode("parametric_eq", "shimmer_bloom_air", 3u, 0.54f);
		SetFxParam(&air, "frequency_hz", 12600.0f);
		SetFxParam(&air, "q", 0.46f);
		SetFxParam(&air, "gain_db", 3.0f);
		CustomEffectNode limiter = MakeFxNode("limiter", "shimmer_bloom_limiter", 4u, 1.0f);
		SetFxParam(&limiter, "ceiling_db", -1.1f);
		*package_out = MakeFxPresetPackage("ShimmerBloom", {pitch, delay, verb, air, limiter});
		return true;
	}

	if (normalized_name == "psychowidener") {
		CustomEffectNode chorus = MakeFxNode("chorus", "psycho_widener_modulated_side", 0u, 0.28f);
		SetFxParam(&chorus, "rate_hz", 0.38f);
		SetFxParam(&chorus, "depth_samples", 20.0f);
		CustomEffectNode haas = MakeFxNode("delay", "psycho_widener_haas", 1u, 0.18f);
		SetFxParam(&haas, "delay_samples", 118.0f);
		SetFxParam(&haas, "feedback", 0.06f);
		CustomEffectNode phaser = MakeFxNode("phaser", "psycho_widener_phase_motion", 2u, 0.12f);
		SetFxParam(&phaser, "rate_hz", 0.09f);
		SetFxParam(&phaser, "depth", 0.34f);
		SetFxParam(&phaser, "feedback", 0.10f);
		CustomEffectNode center = MakeFxNode("parametric_eq", "psycho_widener_center_safety", 3u, 0.50f);
		SetFxParam(&center, "frequency_hz", 180.0f);
		SetFxParam(&center, "q", 0.80f);
		SetFxParam(&center, "gain_db", -0.8f);
		CustomEffectNode limiter = MakeFxNode("limiter", "psycho_widener_limiter", 4u, 1.0f);
		SetFxParam(&limiter, "ceiling_db", -1.0f);
		*package_out = MakeFxPresetPackage("PsychoWidener", {chorus, haas, phaser, center, limiter});
		return true;
	}

	return false;
}

}  // namespace XER::Audio::FX::Customs
