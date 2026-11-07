#pragma once

#include <string>

namespace XER {


// Central runtime-configuration object built from CLI flags before execution.
// Passed through the execution stack so every component reads a single
// source-of-truth instead of consulting environment variables directly.

struct EngineParams {
    // ---- Script ---------------------------------------------------------
    std::string script_path;        // entry-point JS/TS file

    // ---- Security / sandbox  --------------------------------------------
    bool sandbox = false;           // restrict filesystem / network access in scripts

    // ---- Emit / output  -------------------------------------------------
    bool noemit = false;            // dry-run: parse + validate but do not write outputs
    bool emit_source_map = false;   // write source-maps alongside compiled output

    // ---- Compiler details -----------------------------------------------
    bool details_compiler = false;  // print detailed Clang/LLVM diagnostics
    bool verbose = false;           // print extra progress messages throughout the pipeline
    std::string log_level;          // "silent" | "info" | "debug" | "trace"  (default: "info")
    std::string log_file;           // write log to this file (empty = stderr only)

    // ---- Debug  ---------------------------------------------------------
    bool debug = false;             // enable V8/script debug hooks
    bool inspect = false;           // expose V8 inspector on --inspect_port
    int  inspect_port = 9229;       // inspector listen port (used when inspect=true)
    bool break_on_first_line = false; // pause on first script line when inspect=true
    bool dump_ast = false;          // dump V8 AST to stdout for the entry script
    bool dump_bytecode = false;     // dump V8 bytecode to stdout for the entry script

    // ---- Cache  ---------------------------------------------------------
    bool nocacherequire = false;    // bypass require() cache entirely
    std::string cache_dir;          // override default .flowcache directory location
    bool cache_clean = false;       // delete stale cache entries before running
    bool cache_readonly = false;    // read cache but never write/update it

    // ---- Diff  ----------------------------------------------------------
    bool print_diff = false;        // print require-diff summary to stdout
    bool diff_only = false;         // print diff then exit without executing scripts
    bool diff_color = true;         // colorize diff output (disabled by --stack_formating_no_color)

    // ---- Threading / resources  -----------------------------------------
    int  max_cpu_threads = -1;      // limit worker-thread pool size (-1 = auto)
    int  max_memory_used = -1;      // soft memory cap in MiB (-1 = unlimited)
    int  timeout_seconds = -1;      // hard script execution timeout (-1 = none)
    int  async_io_workers = -1;     // async I/O thread pool size (-1 = auto)
    int  async_io_queue_depth = -1; // max outstanding I/O operations (-1 = default)

    // ---- CPU fine-grained control  --------------------------------------
    std::string cpu_affinity;       // comma-separated core ids to pin workers to, e.g. "0,1,2,3"
    int  thread_priority = 0;       // nice value offset: 0 = default, 1..19 = lower, -1..-20 = higher (root only)
    int  v8_platform_workers = -1;  // V8 background thread pool size (-1 = hardware_concurrency)

    // ---- Memory fine-grained control  -----------------------------------
    int  memory_hard_limit_mib = -1;  // RSS hard cap → terminate isolate when exceeded
    int  memory_warn_mib       = -1;  // RSS soft threshold → print warning (must be < hard limit)
    int  memory_check_interval_ms = 250; // ResourceGuard polling interval in ms

    // ---- Output formatting  ---------------------------------------------
    bool stack_formating_no_color = false; // disable ANSI colors in stack traces
    bool compact_errors = false;           // one-line error format instead of rich blocks
    bool show_source_excerpt = true;       // embed source excerpt in error reports
    bool timestamps = false;               // prepend timestamps to log lines

    // ---- Module system  -------------------------------------------------
    bool strict_require = false;    // treat unknown require() targets as fatal errors
    bool allow_remote_require = false; // allow require() from http/https URLs
    std::string module_root;        // override module-resolution root directory

    // ---- TypeScript  ----------------------------------------------------
    std::string ts_compiler;        // force TS compiler: "tsc" | "npx-tsc" | "esbuild"
    bool ts_strict = false;         // pass --strict to tsc
    bool ts_no_check = false;       // compile TS without type checking (faster)
    std::string ts_target;          // e.g. "ES2020" (empty = use default)

    // ---- Environment pass-through  --------------------------------------
    bool inherit_env = true;        // pass current process env to child processes
    std::string env_file;           // load additional env vars from this file (.env)

    // ---- Doctor / diagnostics  ------------------------------------------
    bool doctor_verbose = false;    // extend doctor output with dependency versions

    // ---- Helpers  -------------------------------------------------------

    // Return true if any color output is globally disabled.
    bool colors_disabled() const { return stack_formating_no_color; }

    // Return resolved log-level string, defaulting to "info".
    const std::string& effective_log_level() const {
        static const std::string kDefaultLevel = "info";
        return log_level.empty() ? kDefaultLevel : log_level;
    }

    // Return true when verbose logging is requested at any level.
    bool is_verbose() const {
        return verbose || log_level == "debug" || log_level == "trace";
    }

    // Return true when running without writes (dry-run mode).
    bool is_dry_run() const { return noemit; }
};

// Build EngineParams from the raw environment variables set by main().
// Called early in FlowScript::Run() so every subsystem reads from one object.
EngineParams EngineParamsFromEnv();

}  // namespace XER
