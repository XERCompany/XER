#pragma once

#include <string>
#include <vector>

#include "audio/effects_rack/custom_effect.h"

namespace Engine::Audio::FX::Customs {

std::vector<std::string> ListFxCustomPresetNames();
bool IsFxCustomPresetSupported(const std::string& name);

CustomEffectPackage BuildFxCustomPreset(
	const std::string& name,
	const std::string& clap_plugin_reference = "builtin://gain");

bool RenderFxCustomPreset(
	const std::string& name,
	float sample_rate,
	const std::vector<float>& input,
	std::vector<float>* output,
	CustomEffectReport* report_out,
	const std::string& clap_plugin_reference = "builtin://gain",
	std::string* error_out = nullptr);

bool RenderFxCustomPresetInterleaved(
	const std::string& name,
	float sample_rate,
	const std::vector<float>& input,
	int channels,
	std::vector<float>* output,
	CustomEffectReport* report_out,
	const std::string& clap_plugin_reference = "builtin://gain",
	std::string* error_out = nullptr);

CustomEffectPackage BuildBassBoostEffect();
CustomEffectPackage BuildEqEffect();
CustomEffectPackage BuildHighEffect();
CustomEffectPackage BuildAirHighEffect();
CustomEffectPackage BuildCurveAirEffect();
CustomEffectPackage BuildDeEsserEffect();
CustomEffectPackage BuildCompressorEffect();
CustomEffectPackage BuildLimiterEffect();
CustomEffectPackage BuildAutoTuneEffect();
CustomEffectPackage BuildAutoTuneCorrectPitchEffect();
CustomEffectPackage BuildPitchShifterEffect();
CustomEffectPackage BuildPitchKeyEffect();
CustomEffectPackage BuildAutoPitchEffect();
CustomEffectPackage BuildStereoConverterEffect();
CustomEffectPackage BuildVhsEffect();
CustomEffectPackage BuildRetroVhsEffect();
CustomEffectPackage BuildAnalogWarmthEffect();
CustomEffectPackage BuildTapeEchoEffect();
CustomEffectPackage BuildPlateReverbEffect();
CustomEffectPackage BuildDreamChorusEffect();
CustomEffectPackage BuildDubDelayEffect();
CustomEffectPackage BuildLoFiSamplerEffect();
CustomEffectPackage BuildVocalPresenceEffect();
CustomEffectPackage BuildMasterGlueEffect();
CustomEffectPackage BuildNeonPhaserEffect();
CustomEffectPackage BuildSuperReverbEffect();
CustomEffectPackage BuildRoomReverbEffect();
CustomEffectPackage Build8DReverbStereoEffect();
CustomEffectPackage BuildStudioReverbEffect();
CustomEffectPackage BuildEqReverbEffect();
CustomEffectPackage BuildDelayReverbEffect();
CustomEffectPackage BuildDelayEffect();
CustomEffectPackage BuildAirVoiceEffect();

}  // namespace Engine::Audio::FX::Customs