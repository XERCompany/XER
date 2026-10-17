#include "fx_customs_effect_builder.h"

#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

#include "audio/effects_rack/custom_effect_algorithms.h"
#include "audio/effects_rack/custom_effect_fxdata.h"
#include "audio/fx_customs/fx_customs_mastering_effects.h"
#include "audio/fx_customs/fx_customs_spatial_effects.h"
#include "audio/fx_customs/fx_customs_vintage_effects.h"
#include "audio/fx_customs/fx_customs_vocal_effects.h"

namespace Engine::Audio::FX::Customs {

namespace {

CustomEffectNode MakeNode(const std::string& algorithm_name, const std::string& label, uint32_t stage_index, float mix = 1.0f) {
	CustomEffectNode node = CreateDefaultCustomEffectNode(algorithm_name);
	node.label = label;
	node.stage_index = stage_index;
	node.mix_curve = {CustomEffectCurvePoint{0u, mix}};
	return node;
}

void SetParam(CustomEffectNode* node, const std::string& name, float value) {
	SetCustomEffectParameterValue(&node->parameters, name, value);
}

CustomEffectPackage MakePresetPackage(const std::string& label, std::vector<CustomEffectNode> nodes) {
	CustomEffectPackage package;
	package.label = label;
	package.render_config.block_size = 1024u;
	package.render_config.enable_multicore_render = true;
	package.nodes = std::move(nodes);
	return package;
}

}  // namespace

std::string NormalizeFxCustomPresetName(std::string name) {
	std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});
	name.erase(std::remove(name.begin(), name.end(), ' '), name.end());
	return name;
}

bool BuildFxCustomEffectPackage(
	const std::string& effect_name,
	const std::string& clap_plugin_reference,
	CustomEffectPackage* package_out) {
	if (package_out == nullptr) {
		return false;
	}

	const std::string normalized = NormalizeFxCustomPresetName(effect_name);
	if (BuildFxCustomMasteringEffectPackage(normalized, package_out)
		|| BuildFxCustomVocalEffectPackage(normalized, package_out)
		|| BuildFxCustomVintageEffectPackage(normalized, package_out)
		|| BuildFxCustomSpatialEffectPackage(normalized, package_out)) {
		return true;
	}

	if (normalized == "bassboost") {
		CustomEffectNode sub = MakeNode("parametric_eq", "bass_eq_sub_front", 0u, 1.0f);
		SetParam(&sub, "frequency_hz", 48.0f);
		SetParam(&sub, "q", 0.85f);
		SetParam(&sub, "gain_db", 5.5f);
		CustomEffectNode thump = MakeNode("parametric_eq", "bass_eq_thump_front", 1u, 1.0f);
		SetParam(&thump, "frequency_hz", 82.0f);
		SetParam(&thump, "q", 0.78f);
		SetParam(&thump, "gain_db", 8.0f);
		CustomEffectNode body = MakeNode("parametric_eq", "bass_eq_body_front", 2u, 0.92f);
		SetParam(&body, "frequency_hz", 135.0f);
		SetParam(&body, "q", 0.90f);
		SetParam(&body, "gain_db", 6.0f);
		CustomEffectNode warmth = MakeNode("parametric_eq", "bass_eq_warmth_front", 3u, 0.55f);
		SetParam(&warmth, "frequency_hz", 205.0f);
		SetParam(&warmth, "q", 1.05f);
		SetParam(&warmth, "gain_db", 2.5f);
		CustomEffectNode tube = MakeNode("tube", "bass_harmonic_guard", 4u, 0.18f);
		SetParam(&tube, "drive", 1.45f);
		SetParam(&tube, "output_gain", 0.92f);
		CustomEffectNode limiter = MakeNode("limiter", "bass_limiter", 5u, 1.0f);
		SetParam(&limiter, "ceiling_db", -0.8f);
		*package_out = MakePresetPackage("BassBoost", {sub, thump, body, warmth, tube, limiter});
		return true;
	}
	if (normalized == "eq") {
		CustomEffectNode low = MakeNode("parametric_eq", "eq_body", 0u, 1.0f);
		SetParam(&low, "frequency_hz", 220.0f);
		SetParam(&low, "q", 0.9f);
		SetParam(&low, "gain_db", 3.0f);
		CustomEffectNode high = MakeNode("parametric_eq", "eq_presence", 1u, 1.0f);
		SetParam(&high, "frequency_hz", 4200.0f);
		SetParam(&high, "q", 0.7f);
		SetParam(&high, "gain_db", 2.5f);
		*package_out = MakePresetPackage("EQ", {low, high});
		return true;
	}
	if (normalized == "high") {
		CustomEffectNode high = MakeNode("parametric_eq", "high_boost", 0u, 1.0f);
		SetParam(&high, "frequency_hz", 8200.0f);
		SetParam(&high, "q", 0.8f);
		SetParam(&high, "gain_db", 4.5f);
		*package_out = MakePresetPackage("High", {high});
		return true;
	}
	if (normalized == "airhigh") {
		CustomEffectNode air = MakeNode("parametric_eq", "air_high", 0u, 1.0f);
		SetParam(&air, "frequency_hz", 12000.0f);
		SetParam(&air, "q", 0.6f);
		SetParam(&air, "gain_db", 6.5f);
		*package_out = MakePresetPackage("AIRHigh", {air});
		return true;
	}
	if (normalized == "curveair") {
		CustomEffectNode sheen = MakeNode("parametric_eq", "curve_air_sheen", 0u, 1.0f);
		SetParam(&sheen, "frequency_hz", 6500.0f);
		SetParam(&sheen, "q", 0.7f);
		SetParam(&sheen, "gain_db", 2.5f);
		CustomEffectNode air = MakeNode("parametric_eq", "curve_air_top", 1u, 1.0f);
		SetParam(&air, "frequency_hz", 14500.0f);
		SetParam(&air, "q", 0.5f);
		SetParam(&air, "gain_db", 6.0f);
		*package_out = MakePresetPackage("CurveAIR", {sheen, air});
		return true;
	}
	if (normalized == "deesser") {
		CustomEffectNode cut = MakeNode("parametric_eq", "deesser_cut", 0u, 1.0f);
		SetParam(&cut, "frequency_hz", 6800.0f);
		SetParam(&cut, "q", 3.0f);
		SetParam(&cut, "gain_db", -6.0f);
		CustomEffectNode comp = MakeNode("compressor", "deesser_control", 1u, 1.0f);
		SetParam(&comp, "threshold_db", -28.0f);
		SetParam(&comp, "ratio", 7.0f);
		*package_out = MakePresetPackage("DeEsser", {cut, comp});
		return true;
	}
	if (normalized == "compressor") {
		CustomEffectNode comp = MakeNode("compressor", "compressor_main", 0u, 1.0f);
		SetParam(&comp, "threshold_db", -18.0f);
		SetParam(&comp, "ratio", 4.0f);
		*package_out = MakePresetPackage("Compressor", {comp});
		return true;
	}
	if (normalized == "limiter") {
		CustomEffectNode limiter = MakeNode("limiter", "limiter_main", 0u, 1.0f);
		SetParam(&limiter, "ceiling_db", -0.5f);
		SetParam(&limiter, "release", 0.002f);
		*package_out = MakePresetPackage("Limiter", {limiter});
		return true;
	}
	if (normalized == "autotune") {
		CustomEffectNode pitch = MakeNode("auto_tune_correct_pitch", "autotune_realtime_pitch_corrector", 0u, 1.0f);
		SetParam(&pitch, "correction_amount", 1.0f);
		SetParam(&pitch, "correction_speed", 0.92f);
		SetParam(&pitch, "retune_speed_ms", 0.0f);
		SetParam(&pitch, "humanize", 0.0f);
		SetParam(&pitch, "scale_root", 0.0f);
		SetParam(&pitch, "scale_mode", 0.0f);
		SetParam(&pitch, "presence_db", 1.2f);
		SetParam(&pitch, "wet_mix", 1.0f);
		CustomEffectNode limiter = MakeNode("limiter", "autotune_limiter", 1u, 1.0f);
		*package_out = MakePresetPackage("AutoTune", {pitch, limiter});
		return true;
	}
	if (normalized == "autotunecorrectpitch" || normalized == "autotunecorrect" || normalized == "correctpitch") {
		CustomEffectNode correct = MakeNode("auto_tune_correct_pitch", "autotune_correct_pitch_process", 0u, 1.0f);
		SetParam(&correct, "correction_amount", 0.88f);
		SetParam(&correct, "correction_speed", 0.18f);
		SetParam(&correct, "retune_speed_ms", 18.0f);
		SetParam(&correct, "humanize", 0.12f);
		SetParam(&correct, "scale_root", 0.0f);
		SetParam(&correct, "scale_mode", 0.0f);
		SetParam(&correct, "presence_db", 1.6f);
		SetParam(&correct, "wet_mix", 0.92f);
		CustomEffectNode limiter = MakeNode("limiter", "autotune_correct_pitch_limiter", 1u, 1.0f);
		SetParam(&limiter, "ceiling_db", -0.8f);
		*package_out = MakePresetPackage("AutoTuneCorrectPitch", {correct, limiter});
		return true;
	}
	if (normalized == "pitchkey" || normalized == "pitchshifter") {
		CustomEffectNode pitch = MakeNode("pitch_shift", "pitchkey_shift", 0u, 1.0f);
		SetParam(&pitch, "pitch_ratio", 1.122462f);
		*package_out = MakePresetPackage(normalized == "pitchshifter" ? "PitchShifter" : "PitchKey", {pitch});
		return true;
	}
	if (normalized == "autopitch") {
		CustomEffectNode pitch = MakeNode("pitch_shift", "autopitch_shift", 0u, 1.0f);
		SetParam(&pitch, "pitch_ratio", 0.943874f);
		CustomEffectNode tube = MakeNode("tube", "autopitch_tone", 1u, 0.35f);
		*package_out = MakePresetPackage("AutoPitch", {pitch, tube});
		return true;
	}
	if (normalized == "stereoconverter") {
		CustomEffectNode chorus = MakeNode("chorus", "stereo_width_chorus", 0u, 0.30f);
		SetParam(&chorus, "rate_hz", 0.7f);
		SetParam(&chorus, "depth_samples", 22.0f);
		CustomEffectNode delay = MakeNode("delay", "stereo_width_delay", 1u, 0.20f);
		SetParam(&delay, "delay_samples", 96.0f);
		SetParam(&delay, "feedback", 0.12f);
		*package_out = MakePresetPackage("Stereo Converter", {chorus, delay});
		return true;
	}
	if (normalized == "vhs") {
		CustomEffectNode tube = MakeNode("tube", "vhs_tube", 0u, 0.65f);
		SetParam(&tube, "drive", 2.8f);
		CustomEffectNode crush = MakeNode("bitcrush", "vhs_crush", 1u, 0.55f);
		SetParam(&crush, "bit_depth", 9.0f);
		SetParam(&crush, "downsample_factor", 3.0f);
		CustomEffectNode delay = MakeNode("delay", "vhs_delay", 2u, 0.25f);
		SetParam(&delay, "delay_samples", 720.0f);
		SetParam(&delay, "feedback", 0.18f);
		*package_out = MakePresetPackage("VHS", {tube, crush, delay});
		return true;
	}
	if (normalized == "retrovhs") {
		CustomEffectNode crush = MakeNode("bitcrush", "retro_vhs_crush", 0u, 0.65f);
		SetParam(&crush, "bit_depth", 8.0f);
		SetParam(&crush, "downsample_factor", 4.0f);
		CustomEffectNode flanger = MakeNode("flanger", "retro_vhs_flanger", 1u, 0.30f);
		SetParam(&flanger, "rate_hz", 0.18f);
		SetParam(&flanger, "depth_samples", 14.0f);
		SetParam(&flanger, "feedback", 0.28f);
		CustomEffectNode reverb = MakeNode("reverb_algorithmic", "retro_vhs_room", 2u, 0.25f);
		SetParam(&reverb, "room_size", 0.55f);
		SetParam(&reverb, "damping", 0.35f);
		*package_out = MakePresetPackage("RetroVHS", {crush, flanger, reverb});
		return true;
	}
	if (normalized == "analogwarmth") {
		CustomEffectNode body = MakeNode("parametric_eq", "analog_warmth_body", 0u, 0.86f);
		SetParam(&body, "frequency_hz", 180.0f);
		SetParam(&body, "q", 0.78f);
		SetParam(&body, "gain_db", 2.2f);
		CustomEffectNode tube = MakeNode("tube", "analog_warmth_tube", 1u, 0.42f);
		SetParam(&tube, "drive", 1.85f);
		SetParam(&tube, "output_gain", 0.90f);
		CustomEffectNode glue = MakeNode("compressor", "analog_warmth_glue", 2u, 0.58f);
		SetParam(&glue, "threshold_db", -18.0f);
		SetParam(&glue, "ratio", 2.2f);
		CustomEffectNode limiter = MakeNode("limiter", "analog_warmth_limiter", 3u, 1.0f);
		SetParam(&limiter, "ceiling_db", -0.9f);
		*package_out = MakePresetPackage("AnalogWarmth", {body, tube, glue, limiter});
		return true;
	}
	if (normalized == "tapeecho") {
		CustomEffectNode tube = MakeNode("tube", "tape_echo_preamp", 0u, 0.34f);
		SetParam(&tube, "drive", 2.15f);
		SetParam(&tube, "output_gain", 0.86f);
		CustomEffectNode delay = MakeNode("delay", "tape_echo_delay", 1u, 0.42f);
		SetParam(&delay, "delay_samples", 1180.0f);
		SetParam(&delay, "feedback", 0.46f);
		CustomEffectNode darken = MakeNode("parametric_eq", "tape_echo_head_loss", 2u, 0.70f);
		SetParam(&darken, "frequency_hz", 7600.0f);
		SetParam(&darken, "q", 0.62f);
		SetParam(&darken, "gain_db", -2.8f);
		CustomEffectNode limiter = MakeNode("limiter", "tape_echo_limiter", 3u, 1.0f);
		SetParam(&limiter, "ceiling_db", -1.0f);
		*package_out = MakePresetPackage("TapeEcho", {tube, delay, darken, limiter});
		return true;
	}
	if (normalized == "platereverb") {
		CustomEffectNode predelay = MakeNode("delay", "plate_reverb_predelay", 0u, 0.12f);
		SetParam(&predelay, "delay_samples", 540.0f);
		SetParam(&predelay, "feedback", 0.10f);
		CustomEffectNode plate = MakeNode("reverb_algorithmic", "plate_reverb_tail", 1u, 0.46f);
		SetParam(&plate, "room_size", 0.82f);
		SetParam(&plate, "damping", 0.20f);
		CustomEffectNode shine = MakeNode("parametric_eq", "plate_reverb_shimmer", 2u, 0.54f);
		SetParam(&shine, "frequency_hz", 9800.0f);
		SetParam(&shine, "q", 0.60f);
		SetParam(&shine, "gain_db", 2.8f);
		CustomEffectNode limiter = MakeNode("limiter", "plate_reverb_limiter", 3u, 1.0f);
		SetParam(&limiter, "ceiling_db", -1.0f);
		*package_out = MakePresetPackage("PlateReverb", {predelay, plate, shine, limiter});
		return true;
	}
	if (normalized == "dreamchorus") {
		CustomEffectNode chorus = MakeNode("chorus", "dream_chorus_wide", 0u, 0.44f);
		SetParam(&chorus, "rate_hz", 0.32f);
		SetParam(&chorus, "depth_samples", 34.0f);
		CustomEffectNode phaser = MakeNode("phaser", "dream_chorus_motion", 1u, 0.22f);
		SetParam(&phaser, "rate_hz", 0.16f);
		SetParam(&phaser, "depth", 0.62f);
		SetParam(&phaser, "feedback", 0.12f);
		CustomEffectNode reverb = MakeNode("reverb_algorithmic", "dream_chorus_space", 2u, 0.28f);
		SetParam(&reverb, "room_size", 0.68f);
		SetParam(&reverb, "damping", 0.32f);
		*package_out = MakePresetPackage("DreamChorus", {chorus, phaser, reverb});
		return true;
	}
	if (normalized == "dubdelay") {
		CustomEffectNode delay = MakeNode("delay", "dub_delay_feedback", 0u, 0.52f);
		SetParam(&delay, "delay_samples", 1460.0f);
		SetParam(&delay, "feedback", 0.58f);
		CustomEffectNode tube = MakeNode("tube", "dub_delay_drive", 1u, 0.30f);
		SetParam(&tube, "drive", 2.35f);
		SetParam(&tube, "output_gain", 0.84f);
		CustomEffectNode reverb = MakeNode("reverb_algorithmic", "dub_delay_spring_room", 2u, 0.30f);
		SetParam(&reverb, "room_size", 0.66f);
		SetParam(&reverb, "damping", 0.48f);
		CustomEffectNode limiter = MakeNode("limiter", "dub_delay_limiter", 3u, 1.0f);
		SetParam(&limiter, "ceiling_db", -1.0f);
		*package_out = MakePresetPackage("DubDelay", {delay, tube, reverb, limiter});
		return true;
	}
	if (normalized == "lofisampler") {
		CustomEffectNode crush = MakeNode("bitcrush", "lofi_sampler_converter", 0u, 0.72f);
		SetParam(&crush, "bit_depth", 10.0f);
		SetParam(&crush, "downsample_factor", 3.0f);
		CustomEffectNode body = MakeNode("parametric_eq", "lofi_sampler_mid_bump", 1u, 0.78f);
		SetParam(&body, "frequency_hz", 1450.0f);
		SetParam(&body, "q", 0.92f);
		SetParam(&body, "gain_db", 2.4f);
		CustomEffectNode tube = MakeNode("tube", "lofi_sampler_soft_clip", 2u, 0.28f);
		SetParam(&tube, "drive", 1.95f);
		SetParam(&tube, "output_gain", 0.88f);
		CustomEffectNode limiter = MakeNode("limiter", "lofi_sampler_limiter", 3u, 1.0f);
		SetParam(&limiter, "ceiling_db", -1.2f);
		*package_out = MakePresetPackage("LoFiSampler", {crush, body, tube, limiter});
		return true;
	}
	if (normalized == "vocalpresence") {
		CustomEffectNode gate = MakeNode("gate", "vocal_presence_gate", 0u, 1.0f);
		SetParam(&gate, "threshold_db", -50.0f);
		SetParam(&gate, "hold_samples", 320.0f);
		CustomEffectNode comp = MakeNode("compressor", "vocal_presence_leveler", 1u, 0.84f);
		SetParam(&comp, "threshold_db", -22.0f);
		SetParam(&comp, "ratio", 3.6f);
		CustomEffectNode presence = MakeNode("parametric_eq", "vocal_presence_focus", 2u, 0.90f);
		SetParam(&presence, "frequency_hz", 3600.0f);
		SetParam(&presence, "q", 0.75f);
		SetParam(&presence, "gain_db", 3.2f);
		CustomEffectNode air = MakeNode("parametric_eq", "vocal_presence_air", 3u, 0.82f);
		SetParam(&air, "frequency_hz", 12200.0f);
		SetParam(&air, "q", 0.52f);
		SetParam(&air, "gain_db", 3.8f);
		CustomEffectNode limiter = MakeNode("limiter", "vocal_presence_limiter", 4u, 1.0f);
		SetParam(&limiter, "ceiling_db", -1.0f);
		*package_out = MakePresetPackage("VocalPresence", {gate, comp, presence, air, limiter});
		return true;
	}
	if (normalized == "masterglue") {
		CustomEffectNode comp = MakeNode("compressor", "master_glue_bus_comp", 0u, 0.62f);
		SetParam(&comp, "threshold_db", -14.0f);
		SetParam(&comp, "ratio", 2.0f);
		CustomEffectNode low = MakeNode("parametric_eq", "master_glue_low_shelf", 1u, 0.58f);
		SetParam(&low, "frequency_hz", 95.0f);
		SetParam(&low, "q", 0.72f);
		SetParam(&low, "gain_db", 1.6f);
		CustomEffectNode high = MakeNode("parametric_eq", "master_glue_air_shelf", 2u, 0.52f);
		SetParam(&high, "frequency_hz", 9800.0f);
		SetParam(&high, "q", 0.58f);
		SetParam(&high, "gain_db", 1.8f);
		CustomEffectNode limiter = MakeNode("limiter", "master_glue_limiter", 3u, 1.0f);
		SetParam(&limiter, "ceiling_db", -0.6f);
		*package_out = MakePresetPackage("MasterGlue", {comp, low, high, limiter});
		return true;
	}
	if (normalized == "neonphaser") {
		CustomEffectNode phaser = MakeNode("phaser", "neon_phaser_sweep", 0u, 0.48f);
		SetParam(&phaser, "rate_hz", 0.22f);
		SetParam(&phaser, "depth", 0.86f);
		SetParam(&phaser, "feedback", 0.34f);
		CustomEffectNode flanger = MakeNode("flanger", "neon_phaser_comb", 1u, 0.20f);
		SetParam(&flanger, "rate_hz", 0.18f);
		SetParam(&flanger, "depth_samples", 8.0f);
		SetParam(&flanger, "feedback", 0.18f);
		*package_out = MakePresetPackage("NeonPhaser", {phaser, flanger});
		return true;
	}
	if (normalized == "superreverb") {
		CustomEffectNode reverb = MakeNode("reverb_algorithmic", "super_reverb", 0u, 0.65f);
		SetParam(&reverb, "room_size", 0.95f);
		SetParam(&reverb, "damping", 0.45f);
		*package_out = MakePresetPackage("SuperReverb", {reverb});
		return true;
	}
	if (normalized == "roomreverb") {
		CustomEffectNode early = MakeNode("delay", "room_reverb_early_reflections", 0u, 0.14f);
		SetParam(&early, "delay_samples", 360.0f);
		SetParam(&early, "feedback", 0.16f);
		CustomEffectNode reverb = MakeNode("reverb_algorithmic", "room_reverb_physical_tail", 1u, 0.38f);
		SetParam(&reverb, "room_size", 0.54f);
		SetParam(&reverb, "damping", 0.36f);
		CustomEffectNode air = MakeNode("parametric_eq", "room_reverb_air_loss", 2u, 0.72f);
		SetParam(&air, "frequency_hz", 7200.0f);
		SetParam(&air, "q", 0.85f);
		SetParam(&air, "gain_db", -1.8f);
		*package_out = MakePresetPackage("RoomReverb", {early, reverb, air});
		return true;
	}
	if (normalized == "8dreverbstereo" || normalized == "eightdreverbstereo") {
		CustomEffectNode width = MakeNode("chorus", "8d_orbit_width", 0u, 0.32f);
		SetParam(&width, "rate_hz", 0.42f);
		SetParam(&width, "depth_samples", 28.0f);
		CustomEffectNode orbit = MakeNode("delay", "8d_orbit_micro_delay", 1u, 0.24f);
		SetParam(&orbit, "delay_samples", 118.0f);
		SetParam(&orbit, "feedback", 0.18f);
		CustomEffectNode reverb = MakeNode("reverb_algorithmic", "8d_orbit_room", 2u, 0.42f);
		SetParam(&reverb, "room_size", 0.72f);
		SetParam(&reverb, "damping", 0.42f);
		CustomEffectNode limiter = MakeNode("limiter", "8d_orbit_limiter", 3u, 1.0f);
		SetParam(&limiter, "ceiling_db", -0.9f);
		*package_out = MakePresetPackage("8DReverbStereo", {width, orbit, reverb, limiter});
		return true;
	}
	if (normalized == "studioreverb") {
		CustomEffectNode reverb = MakeNode("reverb_algorithmic", "studio_reverb", 0u, 0.35f);
		SetParam(&reverb, "room_size", 0.60f);
		SetParam(&reverb, "damping", 0.28f);
		*package_out = MakePresetPackage("StudioReverb", {reverb});
		return true;
	}
	if (normalized == "eqreverb") {
		CustomEffectNode eq = MakeNode("parametric_eq", "eq_reverb_eq", 0u, 1.0f);
		SetParam(&eq, "frequency_hz", 4200.0f);
		SetParam(&eq, "q", 0.7f);
		SetParam(&eq, "gain_db", 3.0f);
		CustomEffectNode reverb = MakeNode("reverb_algorithmic", "eq_reverb_room", 1u, 0.30f);
		SetParam(&reverb, "room_size", 0.58f);
		SetParam(&reverb, "damping", 0.27f);
		*package_out = MakePresetPackage("EQReverb", {eq, reverb});
		return true;
	}
	if (normalized == "delayreverb") {
		CustomEffectNode delay = MakeNode("delay", "delay_reverb_delay", 0u, 0.32f);
		SetParam(&delay, "delay_samples", 880.0f);
		SetParam(&delay, "feedback", 0.28f);
		CustomEffectNode reverb = MakeNode("reverb_algorithmic", "delay_reverb_room", 1u, 0.35f);
		SetParam(&reverb, "room_size", 0.72f);
		SetParam(&reverb, "damping", 0.34f);
		*package_out = MakePresetPackage("DelayReverb", {delay, reverb});
		return true;
	}
	if (normalized == "delay") {
		CustomEffectNode delay = MakeNode("delay", "delay_main", 0u, 0.45f);
		SetParam(&delay, "delay_samples", 960.0f);
		SetParam(&delay, "feedback", 0.35f);
		*package_out = MakePresetPackage("Delay", {delay});
		return true;
	}
	if (normalized == "clapplugin" || normalized == "clap" || normalized == "plugin") {
		CustomEffectNode plugin = MakeNode("clap_plugin", "clap_plugin", 0u, 1.0f);
		plugin.plugin_reference = clap_plugin_reference.empty() ? std::string("builtin://gain") : clap_plugin_reference;
		SetParam(&plugin, "gain_db", 0.0f);
		*package_out = MakePresetPackage("CLAPPlugin", {plugin});
		return true;
	}
	if (normalized == "airvoice") {
		CustomEffectNode gate = MakeNode("gate", "airvoice_gate", 0u, 1.0f);
		SetParam(&gate, "threshold_db", -48.0f);
		SetParam(&gate, "hold_samples", 384.0f);
		CustomEffectNode deesser = MakeNode("parametric_eq", "airvoice_deesser", 1u, 1.0f);
		SetParam(&deesser, "frequency_hz", 6500.0f);
		SetParam(&deesser, "q", 2.4f);
		SetParam(&deesser, "gain_db", -4.5f);
		CustomEffectNode comp = MakeNode("compressor", "airvoice_comp", 2u, 1.0f);
		SetParam(&comp, "threshold_db", -20.0f);
		SetParam(&comp, "ratio", 4.5f);
		CustomEffectNode air = MakeNode("parametric_eq", "airvoice_air", 3u, 1.0f);
		SetParam(&air, "frequency_hz", 11800.0f);
		SetParam(&air, "q", 0.55f);
		SetParam(&air, "gain_db", 5.5f);
		CustomEffectNode limiter = MakeNode("limiter", "airvoice_limiter", 4u, 1.0f);
		SetParam(&limiter, "ceiling_db", -1.0f);
		*package_out = MakePresetPackage("AIrVoice", {gate, deesser, comp, air, limiter});
		return true;
	}

	return false;
}

}  // namespace Engine::Audio::FX::Customs
