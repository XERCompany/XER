// cef_runtime.h — Chromium Embedded Framework process bootstrap for XER.
//
// Deep module: callers get three verbs — RunSubprocessIfAny (first line of
// main, mandatory for CEF child processes), Initialize (browser-process
// startup, multi-threaded message loop so no pump hook is needed in the
// app's Win32 loop), Shutdown. When the binary distribution is absent the
// module compiles to inert stubs (XER_HAS_CEF=0): Initialize reports false
// with a reason, everything else is a no-op.
#pragma once

#include <string>
#include <vector>

namespace XER::Cef {

// Options for Initialize(). Paths are UTF-8 absolute or exe-relative; an
// empty path means "next to the running executable" which is where the
// deployment step places CEF binaries.
struct RuntimeOptions {
    std::string resources_dir;   // default: <exe>/cef or <exe>
    std::string locales_dir;     // default: <resources>/locales
    std::string subprocess_path; // default: <exe>/bootstrap.exe
    std::string cache_dir;       // empty -> in-memory cache
    std::string log_file;        // empty -> debug.log; set to a path to
                                 // redirect the Chromium log
    int log_severity = -1;       // -1 -> WARNING; else cef_log_severity_t
                                 // (LOGSEVERITY_DISABLE silences stderr too)
    bool headless = false;       // windowless rendering (no GPU browser)
    // Passes "disable-gpu" to Chromium: the GPU process still launches
    // in software mode but skips hardware init — the right default for
    // embedded panels that only host static/extension pages, and it
    // sidesteps GPU-driver probing entirely.
    bool disable_gpu = false;
    bool enable_remote_debug = false;
    int remote_debug_port = 0;
    // Extra Chromium switches applied via OnBeforeCommandLineProcessing.
    // Each entry is a switch name with optional "name=value" (no "--").
    std::vector<std::string> command_line_switches;
};

// Must be invoked before anything else in main(): CEF subprocesses
// (renderer/GPU/utility launched via bootstrap.exe) never return from it.
// Returns the process exit code when this invocation IS a subprocess —
// caller returns it immediately; returns -1 for the browser process.
int RunSubprocessIfAny(int argc, char** argv);

// One-time browser-process init. Idempotent — repeated calls report the
// existing state. error_out receives a contextual reason on failure
// (missing libcef.dll, CefInitialize failure, etc.).
bool Initialize(const RuntimeOptions& options = {},
                std::string* error_out = nullptr);
bool IsInitialized();
// Drains CEF browsers and shuts the runtime down. Safe when never
// initialized. Called automatically at process exit if forgotten.
void Shutdown();
// True when the module was compiled with real CEF support.
bool Available();

}  // namespace XER::Cef
