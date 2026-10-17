#include "fx_customs_presets.h"

#include <algorithm>

#include "audio/audio_core/audio_interleave_processor.h"
#include "audio/effects_rack/custom_effect.h"
#include "audio/effects_rack/custom_effect_algorithms.h"
#include "audio/effects_rack/custom_effect_core.h"
#include "audio/fx_customs/fx_customs_effect_builder.h"
#include "audio/fx_customs/fx_customs_spatial.h"

namespace Engine::Audio::FX::Customs {

namespace {

CustomEffectPackage BuildNamedPreset(const std::string& name, const std::string& clap_plugin_reference = "builtin://gain") {
	CustomEffectPackage package;
	if (BuildFxCustomEffectPackage(name, clap_plugin_reference, &package)) {
		return package;
	}
	return BuildNamedCustomEffectPackage(name, clap_plugin_reference);
}

}  // namespace

std::vector<std::string> ListFxCustomPresetNames() {
	return ListAvailableCustomEffects();
}

bool IsFxCustomPresetSupported(const std::string& name) {
	const std::string normalized = NormalizeFxCustomPresetName(name);
	const auto names = ListFxCustomPresetNames();
	return std::any_of(names.begin(), names.end(), [&](const std::string& candidate) {
		return NormalizeFxCustomPresetName(candidate) == normalized;
	});
}

CustomEffectPackage BuildFxCustomPreset(const std::string& name, const std::string& clap_plugin_reference) {
	return BuildNamedPreset(name, clap_plugin_reference);
}

bool RenderFxCustomPreset(
	const std::string& name,
	float sample_rate,
	const std::vector<float>& input,
	std::vector<float>* output,
	CustomEffectReport* report_out,
	const std::string& clap_plugin_reference,
	std::string* error_out) {
	return RunCustomEffectPackage(
		BuildFxCustomPreset(name, clap_plugin_reference),
		sample_rate,
		input,
		output,
		report_out,
		error_out);
}

bool RenderFxCustomPresetInterleaved(
	const std::string& name,
	float sample_rate,
	const std::vector<float>& input,
	int channels,
	std::vector<float>* output,
	CustomEffectReport* report_out,
	const std::string& clap_plugin_reference,
	std::string* error_out) {
	if (output == nullptr || channels <= 0 || input.empty() || (input.size() % static_cast<size_t>(channels)) != 0u) {
		if (error_out != nullptr) {
			*error_out = "fx custom interleaved render input is invalid";
		}
		return false;
	}

	const std::string normalized = NormalizeFxCustomPresetName(name);
	const size_t frame_count = input.size() / static_cast<size_t>(channels);
	std::vector<std::vector<float>> input_channels;
	if (!Engine::Audio::Core::AudioInterleaveProcessor::DeinterleavePlanar(
			input.data(),
			channels,
			frame_count,
			input_channels)) {
		if (error_out != nullptr) {
			*error_out = "failed to deinterleave fx custom input";
		}
		return false;
	}

	std::vector<std::vector<float>> output_channels(static_cast<size_t>(channels));
	CustomEffectReport aggregate_report;
	aggregate_report.label = name;
	aggregate_report.channel_count = static_cast<size_t>(channels);
	aggregate_report.worker_count_used = 1u;
	for (int channel_index = 0; channel_index < channels; ++channel_index) {
		CustomEffectPackage package = BuildFxCustomPreset(name, clap_plugin_reference);
		if (IsFxCustomSpatialPreset(normalized)) {
			ApplyFxCustomSpatialVariant(&package, normalized, channel_index, channels);
		}

		CustomEffectReport channel_report;
		if (!RunCustomEffectPackage(
				package,
				sample_rate,
				input_channels[static_cast<size_t>(channel_index)],
				&output_channels[static_cast<size_t>(channel_index)],
				&channel_report,
				error_out)) {
			return false;
		}
		aggregate_report.node_count += channel_report.node_count;
		aggregate_report.stage_count = std::max(aggregate_report.stage_count, channel_report.stage_count);
		aggregate_report.worker_count_used = std::max(aggregate_report.worker_count_used, channel_report.worker_count_used);
		for (const auto& stage_report : channel_report.stage_reports) {
			aggregate_report.stage_reports.push_back("channel=" + std::to_string(channel_index) + "," + stage_report);
		}
		for (const auto& node_report : channel_report.node_reports) {
			aggregate_report.node_reports.push_back("channel=" + std::to_string(channel_index) + "," + node_report);
		}
	}
	if (IsFxCustomSpatialPreset(normalized)
		&& !ApplyFxCustomSharedSpatialBus(normalized, sample_rate, &output_channels, &aggregate_report, error_out)) {
		return false;
	}

	output->assign(frame_count * static_cast<size_t>(channels), 0.0f);
	std::vector<const float*> channel_ptrs(static_cast<size_t>(channels), nullptr);
	for (int channel_index = 0; channel_index < channels; ++channel_index) {
		channel_ptrs[static_cast<size_t>(channel_index)] = output_channels[static_cast<size_t>(channel_index)].data();
	}
	if (!Engine::Audio::Core::AudioInterleaveProcessor::Interleave(
			channel_ptrs.data(),
			channels,
			frame_count,
			output->data())) {
		if (error_out != nullptr) {
			*error_out = "failed to interleave fx custom output";
		}
		return false;
	}
	ComputeCustomEffectReportStats(*output, &aggregate_report);
	if (report_out != nullptr) {
		*report_out = aggregate_report;
	}
	return true;
}

CustomEffectPackage BuildBassBoostEffect() { return BuildNamedPreset("BassBoost"); }
CustomEffectPackage BuildEqEffect() { return BuildNamedPreset("EQ"); }
CustomEffectPackage BuildHighEffect() { return BuildNamedPreset("High"); }
CustomEffectPackage BuildAirHighEffect() { return BuildNamedPreset("AIRHigh"); }
CustomEffectPackage BuildCurveAirEffect() { return BuildNamedPreset("CurveAIR"); }
CustomEffectPackage BuildDeEsserEffect() { return BuildNamedPreset("DeEsser"); }
CustomEffectPackage BuildCompressorEffect() { return BuildNamedPreset("Compressor"); }
CustomEffectPackage BuildLimiterEffect() { return BuildNamedPreset("Limiter"); }
CustomEffectPackage BuildAutoTuneEffect() { return BuildNamedPreset("AutoTune"); }
CustomEffectPackage BuildAutoTuneCorrectPitchEffect() { return BuildNamedPreset("AutoTuneCorrectPitch"); }
CustomEffectPackage BuildPitchShifterEffect() { return BuildNamedPreset("PitchShifter"); }
CustomEffectPackage BuildPitchKeyEffect() { return BuildNamedPreset("PitchKey"); }
CustomEffectPackage BuildAutoPitchEffect() { return BuildNamedPreset("AutoPitch"); }
CustomEffectPackage BuildStereoConverterEffect() { return BuildNamedPreset("Stereo Converter"); }
CustomEffectPackage BuildVhsEffect() { return BuildNamedPreset("VHS"); }
CustomEffectPackage BuildRetroVhsEffect() { return BuildNamedPreset("RetroVHS"); }
CustomEffectPackage BuildAnalogWarmthEffect() { return BuildNamedPreset("AnalogWarmth"); }
CustomEffectPackage BuildTapeEchoEffect() { return BuildNamedPreset("TapeEcho"); }
CustomEffectPackage BuildPlateReverbEffect() { return BuildNamedPreset("PlateReverb"); }
CustomEffectPackage BuildDreamChorusEffect() { return BuildNamedPreset("DreamChorus"); }
CustomEffectPackage BuildDubDelayEffect() { return BuildNamedPreset("DubDelay"); }
CustomEffectPackage BuildLoFiSamplerEffect() { return BuildNamedPreset("LoFiSampler"); }
CustomEffectPackage BuildVocalPresenceEffect() { return BuildNamedPreset("VocalPresence"); }
CustomEffectPackage BuildMasterGlueEffect() { return BuildNamedPreset("MasterGlue"); }
CustomEffectPackage BuildNeonPhaserEffect() { return BuildNamedPreset("NeonPhaser"); }
CustomEffectPackage BuildSuperReverbEffect() { return BuildNamedPreset("SuperReverb"); }
CustomEffectPackage BuildRoomReverbEffect() { return BuildNamedPreset("RoomReverb"); }
CustomEffectPackage Build8DReverbStereoEffect() { return BuildNamedPreset("8DReverbStereo"); }
CustomEffectPackage BuildStudioReverbEffect() { return BuildNamedPreset("StudioReverb"); }
CustomEffectPackage BuildEqReverbEffect() { return BuildNamedPreset("EQReverb"); }
CustomEffectPackage BuildDelayReverbEffect() { return BuildNamedPreset("DelayReverb"); }
CustomEffectPackage BuildDelayEffect() { return BuildNamedPreset("Delay"); }
CustomEffectPackage BuildAirVoiceEffect() { return BuildNamedPreset("AIrVoice"); }

}  // namespace Engine::Audio::FX::Customs
