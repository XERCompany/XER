#pragma once

#include "config.h"
// ResourceGuard – monitors process RSS and script execution wall-clock time.
// When either limit is breached it requests V8 to terminate the running script
// and writes a human-readable reason to the provided atomic string.
//
// Usage:
//   ResourceGuard guard(isolate, opts);
//   guard.Start();               // spawn background thread
//   require_runtime.RunEntry();  // blocked call inside V8
//   guard.Stop();                // join thread; safe to call multiple times
//   if (guard.was_killed()) { ... }

#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>

#include <v8.h>

class ResourceGuard {
public:
    struct Options {
        // Memory -------------------------------------------------------
        // Hard RSS cap in MiB. When the process crosses this threshold the
        // isolate is terminated.  -1 = disabled.
        int memory_hard_limit_mib = -1;

        // Soft RSS threshold: emit a warning to stderr but continue.
        // -1 = disabled.  Must be < memory_hard_limit_mib when both are set.
        int memory_warn_mib = -1;

        // Polling interval for RSS and timeout checks.
        int check_interval_ms = 250;

        // CPU ----------------------------------------------------------
        // Maximum wall-clock seconds the script may run.  -1 = disabled.
        int timeout_seconds = -1;

        // Misc ---------------------------------------------------------
        bool verbose = false;       // log monitoring events to stderr
        bool timestamps = false;    // prepend millisecond timestamps to log lines
    };

    explicit ResourceGuard(v8::Isolate* isolate, Options opts);
    ~ResourceGuard();

    // Non-copyable / non-movable.
    ResourceGuard(const ResourceGuard&) = delete;
    ResourceGuard& operator=(const ResourceGuard&) = delete;

    // Spawn the monitor thread.  Must be called before the script runs.
    void Start();

    // Signal the monitor thread to stop and join it.  Idempotent.
    void Stop();

    // True when the guard terminated the isolate due to a limit breach.
    bool was_killed() const { return was_killed_.load(std::memory_order_acquire); }

    // Human-readable description of what was violated (empty if nothing).
    const std::string& kill_reason() const { return kill_reason_; }

    // Current process RSS in MiB (reads /proc/self/status).
    static size_t CurrentRssMib();

    // Current process CPU time in milliseconds (user + system via getrusage).
    static long CurrentCpuMs();

private:
    void MonitorLoop();
    void Log(const std::string& msg) const;
    void KillIsolate(const std::string& reason);

    v8::Isolate* isolate_;
    Options opts_;

    std::thread           thread_;
    std::atomic<bool>     stop_{false};
    std::atomic<bool>     was_killed_{false};
    std::string           kill_reason_;
    std::atomic<bool>     warned_memory_{false};

    std::chrono::steady_clock::time_point start_time_;
};

// Build ResourceGuard::Options from the current EngineParams environment.
// Reads ENGINE_* env vars directly so it can be called without depending on
// the full EngineParams header in resource_guard.cc.
ResourceGuard::Options ResourceGuardOptionsFromEnv();

// Apply OS-level process memory limits (RLIMIT_DATA) before the V8 isolate is
// created.  hard_mib <= 0 is a no-op.  Returns true on success.
bool ApplyProcessMemoryLimits(int hard_mib, std::string* error_out = nullptr);
