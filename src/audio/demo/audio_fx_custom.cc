#include "helper/string_utils.h"
#include "audio/demo/audio_fx_custom.h"

#include "audio/audio_core/audio_source_loader.h"
#include "audio/effects_rack/custom_effect_struct.h"
#include "audio/file_io_codecs/codec_mp3_lame.h"
#include "audio/file_io_codecs/codec_wav_pcm.h"
#include "audio/fx_customs/fx_customs_presets.h"

#include <json/json.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace XER::Engine::Audio::Demo {

namespace {

std::string NormalizeName(std::string text) {
	text = XER::Engine::Helper::String::ToLowerAscii(text);
	text.erase(std::remove(text.begin(), text.end(), ' '), text.end());
	return text;
}

std::string SlugifyName(std::string text) {
	text = XER::Engine::Helper::String::ToLowerAscii(text);
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
		return std::isalnum(ch) ? static_cast<char>(ch) : static_cast<char>('_');
	});
	text.erase(std::unique(text.begin(), text.end(), [](char lhs, char rhs) {
		return lhs == '_' && rhs == '_';
	}), text.end());
	while (!text.empty() && text.front() == '_') {
		text.erase(text.begin());
	}
	while (!text.empty() && text.back() == '_') {
		text.pop_back();
	}
	return text.empty() ? std::string("fx_custom") : text;
}

std::string BuildStageSlug(size_t stage_index, const std::string& effect_name) {
	std::ostringstream output;
	output << std::setw(2) << std::setfill('0') << stage_index << "_" << SlugifyName(effect_name);
	return output.str();
}

enum class AudioFxBatchMode {
	kParallel,
	kChain,
};

std::string AudioFxBatchModeToString(AudioFxBatchMode batch_mode) {
	switch (batch_mode) {
	case AudioFxBatchMode::kParallel:
		return "parallel";
	case AudioFxBatchMode::kChain:
		return "chain";
	}
	return "parallel";
}

bool ParseAudioFxBatchMode(const std::string& text, AudioFxBatchMode* batch_mode_out, std::string* error_out) {
	if (batch_mode_out == nullptr) {
		if (error_out != nullptr) {
			*error_out = "audio fx batch mode target is null";
		}
		return false;
	}
	const std::string normalized = NormalizeName(text.empty() ? std::string("parallel") : text);
	if (normalized == "parallel") {
		*batch_mode_out = AudioFxBatchMode::kParallel;
		return true;
	}
	if (normalized == "chain") {
		*batch_mode_out = AudioFxBatchMode::kChain;
		return true;
	}
	if (error_out != nullptr) {
		*error_out = "unsupported audio fx batch mode: " + text;
	}
	return false;
}

bool IsClapPluginEffectName(const std::string& effect_name) {
	const std::string normalized = NormalizeName(effect_name);
	return normalized == "clapplugin" || normalized == "clap" || normalized == "plugin";
}

std::string ResolveClapPluginReferenceForStage(
	const AudioFxCustomOptions& options,
	const std::string& effect_name,
	size_t stage_index) {
	if (!IsClapPluginEffectName(effect_name)) {
		return options.clap_plugin_reference.empty() ? std::string("builtin://gain") : options.clap_plugin_reference;
	}

	size_t clap_stage_index = 0;
	for (size_t index = 0; index < stage_index && index < options.effect_names.size(); ++index) {
		if (IsClapPluginEffectName(options.effect_names[index])) {
			++clap_stage_index;
		}
	}

	if (clap_stage_index < options.clap_plugin_references.size() && !options.clap_plugin_references[clap_stage_index].empty()) {
		return options.clap_plugin_references[clap_stage_index];
	}
	if (!options.clap_plugin_reference.empty()) {
		return options.clap_plugin_reference;
	}
	return "builtin://gain";
}

bool WantsStereoByDefault(const std::string& effect_name);

bool WriteTextFile(
	const std::filesystem::path& path,
	const std::string& text,
	std::string* error_out);

bool WriteWaveFile(
	const std::filesystem::path& path,
	const std::vector<float>& samples,
	int sample_rate,
	int channels,
	std::string* error_out);

bool WriteMp3File(
	const std::filesystem::path& path,
	const std::vector<float>& samples,
	int sample_rate,
	int channels,
	std::vector<std::uint8_t>* encoded_out,
	std::string* error_out);

struct EffectArtifact {
	std::string effect_name;
	std::string effect_slug;
	size_t stage_index = 0;
	size_t frame_count = 0;
	std::string render_mode;
	std::filesystem::path stage_input_wav_path;
	std::filesystem::path processed_wav_path;
	std::filesystem::path processed_mp3_path;
	std::filesystem::path report_path;
	XER::Engine::Audio::FX::CustomEffectReport report;
	std::string report_text;
};

bool ContainsStereoPreferredEffect(const std::vector<std::string>& effect_names) {
	return std::any_of(effect_names.begin(), effect_names.end(), [](const std::string& effect_name) {
		return WantsStereoByDefault(effect_name);
	});
}

bool ResolveEffectList(
	const AudioFxCustomOptions& options,
	std::vector<std::string>* effect_names_out,
	std::string* error_out) {
	if (effect_names_out == nullptr) {
		if (error_out != nullptr) {
			*error_out = "audio fx effect list target is null";
		}
		return false;
	}
	*effect_names_out = options.effect_names;
	if (effect_names_out->empty() && !options.effect_name.empty()) {
		effect_names_out->push_back(options.effect_name);
	}
	if (effect_names_out->empty()) {
		if (error_out != nullptr) {
			*error_out = "audio fx command requires at least one effect name";
		}
		return false;
	}
	for (const auto& effect_name : *effect_names_out) {
		if (!XER::Engine::Audio::FX::Customs::IsFxCustomPresetSupported(effect_name)) {
			if (error_out != nullptr) {
				*error_out = "unsupported custom effect preset: " + effect_name;
			}
			return false;
		}
	}
	return true;
}

bool LoadInputAudio(
	const AudioFxCustomOptions& options,
	const std::vector<std::string>& effect_names,
	XER::Engine::Audio::Core::AudioSourceBuffer* input_audio_out,
	std::string* error_out) {
	if (input_audio_out == nullptr) {
		if (error_out != nullptr) {
			*error_out = "audio fx input buffer target is null";
		}
		return false;
	}

	XER::Engine::Audio::Core::AudioSourceLoadOptions load_options;
	load_options.input_path = options.input_path;
	load_options.raw_sample_rate = options.raw_sample_rate;
	load_options.target_sample_rate = options.target_sample_rate;
	load_options.target_channels = options.target_channels > 0
		? options.target_channels
		: (ContainsStereoPreferredEffect(effect_names) ? 2 : 0);
	load_options.strict_mp3_input = options.strict_mp3_input;
	return XER::Engine::Audio::Core::AudioSourceLoader::Load(load_options, input_audio_out, error_out);
}

std::string SerializeJson(const Json::Value& value) {
	Json::StreamWriterBuilder builder;
	builder["commentStyle"] = "None";
	builder["indentation"] = "  ";
	builder["precision"] = 10;
	std::string text = Json::writeString(builder, value);
	if (text.empty() || text.back() != '\n') {
		text.push_back('\n');
	}
	return text;
}

Json::Value BuildCustomEffectReportJson(const XER::Engine::Audio::FX::CustomEffectReport& report) {
	Json::Value value(Json::objectValue);
	value["label"] = report.label;
	value["node_count"] = static_cast<Json::UInt64>(report.node_count);
	value["stage_count"] = static_cast<Json::UInt64>(report.stage_count);
	value["channel_count"] = static_cast<Json::UInt64>(report.channel_count);
	value["worker_count_used"] = static_cast<Json::UInt64>(report.worker_count_used);
	value["peak"] = report.peak;
	value["rms"] = report.rms;
	Json::Value stage_reports(Json::arrayValue);
	for (const auto& stage_report : report.stage_reports) {
		stage_reports.append(stage_report);
	}
	value["stage_reports"] = std::move(stage_reports);
	Json::Value node_reports(Json::arrayValue);
	for (const auto& node_report : report.node_reports) {
		node_reports.append(node_report);
	}
	value["node_reports"] = std::move(node_reports);
	return value;
}

std::string BuildEffectArtifactText(
	const std::string& effect_name,
	const std::string& render_mode,
	size_t stage_index,
	const std::filesystem::path& input_path,
	const std::filesystem::path& stage_input_wav_path,
	const XER::Engine::Audio::Core::AudioSourceBuffer& input_audio,
	const std::filesystem::path& input_wav_path,
	const std::filesystem::path& output_wav_path,
	const std::filesystem::path& output_mp3_path,
	const XER::Engine::Audio::FX::CustomEffectReport& report) {
	std::ostringstream output;
	output << "Audio FX Custom CLI\n";
	output << "effect_name=" << effect_name << "\n";
	output << "render_mode=" << render_mode << "\n";
	output << "stage_index=" << stage_index << "\n";
	output << "input_path=" << input_path.string() << "\n";
	output << "stage_input_wav=" << stage_input_wav_path.string() << "\n";
	output << "source_format=" << input_audio.source_format << "\n";
	output << "codec=" << input_audio.codec_name << "\n";
	output << "decode_backend=" << input_audio.decode_backend << "\n";
	output << "original_sample_rate=" << input_audio.original_sample_rate << "\n";
	output << "original_channels=" << input_audio.original_channels << "\n";
	output << "sample_rate=" << input_audio.sample_rate << "\n";
	output << "render_channels=" << input_audio.channels << "\n";
	output << "frame_count=" << input_audio.frame_count << "\n";
	output << "normalized_input_wav=" << input_wav_path.string() << "\n";
	output << "processed_wav=" << output_wav_path.string() << "\n";
	if (!output_mp3_path.empty()) {
		output << "processed_mp3=" << output_mp3_path.string() << "\n";
	}
	output << XER::Engine::Audio::FX::BuildCustomEffectReportText(report);
	return output.str();
}

bool RenderEffectArtifact(
	const AudioFxCustomOptions& options,
	const XER::Engine::Audio::Core::AudioSourceBuffer& input_audio,
	const std::vector<float>& render_input,
	const std::filesystem::path& stage_input_wav_path,
	const std::filesystem::path& input_wav_path,
	const std::string& effect_name,
	size_t stage_index,
	const std::string& render_mode,
	bool write_processed_wav,
	bool write_processed_mp3,
	std::vector<float>* processed_audio_out,
	std::vector<std::uint8_t>* mp3_output_out,
	EffectArtifact* artifact_out,
	std::string* error_out) {
	if (artifact_out == nullptr) {
		if (error_out != nullptr) {
			*error_out = "audio fx artifact target is null";
		}
		return false;
	}

	std::vector<float> processed_audio;
	artifact_out->effect_name = effect_name;
	artifact_out->effect_slug = options.effect_names.size() > 1u
		? BuildStageSlug(stage_index, effect_name)
		: SlugifyName(effect_name);
	artifact_out->stage_index = stage_index;
	artifact_out->render_mode = render_mode;
	artifact_out->stage_input_wav_path = stage_input_wav_path;
	if (write_processed_wav) {
		artifact_out->processed_wav_path = options.output_dir / (artifact_out->effect_slug + ".wav");
	}
	if (write_processed_mp3) {
		artifact_out->processed_mp3_path = options.output_dir / (artifact_out->effect_slug + ".mp3");
	}
	artifact_out->report_path = options.output_dir / (artifact_out->effect_slug + "_report.txt");
	if (!XER::Engine::Audio::FX::Customs::RenderFxCustomPresetInterleaved(
			effect_name,
			static_cast<float>(input_audio.sample_rate),
			render_input,
			input_audio.channels,
			&processed_audio,
			&artifact_out->report,
			ResolveClapPluginReferenceForStage(options, effect_name, stage_index),
			error_out)) {
		return false;
	}
	artifact_out->frame_count = input_audio.channels > 0
		? processed_audio.size() / static_cast<size_t>(input_audio.channels)
		: 0u;
	if (write_processed_wav && !WriteWaveFile(artifact_out->processed_wav_path, processed_audio, input_audio.sample_rate, input_audio.channels, error_out)) {
		return false;
	}
	if ((write_processed_mp3 || mp3_output_out != nullptr)
		&& !WriteMp3File(
			write_processed_mp3 ? artifact_out->processed_mp3_path : std::filesystem::path(),
			processed_audio,
			input_audio.sample_rate,
			input_audio.channels,
			mp3_output_out,
			error_out)) {
		return false;
	}
	artifact_out->report_text = BuildEffectArtifactText(
		artifact_out->effect_name,
		render_mode,
		stage_index,
		options.input_path,
		stage_input_wav_path,
		input_audio,
		input_wav_path,
		artifact_out->processed_wav_path,
		artifact_out->processed_mp3_path,
		artifact_out->report);
	if (!WriteTextFile(artifact_out->report_path, artifact_out->report_text, error_out)) {
		return false;
	}
	if (processed_audio_out != nullptr) {
		*processed_audio_out = std::move(processed_audio);
	}
	return true;
}

Json::Value BuildEffectArtifactJson(const EffectArtifact& artifact) {
	Json::Value value(Json::objectValue);
	value["effect_name"] = artifact.effect_name;
	value["effect_slug"] = artifact.effect_slug;
	value["render_mode"] = artifact.render_mode;
	value["stage_index"] = static_cast<Json::UInt64>(artifact.stage_index);
	value["stage_input_wav"] = artifact.stage_input_wav_path.string();
	value["processed_wav"] = artifact.processed_wav_path.string();
	if (!artifact.processed_mp3_path.empty()) {
		value["processed_mp3"] = artifact.processed_mp3_path.string();
	}
	value["report_path"] = artifact.report_path.string();
	value["frame_count"] = static_cast<Json::UInt64>(artifact.frame_count);
	value["report"] = BuildCustomEffectReportJson(artifact.report);
	return value;
}

std::string BuildBatchSummaryText(
	const AudioFxCustomOptions& options,
	AudioFxBatchMode batch_mode,
	const XER::Engine::Audio::Core::AudioSourceBuffer& input_audio,
	const std::filesystem::path& input_wav_path,
	const std::filesystem::path& summary_path,
	const std::vector<EffectArtifact>& artifacts) {
	std::ostringstream output;
	output << std::fixed << std::setprecision(6);
	output << "Audio FX Batch CLI\n";
	output << "batch_mode=" << AudioFxBatchModeToString(batch_mode) << "\n";
	output << "input_path=" << options.input_path.string() << "\n";
	output << "source_format=" << input_audio.source_format << "\n";
	output << "codec=" << input_audio.codec_name << "\n";
	output << "decode_backend=" << input_audio.decode_backend << "\n";
	output << "sample_rate=" << input_audio.sample_rate << "\n";
	output << "render_channels=" << input_audio.channels << "\n";
	output << "frame_count=" << input_audio.frame_count << "\n";
	output << "effect_count=" << artifacts.size() << "\n";
	output << "normalized_input_wav=" << input_wav_path.string() << "\n";
	if (!artifacts.empty()) {
		if (!artifacts.back().processed_wav_path.empty()) {
			output << "final_output_wav=" << artifacts.back().processed_wav_path.string() << "\n";
		}
		if (!artifacts.back().processed_mp3_path.empty()) {
			output << "final_output_mp3=" << artifacts.back().processed_mp3_path.string() << "\n";
		}
	}
	output << "summary_path=" << summary_path.string() << "\n";
	for (size_t index = 0; index < artifacts.size(); ++index) {
		const auto& artifact = artifacts[index];
		output << "effect_" << index << "_name=" << artifact.effect_name << "\n";
		output << "effect_" << index << "_mode=" << artifact.render_mode << "\n";
		output << "effect_" << index << "_stage_index=" << artifact.stage_index << "\n";
		output << "effect_" << index << "_stage_input_wav=" << artifact.stage_input_wav_path.string() << "\n";
		output << "effect_" << index << "_wav=" << artifact.processed_wav_path.string() << "\n";
		if (!artifact.processed_mp3_path.empty()) {
			output << "effect_" << index << "_mp3=" << artifact.processed_mp3_path.string() << "\n";
		}
		output << "effect_" << index << "_report=" << artifact.report_path.string() << "\n";
		output << "effect_" << index << "_frames=" << artifact.frame_count << "\n";
		output << "effect_" << index << "_peak=" << artifact.report.peak << "\n";
		output << "effect_" << index << "_rms=" << artifact.report.rms << "\n";
		output << "effect_" << index << "_channels=" << artifact.report.channel_count << "\n";
		output << "effect_" << index << "_workers=" << artifact.report.worker_count_used << "\n";
	}
	return output.str();
}

Json::Value BuildBatchSummaryJson(
	const AudioFxCustomOptions& options,
	AudioFxBatchMode batch_mode,
	const XER::Engine::Audio::Core::AudioSourceBuffer& input_audio,
	const std::filesystem::path& input_wav_path,
	const std::filesystem::path& summary_path,
	const std::vector<EffectArtifact>& artifacts) {
	Json::Value root(Json::objectValue);
	root["command"] = "audio_fx_batch";
	root["batch_mode"] = AudioFxBatchModeToString(batch_mode);
	root["input_path"] = options.input_path.string();
	root["source_format"] = input_audio.source_format;
	root["codec"] = input_audio.codec_name;
	root["decode_backend"] = input_audio.decode_backend;
	root["sample_rate"] = input_audio.sample_rate;
	root["render_channels"] = input_audio.channels;
	root["frame_count"] = static_cast<Json::UInt64>(input_audio.frame_count);
	root["effect_count"] = static_cast<Json::UInt64>(artifacts.size());
	root["normalized_input_wav"] = input_wav_path.string();
	root["summary_path"] = summary_path.string();
	if (!artifacts.empty()) {
		if (!artifacts.back().processed_wav_path.empty()) {
			root["final_output_wav"] = artifacts.back().processed_wav_path.string();
		}
		if (!artifacts.back().processed_mp3_path.empty()) {
			root["final_output_mp3"] = artifacts.back().processed_mp3_path.string();
		}
	}
	Json::Value effects(Json::arrayValue);
	for (const auto& artifact : artifacts) {
		effects.append(BuildEffectArtifactJson(artifact));
	}
	root["effects"] = std::move(effects);
	return root;
}

bool WantsStereoByDefault(const std::string& effect_name) {
	const std::string normalized = NormalizeName(effect_name);
	return normalized == "stereoconverter"
		|| normalized == "superreverb"
		|| normalized == "roomreverb"
		|| normalized == "8dreverbstereo"
		|| normalized == "studioreverb"
		|| normalized == "eqreverb"
		|| normalized == "delayreverb"
		|| normalized == "delay"
		|| normalized == "tapeecho"
		|| normalized == "platereverb"
		|| normalized == "dreamchorus"
		|| normalized == "dubdelay"
		|| normalized == "springtank"
		|| normalized == "dimensionchorus"
		|| normalized == "spaceecho"
		|| normalized == "shimmerbloom"
		|| normalized == "psychowidener"
		|| normalized == "airvoice";
}

bool WriteBinaryFile(
	const std::filesystem::path& path,
	const std::vector<std::uint8_t>& bytes,
	std::string* error_out) {
	std::ofstream output(path, std::ios::binary);
	if (!output.is_open()) {
		if (error_out != nullptr) {
			*error_out = "failed to open binary output: " + path.string();
		}
		return false;
	}
	output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	if (!output) {
		if (error_out != nullptr) {
			*error_out = "failed to write binary output: " + path.string();
		}
		return false;
	}
	return true;
}

bool WriteTextFile(
	const std::filesystem::path& path,
	const std::string& text,
	std::string* error_out) {
	std::ofstream output(path);
	if (!output.is_open()) {
		if (error_out != nullptr) {
			*error_out = "failed to open text output: " + path.string();
		}
		return false;
	}
	output << text;
	if (!output) {
		if (error_out != nullptr) {
			*error_out = "failed to write text output: " + path.string();
		}
		return false;
	}
	return true;
}

bool WriteWaveFile(
	const std::filesystem::path& path,
	const std::vector<float>& samples,
	int sample_rate,
	int channels,
	std::string* error_out) {
	XER::Engine::Audio::CodecIO::WavPcmCodec codec;
	std::vector<std::uint8_t> encoded;
	const size_t frame_count = channels > 0 ? samples.size() / static_cast<size_t>(channels) : 0u;
	if (!codec.Encode16(samples.data(), frame_count, encoded, sample_rate, channels)) {
		if (error_out != nullptr) {
			*error_out = "failed to encode custom fx wave output";
		}
		return false;
	}
	return WriteBinaryFile(path, encoded, error_out);
}

bool EncodeMp3Bytes(
	const std::vector<float>& samples,
	int sample_rate,
	int channels,
	std::vector<std::uint8_t>* encoded_out,
	std::string* error_out) {
	if (encoded_out == nullptr || samples.empty() || sample_rate <= 0 || channels <= 0) {
		if (error_out != nullptr) {
			*error_out = "invalid MP3 encode request";
		}
		return false;
	}
	if (channels > 2) {
		if (error_out != nullptr) {
			*error_out = "MP3 pipe output supports mono or stereo render channels";
		}
		return false;
	}
	if ((samples.size() % static_cast<size_t>(channels)) != 0u) {
		if (error_out != nullptr) {
			*error_out = "MP3 encode input frame alignment is invalid";
		}
		return false;
	}

	XER::Engine::Audio::CodecIO::Mp3LameCodec codec;
	const size_t frame_count = samples.size() / static_cast<size_t>(channels);
	if (!codec.EncodeInterleaved(samples.data(), frame_count, sample_rate, channels, *encoded_out)) {
		if (error_out != nullptr) {
			*error_out = "failed to encode processed audio as MP3; enable LAME or FFmpeg MP3 encoding";
		}
		return false;
	}
	return !encoded_out->empty();
}

bool WriteMp3File(
	const std::filesystem::path& path,
	const std::vector<float>& samples,
	int sample_rate,
	int channels,
	std::vector<std::uint8_t>* encoded_out,
	std::string* error_out) {
	std::vector<std::uint8_t> encoded;
	if (!EncodeMp3Bytes(samples, sample_rate, channels, &encoded, error_out)) {
		return false;
	}
	if (!path.empty() && !WriteBinaryFile(path, encoded, error_out)) {
		return false;
	}
	if (encoded_out != nullptr) {
		*encoded_out = std::move(encoded);
	}
	return true;
}

}  // namespace

bool RunAudioFxCustom(
	const AudioFxCustomOptions& options,
	std::string* report_out,
	std::string* error_out,
	std::vector<std::uint8_t>* mp3_output_out) {
	if (report_out == nullptr) {
		if (error_out != nullptr) {
			*error_out = "audio fx custom report target is null";
		}
		return false;
	}
	if (options.input_path.empty()) {
		if (error_out != nullptr) {
			*error_out = "audio fx custom requires an input audio path";
		}
		return false;
	}
	std::vector<std::string> effect_names;
	if (!ResolveEffectList(options, &effect_names, error_out)) {
		return false;
	}
	if (effect_names.size() != 1u) {
		if (error_out != nullptr) {
			*error_out = "audio fx custom expects exactly one effect name";
		}
		return false;
	}

	std::error_code fs_error;
	std::filesystem::create_directories(options.output_dir, fs_error);
	if (fs_error) {
		if (error_out != nullptr) {
			*error_out = "failed to create audio fx custom output directory";
		}
		return false;
	}

	XER::Engine::Audio::Core::AudioSourceBuffer input_audio;
	if (!LoadInputAudio(options, effect_names, &input_audio, error_out)) {
		return false;
	}

	const bool write_debug_wavs = options.write_intermediate_wavs && !options.pipe_mp3_output;
	const std::filesystem::path input_wav_path = write_debug_wavs ? options.output_dir / "normalized_input.wav" : std::filesystem::path();
	if (write_debug_wavs && !WriteWaveFile(input_wav_path, input_audio.samples, input_audio.sample_rate, input_audio.channels, error_out)) {
		return false;
	}
	EffectArtifact artifact;
	if (!RenderEffectArtifact(
			options,
			input_audio,
			input_audio.samples,
			input_wav_path,
			input_wav_path,
			effect_names.front(),
			0u,
			"single",
			!options.pipe_mp3_output,
			options.pipe_mp3_output && mp3_output_out == nullptr,
			nullptr,
			options.pipe_mp3_output ? mp3_output_out : nullptr,
			&artifact,
			error_out)) {
		return false;
	}
	*report_out = artifact.report_text;
	return true;
}

bool RunAudioFxBatch(
	const AudioFxCustomOptions& options,
	std::string* report_out,
	std::string* error_out,
	std::vector<std::uint8_t>* mp3_output_out) {
	if (report_out == nullptr) {
		if (error_out != nullptr) {
			*error_out = "audio fx batch report target is null";
		}
		return false;
	}
	if (options.input_path.empty()) {
		if (error_out != nullptr) {
			*error_out = "audio fx batch requires an input audio path";
		}
		return false;
	}

	std::vector<std::string> effect_names;
	if (!ResolveEffectList(options, &effect_names, error_out)) {
		return false;
	}
	if (effect_names.size() < 2u) {
		if (error_out != nullptr) {
			*error_out = "audio fx batch expects at least two effects";
		}
		return false;
	}
	AudioFxBatchMode batch_mode = AudioFxBatchMode::kParallel;
	if (!ParseAudioFxBatchMode(options.batch_mode, &batch_mode, error_out)) {
		return false;
	}

	std::error_code fs_error;
	std::filesystem::create_directories(options.output_dir, fs_error);
	if (fs_error) {
		if (error_out != nullptr) {
			*error_out = "failed to create audio fx batch output directory";
		}
		return false;
	}

	XER::Engine::Audio::Core::AudioSourceBuffer input_audio;
	if (!LoadInputAudio(options, effect_names, &input_audio, error_out)) {
		return false;
	}

	const bool write_debug_wavs = options.write_intermediate_wavs && !options.pipe_mp3_output;
	const std::filesystem::path input_wav_path = write_debug_wavs ? options.output_dir / "normalized_input.wav" : std::filesystem::path();
	if (write_debug_wavs && !WriteWaveFile(input_wav_path, input_audio.samples, input_audio.sample_rate, input_audio.channels, error_out)) {
		return false;
	}

	std::vector<EffectArtifact> artifacts;
	artifacts.reserve(effect_names.size());
	std::vector<float> chain_input = input_audio.samples;
	std::filesystem::path chain_input_wav_path = input_wav_path;
	for (size_t index = 0; index < effect_names.size(); ++index) {
		const auto& effect_name = effect_names[index];
		const bool is_final_stage = index + 1u == effect_names.size();
		EffectArtifact artifact;
		std::vector<float> stage_output;
		const std::vector<float>& render_input = batch_mode == AudioFxBatchMode::kChain ? chain_input : input_audio.samples;
		const std::filesystem::path& stage_input_wav_path = batch_mode == AudioFxBatchMode::kChain ? chain_input_wav_path : input_wav_path;
		const bool write_processed_wav = !options.pipe_mp3_output
			&& (batch_mode == AudioFxBatchMode::kParallel || options.write_intermediate_wavs || is_final_stage);
		const bool write_processed_mp3 = options.pipe_mp3_output && is_final_stage && mp3_output_out == nullptr;
		if (!RenderEffectArtifact(
				options,
				input_audio,
				render_input,
				stage_input_wav_path,
				input_wav_path,
				effect_name,
				index,
				AudioFxBatchModeToString(batch_mode),
				write_processed_wav,
				write_processed_mp3,
				batch_mode == AudioFxBatchMode::kChain ? &stage_output : nullptr,
				options.pipe_mp3_output && is_final_stage ? mp3_output_out : nullptr,
				&artifact,
				error_out)) {
			return false;
		}
		artifacts.push_back(std::move(artifact));
		if (batch_mode == AudioFxBatchMode::kChain) {
			chain_input = std::move(stage_output);
			chain_input_wav_path = artifacts.back().processed_wav_path;
		}
	}

	const std::filesystem::path summary_path = options.output_dir / (options.json_summary ? "batch_summary.json" : "batch_summary.txt");
	const std::string summary_text = options.json_summary
		? SerializeJson(BuildBatchSummaryJson(options, batch_mode, input_audio, input_wav_path, summary_path, artifacts))
		: BuildBatchSummaryText(options, batch_mode, input_audio, input_wav_path, summary_path, artifacts);
	if (!WriteTextFile(summary_path, summary_text, error_out)) {
		return false;
	}
	*report_out = summary_text;
	return true;
}

}  // namespace XER::Engine::Audio::Demo