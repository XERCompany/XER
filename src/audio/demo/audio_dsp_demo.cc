#include "audio/demo/audio_dsp_demo.h"

#include "audio/audio_core/audio_source_loader.h"
#include "audio/dsp_algorithms/dsp_phase_vocoder.h"
#include "audio/dsp_algorithms/dsp_spectral_shaper.h"
#include "audio/file_io_codecs/codec_wav_pcm.h"
#include "helper/string_utils.h"

#include <absl/strings/str_format.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

namespace XER::Audio::Demo {

namespace {

constexpr int kDefaultSampleRate = 44100;
constexpr size_t kFFTSize = 1024;
constexpr size_t kHopSize = 256;
constexpr size_t kSyntheticSignalSamples = kHopSize * 64;
constexpr float kSpectralShaperRmsThreshold = 1.0e-3f;
constexpr float kSpectralShaperPeakThreshold = 1.0e-2f;
constexpr float kPhaseVocoderRmsThreshold = 2.0e-3f;
constexpr float kPhaseVocoderPeakThreshold = 5.0e-3f;

struct ErrorMetrics {
	float rms = 0.0f;
	float peak = 0.0f;
};

enum class DemoProcessorKind {
	kSpectralShaper,
	kPhaseVocoder,
};

bool WriteBinaryFile(
	const std::filesystem::path& path,
	const std::vector<std::uint8_t>& bytes,
	std::string* error_out) {
	std::ofstream output(path, std::ios::binary);
	if (!output.is_open()) {
		if (error_out != nullptr) {
			*error_out = absl::StrFormat("failed to open '%s'", path.string());
		}
		return false;
	}

	output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	output.close();
	if (!output) {
		if (error_out != nullptr) {
			*error_out = absl::StrFormat("failed to write '%s'", path.string());
		}
		return false;
	}
	return true;
}

std::vector<float> GenerateInputSignal(size_t sample_count, int sample_rate) {
	std::vector<float> samples(sample_count, 0.0f);
	for (size_t i = 0; i < sample_count; ++i) {
		const float t = static_cast<float>(i) / static_cast<float>(sample_rate);
		samples[i] = 0.45f * std::sin(2.0f * static_cast<float>(M_PI) * 220.0f * t)
			+ 0.30f * std::sin(2.0f * static_cast<float>(M_PI) * 440.0f * t)
			+ 0.15f * std::sin(2.0f * static_cast<float>(M_PI) * 880.0f * t);
	}
	return samples;
}

std::vector<float> TrimLatency(
	const std::vector<float>& processed,
	size_t latency_samples,
	size_t target_frames) {
	if (processed.size() <= latency_samples) {
		return {};
	}

	const size_t available_frames = processed.size() - latency_samples;
	const size_t output_frames = target_frames == 0 ? available_frames : std::min(target_frames, available_frames);
	return std::vector<float>(
		processed.begin() + static_cast<std::ptrdiff_t>(latency_samples),
		processed.begin() + static_cast<std::ptrdiff_t>(latency_samples + output_frames));
}

ErrorMetrics ComputeErrorMetrics(
	const std::vector<float>& reference,
	const std::vector<float>& processed,
	size_t latency_samples) {
	ErrorMetrics metrics;
	if (processed.size() <= latency_samples || reference.empty()) {
		return metrics;
	}

	const size_t comparable = std::min(reference.size(), processed.size() - latency_samples);
	double sum_squared = 0.0;
	for (size_t i = 0; i < comparable; ++i) {
		const float error = processed[i + latency_samples] - reference[i];
		sum_squared += static_cast<double>(error) * static_cast<double>(error);
		metrics.peak = std::max(metrics.peak, std::abs(error));
	}
	metrics.rms = static_cast<float>(std::sqrt(sum_squared / static_cast<double>(comparable)));
	return metrics;
}

template <typename Processor>
std::vector<float> ProcessSignal(
	Processor* processor,
	const std::vector<float>& input,
	size_t hop_size,
	size_t latency_samples,
	std::string* error_out) {
	if (processor == nullptr || hop_size == 0) {
		if (error_out != nullptr) {
			*error_out = "audio dsp processor is not configured";
		}
		return {};
	}

	const size_t base_size = input.size() + latency_samples;
	const size_t padded_size = ((base_size + hop_size - 1) / hop_size) * hop_size;
	std::vector<float> padded_input(padded_size, 0.0f);
	std::copy(input.begin(), input.end(), padded_input.begin());

	std::vector<float> output(padded_size, 0.0f);
	for (size_t offset = 0; offset < padded_size; offset += hop_size) {
		if (!processor->ProcessBlock(padded_input.data() + static_cast<std::ptrdiff_t>(offset), hop_size, output.data() + static_cast<std::ptrdiff_t>(offset))) {
			if (error_out != nullptr) {
				*error_out = "audio dsp processor failed";
			}
			return {};
		}
	}
	return output;
}

bool EncodeWave(
	const std::filesystem::path& path,
	const std::vector<float>& audio,
	int sample_rate,
	std::string* error_out) {
	XER::Audio::CodecIO::WavPcmCodec codec;
	std::vector<std::uint8_t> encoded;
	if (!codec.Encode16(audio.data(), audio.size(), encoded, sample_rate)) {
		if (error_out != nullptr) {
			*error_out = "failed to encode wav";
		}
		return false;
	}
	return WriteBinaryFile(path, encoded, error_out);
}

bool ResolveProcessorKind(
	const std::string& processor_name,
	DemoProcessorKind* processor_kind,
	std::string* error_out) {
	if (processor_kind == nullptr) {
		if (error_out != nullptr) {
			*error_out = "audio processor target is null";
		}
		return false;
	}

	const std::string normalized = XER::Helper::String::ToLowerAscii(processor_name);
	if (normalized == "spectral_shaper") {
		*processor_kind = DemoProcessorKind::kSpectralShaper;
		return true;
	}
	if (normalized == "phase_vocoder") {
		*processor_kind = DemoProcessorKind::kPhaseVocoder;
		return true;
	}

	if (error_out != nullptr) {
		*error_out = "unsupported audio processor: " + processor_name;
	}
	return false;
}

bool BuildSpectralShaperCurve(
	const std::string& profile_name,
	size_t positive_bins,
	std::vector<float>* curve_out,
	std::string* error_out) {
	if (curve_out == nullptr || positive_bins == 0) {
		if (error_out != nullptr) {
			*error_out = "spectral shaper curve target is invalid";
		}
		return false;
	}

	const std::string profile = XER::Helper::String::ToLowerAscii(profile_name.empty() ? std::string("tilt") : profile_name);
	curve_out->assign(positive_bins, 1.0f);
	const float denominator = positive_bins > 1 ? static_cast<float>(positive_bins - 1) : 1.0f;

	if (profile == "unity") {
		return true;
	}
	if (profile == "tilt") {
		for (size_t bin = 0; bin < positive_bins; ++bin) {
			const float t = static_cast<float>(bin) / denominator;
			(*curve_out)[bin] = std::max(0.35f, 1.10f - 0.70f * t);
		}
		return true;
	}
	if (profile == "bright") {
		for (size_t bin = 0; bin < positive_bins; ++bin) {
			const float t = static_cast<float>(bin) / denominator;
			(*curve_out)[bin] = 0.75f + 0.60f * t;
		}
		return true;
	}

	if (error_out != nullptr) {
		*error_out = "unsupported spectral shaper profile: " + profile_name;
	}
	return false;
}

bool RunSelectedProcessorDemo(
	const AudioDSPDemoOptions& options,
	const XER::Audio::Core::AudioSourceBuffer& input_audio,
	const std::string& mode,
	std::string* report_out,
	std::string* error_out) {
	if (report_out == nullptr) {
		if (error_out != nullptr) {
			*error_out = "audio demo report target is null";
		}
		return false;
	}
	if (input_audio.samples.empty() || input_audio.sample_rate <= 0 || input_audio.channels <= 0) {
		if (error_out != nullptr) {
			*error_out = "input audio buffer is empty";
		}
		return false;
	}

	DemoProcessorKind processor_kind = DemoProcessorKind::kSpectralShaper;
	if (!ResolveProcessorKind(options.processor.empty() ? std::string("spectral_shaper") : options.processor, &processor_kind, error_out)) {
		return false;
	}

	std::error_code fs_error;
	std::filesystem::create_directories(options.output_dir, fs_error);
	if (fs_error) {
		if (error_out != nullptr) {
			*error_out = "failed to create audio demo output directory";
		}
		return false;
	}

	const size_t latency_samples = kFFTSize - kHopSize;
	std::string processing_error;
	std::vector<float> processed_signal;
	std::string processor_label;
	std::string processor_detail;

	if (processor_kind == DemoProcessorKind::kSpectralShaper) {
		processor_label = "spectral_shaper";
		processor_detail = options.spectral_shaper_profile.empty() ? "tilt" : options.spectral_shaper_profile;

		XER::Audio::DSP::SpectralShaper shaper(kFFTSize, kHopSize);
		std::vector<float> curve;
		if (!BuildSpectralShaperCurve(processor_detail, (kFFTSize / 2u) + 1u, &curve, error_out)) {
			return false;
		}
		if (!shaper.SetCurve(curve)) {
			if (error_out != nullptr) {
				*error_out = "failed to set spectral shaper curve";
			}
			return false;
		}
		processed_signal = ProcessSignal(&shaper, input_audio.samples, kHopSize, latency_samples, &processing_error);
	} else {
		processor_label = "phase_vocoder";
		processor_detail = absl::StrFormat("%0.3f", options.phase_vocoder_ratio);

		XER::Audio::DSP::PhaseVocoder vocoder(kFFTSize, kHopSize);
		if (!vocoder.Initialize(static_cast<float>(input_audio.sample_rate))) {
			if (error_out != nullptr) {
				*error_out = "failed to initialize phase vocoder";
			}
			return false;
		}
		vocoder.SetTimeStretchRatio(options.phase_vocoder_ratio);
		processed_signal = ProcessSignal(&vocoder, input_audio.samples, kHopSize, latency_samples, &processing_error);
	}

	if (processed_signal.empty()) {
		if (error_out != nullptr) {
			*error_out = processing_error.empty() ? "audio dsp processor failed" : processing_error;
		}
		return false;
	}

	const std::vector<float> aligned_output = TrimLatency(processed_signal, latency_samples, input_audio.samples.size());
	if (aligned_output.empty()) {
		if (error_out != nullptr) {
			*error_out = "failed to trim processed audio latency";
		}
		return false;
	}

	const std::filesystem::path input_wav = options.output_dir / "decoded_input.wav";
	const std::filesystem::path processed_wav = options.output_dir / (processor_label + std::string(".wav"));
	if (!EncodeWave(input_wav, input_audio.samples, input_audio.sample_rate, error_out)
		|| !EncodeWave(processed_wav, aligned_output, input_audio.sample_rate, error_out)) {
		return false;
	}

	*report_out = absl::StrFormat(
		"Audio DSP demo\n"
		"mode=%s\n"
		"input_path=%s\n"
		"source_format=%s\n"
		"codec=%s\n"
		"decode_backend=%s\n"
		"original_sample_rate=%d\n"
		"original_channels=%d\n"
		"sample_rate=%d\n"
		"normalized_channels=%d\n"
		"original_frames=%zu\n"
		"input_frames=%zu\n"
		"processed_frames=%zu\n"
		"fft_size=%zu\n"
		"hop_size=%zu\n"
		"latency_samples=%zu\n"
		"processor=%s\n"
		"processor_detail=%s\n"
		"input_wav=%s\n"
		"processed_wav=%s\n",
		mode,
		options.input_path.empty() ? "<synthetic>" : options.input_path.string(),
		input_audio.source_format.empty() ? "unknown" : input_audio.source_format,
		input_audio.codec_name.empty() ? "unknown" : input_audio.codec_name,
		input_audio.decode_backend.empty() ? "generated" : input_audio.decode_backend,
		input_audio.original_sample_rate,
		input_audio.original_channels,
		input_audio.sample_rate,
		input_audio.channels,
		input_audio.original_frame_count,
		input_audio.frame_count,
		aligned_output.size(),
		kFFTSize,
		kHopSize,
		latency_samples,
		processor_label,
		processor_detail,
		input_wav.string(),
		processed_wav.string());

	if (error_out != nullptr) {
		error_out->clear();
	}
	return true;
}

bool RunSyntheticSmoke(
	const std::filesystem::path& output_dir,
	std::string* report_out,
	std::string* error_out) {
	if (report_out == nullptr) {
		if (error_out != nullptr) {
			*error_out = "audio demo report target is null";
		}
		return false;
	}

	std::error_code fs_error;
	std::filesystem::create_directories(output_dir, fs_error);
	if (fs_error) {
		if (error_out != nullptr) {
			*error_out = "failed to create audio demo output directory";
		}
		return false;
	}

	const std::vector<float> input = GenerateInputSignal(kSyntheticSignalSamples, kDefaultSampleRate);

	XER::Audio::DSP::SpectralShaper shaper(kFFTSize, kHopSize);
	if (!shaper.SetCurve(std::vector<float>((kFFTSize / 2u) + 1u, 1.0f))) {
		if (error_out != nullptr) {
			*error_out = "failed to set spectral shaper curve";
		}
		return false;
	}

	XER::Audio::DSP::PhaseVocoder vocoder(kFFTSize, kHopSize);
	if (!vocoder.Initialize(static_cast<float>(kDefaultSampleRate))) {
		if (error_out != nullptr) {
			*error_out = "failed to initialize phase vocoder";
		}
		return false;
	}
	vocoder.SetTimeStretchRatio(1.0f);

	const size_t latency_samples = kFFTSize - kHopSize;
	std::string processing_error;
	const std::vector<float> shaper_output = ProcessSignal(&shaper, input, kHopSize, latency_samples, &processing_error);
	if (shaper_output.empty()) {
		if (error_out != nullptr) {
			*error_out = processing_error;
		}
		return false;
	}

	const std::vector<float> vocoder_output = ProcessSignal(&vocoder, input, kHopSize, latency_samples, &processing_error);
	if (vocoder_output.empty()) {
		if (error_out != nullptr) {
			*error_out = processing_error;
		}
		return false;
	}

	const ErrorMetrics shaper_metrics = ComputeErrorMetrics(input, shaper_output, latency_samples);
	const ErrorMetrics vocoder_metrics = ComputeErrorMetrics(input, vocoder_output, latency_samples);
	const bool spectral_shaper_ok = shaper_metrics.rms <= kSpectralShaperRmsThreshold
		&& shaper_metrics.peak <= kSpectralShaperPeakThreshold;
	const bool phase_vocoder_ok = vocoder_metrics.rms <= kPhaseVocoderRmsThreshold
		&& vocoder_metrics.peak <= kPhaseVocoderPeakThreshold;
	const bool smoke_ok = spectral_shaper_ok && phase_vocoder_ok;

	const std::filesystem::path input_path = output_dir / "input.wav";
	const std::filesystem::path shaper_path = output_dir / "spectral_shaper_unity.wav";
	const std::filesystem::path vocoder_path = output_dir / "phase_vocoder_unity.wav";
	const std::vector<float> shaper_aligned = TrimLatency(shaper_output, latency_samples, input.size());
	const std::vector<float> vocoder_aligned = TrimLatency(vocoder_output, latency_samples, input.size());
	if (shaper_aligned.empty() || vocoder_aligned.empty()) {
		if (error_out != nullptr) {
			*error_out = "failed to trim synthetic smoke outputs";
		}
		return false;
	}
	if (!EncodeWave(input_path, input, kDefaultSampleRate, error_out)
		|| !EncodeWave(shaper_path, shaper_aligned, kDefaultSampleRate, error_out)
		|| !EncodeWave(vocoder_path, vocoder_aligned, kDefaultSampleRate, error_out)) {
		return false;
	}

	*report_out = absl::StrFormat(
		"Audio DSP demo\n"
		"smoke_status=%s\n"
		"sample_rate=%d\n"
		"fft_size=%zu\n"
		"hop_size=%zu\n"
		"latency_samples=%zu\n"
		"spectral_shaper_status=%s\n"
		"spectral_shaper_rms=%0.6f\n"
		"spectral_shaper_peak=%0.6f\n"
		"phase_vocoder_status=%s\n"
		"phase_vocoder_rms=%0.6f\n"
		"phase_vocoder_peak=%0.6f\n"
		"input_mode=synthetic\n"
		"input_wav=%s\n"
		"spectral_shaper_wav=%s\n"
		"phase_vocoder_wav=%s\n",
		smoke_ok ? "pass" : "fail",
		kDefaultSampleRate,
		kFFTSize,
		kHopSize,
		latency_samples,
		spectral_shaper_ok ? "pass" : "fail",
		shaper_metrics.rms,
		shaper_metrics.peak,
		phase_vocoder_ok ? "pass" : "fail",
		vocoder_metrics.rms,
		vocoder_metrics.peak,
		input_path.string(),
		shaper_path.string(),
		vocoder_path.string());

	if (!smoke_ok) {
		if (error_out != nullptr) {
			*error_out = "audio dsp smoke thresholds exceeded";
		}
		return false;
	}

	if (error_out != nullptr) {
		error_out->clear();
	}
	return true;
}

}  // namespace

bool RunAudioDSPDemo(
	const AudioDSPDemoOptions& options,
	std::string* report_out,
	std::string* error_out) {
	const std::string processor = XER::Helper::String::ToLowerAscii(options.processor);
	if (options.input_path.empty() && (processor.empty() || processor == "smoke")) {
		return RunSyntheticSmoke(options.output_dir, report_out, error_out);
	}

	if (options.input_path.empty()) {
		const int target_sample_rate = options.target_sample_rate > 0 ? options.target_sample_rate : kDefaultSampleRate;
		XER::Audio::Core::AudioSourceBuffer synthetic_input;
		synthetic_input.samples = GenerateInputSignal(kSyntheticSignalSamples, target_sample_rate);
		synthetic_input.sample_rate = target_sample_rate;
		synthetic_input.channels = 1;
		synthetic_input.original_sample_rate = target_sample_rate;
		synthetic_input.original_channels = 1;
		synthetic_input.frame_count = synthetic_input.samples.size();
		synthetic_input.original_frame_count = synthetic_input.samples.size();
		synthetic_input.source_format = "synthetic_tone_mix";
		synthetic_input.codec_name = "generated";
		synthetic_input.decode_backend = "generated";
		return RunSelectedProcessorDemo(
			options,
			synthetic_input,
			"synthetic",
			report_out,
			error_out);
	}

	XER::Audio::Core::AudioSourceLoadOptions load_options;
	load_options.input_path = options.input_path;
	load_options.raw_sample_rate = options.raw_sample_rate;
	load_options.target_sample_rate = options.target_sample_rate;
	XER::Audio::Core::AudioSourceBuffer decoded_audio;
	if (!XER::Audio::Core::AudioSourceLoader::Load(load_options, &decoded_audio, error_out)) {
		return false;
	}

	AudioDSPDemoOptions normalized_options = options;
	if (normalized_options.processor.empty()) {
		normalized_options.processor = "spectral_shaper";
	}
	return RunSelectedProcessorDemo(
		normalized_options,
		decoded_audio,
		"file",
		report_out,
		error_out);
}

bool RunAudioDSPDemo(
	const std::filesystem::path& output_dir,
	std::string* report_out,
	std::string* error_out) {
	AudioDSPDemoOptions options;
	options.output_dir = output_dir;
	return RunAudioDSPDemo(options, report_out, error_out);
}

}  // namespace XER::Audio::Demo