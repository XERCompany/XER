#include "resource_guard.h"

#include "helper/env_utils.h"

#include <sys/resource.h>   // getrusage, setrlimit
#include <sys/time.h>

#include <absl/strings/str_cat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include "flux/terminal/terminal_output_renderer.h"

// ─────────────────────────────────────────────────────────────
//  Build options from environment
// ─────────────────────────────────────────────────────────────

namespace {

int EnvIntOrDefault(const char* key, int def) {
    const std::string raw = XER::Helper::GetEnv(key);
    if (raw.empty()) return def;
    try {
        const int v = std::stoi(raw);
        return v > 0 ? v : def;
    } catch (...) {
        return def;
    }
}

bool EnvBool(const char* key) {
    const std::string raw = XER::Helper::GetEnv(key);
    if (raw.empty()) return false;
    return raw == "1" || raw == "true" || raw == "TRUE" || raw == "on" || raw == "ON";
}

}  // namespace

ResourceGuard::Options ResourceGuardOptionsFromEnv() {
    ResourceGuard::Options o;
    o.memory_hard_limit_mib = EnvIntOrDefault("ENGINE_MEMORY_HARD_LIMIT_MIB", -1);
    o.memory_warn_mib       = EnvIntOrDefault("ENGINE_MEMORY_WARN_MIB",       -1);
    o.check_interval_ms     = EnvIntOrDefault("ENGINE_RESOURCE_CHECK_INTERVAL_MS", 250);
    o.timeout_seconds       = EnvIntOrDefault("ENGINE_TIMEOUT_SECONDS",       -1);
    o.verbose               = EnvBool("ENGINE_VERBOSE") || EnvBool("ENGINE_DEBUG");
    o.timestamps            = EnvBool("ENGINE_TIMESTAMPS");
    return o;
}

// ─────────────────────────────────────────────────────────────
//  Static helpers – system metrics
// ─────────────────────────────────────────────────────────────

size_t ResourceGuard::CurrentRssMib() {
    // Read VmRSS from /proc/self/status – most accurate RSS on Linux.
    std::ifstream f("/proc/self/status");
    if (!f.is_open()) return 0;

    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            // Format: "VmRSS:   12345 kB"
            std::istringstream ss(line);
            std::string label;
            size_t kbytes = 0;
            ss >> label >> kbytes;
            return kbytes / 1024;
        }
    }
    return 0;
}

long ResourceGuard::CurrentCpuMs() {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) return 0;
    const long user_ms   = ru.ru_utime.tv_sec * 1000 + ru.ru_utime.tv_usec / 1000;
    const long system_ms = ru.ru_stime.tv_sec * 1000 + ru.ru_stime.tv_usec / 1000;
    return user_ms + system_ms;
}

// ─────────────────────────────────────────────────────────────
//  ResourceGuard implementation
// ─────────────────────────────────────────────────────────────

ResourceGuard::ResourceGuard(v8::Isolate* isolate, Options opts)
    : isolate_(isolate), opts_(std::move(opts)) {}

ResourceGuard::~ResourceGuard() {
    Stop();
}

void ResourceGuard::Start() {
    if (thread_.joinable()) return;  // already running
    start_time_ = std::chrono::steady_clock::now();
    stop_.store(false, std::memory_order_release);
    thread_ = std::thread(&ResourceGuard::MonitorLoop, this);
}

void ResourceGuard::Stop() {
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) {
        thread_.join();
    }
}

void ResourceGuard::Log(const std::string& msg) const {
    if (!opts_.verbose) return;
    if (opts_.timestamps) {
        const auto now = std::chrono::steady_clock::now();
        const auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now.time_since_epoch()).count();
		flux::terminal::WriteLine(
			flux::terminal::OutputStream::kStderr,
			absl::StrCat("[", ms, "] [resource_guard] ", msg));
    } else {
		flux::terminal::WriteLine(
			flux::terminal::OutputStream::kStderr,
			absl::StrCat("[resource_guard] ", msg));
    }
}

void ResourceGuard::KillIsolate(const std::string& reason) {
    if (was_killed_.exchange(true, std::memory_order_acq_rel)) return;
    kill_reason_ = reason;
	flux::terminal::WriteLine(
		flux::terminal::OutputStream::kStderr,
		absl::StrCat("[resource_guard] LIMIT EXCEEDED – terminating script: ", reason));
    if (isolate_ != nullptr) {
        isolate_->TerminateExecution();
    }
}

void ResourceGuard::MonitorLoop() {
    const auto interval = std::chrono::milliseconds(
        opts_.check_interval_ms > 0 ? opts_.check_interval_ms : 250);

    // Snapshot RSS and CPU time at guard start for delta reporting.
    const size_t rss_start = CurrentRssMib();
    const long   cpu_start = CurrentCpuMs();

    Log("monitor started"
        " | hard_limit=" + std::to_string(opts_.memory_hard_limit_mib) + " MiB"
        " | warn="        + std::to_string(opts_.memory_warn_mib)       + " MiB"
        " | timeout="     + std::to_string(opts_.timeout_seconds)       + " s"
        " | rss_now="     + std::to_string(rss_start)                   + " MiB"
    );

    while (!stop_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(interval);
        if (stop_.load(std::memory_order_acquire)) break;

        // ── Memory check ──────────────────────────────────────────
        const size_t rss = CurrentRssMib();

        // Soft warning threshold.
        if (opts_.memory_warn_mib > 0
                && static_cast<int>(rss) >= opts_.memory_warn_mib
                && !warned_memory_.load(std::memory_order_acquire)) {
            warned_memory_.store(true, std::memory_order_release);
            flux::terminal::WriteLine(
                flux::terminal::OutputStream::kStderr,
                absl::StrCat("[resource_guard] MEMORY WARNING: RSS=", rss,
                    " MiB >= warn=", opts_.memory_warn_mib, " MiB"));
        }

        // Hard kill threshold.
        if (opts_.memory_hard_limit_mib > 0
                && static_cast<int>(rss) >= opts_.memory_hard_limit_mib) {
            KillIsolate("RSS " + std::to_string(rss) + " MiB >= hard_limit "
                        + std::to_string(opts_.memory_hard_limit_mib) + " MiB");
            break;
        }

        // ── Timeout check ─────────────────────────────────────────
        if (opts_.timeout_seconds > 0) {
            const auto now     = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                     now - start_time_).count();
            if (elapsed >= static_cast<long long>(opts_.timeout_seconds)) {
                KillIsolate("timeout " + std::to_string(elapsed)
                            + " s >= limit " + std::to_string(opts_.timeout_seconds) + " s");
                break;
            }
        }

        // ── Periodic verbose stats ────────────────────────────────
        if (opts_.verbose) {
            const long cpu_delta = CurrentCpuMs() - cpu_start;
            Log("rss=" + std::to_string(rss) + " MiB"
                " | cpu_delta=" + std::to_string(cpu_delta) + " ms");
        }
    }

    Log("monitor stopped");
}

// ─────────────────────────────────────────────────────────────
//  OS-level hard limits (called before isolate is created)
// ─────────────────────────────────────────────────────────────

// Apply RLIMIT_AS (virtual address space) = hard_mib * 1.5  and
// RLIMIT_DATA (heap) = hard_mib.  Called from FlowScript::CreateIsolate().
// Returns false and sets error_out on failure (non-fatal – caller may ignore).
bool ApplyProcessMemoryLimits(int hard_mib, std::string* error_out) {
    if (hard_mib <= 0) return true;

    const rlim_t bytes = static_cast<rlim_t>(hard_mib) * 1024 * 1024;

    // RLIMIT_DATA — limits BSS + heap.
    struct rlimit rl_data;
    if (getrlimit(RLIMIT_DATA, &rl_data) == 0) {
        // Only tighten — never loosen an existing limit.
        if (rl_data.rlim_cur == RLIM_INFINITY || bytes < rl_data.rlim_cur) {
            rl_data.rlim_cur = bytes;
            if (setrlimit(RLIMIT_DATA, &rl_data) != 0 && error_out != nullptr) {
                *error_out = "setrlimit(RLIMIT_DATA) failed: " + std::string(std::strerror(errno));
            }
        }
    }

    return true;
}
