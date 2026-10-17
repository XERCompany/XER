#include "app_command.h"

#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>

namespace {

bool IsFlag(const std::string& arg, const std::string& long_flag, const std::string& short_flag = {}) {
    return arg == long_flag || (!short_flag.empty() && arg == short_flag);
}

bool ParsePositiveInt(const std::string& text, int* out) {
    if (text.empty()) {
        return false;
    }

    int value = 0;
    for (char ch : text) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
        value = value * 10 + (ch - '0');
    }
    if (value <= 0) {
        return false;
    }

    *out = value;
    return true;
}

bool ParsePositiveFloat(const std::string& text, float* out) {
    if (text.empty()) {
        return false;
    }

    char* end = nullptr;
    errno = 0;
    const float value = std::strtof(text.c_str(), &end);
    if (errno != 0 || end == text.c_str() || end == nullptr || *end != '\0' || !std::isfinite(value) || value <= 0.0f) {
        return false;
    }

    *out = value;
    return true;
}

void AppendTrimmedCsvValues(const std::string& text, std::vector<std::string>* output) {
    if (output == nullptr) {
        return;
    }
    std::string token;
    auto flush_token = [&token, output]() {
        const auto first = token.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            token.clear();
            return;
        }
        const auto last = token.find_last_not_of(" \t\r\n");
        output->push_back(token.substr(first, last - first + 1));
        token.clear();
    };
    for (char ch : text) {
        if (ch == ',') {
            flush_token();
            continue;
        }
        token.push_back(ch);
    }
    flush_token();
}

// Matches both "--flag value" and "--flag=value" forms.
// Returns true if the flag matched.
// If --flag=value form matched, *value_out is filled and *consumed_inline = true.
// Otherwise *consumed_inline = false; caller must consume next argv element.
bool ParseValueFlag(const std::string& arg,
                    const std::string& flag,
                    std::string* value_out,
                    bool* consumed_inline_value) {
    if (arg == flag) {
        *consumed_inline_value = false;
        return true;
    }

    const std::string with_eq = flag + "=";
    if (arg.rfind(with_eq, 0) == 0) {
        *value_out = arg.substr(with_eq.size());
        *consumed_inline_value = true;
        return true;
    }

    return false;
}

// Consume a mandatory string value for a value-flag.
// Returns false and sets error when value is missing.
bool ConsumeStringValue(const std::string& flag_name,
                        int argc, char** argv,
                        size_t* i,
                        std::string* out,
                        AppCommand::Parsed* parsed) {
    if (*i + 1 >= static_cast<size_t>(argc) || argv[*i + 1] == nullptr) {
        parsed->valid = false;
        parsed->error_message = "Expected value after " + flag_name;
        return false;
    }
    *out = argv[++(*i)];
    return true;
}

// Consume a mandatory positive-integer value for a value-flag.
bool ConsumeIntValue(const std::string& flag_name,
                     int argc, char** argv,
                     size_t* i,
                     int* out,
                     AppCommand::Parsed* parsed) {
    std::string text;
    if (!ConsumeStringValue(flag_name, argc, argv, i, &text, parsed)) {
        return false;
    }
    if (!ParsePositiveInt(text, out)) {
        parsed->valid = false;
        parsed->error_message = "Invalid positive integer for " + flag_name + ": " + text;
        return false;
    }
    return true;
}

}  // namespace

AppCommand::Parsed AppCommand::Parse(int argc, char** argv) {
    Parsed parsed;
    if (argc <= 1) {
        return parsed;
    }

    const std::string command = argv[1] != nullptr ? std::string(argv[1]) : std::string();

    if (command == "help" || command == "--help" || command == "-h") {
        parsed.type = Type::kHelp;
        return parsed;
    }

    if (command == "version" || command == "--version" || command == "-v") {
        parsed.type = Type::kVersion;
        return parsed;
    }

    if (command == "compile") {
        parsed.type = Type::kCompile;
    }

    if (command == "inspect") {
        parsed.type = Type::kInspect;
    }

    if (command == "audio_inspect") {
        parsed.type = Type::kAudioInspect;
    }

    if (command == "audio_fx_custom") {
		parsed.type = Type::kAudioFxCustom;
	}

    if (command == "audio_fx_batch") {
        parsed.type = Type::kAudioFxBatch;
    }

    if (command == "audio_modules_smoke") {
		parsed.type = Type::kAudioModulesSmoke;
	}

    if (command == "audio_analysis_smoke") {
        parsed.type = Type::kAudioAnalysisSmoke;
    }

    if (command == "music_bass_eq_clip" || command == "video_music_clip") {
        parsed.type = Type::kMusicBassEqClip;
    }

    if (command == "audio_demo") {
        parsed.type = Type::kAudioDemo;
    }

    if (command == "spectrogram") {
        parsed.type = Type::kSpectrogram;
    }

    if (command == "onset") {
        parsed.type = Type::kOnset;
    }

    if (command == "doctor") {
        parsed.type = Type::kDoctor;
        // allow --verbose after doctor
    }

    size_t script_index = 1;
    if (command == "run") {
        parsed.type = Type::kRun;
        script_index = 2;
    } else if (command == "compile") {
        parsed.type = Type::kCompile;
        script_index = 2;
    } else if (command == "inspect") {
        parsed.type = Type::kInspect;
        script_index = 2;
    } else if (command == "audio_inspect") {
        parsed.type = Type::kAudioInspect;
        script_index = 2;
	} else if (command == "audio_fx_custom") {
		parsed.type = Type::kAudioFxCustom;
		script_index = 2;
    } else if (command == "audio_fx_batch") {
        parsed.type = Type::kAudioFxBatch;
        script_index = 2;
        } else if (command == "audio_modules_smoke") {
		parsed.type = Type::kAudioModulesSmoke;
		script_index = 2;
        } else if (command == "audio_analysis_smoke") {
		parsed.type = Type::kAudioAnalysisSmoke;
		script_index = 2;
    } else if (command == "music_bass_eq_clip" || command == "video_music_clip") {
        parsed.type = Type::kMusicBassEqClip;
        script_index = 2;
    } else if (command == "audio_demo") {
        parsed.type = Type::kAudioDemo;
        script_index = 2;
    } else if (command == "spectrogram") {
        parsed.type = Type::kSpectrogram;
        script_index = 2;
    } else if (command == "onset") {
        parsed.type = Type::kOnset;
        script_index = 2;
    } else if (command != "doctor") {
        script_index = 1;
    } else {
        script_index = 2;
    }

    for (size_t i = script_index; i < static_cast<size_t>(argc); ++i) {
        const char* raw = argv[i];
        if (raw == nullptr) continue;
        const std::string arg(raw);
        if (arg.empty()) continue;

        // ---- Script path ---------------------------------------------------
        if (IsFlag(arg, "--script", "-s")) {
            if (!ConsumeStringValue("--script/-s", argc, argv, &i, &parsed.script_path, &parsed))
                return parsed;
            continue;
        }

        // ---- Security ------------------------------------------------------
        if (arg == "--sandbox")                  { parsed.sandbox = true;             continue; }

        // ---- Emit ----------------------------------------------------------
        if (arg == "--noemit")                   { parsed.noemit = true;              continue; }
        if (arg == "--emit_source_map")          { parsed.emit_source_map = true;     continue; }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--output_dir", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--output_dir", argc, argv, &i, &value, &parsed))
                    return parsed;
                parsed.output_dir = value; continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--audio_input", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--audio_input", argc, argv, &i, &value, &parsed))
                    return parsed;
                parsed.audio_input_path = value; continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--file-audio-config", &value, &inline_v)
                || ParseValueFlag(arg, "--file_audio_config", &value, &inline_v)
                || ParseValueFlag(arg, "--audio_config", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--file-audio-config", argc, argv, &i, &value, &parsed))
                    return parsed;
                parsed.audio_config_file = value; continue;
            }
        }
		{
			std::string value; bool inline_v = false;
			if (ParseValueFlag(arg, "--audio_effect", &value, &inline_v)) {
				if (!inline_v && !ConsumeStringValue("--audio_effect", argc, argv, &i, &value, &parsed))
					return parsed;
                if (parsed.type == Type::kAudioFxBatch) {
                    parsed.audio_effect_names.push_back(value);
                } else {
                    parsed.audio_effect_name = value;
                }
                continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--audio_effects", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--audio_effects", argc, argv, &i, &value, &parsed))
                    return parsed;
                if (parsed.type == Type::kAudioFxBatch) {
                    AppendTrimmedCsvValues(value, &parsed.audio_effect_names);
                } else {
                    std::vector<std::string> single_values;
                    AppendTrimmedCsvValues(value, &single_values);
                    if (!single_values.empty()) {
                        parsed.audio_effect_name = single_values.front();
                    }
                }
                continue;
			}
		}
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--audio_processor", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--audio_processor", argc, argv, &i, &value, &parsed))
                    return parsed;
                parsed.audio_processor = value; continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--clap_plugin", &value, &inline_v) || ParseValueFlag(arg, "--clap_plugin_reference", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--clap_plugin", argc, argv, &i, &value, &parsed))
                    return parsed;
                parsed.audio_clap_plugin_reference = value; continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--clap_plugins", &value, &inline_v) || ParseValueFlag(arg, "--clap_plugin_references", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--clap_plugins", argc, argv, &i, &value, &parsed))
                    return parsed;
                AppendTrimmedCsvValues(value, &parsed.audio_clap_plugin_references);
                if (!parsed.audio_clap_plugin_references.empty()) {
                    parsed.audio_clap_plugin_reference = parsed.audio_clap_plugin_references.front();
                }
                continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--audio_shaper_profile", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--audio_shaper_profile", argc, argv, &i, &value, &parsed))
                    return parsed;
                parsed.audio_shaper_profile = value; continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--audio_stretch_ratio", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--audio_stretch_ratio", argc, argv, &i, &value, &parsed))
                    return parsed;
                if (!ParsePositiveFloat(value, &parsed.audio_stretch_ratio)) {
                    parsed.valid = false;
                    parsed.error_message = "Invalid value for --audio_stretch_ratio: " + value;
                    return parsed;
                }
                continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--audio_raw_sample_rate", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--audio_raw_sample_rate", argc, argv, &i, &value, &parsed))
                    return parsed;
                if (!ParsePositiveInt(value, &parsed.audio_raw_sample_rate)) {
                    parsed.valid = false;
                    parsed.error_message = "Invalid value for --audio_raw_sample_rate: " + value;
                    return parsed;
                }
                continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--target_sample_rate", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--target_sample_rate", argc, argv, &i, &value, &parsed))
                    return parsed;
                if (!ParsePositiveInt(value, &parsed.target_sample_rate)) {
                    parsed.valid = false;
                    parsed.error_message = "Invalid value for --target_sample_rate: " + value;
                    return parsed;
                }
                continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--target_channels", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--target_channels", argc, argv, &i, &value, &parsed))
                    return parsed;
                if (!ParsePositiveInt(value, &parsed.audio_target_channels)) {
                    parsed.valid = false;
                    parsed.error_message = "Invalid value for --target_channels: " + value;
                    return parsed;
                }
                continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--audio_batch_mode", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--audio_batch_mode", argc, argv, &i, &value, &parsed))
                    return parsed;
                parsed.audio_batch_mode = value;
                continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--video_output", &value, &inline_v) || ParseValueFlag(arg, "--output", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--video_output", argc, argv, &i, &value, &parsed))
                    return parsed;
                parsed.video_output_path = value;
                continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--clip_duration", &value, &inline_v) || ParseValueFlag(arg, "--duration_seconds", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--clip_duration", argc, argv, &i, &value, &parsed))
                    return parsed;
                if (!ParsePositiveFloat(value, &parsed.clip_duration_seconds)) {
                    parsed.valid = false;
                    parsed.error_message = "Invalid value for --clip_duration: " + value;
                    return parsed;
                }
                continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--video_width", &value, &inline_v) || ParseValueFlag(arg, "--width", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--video_width", argc, argv, &i, &value, &parsed))
                    return parsed;
                if (!ParsePositiveInt(value, &parsed.video_width)) {
                    parsed.valid = false;
                    parsed.error_message = "Invalid value for --video_width: " + value;
                    return parsed;
                }
                continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--video_height", &value, &inline_v) || ParseValueFlag(arg, "--height", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--video_height", argc, argv, &i, &value, &parsed))
                    return parsed;
                if (!ParsePositiveInt(value, &parsed.video_height)) {
                    parsed.valid = false;
                    parsed.error_message = "Invalid value for --video_height: " + value;
                    return parsed;
                }
                continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--video_fps", &value, &inline_v) || ParseValueFlag(arg, "--fps", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--video_fps", argc, argv, &i, &value, &parsed))
                    return parsed;
                if (!ParsePositiveInt(value, &parsed.video_fps)) {
                    parsed.valid = false;
                    parsed.error_message = "Invalid value for --video_fps: " + value;
                    return parsed;
                }
                continue;
            }
        }
        if (arg == "--json")                    { parsed.json_output = true;         continue; }
        if (arg == "--pipe_mp3" || arg == "--mp3_pipe" || arg == "--pipe-mp3") {
            parsed.audio_pipe_mp3 = true;
            continue;
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--xer_key", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--xer_key", argc, argv, &i, &value, &parsed))
                    return parsed;
                parsed.xer_key = value; continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--xer_key_file", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--xer_key_file", argc, argv, &i, &value, &parsed))
                    return parsed;
                parsed.xer_key_file = value; continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--xer_key_env", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--xer_key_env", argc, argv, &i, &value, &parsed))
                    return parsed;
                parsed.xer_key_env = value; continue;
            }
        }

        // ---- Compiler / logging -------------------------------------------
        if (arg == "--details_compiler")         { parsed.details_compiler = true;    continue; }
        if (arg == "--verbose")                  { parsed.verbose = true;             continue; }

        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--log_level", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--log_level", argc, argv, &i, &value, &parsed))
                    return parsed;
                parsed.log_level = value; continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--log_file", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--log_file", argc, argv, &i, &value, &parsed))
                    return parsed;
                parsed.log_file = value; continue;
            }
        }

        // ---- Debug ---------------------------------------------------------
        if (arg == "--debug")                    { parsed.debug = true;               continue; }
        if (arg == "--inspect")                  { parsed.inspect = true;             continue; }
        if (arg == "--break_on_first_line")      { parsed.break_on_first_line = true; continue; }
        if (arg == "--dump_ast")                 { parsed.dump_ast = true;            continue; }
        if (arg == "--dump_bytecode")            { parsed.dump_bytecode = true;       continue; }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--inspect_port", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--inspect_port", argc, argv, &i, &value, &parsed))
                    return parsed;
                if (!ParsePositiveInt(value, &parsed.inspect_port)) {
                    parsed.valid = false;
                    parsed.error_message = "Invalid value for --inspect_port: " + value;
                    return parsed;
                }
                continue;
            }
        }

        // ---- Cache ---------------------------------------------------------
        if (arg == "--nocacherequire")           { parsed.nocacherequire = true;      continue; }
        if (arg == "--cache_clean")              { parsed.cache_clean = true;         continue; }
        if (arg == "--cache_readonly")           { parsed.cache_readonly = true;      continue; }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--cache_dir", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--cache_dir", argc, argv, &i, &value, &parsed))
                    return parsed;
                parsed.cache_dir = value; continue;
            }
        }

        // ---- Diff ----------------------------------------------------------
        if (arg == "--print_diff")               { parsed.print_diff = true;          continue; }
        if (arg == "--diff_only")                { parsed.diff_only = true;           continue; }

        // ---- Threading / resources -----------------------------------------
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--max_cpu_threads", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--max_cpu_threads", argc, argv, &i, &value, &parsed))
                    return parsed;
                if (!ParsePositiveInt(value, &parsed.max_cpu_threads)) {
                    parsed.valid = false;
                    parsed.error_message = "Invalid value for --max_cpu_threads: " + value;
                    return parsed;
                }
                continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--max_memory_used", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--max_memory_used", argc, argv, &i, &value, &parsed))
                    return parsed;
                if (!ParsePositiveInt(value, &parsed.max_memory_used)) {
                    parsed.valid = false;
                    parsed.error_message = "Invalid value for --max_memory_used: " + value;
                    return parsed;
                }
                continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--timeout_seconds", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--timeout_seconds", argc, argv, &i, &value, &parsed))
                    return parsed;
                if (!ParsePositiveInt(value, &parsed.timeout_seconds)) {
                    parsed.valid = false;
                    parsed.error_message = "Invalid value for --timeout_seconds: " + value;
                    return parsed;
                }
                continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--async_io_workers", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--async_io_workers", argc, argv, &i, &value, &parsed))
                    return parsed;
                if (!ParsePositiveInt(value, &parsed.async_io_workers)) {
                    parsed.valid = false;
                    parsed.error_message = "Invalid value for --async_io_workers: " + value;
                    return parsed;
                }
                continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--async_io_queue_depth", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--async_io_queue_depth", argc, argv, &i, &value, &parsed))
                    return parsed;
                if (!ParsePositiveInt(value, &parsed.async_io_queue_depth)) {
                    parsed.valid = false;
                    parsed.error_message = "Invalid value for --async_io_queue_depth: " + value;
                    return parsed;
                }
                continue;
            }
        }

        // ---- Output formatting ---------------------------------------------
        if (arg == "--stack_formating_no_color") { parsed.stack_formating_no_color = true; continue; }
        // ---- CPU fine-grained control -------------------------------------
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--cpu_affinity", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--cpu_affinity", argc, argv, &i, &value, &parsed))
                    return parsed;
                parsed.cpu_affinity = value; continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--thread_priority", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--thread_priority", argc, argv, &i, &value, &parsed))
                    return parsed;
                try { parsed.thread_priority = std::stoi(value); } catch (...) {
                    parsed.valid = false;
                    parsed.error_message = "Invalid value for --thread_priority: " + value;
                    return parsed;
                }
                continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--v8_platform_workers", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--v8_platform_workers", argc, argv, &i, &value, &parsed))
                    return parsed;
                if (!ParsePositiveInt(value, &parsed.v8_platform_workers)) {
                    parsed.valid = false;
                    parsed.error_message = "Invalid value for --v8_platform_workers: " + value;
                    return parsed;
                }
                continue;
            }
        }

        // ---- Memory fine-grained control ----------------------------------
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--memory_hard_limit", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--memory_hard_limit", argc, argv, &i, &value, &parsed))
                    return parsed;
                if (!ParsePositiveInt(value, &parsed.memory_hard_limit_mib)) {
                    parsed.valid = false;
                    parsed.error_message = "Invalid value for --memory_hard_limit: " + value;
                    return parsed;
                }
                continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--memory_warn", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--memory_warn", argc, argv, &i, &value, &parsed))
                    return parsed;
                if (!ParsePositiveInt(value, &parsed.memory_warn_mib)) {
                    parsed.valid = false;
                    parsed.error_message = "Invalid value for --memory_warn: " + value;
                    return parsed;
                }
                continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--memory_check_interval_ms", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--memory_check_interval_ms", argc, argv, &i, &value, &parsed))
                    return parsed;
                if (!ParsePositiveInt(value, &parsed.memory_check_interval_ms)) {
                    parsed.valid = false;
                    parsed.error_message = "Invalid value for --memory_check_interval_ms: " + value;
                    return parsed;
                }
                continue;
            }
        }

        // ---- Output formatting (continued) --------------------------------
        if (arg == "--compact_errors")           { parsed.compact_errors = true;           continue; }
        if (arg == "--no_source_excerpt")        { parsed.no_source_excerpt = true;        continue; }
        if (arg == "--timestamps")               { parsed.timestamps = true;               continue; }

        // ---- Module system -------------------------------------------------
        if (arg == "--strict_require")           { parsed.strict_require = true;           continue; }
        if (arg == "--allow_remote_require")     { parsed.allow_remote_require = true;     continue; }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--module_root", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--module_root", argc, argv, &i, &value, &parsed))
                    return parsed;
                parsed.module_root = value; continue;
            }
        }

        // ---- TypeScript ----------------------------------------------------
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--ts_compiler", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--ts_compiler", argc, argv, &i, &value, &parsed))
                    return parsed;
                parsed.ts_compiler = value; continue;
            }
        }
        if (arg == "--ts_strict")                { parsed.ts_strict = true;            continue; }
        if (arg == "--ts_no_check")              { parsed.ts_no_check = true;          continue; }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--ts_target", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--ts_target", argc, argv, &i, &value, &parsed))
                    return parsed;
                parsed.ts_target = value; continue;
            }
        }

        // ---- Environment ---------------------------------------------------
        if (arg == "--no_inherit_env")           { parsed.no_inherit_env = true;       continue; }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--env_file", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--env_file", argc, argv, &i, &value, &parsed))
                    return parsed;
                parsed.env_file = value; continue;
            }
        }

        // ---- Doctor --------------------------------------------------------
        if (arg == "--doctor_verbose")           { parsed.doctor_verbose = true;       continue; }

        // ---- Watch / ecosystem ---------------------------------------------
        if (arg == "--watch")                    { parsed.watch = true;                continue; }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--watch_interval_ms", &value, &inline_v)) {
                if (!inline_v && !ConsumeStringValue("--watch_interval_ms", argc, argv, &i, &value, &parsed))
                    return parsed;
                if (!ParsePositiveInt(value, &parsed.watch_interval_ms)) {
                    parsed.valid = false;
                    parsed.error_message = "Invalid value for --watch_interval_ms: " + value;
                    return parsed;
                }
                continue;
            }
        }
        {
            std::string value; bool inline_v = false;
            if (ParseValueFlag(arg, "--ecosystem", &value, &inline_v)) {
                if (inline_v) {
                    parsed.ecosystem_manifest_path = value;
                    continue;
                }

                if (i + 1 < static_cast<size_t>(argc)
                    && argv[i + 1] != nullptr
                    && argv[i + 1][0] != '-') {
                    parsed.ecosystem_manifest_path = argv[++i];
                } else {
                    parsed.ecosystem_manifest_path = "ecosystem.json";
                }
                continue;
            }
        }

        if (arg == "-" && parsed.type == Type::kAudioFxCustom) {
            if (parsed.audio_effect_name.empty()) {
                parsed.valid = false;
                parsed.error_message = "audio_fx_custom requires an effect name before stdin input '-'";
                return parsed;
            }
            if (parsed.audio_input_path.empty()) {
                parsed.audio_input_path = arg;
                continue;
            }
        }

        if (arg == "-" && parsed.type == Type::kAudioFxBatch) {
            if (parsed.audio_input_path.empty()) {
                parsed.audio_input_path = arg;
                continue;
            }
        }

        // ---- Unknown flag --------------------------------------------------
        if (arg[0] == '-') {
            parsed.valid = false;
            parsed.error_message = "Unknown flag: " + arg;
            return parsed;
        }

        if (parsed.type == Type::kAudioFxCustom) {
            if (parsed.audio_effect_name.empty()) {
                parsed.audio_effect_name = arg;
                continue;
            }
            if (parsed.audio_input_path.empty()) {
                parsed.audio_input_path = arg;
                continue;
            }

            parsed.valid = false;
            parsed.error_message = std::string("Unexpected extra audio_fx_custom argument: ") + arg;
            return parsed;
        }

        if (parsed.type == Type::kAudioFxBatch) {
            if (parsed.audio_input_path.empty()) {
                parsed.audio_input_path = arg;
                continue;
            }
            parsed.valid = false;
            parsed.error_message = std::string("Unexpected extra audio_fx_batch argument: ") + arg;
            return parsed;
        }

        if (parsed.type == Type::kAudioInspect || parsed.type == Type::kAudioModulesSmoke || parsed.type == Type::kAudioAnalysisSmoke || parsed.type == Type::kMusicBassEqClip || parsed.type == Type::kAudioDemo || parsed.type == Type::kSpectrogram || parsed.type == Type::kOnset) {
            if (parsed.audio_input_path.empty()) {
                parsed.audio_input_path = arg;
                continue;
            }

            parsed.valid = false;
            const char* command_name = parsed.type == Type::kAudioInspect
                ? "audio_inspect"
                : (parsed.type == Type::kAudioModulesSmoke
					? "audio_modules_smoke"
                : (parsed.type == Type::kAudioAnalysisSmoke
					? "audio_analysis_smoke"
                : (parsed.type == Type::kMusicBassEqClip
                    ? "music_bass_eq_clip"
                : (parsed.type == Type::kAudioDemo
                    ? "audio_demo"
                    : (parsed.type == Type::kSpectrogram ? "spectrogram" : "onset")))));
            parsed.error_message = std::string("Unexpected extra ") + command_name + " argument: " + arg;
            return parsed;
        }

        parsed.script_path = arg;
    }

    if (!parsed.audio_config_file.empty() && parsed.type == Type::kRun && parsed.script_path == "example/project.js") {
        parsed.type = Type::kAudioFxConfig;
    }

    if (parsed.json_output && parsed.type != Type::kAudioInspect && parsed.type != Type::kAudioFxBatch && parsed.audio_config_file.empty()) {
        parsed.valid = false;
        parsed.error_message = "--json is currently supported only for audio_inspect and audio_fx_batch";
        return parsed;
    }

    return parsed;
}

std::string AppCommand::BuildHelpText(const std::string& binary_name) {
    std::ostringstream out;
    out << "Usage:\n";
    out << "  " << binary_name << " run [script.js] [options]\n";
    out << "  " << binary_name << " compile [script.xer] [options]\n";
    out << "  " << binary_name << " inspect [artifact.bin|artifact.bak]\n";
    out << "  " << binary_name << " --file-audio-config ./temp_audio.json\n";
    out << "  " << binary_name << " audio_inspect <input_audio> [--output_dir dir] [--target_sample_rate hz] [--json]\n";
    out << "  " << binary_name << " audio_fx_custom <effect_name> <input.mp3|-> [--output_dir dir] [--target_sample_rate hz] [--target_channels n] [--clap_plugin path|ref] [--pipe_mp3]\n";
	out << "  " << binary_name << " audio_fx_batch <input.mp3|-> --audio_effect <name> [--audio_effect <name> ...] [--output_dir dir] [--target_sample_rate hz] [--target_channels n] [--audio_batch_mode parallel|chain] [--clap_plugin path|ref] [--json] [--pipe_mp3]\n";
    out << "  " << binary_name << " audio_modules_smoke <input_audio> [--output_dir dir]\n";
    out << "  " << binary_name << " audio_analysis_smoke <input_audio> [--output_dir dir] [--target_sample_rate hz]\n";
    out << "  " << binary_name << " music_bass_eq_clip <input_audio> [--output_dir dir] [--video_output clip.mp4] [--clip_duration 15] [--width 1280] [--height 720] [--fps 30]\n";
    out << "  " << binary_name << " audio_demo [input_audio] [--output_dir dir] [--audio_processor name]\n";
    out << "  " << binary_name << " spectrogram <input_audio> [--output_dir dir] [--target_sample_rate hz]\n";
    out << "  " << binary_name << " onset <input_audio> [--output_dir dir] [--target_sample_rate hz]\n";
    out << "  " << binary_name << " run --script path/to/script.js [options]\n";
    out << "\nScript:\n";
    out << "  --script, -s <path>          Entry-point JS/TS/XER file\n";
    out << "\nSecurity:\n";
    out << "  --sandbox                    Restrict FS/network access inside scripts\n";
    out << "\nEmit / output:\n";
    out << "  --noemit                     Dry-run: parse and validate only\n";
    out << "  --emit_source_map            Write source-maps alongside compiled output\n";
    out << "  --output_dir <path>          Write generated compile artifacts into directory\n";
    out << "  --audio_input <path>         Audio file for audio_inspect/audio_fx_custom/audio_fx_batch/audio_modules_smoke/audio_analysis_smoke/music_bass_eq_clip/audio_demo/spectrogram/onset\n";
    out << "  --file-audio-config <path>   Load audio FX input/effects/output/pipe options from a JSON file\n";
    out << "  --audio_effect <name>        Custom preset name for audio_fx_custom or one batch item for audio_fx_batch\n";
    out << "  --audio_effects <a,b,c>      Comma-separated effect list for audio_fx_batch\n";
    out << "  --clap_plugin <path|ref>     CLAP plugin reference for CLAPPlugin/custom effect nodes\n";
    out << "  --clap_plugins <a,b,c>       Ordered CLAP plugin references for repeated CLAPPlugin stages\n";
    out << "  --audio_processor <name>     spectral_shaper|phase_vocoder\n";
    out << "  --audio_shaper_profile <n>   unity|tilt|bright\n";
    out << "  --audio_stretch_ratio <x>    Phase vocoder time-stretch ratio (> 0)\n";
    out << "  --audio_raw_sample_rate <n>  Sample rate for headerless .raw/.pcm input\n";
    out << "  --target_sample_rate <n>     Normalize file-based audio commands to target sample rate\n";
    out << "  --target_channels <n>        Normalize audio_fx_custom/audio_fx_batch output to a target channel count\n";
    out << "  --audio_batch_mode <mode>   parallel|chain for audio_fx_batch rendering order\n";
    out << "  --video_output <path>        MP4 path for music_bass_eq_clip\n";
    out << "  --clip_duration <seconds>    Music visualizer clip length, clamped to 15 seconds\n";
    out << "  --width/--height <n>         music_bass_eq_clip frame size\n";
    out << "  --fps <n>                    music_bass_eq_clip frame rate\n";
    out << "  --pipe_mp3                   Read strict MP3 from path/stdin '-' and stream processed MP3 bytes to stdout\n";
    out << "  --json                       Emit audio_inspect or audio_fx_batch summary as JSON\n";
    out << "\nXER protection:\n";
    out << "  --xer_key <secret>           Encrypt XER .bin payloads with AES-256-GCM\n";
    out << "  --xer_key_file <path>        Read XER encryption key from file\n";
    out << "  --xer_key_env <name>         Read XER encryption key from environment variable\n";
    out << "\nCompiler / logging:\n";
    out << "  --details_compiler           Print detailed Clang/LLVM diagnostics\n";
    out << "  --verbose                    Extra progress messages throughout pipeline\n";
    out << "  --log_level <level>          silent|info|debug|trace  (default: info)\n";
    out << "  --log_file <path>            Append logs to file (default: stderr)\n";
    out << "\nDebug:\n";
    out << "  --debug                      Enable V8 debug hooks\n";
    out << "  --inspect                    Expose V8 inspector\n";
    out << "  --inspect_port <n>           Inspector listen port (default: 9229)\n";
    out << "  --break_on_first_line        Pause on first script line when inspecting\n";
    out << "  --dump_ast                   Dump V8 AST of entry script to stdout\n";
    out << "  --dump_bytecode              Dump V8 bytecode of entry script to stdout\n";
    out << "\nCache:\n";
    out << "  --nocacherequire             Bypass require() cache entirely\n";
    out << "  --cache_dir <path>           Override default .flowcache directory\n";
    out << "  --cache_clean                Delete stale cache entries before running\n";
    out << "  --cache_readonly             Read cache but never write/update it\n";
    out << "\nDiff:\n";
    out << "  --print_diff                 Print require-diff summary per script\n";
    out << "  --diff_only                  Print diff then exit without running scripts\n";
    out << "\nThreading / resources:\n";
    out << "  --max_cpu_threads <n>        Limit worker thread pool size\n";
    out << "  --max_memory_used <n>        Soft memory cap in MiB\n";
    out << "  --timeout_seconds <n>        Hard script execution timeout\n";
    out << "  --async_io_workers <n>       Async I/O thread pool size\n";
    out << "  --async_io_queue_depth <n>   Max outstanding I/O operations\n";
    out << "\nOutput formatting:\n";
    out << "\nCPU fine-grained control:\n";
    out << "  --cpu_affinity <ids>         Pin worker threads to CPU cores, e.g. \"0,1,2,3\"\n";
    out << "  --thread_priority <n>        Nice-value offset: 0=default, 1-19=lower, -1..-20=higher (root)\n";
    out << "  --v8_platform_workers <n>    V8 background thread pool size\n";
    out << "\nMemory fine-grained control:\n";
    out << "  --memory_hard_limit <n>      RSS hard cap in MiB: script terminated when exceeded\n";
    out << "  --memory_warn <n>            RSS soft threshold in MiB: prints warning but continues\n";
    out << "  --memory_check_interval_ms <n>  ResourceGuard polling interval (default: 250 ms)\n";
    out << "\nOutput formatting:\n";
    out << "  --stack_formating_no_color   Disable ANSI colors in stack traces\n";
    out << "  --compact_errors             One-line error format\n";
    out << "  --no_source_excerpt          Omit source excerpt from error reports\n";
    out << "  --timestamps                 Prepend timestamps to log lines\n";
    out << "\nModule system:\n";
    out << "  --strict_require             Treat unknown require() targets as fatal\n";
    out << "  --allow_remote_require       Allow require() from http/https URLs\n";
    out << "  --module_root <path>         Override module-resolution root directory\n";
    out << "\nTypeScript:\n";
    out << "  --ts_compiler <name>         tsc|npx-tsc|esbuild\n";
    out << "  --ts_strict                  Pass --strict to tsc\n";
    out << "  --ts_no_check                Compile TS without type-checking (faster)\n";
    out << "  --ts_target <target>         e.g. ES2020\n";
    out << "\nEnvironment:\n";
    out << "  --no_inherit_env             Do not pass current env to child processes\n";
    out << "  --env_file <path>            Load additional env vars from file (.env)\n";
    out << "\nWatch / ecosystem:\n";
    out << "  --watch                      Re-run script when watched files change\n";
    out << "  --watch_interval_ms <n>      Watch polling interval in ms (default: 350)\n";
    out << "  --ecosystem[=path]           Load ecosystem manifest (default: ecosystem.json)\n";
    out << "\nCommands:\n";
    out << "  " << binary_name << " compile <script.xer> [--output_dir dir] [--xer_key_file path]   Build .bin/.bak only\n";
    out << "  " << binary_name << " inspect <artifact.bin|artifact.bak>   Print XER metadata without execution\n";
    out << "  " << binary_name << " audio_inspect <input_audio> [--output_dir dir] [--target_sample_rate hz] [--json]   Load, normalize, and print audio metadata without DSP\n";
    out << "  " << binary_name << " audio_fx_custom <effect_name> <input.mp3|-> [--output_dir dir] [--target_sample_rate hz] [--target_channels n] [--pipe_mp3]   Render one named custom FX preset from strict MP3 input\n";
    out << "  " << binary_name << " audio_fx_batch <input.mp3|-> --audio_effect <name> [--audio_effect <name> ...] [--output_dir dir] [--target_sample_rate hz] [--target_channels n] [--audio_batch_mode parallel|chain] [--json] [--pipe_mp3]   Render multiple custom FX presets from strict MP3 input\n";
    out << "  " << binary_name << " audio_modules_smoke <input_audio> [--output_dir dir]   Run plugin/MIDI/codec smoke validation on one input\n";
    out << "  " << binary_name << " audio_analysis_smoke <input_audio> [--output_dir dir] [--target_sample_rate hz]   Run beat/pitch/loudness analysis and export report artifacts\n";
    out << "  " << binary_name << " music_bass_eq_clip <input_audio> [--output_dir dir] [--video_output clip.mp4]   Create a <=15s bass/EQ-reactive video clip from a music file\n";
    out << "  " << binary_name << " audio_demo [input_audio] [--audio_processor name] [--output_dir dir]   Run STFT smoke demo or file-based processing\n";
    out << "  " << binary_name << " spectrogram <input_audio> [--output_dir dir] [--target_sample_rate hz]   Build a spectrogram raw dump from normalized audio\n";
    out << "  " << binary_name << " onset <input_audio> [--output_dir dir] [--target_sample_rate hz]   Detect onset times from normalized audio\n";
    out << "  " << binary_name << " doctor [--doctor_verbose]   Check environment\n";
    out << "  " << binary_name << " version\n";
    out << "  " << binary_name << " help\n";
    return out.str();
}