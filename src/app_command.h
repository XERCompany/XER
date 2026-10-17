#pragma once

#include <string>
#include <vector>

class AppCommand {
public:
    enum class Type {
        kRun,
        kCompile,
        kInspect,
        kAudioInspect,
        kAudioFxConfig,
        kAudioFxCustom,
        kAudioFxBatch,
        kAudioModulesSmoke,
        kAudioAnalysisSmoke,
        kMusicBassEqClip,
        kAudioDemo,
        kSpectrogram,
        kOnset,
        kHelp,
        kVersion,
        kDoctor,
    };

    struct Parsed {
        Type type = Type::kRun;
        std::string script_path = "example/project.js";

        // ---- Security / sandbox --------------------------------------------
        bool sandbox = false;

        // ---- Emit / output -------------------------------------------------
        bool noemit           = false;
        bool emit_source_map  = false;
        std::string output_dir;
        std::string audio_input_path;
        std::string audio_config_file;
        std::string audio_effect_name;
        std::vector<std::string> audio_effect_names;
        std::string audio_processor;
        std::string audio_shaper_profile = "tilt";
        std::string audio_clap_plugin_reference = "builtin://gain";
        std::vector<std::string> audio_clap_plugin_references;
        float audio_stretch_ratio = 1.0f;
        int audio_raw_sample_rate = 44100;
        int target_sample_rate = 44100;
        int audio_target_channels = -1;
        std::string audio_batch_mode = "parallel";
        std::string video_output_path;
        float clip_duration_seconds = 15.0f;
        int video_width = 1280;
        int video_height = 720;
        int video_fps = 30;
        bool json_output = false;
        bool audio_pipe_mp3 = false;
        std::string xer_key;
        std::string xer_key_file;
        std::string xer_key_env;

        // ---- Compiler details ----------------------------------------------
        bool details_compiler = false;
        bool verbose          = false;
        std::string log_level;    // "silent"|"info"|"debug"|"trace"
        std::string log_file;     // path to log file (empty = stderr only)

        // ---- Debug ---------------------------------------------------------
        bool debug              = false;
        bool inspect            = false;
        int  inspect_port       = 9229;
        bool break_on_first_line = false;
        bool dump_ast           = false;
        bool dump_bytecode      = false;

        // ---- Cache ---------------------------------------------------------
        bool nocacherequire = false;
        std::string cache_dir;      // override default .flowcache dir
        bool cache_clean    = false;
        bool cache_readonly = false;

        // ---- Diff ----------------------------------------------------------
        bool print_diff = false;
        bool diff_only  = false;

        // ---- Threading / resources -----------------------------------------
        int  max_cpu_threads     = -1;
        int  max_memory_used     = -1;
        int  timeout_seconds     = -1;
        int  async_io_workers    = -1;
        int  async_io_queue_depth = -1;

        // ---- CPU fine-grained control -----------------------------------
        std::string cpu_affinity;        // e.g. "0,1,2,3" – core IDs for worker affinity
        int  thread_priority = 0;        // nice-value offset (0 = default)
        int  v8_platform_workers = -1;   // V8 background thread pool size

        // ---- Memory fine-grained control --------------------------------
        int  memory_hard_limit_mib = -1; // RSS hard cap in MiB
        int  memory_warn_mib       = -1; // RSS soft warning threshold in MiB
        int  memory_check_interval_ms = 250; // ResourceGuard poll interval

        // ---- Output formatting ---------------------------------------------
        bool stack_formating_no_color = false;
        bool compact_errors           = false;
        bool no_source_excerpt        = false;
        bool timestamps               = false;

        // ---- Module system -------------------------------------------------
        bool strict_require       = false;
        bool allow_remote_require = false;
        std::string module_root;

        // ---- TypeScript ----------------------------------------------------
        std::string ts_compiler;   // "tsc"|"npx-tsc"|"esbuild"
        bool ts_strict   = false;
        bool ts_no_check = false;
        std::string ts_target;     // e.g. "ES2020"

        // ---- Environment ---------------------------------------------------
        bool no_inherit_env = false;
        std::string env_file;

        // ---- Doctor --------------------------------------------------------
        bool doctor_verbose = false;

        // ---- Watch / ecosystem ---------------------------------------------
        bool watch = false;
        int watch_interval_ms = 350;
        std::string ecosystem_manifest_path;

        // ---- Validity ------------------------------------------------------
        bool valid = true;
        std::string error_message;
    };

    static Parsed Parse(int argc, char** argv);
    static std::string BuildHelpText(const std::string& binary_name);
}; 