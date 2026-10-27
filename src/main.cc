
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <json/json.h>
#include <absl/strings/str_cat.h>
#include <iterator>
#include <sstream>
#include <string>
#include <cstdlib>
#include <system_error>
#include <utility>
#include <vector>

#include "app_command.h"
#include "audio/analysis_ai/anal_onset_detection.h"
#include "audio/analysis_ai/anal_spectrogram_generator.h"
#include "audio/demo/audio_analysis_smoke.h"
#include "audio/audio_core/audio_source_loader.h"
#include "audio/demo/audio_fx_custom.h"
#include "audio/demo/audio_modules_smoke.h"
#include "audio/file_io_codecs/codec_wav_pcm.h"
#include "audio/demo/audio_dsp_demo.h"
#include "audio_config_helper.h"
#include "cache/cache_configuration.h"
#include "ecosystem/ecosystem_manifest_loader.h"
#include "engine_params.h"
#include "error_handler/err_capture.h"
#include "error_handler/err_monitor.h"
#include "flux/terminal/terminal_output_renderer.h"
#include "flowscript/flow_script.h"
#include "helper/string.h"
#include "json_config_helper.h"
#include "provider.h"
#include "runtime_safety/safe_integrity_check.h"
#include "compilerapi/runtime_live.h"
#include "video/music_bass_eq_clip.h"
#include "watch/watch_live_updatex_script.h"
#include "xer/xer_encode.h"
#include "xer/xer_script.h"

namespace {

void WriteOutput(flux::terminal::OutputStream stream, const std::string& text) {
	flux::terminal::Write(stream, text);
}

void WriteOutputLine(flux::terminal::OutputStream stream, const std::string& text) {
	flux::terminal::WriteLine(stream, text);
}

void WriteStdout(const std::string& text) {
	WriteOutput(flux::terminal::OutputStream::kStdout, text);
}

void WriteStdoutLine(const std::string& text) {
	WriteOutputLine(flux::terminal::OutputStream::kStdout, text);
}

void WriteStderrLine(const std::string& text) {
	WriteOutputLine(flux::terminal::OutputStream::kStderr, text);
}

void WriteStderr(const std::string& text) {
	WriteOutput(flux::terminal::OutputStream::kStderr, text);
}

bool WriteStdoutBytes(const std::vector<std::uint8_t>& bytes) {
	if (bytes.empty()) {
		return false;
	}
	std::cout.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	std::cout.flush();
	return static_cast<bool>(std::cout);
}

std::filesystem::path MakeTempMp3Path() {
	const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	return std::filesystem::temp_directory_path() / ("xer_pipe_" + std::to_string(now) + ".mp3");
}

class TempPathGuard {
public:
	TempPathGuard() = default;
	explicit TempPathGuard(std::filesystem::path path) : path_(std::move(path)) {}
	~TempPathGuard() {
		if (path_.empty()) {
			return;
		}
		std::error_code error;
		std::filesystem::remove(path_, error);
	}
	const std::filesystem::path& path() const { return path_; }
	void Reset(std::filesystem::path path) { path_ = std::move(path); }

private:
	std::filesystem::path path_;
};

bool ReadStdinMp3ToTempFile(TempPathGuard* guard, std::string* path_out, std::string* error_out) {
	if (guard == nullptr || path_out == nullptr) {
		if (error_out != nullptr) {
			*error_out = "stdin MP3 target is invalid";
		}
		return false;
	}
	std::vector<char> bytes((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
	if (bytes.empty()) {
		if (error_out != nullptr) {
			*error_out = "stdin MP3 stream is empty";
		}
		return false;
	}

	guard->Reset(MakeTempMp3Path());
	std::ofstream output(guard->path(), std::ios::binary);
	if (!output.is_open()) {
		if (error_out != nullptr) {
			*error_out = "failed to create temporary MP3 pipe input";
		}
		return false;
	}
	output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	if (!output) {
		if (error_out != nullptr) {
			*error_out = "failed to write temporary MP3 pipe input";
		}
		return false;
	}
	*path_out = guard->path().string();
	return true;
}

bool PrepareAudioFxInputPath(
	const AppCommand::Parsed& parsed,
	TempPathGuard* stdin_guard,
	std::string* input_path_out,
	std::string* error_out) {
	if (input_path_out == nullptr) {
		if (error_out != nullptr) {
			*error_out = "audio FX input path target is invalid";
		}
		return false;
	}
	if (parsed.audio_input_path == "-" || (parsed.audio_pipe_mp3 && parsed.audio_input_path.empty())) {
		return ReadStdinMp3ToTempFile(stdin_guard, input_path_out, error_out);
	}
	*input_path_out = parsed.audio_input_path;
	return true;
}

void SetEnvValue(const char* key, const std::string& value) {
	setenv(key, value.c_str(), 1);
}

void SetEnvFlag(const char* key, bool enabled) {
	if (!enabled) {
		return;
	}
	setenv(key, "1", 1);
}

// Parse a simple KEY=VALUE .env file and set each variable in the environment.
// Lines starting with '#' and empty lines are ignored.
void LoadEnvFile(const std::string& path) {
	std::ifstream f(path);
	if (!f.is_open()) {
		WriteStderrLine(absl::StrCat("[warn] --env_file not found: ", path));
		return;
	}
	std::string line;
	while (std::getline(f, line)) {
		if (line.empty() || line[0] == '#') continue;
		const auto eq = line.find('=');
		if (eq == std::string::npos) continue;
		const std::string key   = line.substr(0, eq);
		const std::string value = line.substr(eq + 1);
		// Do not overwrite existing env (0 = no-overwrite).
		setenv(key.c_str(), value.c_str(), 0);
	}
}

void ExportParsedToEnv(const AppCommand::Parsed& p) {
	// Script
	SetEnvValue("ENGINE_SCRIPT_PATH", p.script_path);

	// Security
	SetEnvFlag("ENGINE_SANDBOX", p.sandbox);

	// Emit
	SetEnvFlag("ENGINE_NOEMIT",          p.noemit);
	SetEnvFlag("ENGINE_EMIT_SOURCE_MAP", p.emit_source_map);
	if (!p.xer_key.empty()) SetEnvValue("ENGINE_XER_KEY", p.xer_key);
	if (!p.xer_key_file.empty()) SetEnvValue("ENGINE_XER_KEY_FILE", p.xer_key_file);
	if (!p.xer_key_env.empty()) SetEnvValue("ENGINE_XER_KEY_ENV", p.xer_key_env);

	// Compiler / logging
	SetEnvFlag("ENGINE_DETAILS_COMPILER", p.details_compiler);
	SetEnvFlag("ENGINE_VERBOSE",          p.verbose);
	if (!p.log_level.empty()) SetEnvValue("ENGINE_LOG_LEVEL", p.log_level);
	if (!p.log_file.empty())  SetEnvValue("ENGINE_LOG_FILE",  p.log_file);

	// Debug
	SetEnvFlag("ENGINE_DEBUG",              p.debug);
	SetEnvFlag("ENGINE_INSPECT",            p.inspect);
	SetEnvFlag("ENGINE_BREAK_ON_FIRST_LINE", p.break_on_first_line);
	SetEnvFlag("ENGINE_DUMP_AST",           p.dump_ast);
	SetEnvFlag("ENGINE_DUMP_BYTECODE",      p.dump_bytecode);
	if (p.inspect) SetEnvValue("ENGINE_INSPECT_PORT", std::to_string(p.inspect_port));

	// Cache
	SetEnvFlag("ENGINE_REQUIRE_NO_CACHE", p.nocacherequire);
	SetEnvFlag("ENGINE_CACHE_CLEAN",      p.cache_clean);
	SetEnvFlag("ENGINE_CACHE_READONLY",   p.cache_readonly);
	if (!p.cache_dir.empty()) SetEnvValue("ENGINE_CACHE_DIR", p.cache_dir);

	// Diff
	SetEnvFlag("ENGINE_PRINT_DIFF", p.print_diff);
	SetEnvFlag("ENGINE_DIFF_ONLY",  p.diff_only);

	// Threading / resources
	if (p.max_cpu_threads > 0)      SetEnvValue("ENGINE_MAX_CPU_THREADS",      std::to_string(p.max_cpu_threads));
	if (p.max_memory_used > 0)      SetEnvValue("ENGINE_MAX_MEMORY_USED",      std::to_string(p.max_memory_used));
	if (p.timeout_seconds > 0)      SetEnvValue("ENGINE_TIMEOUT_SECONDS",      std::to_string(p.timeout_seconds));
	if (p.async_io_workers > 0)     SetEnvValue("ENGINE_ASYNC_IO_WORKERS",     std::to_string(p.async_io_workers));
	if (p.async_io_queue_depth > 0) SetEnvValue("ENGINE_ASYNC_IO_QUEUE_DEPTH", std::to_string(p.async_io_queue_depth));

	// CPU fine-grained
	if (!p.cpu_affinity.empty())      SetEnvValue("ENGINE_CPU_AFFINITY",         p.cpu_affinity);
	if (p.thread_priority != 0)       SetEnvValue("ENGINE_THREAD_PRIORITY",      std::to_string(p.thread_priority));
	if (p.v8_platform_workers > 0)    SetEnvValue("ENGINE_V8_PLATFORM_WORKERS",  std::to_string(p.v8_platform_workers));

	// Memory fine-grained
	if (p.memory_hard_limit_mib > 0)    SetEnvValue("ENGINE_MEMORY_HARD_LIMIT_MIB",         std::to_string(p.memory_hard_limit_mib));
	if (p.memory_warn_mib > 0)          SetEnvValue("ENGINE_MEMORY_WARN_MIB",               std::to_string(p.memory_warn_mib));
	if (p.memory_check_interval_ms > 0) SetEnvValue("ENGINE_RESOURCE_CHECK_INTERVAL_MS",    std::to_string(p.memory_check_interval_ms));

	// Output formatting
	SetEnvFlag("ENGINE_STACK_FORMATING_NO_COLOR", p.stack_formating_no_color);
	SetEnvFlag("ENGINE_COMPACT_ERRORS",           p.compact_errors);
	SetEnvFlag("ENGINE_NO_SOURCE_EXCERPT",        p.no_source_excerpt);
	SetEnvFlag("ENGINE_TIMESTAMPS",               p.timestamps);
	if (p.stack_formating_no_color) setenv("NO_COLOR", "1", 1);

	// Module system
	SetEnvFlag("ENGINE_STRICT_REQUIRE",       p.strict_require);
	SetEnvFlag("ENGINE_ALLOW_REMOTE_REQUIRE", p.allow_remote_require);
	if (!p.module_root.empty()) SetEnvValue("ENGINE_MODULE_ROOT", p.module_root);

	// TypeScript
	if (!p.ts_compiler.empty()) SetEnvValue("ENGINE_TS_COMPILER", p.ts_compiler);
	SetEnvFlag("ENGINE_TS_STRICT",   p.ts_strict);
	SetEnvFlag("ENGINE_TS_NO_CHECK", p.ts_no_check);
	if (!p.ts_target.empty()) SetEnvValue("ENGINE_TS_TARGET", p.ts_target);

	// Environment
	SetEnvFlag("ENGINE_NO_INHERIT_ENV", p.no_inherit_env);
	if (!p.env_file.empty()) SetEnvValue("ENGINE_ENV_FILE", p.env_file);

	// Doctor
	SetEnvFlag("ENGINE_DOCTOR_VERBOSE", p.doctor_verbose);
}

bool ParseCommandFromTokens(const std::vector<std::string>& tokens,
                            AppCommand::Parsed* out,
                            std::string* error_message) {
	if (out == nullptr) {
		if (error_message) {
			*error_message = "Internal error: ParseCommandFromTokens target is null";
		}
		return false;
	}

	std::vector<std::string> storage = tokens;
	if (storage.empty()) {
		storage.emplace_back("EngineBuilder");
	}

	std::vector<char*> argv_like;
	argv_like = storage
		| ranges::views::transform([](std::string& token) {
			return token.data();
		})
		| ranges::to<std::vector<char*>>();

	*out = AppCommand::Parse(static_cast<int>(argv_like.size()), argv_like.data());
	if (!out->valid && error_message) {
		*error_message = out->error_message;
	}
	return out->valid;
}

bool WriteFloatVectorBinary(
	const std::filesystem::path& path,
	const std::vector<float>& values,
	std::string* error_out) {
	std::ofstream output(path, std::ios::binary);
	if (!output.is_open()) {
		if (error_out != nullptr) {
			*error_out = "Failed to open output file: " + path.string();
		}
		return false;
	}

	if (!values.empty()) {
		output.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(float)));
	}
	if (!output) {
		if (error_out != nullptr) {
			*error_out = "Failed to write output file: " + path.string();
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
			*error_out = "Failed to open text output file: " + path.string();
		}
		return false;
	}

	output << text;
	if (!output) {
		if (error_out != nullptr) {
			*error_out = "Failed to write text output file: " + path.string();
		}
		return false;
	}
	return true;
}

bool WriteWaveFile(
	const std::filesystem::path& path,
	const std::vector<float>& samples,
	int sample_rate,
	std::string* error_out) {
	Engine::Audio::CodecIO::WavPcmCodec codec;
	std::vector<std::uint8_t> encoded;
	if (!codec.Encode16(samples.data(), samples.size(), encoded, sample_rate)) {
		if (error_out != nullptr) {
			*error_out = "Failed to encode wave output";
		}
		return false;
	}

	std::ofstream output(path, std::ios::binary);
	if (!output.is_open()) {
		if (error_out != nullptr) {
			*error_out = "Failed to open wave output file: " + path.string();
		}
		return false;
	}

	output.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
	if (!output) {
		if (error_out != nullptr) {
			*error_out = "Failed to write wave output file: " + path.string();
		}
		return false;
	}
	return true;
}

struct AudioSignalStats {
	double rms = 0.0;
	float peak = 0.0f;
};

AudioSignalStats ComputeAudioSignalStats(const std::vector<float>& samples) {
	AudioSignalStats stats;
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

double SecondsFromFrames(size_t frame_count, int sample_rate) {
	if (sample_rate <= 0) {
		return 0.0;
	}
	return static_cast<double>(frame_count) / static_cast<double>(sample_rate);
}

Json::Value BuildAudioInspectReportJson(
	const AppCommand::Parsed& parsed,
	const Engine::Audio::Core::AudioSourceBuffer& audio_buffer,
	const AudioSignalStats& stats) {
	Json::Value report(Json::objectValue);
	report["command"] = "audio_inspect";
	report["input_path"] = parsed.audio_input_path;

	Json::Value source(Json::objectValue);
	source["format"] = audio_buffer.source_format.empty() ? "unknown" : audio_buffer.source_format;
	source["codec"] = audio_buffer.codec_name.empty() ? "unknown" : audio_buffer.codec_name;
	source["decode_backend"] = audio_buffer.decode_backend.empty() ? "unknown" : audio_buffer.decode_backend;
	source["sample_rate"] = audio_buffer.original_sample_rate;
	source["channels"] = audio_buffer.original_channels;
	source["frames"] = static_cast<Json::UInt64>(audio_buffer.original_frame_count);
	source["duration_seconds"] = SecondsFromFrames(audio_buffer.original_frame_count, audio_buffer.original_sample_rate);
	report["source"] = source;

	Json::Value normalized(Json::objectValue);
	normalized["sample_rate"] = audio_buffer.sample_rate;
	normalized["channels"] = audio_buffer.channels;
	normalized["frames"] = static_cast<Json::UInt64>(audio_buffer.frame_count);
	normalized["duration_seconds"] = SecondsFromFrames(audio_buffer.frame_count, audio_buffer.sample_rate);
	normalized["peak"] = stats.peak;
	normalized["rms"] = stats.rms;
	report["normalized"] = normalized;

	Json::Value options(Json::objectValue);
	options["target_sample_rate"] = parsed.target_sample_rate;
	options["raw_sample_rate"] = parsed.audio_raw_sample_rate;
	options["json_output"] = parsed.json_output;
	report["options"] = options;

	return report;
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

std::string BuildAudioInspectTextReport(const Json::Value& report) {
	std::ostringstream output;
	output << std::fixed << std::setprecision(6);
	output << "Audio Inspect CLI\n";
	output << "input_path=" << report["input_path"].asString() << "\n";
	output << "source_format=" << report["source"]["format"].asString() << "\n";
	output << "codec=" << report["source"]["codec"].asString() << "\n";
	output << "decode_backend=" << report["source"]["decode_backend"].asString() << "\n";
	output << "original_sample_rate=" << report["source"]["sample_rate"].asInt() << "\n";
	output << "original_channels=" << report["source"]["channels"].asInt() << "\n";
	output << "original_frames=" << report["source"]["frames"].asUInt64() << "\n";
	output << "original_duration_seconds=" << report["source"]["duration_seconds"].asDouble() << "\n";
	output << "sample_rate=" << report["normalized"]["sample_rate"].asInt() << "\n";
	output << "normalized_channels=" << report["normalized"]["channels"].asInt() << "\n";
	output << "normalized_frames=" << report["normalized"]["frames"].asUInt64() << "\n";
	output << "normalized_duration_seconds=" << report["normalized"]["duration_seconds"].asDouble() << "\n";
	output << "normalized_peak=" << report["normalized"]["peak"].asDouble() << "\n";
	output << "normalized_rms=" << report["normalized"]["rms"].asDouble() << "\n";
	output << "target_sample_rate=" << report["options"]["target_sample_rate"].asInt() << "\n";
	const Json::Value& artifacts = report["artifacts"];
	if (artifacts.isObject()) {
		if (artifacts.isMember("report_path")) {
			output << "report_path=" << artifacts["report_path"].asString() << "\n";
		}
		if (artifacts.isMember("normalized_wav")) {
			output << "normalized_wav=" << artifacts["normalized_wav"].asString() << "\n";
		}
	}
	return output.str();
}

bool WriteOnsetTimes(
	const std::filesystem::path& path,
	const std::vector<size_t>& onset_frames,
	const std::vector<double>& onset_times,
	std::string* error_out) {
	std::ofstream output(path);
	if (!output.is_open()) {
		if (error_out != nullptr) {
			*error_out = "Failed to open onset output file: " + path.string();
		}
		return false;
	}

	output << "frame,time_seconds\n";
	output << std::fixed << std::setprecision(6);
	for (size_t index = 0; index < onset_frames.size() && index < onset_times.size(); ++index) {
		output << onset_frames[index] << "," << onset_times[index] << "\n";
	}
	if (!output) {
		if (error_out != nullptr) {
			*error_out = "Failed to write onset output file: " + path.string();
		}
		return false;
	}
	return true;
}

Engine::Audio::Core::AudioSourceLoadOptions BuildAudioLoadOptions(const AppCommand::Parsed& parsed) {
	Engine::Audio::Core::AudioSourceLoadOptions load_options;
	load_options.input_path = parsed.audio_input_path.empty()
		? std::filesystem::path()
		: std::filesystem::path(parsed.audio_input_path);
	load_options.raw_sample_rate = parsed.audio_raw_sample_rate;
	load_options.target_sample_rate = parsed.target_sample_rate;
	return load_options;
}

}  // namespace

int main(int argc, char** argv) {
	Engine::ErrorHandler::CaptureConfiguration initial_capture_config;
	initial_capture_config.dump_dir = ".";
	Engine::ErrorHandler::InitializeCrashCapture(initial_capture_config);

	AppCommand::Parsed parsed = AppCommand::Parse(argc, argv);
	if (!parsed.valid) {
		WriteStderrLine(parsed.error_message);
		WriteStderrLine(AppCommand::BuildHelpText(argv[0]));
		return 2;
	}

	const bool cli_watch = parsed.watch;
	const int cli_watch_interval_ms = parsed.watch_interval_ms;
	if (!parsed.ecosystem_manifest_path.empty()) {
		EcoSystemManifestLoader loader(parsed.ecosystem_manifest_path);
		EcoSystemManifest manifest;
		std::string ecosystem_error;
		if (!loader.Load(&manifest, &ecosystem_error)) {
			WriteStderrLine(absl::StrCat("Failed to load ecosystem manifest: ", ecosystem_error));
			return 2;
		}

		const std::vector<std::string> manifest_tokens = EcoSystemManifestLoader::BuildArgv(manifest);
		AppCommand::Parsed manifest_parsed;
		if (!ParseCommandFromTokens(manifest_tokens, &manifest_parsed, &ecosystem_error)) {
			WriteStderrLine(absl::StrCat("Invalid manifest CLI args: ", ecosystem_error));
			return 2;
		}

		parsed = manifest_parsed;
		if (cli_watch) {
			parsed.watch = true;
			parsed.watch_interval_ms = cli_watch_interval_ms;
		}
	}

	if (!parsed.audio_config_file.empty()) {
		std::string audio_config_error;
		if (!AudioConfigHelper::ApplyAudioConfigFile(&parsed, &audio_config_error)) {
			WriteStderrLine(absl::StrCat("Audio config failed: ", audio_config_error));
			return 2;
		}
	}

	if (parsed.type == AppCommand::Type::kHelp) {
		WriteStdoutLine(AppCommand::BuildHelpText(argv[0]));
		return 0;
	}

	if (parsed.type == AppCommand::Type::kVersion) {
		WriteStdoutLine("EngineBuilder version 1.1.0");
		return 0;
	}

	if (parsed.type == AppCommand::Type::kDoctor) {
		const bool has_default_script = std::filesystem::exists("example/project.js");
		const bool has_build_dir      = std::filesystem::exists("build");
		std::ostringstream doctor_output;
		doctor_output << "Doctor summary:\n";
		doctor_output << "  example/project.js : " << (has_default_script ? "ok" : "missing") << "\n";
		doctor_output << "  build directory    : " << (has_build_dir      ? "ok" : "missing") << "\n";
		if (parsed.doctor_verbose) {
			// Extra dependency checks.
			const bool has_node = std::filesystem::exists("/usr/bin/node") ||
			                      std::filesystem::exists("/usr/local/bin/node");
			const bool has_tsc  = std::filesystem::exists("/usr/bin/tsc") ||
			                      std::filesystem::exists("/usr/local/bin/tsc");
			doctor_output << "  node               : " << (has_node ? "ok" : "missing") << "\n";
			doctor_output << "  tsc                : " << (has_tsc  ? "ok" : "missing") << "\n";
		}
		WriteStdout(doctor_output.str());
		return (has_default_script && has_build_dir) ? 0 : 1;
	}

	if (parsed.type == AppCommand::Type::kAudioDemo) {
		Engine::Audio::Demo::AudioDSPDemoOptions demo_options;
		demo_options.output_dir = parsed.output_dir.empty()
			? std::filesystem::path("out/audio_demo")
			: std::filesystem::path(parsed.output_dir);
		demo_options.input_path = parsed.audio_input_path.empty()
			? std::filesystem::path()
			: std::filesystem::path(parsed.audio_input_path);
		demo_options.processor = parsed.audio_processor;
		demo_options.phase_vocoder_ratio = parsed.audio_stretch_ratio;
		demo_options.spectral_shaper_profile = parsed.audio_shaper_profile;
		demo_options.raw_sample_rate = parsed.audio_raw_sample_rate;
		demo_options.target_sample_rate = parsed.target_sample_rate;
		std::string report;
		std::string demo_error;
		if (!Engine::Audio::Demo::RunAudioDSPDemo(demo_options, &report, &demo_error)) {
			WriteStderrLine(absl::StrCat("Audio demo failed: ", demo_error));
			return 1;
		}
		WriteStdout(report);
		return 0;
	}

	if (parsed.type == AppCommand::Type::kAudioFxCustom) {
		TempPathGuard stdin_guard;
		std::string input_path;
		std::string input_error;
		if (!PrepareAudioFxInputPath(parsed, &stdin_guard, &input_path, &input_error)) {
			WriteStderrLine(absl::StrCat("Audio custom fx failed: ", input_error));
			return 1;
		}
		Engine::Audio::Demo::AudioFxCustomOptions fx_options;
		fx_options.output_dir = parsed.output_dir.empty()
			? std::filesystem::path("out/audio_fx_custom")
			: std::filesystem::path(parsed.output_dir);
		fx_options.input_path = input_path.empty()
			? std::filesystem::path()
			: std::filesystem::path(input_path);
		fx_options.effect_name = parsed.audio_effect_name;
		fx_options.clap_plugin_reference = parsed.audio_clap_plugin_reference;
		fx_options.clap_plugin_references = parsed.audio_clap_plugin_references;
		fx_options.raw_sample_rate = parsed.audio_raw_sample_rate;
		fx_options.target_sample_rate = parsed.target_sample_rate;
		fx_options.target_channels = parsed.audio_target_channels;
		fx_options.batch_mode = parsed.audio_batch_mode;
		fx_options.json_summary = parsed.json_output;
		fx_options.strict_mp3_input = true;
		fx_options.pipe_mp3_output = parsed.audio_pipe_mp3;
		fx_options.write_intermediate_wavs = !parsed.audio_pipe_mp3;
		std::string report;
		std::string fx_error;
		std::vector<std::uint8_t> mp3_output;
		if (!Engine::Audio::Demo::RunAudioFxCustom(fx_options, &report, &fx_error, parsed.audio_pipe_mp3 ? &mp3_output : nullptr)) {
			WriteStderrLine(absl::StrCat("Audio custom fx failed: ", fx_error));
			return 1;
		}
		if (parsed.audio_pipe_mp3) {
			WriteStderr(report);
			return WriteStdoutBytes(mp3_output) ? 0 : 1;
		}
		WriteStdout(report);
		return 0;
	}

	if (parsed.type == AppCommand::Type::kAudioFxBatch) {
		TempPathGuard stdin_guard;
		std::string input_path;
		std::string input_error;
		if (!PrepareAudioFxInputPath(parsed, &stdin_guard, &input_path, &input_error)) {
			WriteStderrLine(absl::StrCat("Audio custom fx batch failed: ", input_error));
			return 1;
		}
		Engine::Audio::Demo::AudioFxCustomOptions fx_options;
		fx_options.output_dir = parsed.output_dir.empty()
			? std::filesystem::path("out/audio_fx_batch")
			: std::filesystem::path(parsed.output_dir);
		fx_options.input_path = input_path.empty()
			? std::filesystem::path()
			: std::filesystem::path(input_path);
		fx_options.effect_names = parsed.audio_effect_names;
		fx_options.clap_plugin_reference = parsed.audio_clap_plugin_reference;
		fx_options.clap_plugin_references = parsed.audio_clap_plugin_references;
		fx_options.raw_sample_rate = parsed.audio_raw_sample_rate;
		fx_options.target_sample_rate = parsed.target_sample_rate;
		fx_options.target_channels = parsed.audio_target_channels;
		fx_options.batch_mode = parsed.audio_batch_mode;
		fx_options.json_summary = parsed.json_output;
		fx_options.strict_mp3_input = true;
		fx_options.pipe_mp3_output = parsed.audio_pipe_mp3;
		fx_options.write_intermediate_wavs = !parsed.audio_pipe_mp3;
		std::string report;
		std::string fx_error;
		std::vector<std::uint8_t> mp3_output;
		if (!Engine::Audio::Demo::RunAudioFxBatch(fx_options, &report, &fx_error, parsed.audio_pipe_mp3 ? &mp3_output : nullptr)) {
			WriteStderrLine(absl::StrCat("Audio custom fx batch failed: ", fx_error));
			return 1;
		}
		if (parsed.audio_pipe_mp3) {
			WriteStderr(report);
			return WriteStdoutBytes(mp3_output) ? 0 : 1;
		}
		WriteStdout(report);
		return 0;
	}

	if (parsed.type == AppCommand::Type::kAudioModulesSmoke) {
		Engine::Audio::Demo::AudioModulesSmokeOptions smoke_options;
		smoke_options.input_path = parsed.audio_input_path.empty()
			? std::filesystem::path()
			: std::filesystem::path(parsed.audio_input_path);
		smoke_options.output_dir = parsed.output_dir.empty()
			? std::filesystem::path("out/audio_modules_smoke")
			: std::filesystem::path(parsed.output_dir);

		std::string report;
		std::string smoke_error;
		if (!Engine::Audio::Demo::RunAudioModulesSmoke(smoke_options, &report, &smoke_error)) {
			WriteStderrLine(absl::StrCat("Audio modules smoke failed: ", smoke_error));
			return 1;
		}
		WriteStdout(report);
		return 0;
	}

	if (parsed.type == AppCommand::Type::kAudioAnalysisSmoke) {
		Engine::Audio::Demo::AudioAnalysisSmokeOptions analysis_options;
		analysis_options.input_path = parsed.audio_input_path.empty()
			? std::filesystem::path()
			: std::filesystem::path(parsed.audio_input_path);
		analysis_options.output_dir = parsed.output_dir.empty()
			? std::filesystem::path("out/audio_analysis_smoke")
			: std::filesystem::path(parsed.output_dir);
		analysis_options.raw_sample_rate = parsed.audio_raw_sample_rate;
		analysis_options.target_sample_rate = parsed.target_sample_rate;

		std::string report;
		std::string analysis_error;
		if (!Engine::Audio::Demo::RunAudioAnalysisSmoke(analysis_options, &report, &analysis_error)) {
			WriteStderrLine(absl::StrCat("Audio analysis smoke failed: ", analysis_error));
			return 1;
		}
		WriteStdout(report);
		return 0;
	}

	if (parsed.type == AppCommand::Type::kMusicBassEqClip) {
		video::MusicBassEqClipOptions clip_options;
		clip_options.input_path = parsed.audio_input_path.empty()
			? std::filesystem::path()
			: std::filesystem::path(parsed.audio_input_path);
		clip_options.output_dir = parsed.output_dir.empty()
			? std::filesystem::path("out/music_bass_eq_clip")
			: std::filesystem::path(parsed.output_dir);
		clip_options.output_path = parsed.video_output_path.empty()
			? std::filesystem::path()
			: std::filesystem::path(parsed.video_output_path);
		clip_options.duration_seconds = parsed.clip_duration_seconds;
		clip_options.width = parsed.video_width;
		clip_options.height = parsed.video_height;
		clip_options.fps = parsed.video_fps;
		clip_options.target_sample_rate = parsed.target_sample_rate;

		video::MusicBassEqClipResult clip_result;
		std::string clip_error;
		if (!video::GenerateMusicBassEqClip(clip_options, &clip_result, &clip_error)) {
			WriteStderrLine(absl::StrCat("Music bass EQ clip failed: ", clip_error));
			return 1;
		}
		WriteStdout(video::BuildMusicBassEqClipReport(clip_options, clip_result));
		return 0;
	}

	if (parsed.type == AppCommand::Type::kAudioInspect) {
		if (parsed.audio_input_path.empty()) {
			WriteStderrLine("Audio inspect command requires an input audio path");
			return 2;
		}

		std::string inspect_error;
		const auto load_options = BuildAudioLoadOptions(parsed);
		Engine::Audio::Core::AudioSourceBuffer audio_buffer;
		if (!Engine::Audio::Core::AudioSourceLoader::Load(load_options, &audio_buffer, &inspect_error)) {
			WriteStderrLine(absl::StrCat("Audio inspect failed: ", inspect_error));
			return 1;
		}

		const AudioSignalStats stats = ComputeAudioSignalStats(audio_buffer.samples);
		Json::Value report_json = BuildAudioInspectReportJson(parsed, audio_buffer, stats);

		if (!parsed.output_dir.empty()) {
			const std::filesystem::path output_dir = parsed.output_dir;
			std::error_code fs_error;
			std::filesystem::create_directories(output_dir, fs_error);
			if (fs_error) {
				WriteStderrLine("Audio inspect failed: could not create output directory");
				return 1;
			}

			const std::filesystem::path report_path = output_dir / (parsed.json_output ? "audio_inspect.json" : "audio_inspect.txt");
			const std::filesystem::path wav_path = output_dir / "normalized_input.wav";
			report_json["artifacts"]["report_path"] = report_path.string();
			report_json["artifacts"]["normalized_wav"] = wav_path.string();
			const std::string serialized_report = parsed.json_output
				? SerializeJson(report_json)
				: BuildAudioInspectTextReport(report_json);
			std::string io_error;
			if (!WriteTextFile(report_path, serialized_report, &io_error)
				|| !WriteWaveFile(wav_path, audio_buffer.samples, audio_buffer.sample_rate, &io_error)) {
				WriteStderrLine(absl::StrCat("Audio inspect failed: ", io_error));
				return 1;
			}
		}

		WriteStdout(parsed.json_output ? SerializeJson(report_json) : BuildAudioInspectTextReport(report_json));
		return 0;
	}

	if (parsed.type == AppCommand::Type::kSpectrogram) {
		if (parsed.audio_input_path.empty()) {
			WriteStderrLine("Spectrogram command requires an input audio path");
			return 2;
		}

		const std::filesystem::path output_dir = parsed.output_dir.empty()
			? std::filesystem::path("out/audio_spectrogram")
			: std::filesystem::path(parsed.output_dir);
		std::error_code fs_error;
		std::filesystem::create_directories(output_dir, fs_error);
		if (fs_error) {
			WriteStderrLine("Failed to create spectrogram output directory");
			return 1;
		}

		Engine::Audio::AnalysisAI::SpectrogramGenerator generator;
		std::string analysis_error;
		const auto load_options = BuildAudioLoadOptions(parsed);
		if (!generator.GenerateFromFile(load_options, &analysis_error)) {
			WriteStderrLine(absl::StrCat("Spectrogram command failed: ", analysis_error));
			return 1;
		}

		const std::filesystem::path raw_output_path = output_dir / "spectrogram.raw";
		if (!generator.ExportAsRaw(raw_output_path.string())) {
			WriteStderrLine("Spectrogram command failed: could not export raw spectrogram");
			return 1;
		}

		std::ostringstream spectrogram_output;
		spectrogram_output << "Spectrogram CLI\n";
		spectrogram_output << "input_path=" << parsed.audio_input_path << "\n";
		spectrogram_output << "target_sample_rate=" << parsed.target_sample_rate << "\n";
		spectrogram_output << "frame_count=" << generator.GetFrameCount() << "\n";
		spectrogram_output << "bin_count=" << generator.GetBinCount() << "\n";
		spectrogram_output << "raw_output=" << raw_output_path.string() << "\n";
		spectrogram_output << generator.GetReport() << "\n";
		WriteStdout(spectrogram_output.str());
		return 0;
	}

	if (parsed.type == AppCommand::Type::kOnset) {
		if (parsed.audio_input_path.empty()) {
			WriteStderrLine("Onset command requires an input audio path");
			return 2;
		}

		const std::filesystem::path output_dir = parsed.output_dir.empty()
			? std::filesystem::path("out/audio_onset")
			: std::filesystem::path(parsed.output_dir);
		std::error_code fs_error;
		std::filesystem::create_directories(output_dir, fs_error);
		if (fs_error) {
			WriteStderrLine("Failed to create onset output directory");
			return 1;
		}

		Engine::Audio::AnalysisAI::OnsetDetector detector;
		std::string analysis_error;
		const auto load_options = BuildAudioLoadOptions(parsed);
		if (!detector.DetectOnsetsFromFile(load_options, &analysis_error)) {
			WriteStderrLine(absl::StrCat("Onset command failed: ", analysis_error));
			return 1;
		}

		const auto onset_times = detector.GetOnsetTimesSeconds(parsed.target_sample_rate);
		const std::filesystem::path onset_times_path = output_dir / "onsets.csv";
		const std::filesystem::path flux_curve_path = output_dir / "flux_curve.raw";
		if (!WriteOnsetTimes(onset_times_path, detector.GetOnsetFrames(), onset_times, &analysis_error)
			|| !WriteFloatVectorBinary(flux_curve_path, detector.GetFluxCurve(), &analysis_error)) {
			WriteStderrLine(absl::StrCat("Onset command failed: ", analysis_error));
			return 1;
		}

		std::ostringstream onset_output;
		onset_output << "Onset CLI\n";
		onset_output << "input_path=" << parsed.audio_input_path << "\n";
		onset_output << "target_sample_rate=" << parsed.target_sample_rate << "\n";
		onset_output << "onset_count=" << detector.GetOnsetFrames().size() << "\n";
		onset_output << "onset_times_csv=" << onset_times_path.string() << "\n";
		onset_output << "flux_curve_raw=" << flux_curve_path.string() << "\n";
		onset_output << detector.GetReport() << "\n";
		WriteStdout(onset_output.str());
		return 0;
	}

	// Load .env file first so command-line flags can override it.
	if (!parsed.env_file.empty()) {
		LoadEnvFile(parsed.env_file);
	}

	// Export all flags to environment before building EngineParams.
	ExportParsedToEnv(parsed);

	Engine::ErrorHandler::MonitorConfiguration monitor_config;
	monitor_config.verbose = parsed.verbose;
	monitor_config.timestamps = parsed.timestamps;
	monitor_config.persist_reports = true;
	monitor_config.log_level = parsed.log_level;
	Engine::ErrorHandler::InitializeMonitor(monitor_config);

	Engine::RuntimeSafety::StartupOptions startup_options;
	startup_options.script_path = parsed.script_path.empty()
		? std::filesystem::path()
		: std::filesystem::path(parsed.script_path);
	startup_options.sandbox = parsed.sandbox;
	startup_options.verbose = parsed.verbose;
	startup_options.async_io_workers = parsed.async_io_workers;
	startup_options.async_io_queue_depth = parsed.async_io_queue_depth;
	Engine::RuntimeSafety::StartupState startup_state;
	std::string startup_error;
	if (!Engine::RuntimeSafety::BootstrapStartup(startup_options, &startup_state, &startup_error)) {
		Engine::ErrorHandler::ReportStartupError("bootstrap", startup_error);
		return 1;
	}
	(void)startup_state;

	const std::string script_path = parsed.script_path;
	if (!std::filesystem::exists(script_path)) {
		WriteStderrLine(absl::StrCat("Script not found: ", script_path));
		return 1;
	}
	Engine::ErrorHandler::UpdateCrashContext(script_path, ".");

	if (parsed.type == AppCommand::Type::kInspect) {
		Xer::XerEncode encoder;
		std::string inspect_report;
		std::string inspect_error;
		if (!encoder.InspectFile(script_path, &inspect_report, &inspect_error)) {
			WriteStderrLine(absl::StrCat("Failed to inspect XER artifact: ", inspect_error));
			return 1;
		}
		WriteStdout(inspect_report);
		if (inspect_report.empty() || inspect_report.back() != '\n') {
			WriteStdout("\n");
		}
		return 0;
	}

	if (parsed.type == AppCommand::Type::kCompile) {
		const std::string extension = Helper::String::CanonicalizeToken(std::filesystem::path(script_path).extension().string());
		if (extension != ".xer") {
			WriteStderrLine(absl::StrCat("Compile command only supports .xer sources: ", script_path));
			return 1;
		}

		Xer::XerEncode encoder;
		std::string protection_error;
		const Xer::XerProtectionOptions protection = Xer::ResolveProtectionOptionsFromEnvironment(&protection_error);
		if (!protection_error.empty()) {
			WriteStderrLine(absl::StrCat("Failed to resolve XER encryption key: ", protection_error));
			return 1;
		}
		std::string compile_error;
		const Xer::XerEncodedBlock block = encoder.Compile(script_path, protection, &compile_error);
		if (!compile_error.empty()) {
			WriteStderrLine(absl::StrCat("Failed to compile .xer script: ", compile_error));
			return 1;
		}

		const std::filesystem::path output_dir = parsed.output_dir.empty()
			? std::filesystem::path()
			: std::filesystem::path(parsed.output_dir);
		if (!encoder.WriteArtifacts(script_path, block, output_dir, &compile_error)) {
			WriteStderrLine(absl::StrCat("Failed to write compile artifacts: ", compile_error));
			return 1;
		}

		const std::filesystem::path destination_dir = output_dir.empty()
			? std::filesystem::path(script_path).parent_path()
			: output_dir;
		const std::string stem = std::filesystem::path(script_path).stem().string();
		std::ostringstream compile_output;
		compile_output << "Compiled XER artifacts:\n";
		compile_output << "  " << (destination_dir / (stem + ".bin")).string() << "\n";
		compile_output << "  " << (destination_dir / (stem + ".bak")).string() << "\n";
		WriteStdout(compile_output.str());
		return 0;
	}

	const bool use_xer_runtime = XerScript::Supports(script_path);

	if (parsed.watch) {
		WatchLiveUpdatexScriptConfig watch_cfg;
		watch_cfg.script_path = script_path;
		watch_cfg.poll_interval_ms = parsed.watch_interval_ms;
		WatchLiveUpdatexScript watcher(std::move(watch_cfg));
		const int watch_code = watcher.Run();
		if (!use_xer_runtime) {
			FlowScript::Shutdown();
		}
		return watch_code;
	}

	if (use_xer_runtime) {
		XerScript script(script_path);
		if (!script.Run()) {
			WriteStderrLine(absl::StrCat("Failed to run .xer script: ", script_path));
			return 1;
		}
		return 0;
	}

	FlowScript script(script_path);
	if (!script.Run()) {
		WriteStderrLine(absl::StrCat("Failed to run script: ", script_path));
		FlowScript::Shutdown();
		return 1;
	}

	FlowScript::Shutdown();
	return 0;
}
