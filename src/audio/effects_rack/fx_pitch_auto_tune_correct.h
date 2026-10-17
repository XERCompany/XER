#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "audio/dsp_algorithms/dsp_pitch_shifter_granular.h"
#include "fx_dynamics_compressor.h"
#include "fx_dynamics_limiter.h"
#include "fx_eq_parametric.h"

namespace Engine::Audio::FX {

class AutoTuneCorrectPitch {
public:
	AutoTuneCorrectPitch();
	~AutoTuneCorrectPitch();

	bool Initialize(float sample_rate, size_t max_block_size);
	void SetCorrectionAmount(float amount);
	void SetCorrectionSpeed(float speed);
	void SetRetuneSpeedMs(float milliseconds);
	void SetHumanize(float humanize);
	void SetScaleRoot(float semitone);
	void SetScaleMode(float mode);
	void SetPresenceDb(float gain_db);
	void SetWetMix(float mix);
	bool ProcessBlock(const float* input, size_t frame_count, float* output);
	void Reset();
	std::string GetReport() const;

private:
	struct PitchEstimate {
		float frequency_hz = 0.0f;
		float confidence = 0.0f;
	};

	PitchEstimate EstimatePitch(const float* input, size_t frame_count) const;
	float ResolveTargetMidi(float pitch_midi) const;
	bool IsScaleDegreeAllowed(int degree) const;
	float ComputeCorrectionRatio(const PitchEstimate& pitch);

	float sample_rate_ = 44100.0f;
	size_t max_block_size_ = 0;
	float correction_amount_ = 0.88f;
	float correction_speed_ = 0.18f;
	float retune_speed_ms_ = 18.0f;
	float humanize_ = 0.12f;
	int scale_root_ = 0;
	int scale_mode_ = 0;
	float presence_db_ = 1.6f;
	float wet_mix_ = 0.92f;
	float smoothed_ratio_ = 1.0f;
	float last_target_midi_ = 69.0f;
	float last_pitch_hz_ = 0.0f;
	float last_confidence_ = 0.0f;
	DSP::GranularPitchShifter pitch_shifter_;
	ParametricEQ presence_eq_;
	DynamicsCompressor compressor_;
	DynamicsLimiter limiter_;
	std::vector<float> pitch_buffer_;
	std::vector<float> tone_buffer_;
	std::vector<float> dynamics_buffer_;
};

}  // namespace Engine::Audio::FX