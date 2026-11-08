#include "audio_modules_smoke.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "audio/file_io_codecs/codec_file_streamer.h"
#include "audio/file_io_codecs/codec_aac_adts.h"
#include "audio/file_io_codecs/codec_metadata_id3.h"
#include "audio/file_io_codecs/codec_mp3_lame.h"
#include "audio/file_io_codecs/codec_ogg_vorbis.h"
#include "audio/file_io_codecs/codec_wav_float.h"
#include "audio/file_io_codecs/codec_wav_pcm.h"
#include "audio/dsp_algorithms/dsp_pitch_shifter_granular.h"
#include "audio/effects_rack/custom_effect.h"
#include "audio/effects_rack/custom_effect_package.h"
#include "audio/effects_rack/fx_reverb_convolution.h"
#include "audio/midi_sequencing/midi_clock_generator.h"
#include "audio/midi_sequencing/midi_controller_mapping.h"
#include "audio/midi_sequencing/midi_event_dispatcher.h"
#include "audio/midi_sequencing/midi_parser.h"
#include "audio/midi_sequencing/midi_pattern_sequencer.h"
#include "audio/midi_sequencing/midi_sysex_handler.h"
#include "audio/plugin_wrappers/au_host_interface.h"
#include "audio/plugin_wrappers/clap_host_interface.h"
#include "audio/plugin_wrappers/vst3_host_interface.h"
#include "audio/plugin_wrappers/vst3_external_host.h"
#include "audio/synth_engine/synth_modulation_matrix.h"
#include "audio/synth_engine/synth_voice_manager.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"
#include <cmath>

namespace XER::Audio::Demo {

namespace {

struct SignalStats {
	float peak = 0.0f;
	double rms = 0.0;
};

struct SynthSmokeResult {
	std::vector<float> output;
	size_t active_voice_count = 0;
	size_t resident_voice_count = 0;
	float gain_min = 0.0f;
	float gain_max = 0.0f;
	double gain_average = 0.0;
};

SignalStats ComputeSignalStats(const std::vector<float>& samples) {
	SignalStats stats;
	if (samples.empty()) {
		return stats;
	}
	double sum_squared = 0.0;
	for (float sample : samples) {
		const float absolute = std::abs(sample);
		stats.peak = std::max(stats.peak, absolute);
		sum_squared += static_cast<double>(sample) * static_cast<double>(sample);
	}
	stats.rms = std::sqrt(sum_squared / static_cast<double>(samples.size()));
	return stats;
}

bool ReadInputBytes(const std::filesystem::path& path, std::vector<uint8_t>* bytes_out, std::string* error_out) {
	if (bytes_out == nullptr) {
		if (error_out != nullptr) {
			*error_out = "byte output target is null";
		}
		return false;
	}
	std::error_code size_error;
	const std::uintmax_t file_size = std::filesystem::file_size(path, size_error);
	if (size_error) {
		if (error_out != nullptr) {
			*error_out = "failed to determine input size: " + path.string();
		}
		return false;
	}

	XER::Audio::CodecIO::CodecFileStreamer streamer;
	if (!streamer.OpenRead(path.string())) {
		if (error_out != nullptr) {
			*error_out = "failed to open input through CodecFileStreamer: " + path.string();
		}
		return false;
	}

	bytes_out->assign(static_cast<size_t>(file_size), 0);
	const size_t bytes_read = streamer.ReadBytes(bytes_out->data(), bytes_out->size());
	streamer.Close();
	if (bytes_read != bytes_out->size()) {
		if (error_out != nullptr) {
			*error_out = "failed to read complete input through CodecFileStreamer";
		}
		return false;
	}
	return true;
}

int DetectSampleRate(const std::filesystem::path& path) {
	XER::Bridge::FFmpeg::MediaInfo media_info;
	if (!XER::Bridge::FFmpeg::ProbeMedia(path.string(), &media_info, nullptr)) {
		return 44100;
	}
	for (const auto& stream : media_info.streams) {
		if (stream.media_type == "audio" && stream.sample_rate > 0) {
			return stream.sample_rate;
		}
	}
	return 44100;
}

template <typename Wrapper>
bool ProcessInBlocks(Wrapper* wrapper, const std::vector<float>& input, std::vector<float>* output, uint32_t block_size) {
	if (wrapper == nullptr || output == nullptr || block_size == 0) {
		return false;
	}
	output->assign(input.size(), 0.0f);
	std::vector<float> scratch(block_size, 0.0f);
	for (size_t cursor = 0; cursor < input.size(); cursor += block_size) {
		const uint32_t frames = static_cast<uint32_t>(std::min<size_t>(block_size, input.size() - cursor));
		if (!wrapper->Process(input.data() + static_cast<std::ptrdiff_t>(cursor), scratch.data(), frames)) {
			return false;
		}
		std::copy(scratch.begin(), scratch.begin() + frames, output->begin() + static_cast<std::ptrdiff_t>(cursor));
	}
	return true;
}

bool WriteWaveArtifact(const std::filesystem::path& path, const std::vector<float>& samples, int sample_rate) {
	XER::Audio::CodecIO::WavPcmCodec codec;
	std::vector<uint8_t> bytes;
	if (!codec.Encode16(samples.data(), samples.size(), bytes, sample_rate)) {
		return false;
	}
	std::ofstream output(path, std::ios::binary);
	if (!output.is_open()) {
		return false;
	}
	output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	return static_cast<bool>(output);
}

bool WriteTextArtifact(const std::filesystem::path& path, const std::string& text) {
	std::ofstream output(path);
	if (!output.is_open()) {
		return false;
	}
	output << text;
	return static_cast<bool>(output);
}

std::vector<float> BuildSyntheticImpulseResponse(size_t ir_size) {
	std::vector<float> impulse_response(ir_size, 0.0f);
	if (impulse_response.empty()) {
		return impulse_response;
	}
	impulse_response[0] = 1.0f;
	for (size_t index = 1; index < impulse_response.size(); ++index) {
		const float t = static_cast<float>(index) / static_cast<float>(impulse_response.size() - 1);
		impulse_response[index] = 0.45f * std::exp(-5.0f * t);
	}
	return impulse_response;
}

float MidiNoteToFrequency(uint8_t note) {
	return 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
}

SynthSmokeResult RunSynthVoiceManagerSmoke(int sample_rate) {
	SynthSmokeResult result;
	if (sample_rate <= 0) {
		return result;
	}
	XER::Audio::Synth::SynthVoiceManager synth;
	XER::Audio::Synth::SynthModulationMatrix modulation_matrix;
	constexpr uint32_t kLfoSource = 1u;
	constexpr uint32_t kDensitySource = 2u;
	constexpr uint32_t kOutputGainDestination = 10u;
	synth.SetMaxVoices(3u);
	modulation_matrix.SetAmount(kLfoSource, kOutputGainDestination, 0.30f);
	modulation_matrix.SetAmount(kDensitySource, kOutputGainDestination, 0.18f);
	modulation_matrix.ConfigureDestination(kOutputGainDestination, 0.72f, 0.18f, 1.15f);
	const size_t total_frames = static_cast<size_t>(sample_rate) + static_cast<size_t>(sample_rate / 4);
	result.output.assign(total_frames, 0.0f);
	result.gain_min = 1.15f;
	for (size_t frame = 0; frame < total_frames; ++frame) {
		if (frame == 0u) {
			synth.NoteOn(60u, MidiNoteToFrequency(60u));
		}
		if (frame == static_cast<size_t>(sample_rate / 8)) {
			synth.NoteOn(64u, MidiNoteToFrequency(64u));
		}
		if (frame == static_cast<size_t>(sample_rate / 4)) {
			synth.NoteOn(67u, MidiNoteToFrequency(67u));
		}
		if (frame == static_cast<size_t>((sample_rate * 3) / 8)) {
			synth.NoteOn(72u, MidiNoteToFrequency(72u));
		}
		if (frame == static_cast<size_t>(sample_rate / 2)) {
			synth.NoteOff(64u);
		}
		if (frame == static_cast<size_t>((sample_rate * 5) / 8)) {
			synth.NoteOff(67u);
		}
		if (frame == static_cast<size_t>((sample_rate * 3) / 4)) {
			synth.NoteOff(72u);
		}
		const float time_seconds = static_cast<float>(frame) / static_cast<float>(sample_rate);
		modulation_matrix.SetSourceValue(kLfoSource, std::sin(2.0f * static_cast<float>(M_PI) * 1.9f * time_seconds));
		modulation_matrix.SetSourceValue(kDensitySource, static_cast<float>(synth.GetActiveVoiceCount()) / 3.0f);
		float modulated_gain = 1.0f;
		if (!modulation_matrix.Apply(kOutputGainDestination, &modulated_gain)) {
			return {};
		}
		result.output[frame] = synth.RenderSample(static_cast<float>(sample_rate)) * modulated_gain;
		result.gain_min = std::min(result.gain_min, modulated_gain);
		result.gain_max = std::max(result.gain_max, modulated_gain);
		result.gain_average += modulated_gain;
	}
	if (!result.output.empty()) {
		result.gain_average /= static_cast<double>(result.output.size());
	}
	result.active_voice_count = synth.GetActiveVoiceCount();
	result.resident_voice_count = synth.GetResidentVoiceCount();
	return result;
}

bool ProcessEffectInBlocks(XER::Audio::FX::ReverbConvolution* effect, const std::vector<float>& input, std::vector<float>* output, size_t block_size) {
	if (effect == nullptr || output == nullptr || block_size == 0) {
		return false;
	}
	output->assign(input.size(), 0.0f);
	std::vector<float> scratch(block_size, 0.0f);
	for (size_t cursor = 0; cursor < input.size(); cursor += block_size) {
		const size_t frames = std::min(block_size, input.size() - cursor);
		if (!effect->ProcessBlock(input.data() + static_cast<std::ptrdiff_t>(cursor), frames, scratch.data())) {
			return false;
		}
		std::copy(scratch.begin(), scratch.begin() + static_cast<std::ptrdiff_t>(frames), output->begin() + static_cast<std::ptrdiff_t>(cursor));
	}
	return true;
}

bool ProcessGranularInBlocks(
	XER::Audio::DSP::GranularPitchShifter* shifter,
	const std::vector<float>& input,
	std::vector<float>* output,
	size_t block_size) {
	if (shifter == nullptr || output == nullptr || block_size == 0) {
		return false;
	}
	output->assign(input.size(), 0.0f);
	std::vector<float> scratch(block_size, 0.0f);
	for (size_t cursor = 0; cursor < input.size(); cursor += block_size) {
		const size_t frames = std::min(block_size, input.size() - cursor);
		if (!shifter->ProcessBlock(input.data() + static_cast<std::ptrdiff_t>(cursor), frames, scratch.data())) {
			return false;
		}
		std::copy(scratch.begin(), scratch.begin() + static_cast<std::ptrdiff_t>(frames), output->begin() + static_cast<std::ptrdiff_t>(cursor));
	}
	return true;
}

std::filesystem::path ResolveExternalClapSmokePluginPath() {
	std::error_code path_error;
	const std::filesystem::path executable_path = std::filesystem::read_symlink("/proc/self/exe", path_error);
	if (path_error) {
		return {};
	}
	const std::filesystem::path candidate = executable_path.parent_path() / "EngineClapSmokePlugin.clap";
	return std::filesystem::exists(candidate) ? candidate : std::filesystem::path{};
}

}  // namespace

bool RunAudioModulesSmoke(
	const AudioModulesSmokeOptions& options,
	std::string* report,
	std::string* error_out) {
	if (report == nullptr) {
		if (error_out != nullptr) {
			*error_out = "report target is null";
		}
		return false;
	}
	if (options.input_path.empty()) {
		if (error_out != nullptr) {
			*error_out = "audio_modules_smoke requires an input file";
		}
		return false;
	}

	std::vector<uint8_t> input_bytes;
	if (!ReadInputBytes(options.input_path, &input_bytes, error_out)) {
		return false;
	}

	XER::Audio::CodecIO::Mp3LameCodec mp3_codec;
	std::vector<float> decoded_mono;
	if (!mp3_codec.Decode(input_bytes.data(), input_bytes.size(), decoded_mono)) {
		if (error_out != nullptr) {
			*error_out = "direct MP3 decode failed for smoke input";
		}
		return false;
	}
	const int sample_rate = DetectSampleRate(options.input_path);
	const size_t smoke_frames = std::min<size_t>(decoded_mono.size(), 32768u);
	std::vector<float> smoke_input(decoded_mono.begin(), decoded_mono.begin() + static_cast<std::ptrdiff_t>(smoke_frames));
	if (smoke_input.empty()) {
		if (error_out != nullptr) {
			*error_out = "decoded MP3 smoke buffer is empty";
		}
		return false;
	}
	const std::vector<float>& processing_input = options.output_dir.empty() ? smoke_input : decoded_mono;

	XER::Audio::MIDI::MidiParser midi_parser;
	const std::vector<uint8_t> midi_bytes = {
		0x90, 60, 100,
		64, 110,
		0xB0, 74, 127,
		0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7,
		0xFA,
	};
	std::vector<XER::Audio::MIDI::MidiEvent> midi_events;
	if (!midi_parser.ParseStream(midi_bytes.data(), midi_bytes.size(), midi_events)) {
		if (error_out != nullptr) {
			*error_out = "MIDI parser smoke sequence failed";
		}
		return false;
	}

	XER::Audio::MIDI::MidiEventDispatcher dispatcher;
	size_t dispatched_events = 0;
	dispatcher.AddListener([&dispatched_events](const XER::Audio::MIDI::MidiEvent&) {
		++dispatched_events;
	});
	dispatcher.DispatchBatch(midi_events);

	XER::Audio::MIDI::MidiControllerMapping controller_mapping;
	controller_mapping.Bind(74, 0, -12.0f, 12.0f, false, 0, "gain_db");
	uint32_t mapped_param_id = 0;
	float mapped_gain_db = 0.0f;
	bool have_mapped_value = false;
	for (const auto& event : midi_events) {
		if (controller_mapping.MapEvent(event, &mapped_param_id, &mapped_gain_db)) {
			have_mapped_value = true;
			break;
		}
	}
	if (!have_mapped_value) {
		if (error_out != nullptr) {
			*error_out = "failed to map MIDI CC event to plugin parameter";
		}
		return false;
	}

	XER::Audio::MIDI::MidiSysexHandler sysex_handler;
	XER::Audio::MIDI::MidiSysexMessage sysex_message;
	bool parsed_sysex = false;
	for (const auto& event : midi_events) {
		if (event.type == XER::Audio::MIDI::MidiMessageType::kSysEx) {
			parsed_sysex = sysex_handler.Parse(event.message, &sysex_message);
			break;
		}
	}
	if (!parsed_sysex) {
		if (error_out != nullptr) {
			*error_out = "failed to parse MIDI SysEx smoke event";
		}
		return false;
	}

	XER::Audio::MIDI::MidiPatternSequencer sequencer;
	sequencer.SetPattern(midi_events);
	sequencer.SetLoopLength(4);
	const std::vector<XER::Audio::MIDI::MidiEvent> looped_events = sequencer.CollectRange(0, 8);

	XER::Audio::MIDI::MidiClockGenerator clock_generator;
	clock_generator.SetTempoBpm(120.0);
	const uint32_t clock_pulses = clock_generator.AdvanceSamples(static_cast<uint32_t>(sample_rate), static_cast<uint32_t>(sample_rate));

	XER::Audio::Plugin::Vst3HostInterface vst3_host;
	if (!vst3_host.Initialize(static_cast<double>(sample_rate), 1024u)
		|| !vst3_host.LoadPlugin("builtin://gain")
		|| !vst3_host.SetParameter(mapped_param_id, mapped_gain_db)) {
		if (error_out != nullptr) {
			*error_out = "failed to initialize built-in VST3 host smoke processor";
		}
		return false;
	}
	std::vector<float> vst3_output;
	if (!ProcessInBlocks(&vst3_host, processing_input, &vst3_output, 1024u)) {
		if (error_out != nullptr) {
			*error_out = "VST3 built-in smoke processing failed";
		}
		return false;
	}

	XER::Audio::Plugin::ClapHostInterface clap_host;
	const std::filesystem::path external_clap_plugin = ResolveExternalClapSmokePluginPath();
	const bool use_external_clap = !external_clap_plugin.empty();
	if (!clap_host.Initialize(static_cast<double>(sample_rate), 1024u)
		|| !clap_host.LoadPlugin(use_external_clap ? external_clap_plugin.string() : "builtin://chorus")
		|| !clap_host.SetParameter(0u, use_external_clap ? mapped_gain_db : 0.9f)
		|| (!use_external_clap && !clap_host.SetParameter(2u, 0.45f))) {
		if (error_out != nullptr) {
			*error_out = "failed to initialize CLAP host smoke processor";
		}
		return false;
	}
	std::vector<float> clap_output;
	if (!ProcessInBlocks(&clap_host, processing_input, &clap_output, 1024u)) {
		if (error_out != nullptr) {
			*error_out = "CLAP built-in smoke processing failed";
		}
		return false;
	}

	XER::Audio::Plugin::AuHostInterface au_host;
	if (!au_host.Initialize(static_cast<double>(sample_rate), 1024u)
		|| !au_host.LoadComponent("builtin://parametric_eq")
		|| !au_host.SetParameter(0u, 1800.0f)
		|| !au_host.SetParameter(1u, 0.8f)
		|| !au_host.SetParameter(2u, 6.0f)) {
		if (error_out != nullptr) {
			*error_out = "failed to initialize built-in AU host smoke processor";
		}
		return false;
	}
	std::vector<float> au_output;
	if (!ProcessInBlocks(&au_host, processing_input, &au_output, 1024u)) {
		if (error_out != nullptr) {
			*error_out = "AU built-in smoke processing failed";
		}
		return false;
	}

	XER::Audio::FX::ReverbConvolution convolution_reverb;
	const std::vector<float> convolution_ir = BuildSyntheticImpulseResponse(128u);
	if (!convolution_reverb.LoadImpulseResponse(convolution_ir.data(), convolution_ir.size())) {
		if (error_out != nullptr) {
			*error_out = "convolution reverb IR load failed";
		}
		return false;
	}
	convolution_reverb.SetMix(0.35f);
	std::vector<float> convolution_output;
	if (!ProcessEffectInBlocks(&convolution_reverb, processing_input, &convolution_output, 1024u)) {
		if (error_out != nullptr) {
			*error_out = "convolution reverb smoke processing failed";
		}
		return false;
	}

	const XER::Audio::FX::CustomEffectPackage custom_effect_package =
		XER::Audio::FX::BuildExampleCustomEffectPackage("builtin://gain");
	std::vector<float> custom_effect_output;
	XER::Audio::FX::CustomEffectReport custom_effect_report;
	if (!XER::Audio::FX::RunCustomEffectPackage(
			custom_effect_package,
			static_cast<float>(sample_rate),
			processing_input,
			&custom_effect_output,
			&custom_effect_report,
			error_out)) {
		if (error_out != nullptr && error_out->empty()) {
			*error_out = "custom effect example render failed";
		}
		return false;
	}
	std::string custom_effect_package_text;
	std::string custom_effect_package_error;
	if (!XER::Audio::FX::SerializeCustomEffectPackage(
			custom_effect_package,
			&custom_effect_package_text,
			&custom_effect_package_error)) {
		if (error_out != nullptr) {
			*error_out = custom_effect_package_error.empty()
				? std::string("custom effect example serialization failed")
				: custom_effect_package_error;
		}
		return false;
	}

	const SynthSmokeResult synth_smoke = RunSynthVoiceManagerSmoke(sample_rate);
	if (synth_smoke.output.empty()) {
		if (error_out != nullptr) {
			*error_out = "synth voice-manager smoke generation failed";
		}
		return false;
	}

	XER::Audio::DSP::GranularPitchShifter granular_shifter;
	if (!granular_shifter.Initialize(static_cast<float>(sample_rate), 384u, 4096u)) {
		if (error_out != nullptr) {
			*error_out = "granular pitch shifter initialization failed";
		}
		return false;
	}
	granular_shifter.SetPitchRatio(0.25f);
	std::vector<float> granular_output;
	if (!ProcessGranularInBlocks(&granular_shifter, processing_input, &granular_output, 512u)) {
		if (error_out != nullptr) {
			*error_out = "granular pitch shifter smoke processing failed";
		}
		return false;
	}

	XER::Audio::CodecIO::WavFloatCodec wav_float_codec;
	std::vector<uint8_t> wav_float_bytes;
	if (!wav_float_codec.Encode32(smoke_input.data(), std::min<size_t>(smoke_input.size(), 4096u), wav_float_bytes)) {
		if (error_out != nullptr) {
			*error_out = "WAV float encode smoke failed";
		}
		return false;
	}
	std::vector<float> wav_float_roundtrip;
	if (!wav_float_codec.Decode32(wav_float_bytes.data(), wav_float_bytes.size(), wav_float_roundtrip)) {
		if (error_out != nullptr) {
			*error_out = "WAV float decode smoke failed";
		}
		return false;
	}

	XER::Audio::CodecIO::MetadataId3 id3_codec;
	const XER::Audio::CodecIO::Id3Tag synthetic_tag{"hoodak", "smoke", "modules"};
	const std::vector<uint8_t> id3_bytes = id3_codec.Build(synthetic_tag);
	XER::Audio::CodecIO::Id3Tag parsed_tag;
	if (!id3_codec.Parse(id3_bytes.data(), id3_bytes.size(), parsed_tag)) {
		if (error_out != nullptr) {
			*error_out = "ID3 parse/build smoke failed";
		}
		return false;
	}

	XER::Audio::CodecIO::AacAdtsCodec aac_codec;
	std::vector<uint8_t> aac_bytes;
	if (!aac_codec.Encode(smoke_input.data(), std::min<size_t>(smoke_input.size(), 8192u), aac_bytes)) {
		if (error_out != nullptr) {
			*error_out = "AAC encode smoke failed";
		}
		return false;
	}
	std::vector<float> aac_roundtrip;
	if (!aac_codec.Decode(aac_bytes.data(), aac_bytes.size(), aac_roundtrip)) {
		if (error_out != nullptr) {
			*error_out = "AAC decode smoke failed after encode";
		}
		return false;
	}

	XER::Audio::CodecIO::OggVorbisCodec ogg_codec;
	std::vector<uint8_t> ogg_bytes;
	if (!ogg_codec.Encode(smoke_input.data(), std::min<size_t>(smoke_input.size(), 8192u), ogg_bytes)) {
		if (error_out != nullptr) {
			*error_out = "Ogg/Vorbis encode smoke failed";
		}
		return false;
	}
	std::vector<float> ogg_roundtrip;
	if (!ogg_codec.Decode(ogg_bytes.data(), ogg_bytes.size(), ogg_roundtrip)) {
		if (error_out != nullptr) {
			*error_out = "Ogg/Vorbis decode smoke failed after encode";
		}
		return false;
	}

	if (!options.output_dir.empty()) {
		std::error_code fs_error;
		std::filesystem::create_directories(options.output_dir, fs_error);
		if (fs_error) {
			if (error_out != nullptr) {
				*error_out = "failed to create smoke output directory";
			}
			return false;
		}
		if (!WriteWaveArtifact(options.output_dir / "vst3_gain.wav", vst3_output, sample_rate)
			|| !WriteWaveArtifact(options.output_dir / "clap_chorus.wav", clap_output, sample_rate)
			|| !WriteWaveArtifact(options.output_dir / "au_parametric_eq.wav", au_output, sample_rate)
			|| !WriteWaveArtifact(options.output_dir / "convolution_reverb.wav", convolution_output, sample_rate)
			|| !WriteWaveArtifact(options.output_dir / "custom_effect_example.wav", custom_effect_output, sample_rate)
			|| !WriteWaveArtifact(options.output_dir / "synth_polyphony.wav", synth_smoke.output, sample_rate)
			|| !WriteWaveArtifact(options.output_dir / "granular_pitch_shift.wav", granular_output, sample_rate)
			|| !WriteTextArtifact(options.output_dir / "custom_effect_example.txt", custom_effect_package_text)) {
			if (error_out != nullptr) {
				*error_out = "failed to write smoke wave artifacts";
			}
			return false;
		}
	}

	const SignalStats input_stats = ComputeSignalStats(processing_input);
	const SignalStats vst3_stats = ComputeSignalStats(vst3_output);
	const SignalStats clap_stats = ComputeSignalStats(clap_output);
	const SignalStats au_stats = ComputeSignalStats(au_output);
	const SignalStats convolution_stats = ComputeSignalStats(convolution_output);
	const SignalStats custom_effect_stats = ComputeSignalStats(custom_effect_output);
	const SignalStats synth_stats = ComputeSignalStats(synth_smoke.output);
	const SignalStats granular_stats = ComputeSignalStats(granular_output);

	std::ostringstream output;
	output << std::fixed << std::setprecision(6);
	output << "Audio Modules Smoke\n";
	output << "input_path=" << options.input_path.string() << "\n";
	output << "sample_rate=" << sample_rate << "\n";
	output << "decoded_frames=" << decoded_mono.size() << "\n";
	output << "smoke_frames=" << smoke_input.size() << "\n";
	output << "processed_frames=" << processing_input.size() << "\n";
	output << "input_peak=" << input_stats.peak << "\n";
	output << "input_rms=" << input_stats.rms << "\n";
	output << "midi_event_count=" << midi_events.size() << "\n";
	output << "midi_dispatched_events=" << dispatched_events << "\n";
	output << "midi_looped_events=" << looped_events.size() << "\n";
	output << "midi_clock_pulses=" << clock_pulses << "\n";
	output << "midi_sysex_manufacturer_id=" << sysex_message.manufacturer_id << "\n";
	output << "mapped_param_id=" << mapped_param_id << "\n";
	output << "mapped_gain_db=" << mapped_gain_db << "\n";
	output << "vst3_plugin=" << vst3_host.GetLoadedPluginId() << "\n";
	output << "vst3_mode=" << vst3_host.GetBackendMode() << "\n";
	output << "vst3_peak=" << vst3_stats.peak << "\n";
	output << "vst3_rms=" << vst3_stats.rms << "\n";
	output << "clap_plugin=" << clap_host.GetLoadedPluginId() << "\n";
	output << "clap_mode=" << (use_external_clap ? "external" : "builtin") << "\n";
	output << "clap_peak=" << clap_stats.peak << "\n";
	output << "clap_rms=" << clap_stats.rms << "\n";
	output << "au_plugin=" << au_host.GetLoadedComponentId() << "\n";
	output << "au_mode=" << au_host.GetBackendMode() << "\n";
	output << "au_peak=" << au_stats.peak << "\n";
	output << "au_rms=" << au_stats.rms << "\n";
	output << "convolution_ir_frames=" << convolution_ir.size() << "\n";
	output << "convolution_peak=" << convolution_stats.peak << "\n";
	output << "convolution_rms=" << convolution_stats.rms << "\n";
	output << "custom_effect_peak=" << custom_effect_stats.peak << "\n";
	output << "custom_effect_rms=" << custom_effect_stats.rms << "\n";
	output << "custom_effect_nodes=" << custom_effect_report.node_count << "\n";
	output << "custom_effect_stages=" << custom_effect_report.stage_count << "\n";
	output << "custom_effect_workers=" << custom_effect_report.worker_count_used << "\n";
	output << "custom_effect_summary=" << custom_effect_report.label << "\n";
	output << "granular_ratio=" << granular_shifter.GetPitchRatio() << "\n";
	output << "granular_peak=" << granular_stats.peak << "\n";
	output << "granular_rms=" << granular_stats.rms << "\n";
	output << "granular_report=" << granular_shifter.GetReport() << "\n";
	output << "synth_active_voices=" << synth_smoke.active_voice_count << "\n";
	output << "synth_resident_voices=" << synth_smoke.resident_voice_count << "\n";
	output << "synth_peak=" << synth_stats.peak << "\n";
	output << "synth_rms=" << synth_stats.rms << "\n";
	output << "synth_gain_min=" << synth_smoke.gain_min << "\n";
	output << "synth_gain_max=" << synth_smoke.gain_max << "\n";
	output << "synth_gain_average=" << synth_smoke.gain_average << "\n";
	output << "wav_float_roundtrip_frames=" << wav_float_roundtrip.size() << "\n";
	output << "aac_encoded_bytes=" << aac_bytes.size() << "\n";
	output << "aac_roundtrip_frames=" << aac_roundtrip.size() << "\n";
	output << "ogg_encoded_bytes=" << ogg_bytes.size() << "\n";
	output << "ogg_roundtrip_frames=" << ogg_roundtrip.size() << "\n";
	output << "id3_title=" << parsed_tag.title << "\n";
	output << "id3_artist=" << parsed_tag.artist << "\n";
	output << "id3_album=" << parsed_tag.album << "\n";
	for (size_t index = 0; index < custom_effect_report.stage_reports.size(); ++index) {
		output << "custom_effect_stage_" << index << "=" << custom_effect_report.stage_reports[index] << "\n";
	}
	for (size_t index = 0; index < custom_effect_report.node_reports.size(); ++index) {
		output << "custom_effect_node_" << index << "=" << custom_effect_report.node_reports[index] << "\n";
	}
	output << "smoke_status=pass\n";

	if (!options.output_dir.empty()) {
		output << "output_dir=" << options.output_dir.string() << "\n";
	}

	*report = output.str();
	return true;
}

}  // namespace XER::Audio::Demo