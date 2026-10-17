#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "custom_effect_node.h"
#include "audio/dsp_algorithms/dsp_pitch_shifter_granular.h"
#include "audio/plugin_wrappers/clap_host_interface.h"
#include "fx_distortion_bitcrush.h"
#include "fx_distortion_tube.h"
#include "fx_dynamics_compressor.h"
#include "fx_dynamics_expander.h"
#include "fx_dynamics_gate.h"
#include "fx_dynamics_limiter.h"
#include "fx_eq_parametric.h"
#include "fx_mod_chorus.h"
#include "fx_mod_flanger.h"
#include "fx_mod_phaser.h"
#include "fx_pitch_auto_tune_correct.h"
#include "fx_reverb_algorithmic.h"

namespace Engine::Audio::FX {

class CustomEffectNodeWrapper {
public:
	bool Initialize(const CustomEffectNode& node, float sample_rate, size_t max_block_size, std::string* error_out = nullptr);
	bool ProcessBlock(
		const float* input,
		size_t frame_count,
		size_t frame_offset,
		float* output,
		std::string* error_out = nullptr);
	std::string GetReport() const;
	const CustomEffectNode& GetNode() const { return node_; }

private:
	CustomEffectNode node_;
	float sample_rate_ = 44100.0f;
	size_t max_block_size_ = 0;
	Plugin::ClapHostInterface clap_host_;
	DSP::CircularDelayLine delay_line_;
	DSP::GranularPitchShifter pitch_shifter_;
	DistortionBitcrush bitcrush_;
	DistortionTube tube_;
	DynamicsCompressor compressor_;
	DynamicsExpander expander_;
	DynamicsGate gate_;
	DynamicsLimiter limiter_;
	ModChorus chorus_;
	ModFlanger flanger_;
	ModPhaser phaser_;
	ParametricEQ parametric_eq_;
	AutoTuneCorrectPitch auto_tune_correct_pitch_;
	ReverbAlgorithmic reverb_algorithmic_;
	float delay_feedback_state_ = 0.0f;
	std::vector<float> wet_buffer_;
};

}  // namespace Engine::Audio::FX
