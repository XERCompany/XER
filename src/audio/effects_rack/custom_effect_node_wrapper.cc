#include "custom_effect_node_wrapper.h"

#include <algorithm>
#include <optional>

#include "custom_effect_accel.h"
#include "custom_effect_core.h"
#include "custom_effect_fxdata.h"

namespace Engine::Audio::FX {

namespace {

std::optional<uint32_t> ResolveClapParameterId(
	const std::vector<Plugin::PluginParameterInfo>& infos,
	const std::string& name) {
	for (const auto& info : infos) {
		if (info.name == name) {
			return info.id;
		}
	}
	try {
		return static_cast<uint32_t>(std::stoul(name));
	} catch (...) {
		return std::nullopt;
	}
}

}  // namespace

bool CustomEffectNodeWrapper::Initialize(const CustomEffectNode& node, float sample_rate, size_t max_block_size, std::string* error_out) {
	if (!ValidateCustomEffectNode(node, error_out) || sample_rate <= 0.0f || max_block_size == 0) {
		return false;
	}

	node_ = node;
	sample_rate_ = sample_rate;
	max_block_size_ = max_block_size;
	wet_buffer_.assign(max_block_size_, 0.0f);
	if (node_.backend == CustomEffectBackend::kClapPlugin) {
		if (!clap_host_.Initialize(sample_rate_, static_cast<uint32_t>(max_block_size_))
			|| !clap_host_.LoadPlugin(node_.plugin_reference)) {
			if (error_out != nullptr) {
				*error_out = "failed to initialize CLAP custom effect node: " + node_.label;
			}
			return false;
		}
		const auto parameter_infos = clap_host_.GetParameters();
		for (const auto& parameter : node_.parameters) {
			const auto parameter_id = ResolveClapParameterId(parameter_infos, parameter.name);
			if (!parameter_id.has_value()) {
				continue;
			}
			clap_host_.SetParameter(*parameter_id, parameter.value);
		}
		return true;
	}

	switch (node_.algorithm) {
	case CustomEffectAlgorithm::kPassthrough:
	case CustomEffectAlgorithm::kCompressor:
	case CustomEffectAlgorithm::kExpander:
	case CustomEffectAlgorithm::kGate:
	case CustomEffectAlgorithm::kLimiter:
	case CustomEffectAlgorithm::kBitcrush:
	case CustomEffectAlgorithm::kTube:
		return true;
	case CustomEffectAlgorithm::kChorus:
		chorus_.SetRateHz(GetCustomEffectParameterValue(node_.parameters, "rate_hz", 0.8f));
		chorus_.SetDepthSamples(GetCustomEffectParameterValue(node_.parameters, "depth_samples", 14.0f));
		chorus_.SetMix(1.0f);
		return chorus_.Initialize(sample_rate_, std::max<size_t>(4096u, max_block_size_ * 2));
	case CustomEffectAlgorithm::kFlanger:
		flanger_.SetRateHz(GetCustomEffectParameterValue(node_.parameters, "rate_hz", 0.35f));
		flanger_.SetDepthSamples(GetCustomEffectParameterValue(node_.parameters, "depth_samples", 10.0f));
		flanger_.SetFeedback(GetCustomEffectParameterValue(node_.parameters, "feedback", 0.25f));
		flanger_.SetMix(1.0f);
		return flanger_.Initialize(sample_rate_, std::max<size_t>(4096u, max_block_size_ * 2));
	case CustomEffectAlgorithm::kPhaser:
		phaser_.SetRateHz(GetCustomEffectParameterValue(node_.parameters, "rate_hz", 0.30f));
		phaser_.SetDepth(GetCustomEffectParameterValue(node_.parameters, "depth", 0.7f));
		phaser_.SetFeedback(GetCustomEffectParameterValue(node_.parameters, "feedback", 0.2f));
		phaser_.SetMix(1.0f);
		return phaser_.Initialize(sample_rate_, 4u);
	case CustomEffectAlgorithm::kParametricEq:
		if (!parametric_eq_.Initialize(sample_rate_, 1u)) {
			return false;
		}
		return parametric_eq_.SetBand(0u, ParametricBand{
			GetCustomEffectParameterValue(node_.parameters, "frequency_hz", 2200.0f),
			GetCustomEffectParameterValue(node_.parameters, "q", 0.8f),
			GetCustomEffectParameterValue(node_.parameters, "gain_db", 0.0f),
			true,
		});
	case CustomEffectAlgorithm::kReverbAlgorithmic:
		reverb_algorithmic_.SetRoomSize(GetCustomEffectParameterValue(node_.parameters, "room_size", 0.6f));
		reverb_algorithmic_.SetDamping(GetCustomEffectParameterValue(node_.parameters, "damping", 0.3f));
		reverb_algorithmic_.SetMix(1.0f);
		return reverb_algorithmic_.Initialize(sample_rate_, std::max<size_t>(4096u, max_block_size_ * 8));
	case CustomEffectAlgorithm::kPitchShift:
		if (!pitch_shifter_.Initialize(sample_rate_, 384u, 4096u)) {
			return false;
		}
		pitch_shifter_.SetPitchRatio(GetCustomEffectParameterValue(node_.parameters, "pitch_ratio", 1.0f));
		return true;
	case CustomEffectAlgorithm::kAutoTuneCorrectPitch:
		if (!auto_tune_correct_pitch_.Initialize(sample_rate_, max_block_size_)) {
			return false;
		}
		auto_tune_correct_pitch_.SetCorrectionAmount(GetCustomEffectParameterValue(node_.parameters, "correction_amount", 0.88f));
		auto_tune_correct_pitch_.SetCorrectionSpeed(GetCustomEffectParameterValue(node_.parameters, "correction_speed", 0.18f));
		auto_tune_correct_pitch_.SetRetuneSpeedMs(GetCustomEffectParameterValue(node_.parameters, "retune_speed_ms", 18.0f));
		auto_tune_correct_pitch_.SetHumanize(GetCustomEffectParameterValue(node_.parameters, "humanize", 0.12f));
		auto_tune_correct_pitch_.SetScaleRoot(GetCustomEffectParameterValue(node_.parameters, "scale_root", 0.0f));
		auto_tune_correct_pitch_.SetScaleMode(GetCustomEffectParameterValue(node_.parameters, "scale_mode", 0.0f));
		auto_tune_correct_pitch_.SetPresenceDb(GetCustomEffectParameterValue(node_.parameters, "presence_db", 1.6f));
		auto_tune_correct_pitch_.SetWetMix(GetCustomEffectParameterValue(node_.parameters, "wet_mix", 0.92f));
		return true;
	case CustomEffectAlgorithm::kDelay:
		delay_feedback_state_ = 0.0f;
		return delay_line_.Initialize(std::max<size_t>(4096u, max_block_size_ * 8));
	case CustomEffectAlgorithm::kPlugin:
		return false;
	case CustomEffectAlgorithm::kUnknown:
		break;
	}
	if (error_out != nullptr) {
		*error_out = "unsupported custom effect node algorithm";
	}
	return false;
}

bool CustomEffectNodeWrapper::ProcessBlock(
	const float* input,
	size_t frame_count,
	size_t frame_offset,
	float* output,
	std::string* error_out) {
	if (input == nullptr || output == nullptr || frame_count == 0 || frame_count > max_block_size_) {
		if (error_out != nullptr) {
			*error_out = "invalid custom effect processing block";
		}
		return false;
	}

	if (!node_.enabled) {
		std::copy(input, input + static_cast<std::ptrdiff_t>(frame_count), output);
		return true;
	}

	bool processed = false;
	if (node_.backend == CustomEffectBackend::kClapPlugin) {
		processed = clap_host_.Process(input, wet_buffer_.data(), static_cast<uint32_t>(frame_count));
	} else {
	switch (node_.algorithm) {
	case CustomEffectAlgorithm::kPassthrough:
		std::copy(input, input + static_cast<std::ptrdiff_t>(frame_count), wet_buffer_.begin());
		processed = true;
		break;
	case CustomEffectAlgorithm::kCompressor:
		compressor_.SetThresholdDb(GetCustomEffectParameterValue(node_.parameters, "threshold_db", -16.0f));
		compressor_.SetRatio(GetCustomEffectParameterValue(node_.parameters, "ratio", 3.0f));
		processed = compressor_.ProcessBlock(input, frame_count, wet_buffer_.data());
		break;
	case CustomEffectAlgorithm::kExpander:
		expander_.SetThresholdDb(GetCustomEffectParameterValue(node_.parameters, "threshold_db", -36.0f));
		expander_.SetRatio(GetCustomEffectParameterValue(node_.parameters, "ratio", 2.5f));
		expander_.SetRangeDb(GetCustomEffectParameterValue(node_.parameters, "range_db", 18.0f));
		processed = expander_.ProcessBlock(input, frame_count, wet_buffer_.data());
		break;
	case CustomEffectAlgorithm::kGate:
		gate_.SetThresholdDb(GetCustomEffectParameterValue(node_.parameters, "threshold_db", -42.0f));
		gate_.SetHoldSamples(static_cast<size_t>(std::max(1.0f, GetCustomEffectParameterValue(node_.parameters, "hold_samples", 256.0f))));
		processed = gate_.ProcessBlock(input, frame_count, wet_buffer_.data());
		break;
	case CustomEffectAlgorithm::kLimiter:
		limiter_.SetCeilingDb(GetCustomEffectParameterValue(node_.parameters, "ceiling_db", -1.0f));
		limiter_.SetRelease(GetCustomEffectParameterValue(node_.parameters, "release", 0.002f));
		processed = limiter_.ProcessBlock(input, frame_count, wet_buffer_.data());
		break;
	case CustomEffectAlgorithm::kBitcrush:
		bitcrush_.SetBitDepth(static_cast<int>(GetCustomEffectParameterValue(node_.parameters, "bit_depth", 10.0f)));
		bitcrush_.SetDownsampleFactor(static_cast<int>(GetCustomEffectParameterValue(node_.parameters, "downsample_factor", 2.0f)));
		processed = bitcrush_.ProcessBlock(input, frame_count, wet_buffer_.data());
		break;
	case CustomEffectAlgorithm::kTube:
		tube_.SetDrive(GetCustomEffectParameterValue(node_.parameters, "drive", 2.4f));
		tube_.SetOutputGain(GetCustomEffectParameterValue(node_.parameters, "output_gain", 0.85f));
		processed = tube_.ProcessBlock(input, frame_count, wet_buffer_.data());
		break;
	case CustomEffectAlgorithm::kChorus:
		processed = chorus_.ProcessBlock(input, frame_count, wet_buffer_.data());
		break;
	case CustomEffectAlgorithm::kFlanger:
		processed = flanger_.ProcessBlock(input, frame_count, wet_buffer_.data());
		break;
	case CustomEffectAlgorithm::kPhaser:
		processed = phaser_.ProcessBlock(input, frame_count, wet_buffer_.data());
		break;
	case CustomEffectAlgorithm::kParametricEq:
		processed = parametric_eq_.ProcessBlock(input, frame_count, wet_buffer_.data());
		break;
	case CustomEffectAlgorithm::kReverbAlgorithmic:
		processed = reverb_algorithmic_.ProcessBlock(input, frame_count, wet_buffer_.data());
		break;
	case CustomEffectAlgorithm::kPitchShift:
		pitch_shifter_.SetPitchRatio(GetCustomEffectParameterValue(node_.parameters, "pitch_ratio", 1.0f));
		processed = pitch_shifter_.ProcessBlock(input, frame_count, wet_buffer_.data());
		break;
	case CustomEffectAlgorithm::kAutoTuneCorrectPitch:
		auto_tune_correct_pitch_.SetCorrectionAmount(GetCustomEffectParameterValue(node_.parameters, "correction_amount", 0.88f));
		auto_tune_correct_pitch_.SetCorrectionSpeed(GetCustomEffectParameterValue(node_.parameters, "correction_speed", 0.18f));
		auto_tune_correct_pitch_.SetRetuneSpeedMs(GetCustomEffectParameterValue(node_.parameters, "retune_speed_ms", 18.0f));
		auto_tune_correct_pitch_.SetHumanize(GetCustomEffectParameterValue(node_.parameters, "humanize", 0.12f));
		auto_tune_correct_pitch_.SetScaleRoot(GetCustomEffectParameterValue(node_.parameters, "scale_root", 0.0f));
		auto_tune_correct_pitch_.SetScaleMode(GetCustomEffectParameterValue(node_.parameters, "scale_mode", 0.0f));
		auto_tune_correct_pitch_.SetPresenceDb(GetCustomEffectParameterValue(node_.parameters, "presence_db", 1.6f));
		auto_tune_correct_pitch_.SetWetMix(GetCustomEffectParameterValue(node_.parameters, "wet_mix", 0.92f));
		processed = auto_tune_correct_pitch_.ProcessBlock(input, frame_count, wet_buffer_.data());
		break;
	case CustomEffectAlgorithm::kDelay: {
		const size_t delay_samples = static_cast<size_t>(std::max(1.0f, GetCustomEffectParameterValue(node_.parameters, "delay_samples", 640.0f)));
		const float feedback = std::clamp(GetCustomEffectParameterValue(node_.parameters, "feedback", 0.35f), -0.95f, 0.95f);
		delay_line_.SetDelaySamples(delay_samples);
		for (size_t i = 0; i < frame_count; ++i) {
			const float delayed = delay_line_.Process(input[i] + delay_feedback_state_ * feedback);
			delay_feedback_state_ = delayed;
			wet_buffer_[i] = delayed;
		}
		processed = true;
		break;
	}
	case CustomEffectAlgorithm::kPlugin:
		break;
	case CustomEffectAlgorithm::kUnknown:
		break;
	}
	}
	if (!processed) {
		if (error_out != nullptr) {
			*error_out = "custom effect node block processing failed: " + node_.label;
		}
		return false;
	}

	float constant_mix = 1.0f;
	if (ResolveCustomEffectNodeConstantMix(node_, &constant_mix)) {
		MixDryWetConstant(input, wet_buffer_.data(), frame_count, constant_mix, output);
	} else {
		for (size_t index = 0; index < frame_count; ++index) {
			const float mix = ResolveCustomEffectNodeMix(node_, frame_offset + index);
			output[index] = MixCustomEffectDryWet(input[index], wet_buffer_[index], mix);
		}
	}
	return true;
}

std::string CustomEffectNodeWrapper::GetReport() const {
	if (node_.backend == CustomEffectBackend::kClapPlugin) {
		return node_.label + ":clap=" + clap_host_.GetLoadedPluginId();
	}
	switch (node_.algorithm) {
	case CustomEffectAlgorithm::kCompressor:
		return node_.label + ":" + compressor_.GetReport();
	case CustomEffectAlgorithm::kExpander:
		return node_.label + ":" + expander_.GetReport();
	case CustomEffectAlgorithm::kGate:
		return node_.label + ":" + gate_.GetReport();
	case CustomEffectAlgorithm::kLimiter:
		return node_.label + ":" + limiter_.GetReport();
	case CustomEffectAlgorithm::kBitcrush:
		return node_.label + ":" + bitcrush_.GetReport();
	case CustomEffectAlgorithm::kTube:
		return node_.label + ":" + tube_.GetReport();
	case CustomEffectAlgorithm::kChorus:
		return node_.label + ":" + chorus_.GetReport();
	case CustomEffectAlgorithm::kFlanger:
		return node_.label + ":" + flanger_.GetReport();
	case CustomEffectAlgorithm::kPhaser:
		return node_.label + ":" + phaser_.GetReport();
	case CustomEffectAlgorithm::kParametricEq:
		return node_.label + ":" + parametric_eq_.GetReport();
	case CustomEffectAlgorithm::kReverbAlgorithmic:
		return node_.label + ":" + reverb_algorithmic_.GetReport();
	case CustomEffectAlgorithm::kPitchShift:
		return node_.label + ":" + pitch_shifter_.GetReport();
	case CustomEffectAlgorithm::kAutoTuneCorrectPitch:
		return node_.label + ":" + auto_tune_correct_pitch_.GetReport();
	case CustomEffectAlgorithm::kDelay:
		return node_.label + ":delay";
	case CustomEffectAlgorithm::kPassthrough:
		return node_.label + ":passthrough";
	case CustomEffectAlgorithm::kPlugin:
		return node_.label + ":plugin";
	case CustomEffectAlgorithm::kUnknown:
		break;
	}
	return node_.label + ":unknown";
}

}  // namespace Engine::Audio::FX
