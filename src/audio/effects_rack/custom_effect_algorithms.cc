#include "custom_effect_algorithms.h"

#include "custom_effect_eq_lite.h"

namespace Engine::Audio::FX {

std::vector<std::string> ListCustomEffectAlgorithms() {
	return {
		"passthrough",
		"compressor",
		"expander",
		"gate",
		"chorus",
		"flanger",
		"phaser",
		"parametric_eq",
		"limiter",
		"bitcrush",
		"tube",
		"reverb_algorithmic",
		"pitch_shift",
		"auto_tune_correct_pitch",
		"delay",
		"clap_plugin",
	};
}

CustomEffectNode CreateDefaultCustomEffectNode(const std::string& algorithm_name) {
	CustomEffectAlgorithm algorithm = CustomEffectAlgorithm::kUnknown;
	ParseCustomEffectAlgorithm(algorithm_name, &algorithm);
	CustomEffectNode node;
	node.label = algorithm_name;
	node.backend = CustomEffectBackend::kBuiltinAlgorithm;
	node.algorithm = algorithm;
	switch (algorithm) {
	case CustomEffectAlgorithm::kPassthrough:
		node.mix_curve.push_back(CustomEffectCurvePoint{0u, 1.0f});
		break;
	case CustomEffectAlgorithm::kCompressor:
		node.mix_curve.push_back(CustomEffectCurvePoint{0u, 0.85f});
		node.parameters.push_back(CustomEffectParameter{"threshold_db", -16.0f});
		node.parameters.push_back(CustomEffectParameter{"ratio", 3.0f});
		break;
	case CustomEffectAlgorithm::kExpander:
		node.mix_curve.push_back(CustomEffectCurvePoint{0u, 1.0f});
		node.parameters.push_back(CustomEffectParameter{"threshold_db", -36.0f});
		node.parameters.push_back(CustomEffectParameter{"ratio", 2.5f});
		node.parameters.push_back(CustomEffectParameter{"range_db", 18.0f});
		break;
	case CustomEffectAlgorithm::kGate:
		node.mix_curve.push_back(CustomEffectCurvePoint{0u, 1.0f});
		node.parameters.push_back(CustomEffectParameter{"threshold_db", -42.0f});
		node.parameters.push_back(CustomEffectParameter{"hold_samples", 256.0f});
		break;
	case CustomEffectAlgorithm::kChorus:
		node.mix_curve.push_back(CustomEffectCurvePoint{0u, 0.35f});
		node.parameters.push_back(CustomEffectParameter{"rate_hz", 0.8f});
		node.parameters.push_back(CustomEffectParameter{"depth_samples", 14.0f});
		break;
	case CustomEffectAlgorithm::kFlanger:
		node.mix_curve.push_back(CustomEffectCurvePoint{0u, 0.40f});
		node.parameters.push_back(CustomEffectParameter{"rate_hz", 0.35f});
		node.parameters.push_back(CustomEffectParameter{"depth_samples", 10.0f});
		node.parameters.push_back(CustomEffectParameter{"feedback", 0.25f});
		break;
	case CustomEffectAlgorithm::kPhaser:
		node.mix_curve.push_back(CustomEffectCurvePoint{0u, 0.40f});
		node.parameters.push_back(CustomEffectParameter{"rate_hz", 0.30f});
		node.parameters.push_back(CustomEffectParameter{"depth", 0.7f});
		node.parameters.push_back(CustomEffectParameter{"feedback", 0.2f});
		break;
	case CustomEffectAlgorithm::kParametricEq:
		return CreateCustomEffectLiteEqNode(4.5f);
	case CustomEffectAlgorithm::kLimiter:
		node.mix_curve.push_back(CustomEffectCurvePoint{0u, 1.0f});
		node.parameters.push_back(CustomEffectParameter{"ceiling_db", -1.0f});
		node.parameters.push_back(CustomEffectParameter{"release", 0.002f});
		break;
	case CustomEffectAlgorithm::kBitcrush:
		node.mix_curve.push_back(CustomEffectCurvePoint{0u, 0.75f});
		node.parameters.push_back(CustomEffectParameter{"bit_depth", 10.0f});
		node.parameters.push_back(CustomEffectParameter{"downsample_factor", 2.0f});
		break;
	case CustomEffectAlgorithm::kTube:
		node.mix_curve.push_back(CustomEffectCurvePoint{0u, 0.70f});
		node.parameters.push_back(CustomEffectParameter{"drive", 2.4f});
		node.parameters.push_back(CustomEffectParameter{"output_gain", 0.85f});
		break;
	case CustomEffectAlgorithm::kReverbAlgorithmic:
		node.mix_curve.push_back(CustomEffectCurvePoint{0u, 0.35f});
		node.parameters.push_back(CustomEffectParameter{"room_size", 0.6f});
		node.parameters.push_back(CustomEffectParameter{"damping", 0.3f});
		break;
	case CustomEffectAlgorithm::kPitchShift:
		node.mix_curve.push_back(CustomEffectCurvePoint{0u, 1.0f});
		node.parameters.push_back(CustomEffectParameter{"pitch_ratio", 1.0f});
		break;
	case CustomEffectAlgorithm::kAutoTuneCorrectPitch:
		node.mix_curve.push_back(CustomEffectCurvePoint{0u, 1.0f});
		node.parameters.push_back(CustomEffectParameter{"correction_amount", 0.88f});
		node.parameters.push_back(CustomEffectParameter{"correction_speed", 0.18f});
		node.parameters.push_back(CustomEffectParameter{"retune_speed_ms", 18.0f});
		node.parameters.push_back(CustomEffectParameter{"humanize", 0.12f});
		node.parameters.push_back(CustomEffectParameter{"scale_root", 0.0f});
		node.parameters.push_back(CustomEffectParameter{"scale_mode", 0.0f});
		node.parameters.push_back(CustomEffectParameter{"presence_db", 1.6f});
		node.parameters.push_back(CustomEffectParameter{"wet_mix", 0.92f});
		break;
	case CustomEffectAlgorithm::kDelay:
		node.mix_curve.push_back(CustomEffectCurvePoint{0u, 0.45f});
		node.parameters.push_back(CustomEffectParameter{"delay_samples", 640.0f});
		node.parameters.push_back(CustomEffectParameter{"feedback", 0.35f});
		break;
	case CustomEffectAlgorithm::kPlugin:
		node.label = "clap_plugin";
		node.backend = CustomEffectBackend::kClapPlugin;
		node.plugin_reference = "builtin://gain";
		node.parameters.push_back(CustomEffectParameter{"gain_db", 3.0f});
		node.mix_curve.push_back(CustomEffectCurvePoint{0u, 1.0f});
		break;
	case CustomEffectAlgorithm::kUnknown:
		break;
	}
	return node;
}

std::vector<std::string> ListCustomEffectPresets() {
	return {
		"BassBoost",
		"EQ",
		"High",
		"AIRHigh",
		"CurveAIR",
		"DeEsser",
		"Compressor",
		"Limiter",
		"AutoTune",
		"AutoTuneCorrectPitch",
		"PitchShifter",
		"PitchKey",
		"AutoPitch",
		"Stereo Converter",
		"VHS",
		"RetroVHS",
		"AnalogWarmth",
		"TapeEcho",
		"PlateReverb",
		"DreamChorus",
		"DubDelay",
		"LoFiSampler",
		"VocalPresence",
		"MasterGlue",
		"NeonPhaser",
		"PultecLowEnd",
		"SSLBusComp",
		"TransientPunch",
		"ExciterAir",
		"LA2AVocal",
		"RadioAnnouncer",
		"SilkyDeEsser",
		"TapeMachine",
		"VinylMaster",
		"SpringTank",
		"DimensionChorus",
		"SpaceEcho",
		"ShimmerBloom",
		"PsychoWidener",
		"SuperReverb",
		"RoomReverb",
		"8DReverbStereo",
		"StudioReverb",
		"EQReverb",
		"DelayReverb",
		"Delay",
		"CLAPPlugin",
		"AIrVoice",
	};
}

}  // namespace Engine::Audio::FX
