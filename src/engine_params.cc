#include "engine_params.h"

#include "helper/env_utils.h"

#include <string>

using XER::EngineParams;

namespace {

bool EnvBool(const char* key) {
    const std::string raw = XER::Helper::GetEnv(key);
    if (raw.empty()) {
        return false;
    }

    return raw == "1" || raw == "true" || raw == "TRUE" || raw == "on" || raw == "ON";
}

int EnvInt(const char* key, int default_value) {
    const std::string raw = XER::Helper::GetEnv(key);
    if (raw.empty()) {
        return default_value;
    }

    try {
        const int v = std::stoi(raw);
        return v > 0 ? v : default_value;
    } catch (...) {
        return default_value;
    }
}

std::string EnvStr(const char* key, const std::string& default_value = {}) {
    const std::string raw = XER::Helper::GetEnv(key);
    return raw.empty() ? default_value : raw;
}

}  // namespace

EngineParams EngineParamsFromEnv() {
    EngineParams p;

    // Script
    p.script_path = EnvStr("ENGINE_SCRIPT_PATH");

    // Security
    p.sandbox = EnvBool("ENGINE_SANDBOX");

    // Emit
    p.noemit          = EnvBool("ENGINE_NOEMIT");
    p.emit_source_map = EnvBool("ENGINE_EMIT_SOURCE_MAP");

    // Compiler details
    p.details_compiler = EnvBool("ENGINE_DETAILS_COMPILER");
    p.verbose          = EnvBool("ENGINE_VERBOSE");
    p.log_level        = EnvStr("ENGINE_LOG_LEVEL", "info");
    p.log_file         = EnvStr("ENGINE_LOG_FILE");

    // Debug
    p.debug              = EnvBool("ENGINE_DEBUG");
    p.inspect            = EnvBool("ENGINE_INSPECT");
    p.inspect_port       = EnvInt("ENGINE_INSPECT_PORT", 9229);
    p.break_on_first_line = EnvBool("ENGINE_BREAK_ON_FIRST_LINE");
    p.dump_ast           = EnvBool("ENGINE_DUMP_AST");
    p.dump_bytecode      = EnvBool("ENGINE_DUMP_BYTECODE");

    // Cache
    p.nocacherequire = EnvBool("ENGINE_REQUIRE_NO_CACHE");
    p.cache_dir      = EnvStr("ENGINE_CACHE_DIR");
    p.cache_clean    = EnvBool("ENGINE_CACHE_CLEAN");
    p.cache_readonly = EnvBool("ENGINE_CACHE_READONLY");

    // Diff
    p.print_diff = EnvBool("ENGINE_PRINT_DIFF");
    p.diff_only  = EnvBool("ENGINE_DIFF_ONLY");
    p.diff_color = !EnvBool("ENGINE_STACK_FORMATING_NO_COLOR");

    // Threading / resources
    p.max_cpu_threads    = EnvInt("ENGINE_MAX_CPU_THREADS", -1);
    p.max_memory_used    = EnvInt("ENGINE_MAX_MEMORY_USED", -1);
    p.timeout_seconds    = EnvInt("ENGINE_TIMEOUT_SECONDS", -1);
    p.async_io_workers   = EnvInt("ENGINE_ASYNC_IO_WORKERS", -1);
    p.async_io_queue_depth = EnvInt("ENGINE_ASYNC_IO_QUEUE_DEPTH", -1);

    // CPU fine-grained
    p.cpu_affinity       = EnvStr("ENGINE_CPU_AFFINITY");
    p.thread_priority    = EnvInt("ENGINE_THREAD_PRIORITY", 0);
    p.v8_platform_workers = EnvInt("ENGINE_V8_PLATFORM_WORKERS", -1);

    // Memory fine-grained
    p.memory_hard_limit_mib    = EnvInt("ENGINE_MEMORY_HARD_LIMIT_MIB", -1);
    p.memory_warn_mib          = EnvInt("ENGINE_MEMORY_WARN_MIB",       -1);
    p.memory_check_interval_ms = EnvInt("ENGINE_RESOURCE_CHECK_INTERVAL_MS", 250);

    // Output formatting
    p.stack_formating_no_color = EnvBool("ENGINE_STACK_FORMATING_NO_COLOR");
    p.compact_errors           = EnvBool("ENGINE_COMPACT_ERRORS");
    p.show_source_excerpt      = !EnvBool("ENGINE_NO_SOURCE_EXCERPT");
    p.timestamps               = EnvBool("ENGINE_TIMESTAMPS");

    // Module system
    p.strict_require       = EnvBool("ENGINE_STRICT_REQUIRE");
    p.allow_remote_require = EnvBool("ENGINE_ALLOW_REMOTE_REQUIRE");
    p.module_root          = EnvStr("ENGINE_MODULE_ROOT");

    // TypeScript
    p.ts_compiler = EnvStr("ENGINE_TS_COMPILER");
    p.ts_strict   = EnvBool("ENGINE_TS_STRICT");
    p.ts_no_check = EnvBool("ENGINE_TS_NO_CHECK");
    p.ts_target   = EnvStr("ENGINE_TS_TARGET");

    // Environment pass-through
    p.inherit_env = !EnvBool("ENGINE_NO_INHERIT_ENV");
    p.env_file    = EnvStr("ENGINE_ENV_FILE");

    // Doctor
    p.doctor_verbose = EnvBool("ENGINE_DOCTOR_VERBOSE");

    return p;
}
