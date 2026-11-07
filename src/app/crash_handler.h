#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace XER::App::Crash {

// Installs unhandled-exception / terminate / purecall / invalid-parameter
// handlers. Call FIRST in main(), before any other subsystem initializes —
// the handler resolves the crash directory lazily at crash time so it works
// even when the cache layer is not up yet.
// Idempotent; safe to call once per process.
void Install();

// Parses --crash-depth=<n> command line arguments (0 when absent).
// Used by the app to suppress duplicate crash dialogs after auto-restart.
int RestartDepth(int argc, char** argv);

// Consumes the pending-crash marker (crashes/pending.txt). Returns the
// report directory exactly once — the marker is cleared so the startup
// dialog is shown a single time.
std::optional<std::filesystem::path> ConsumePendingReport();

// Opens the report directory in Explorer.
void OpenReportFolder(const std::filesystem::path& dir);

// Session context recorded into state.json at crash time. The app calls
// SetSessionProject when a project/graph is opened; values are copied.
// `context` is a free-form label (project/graph name) for triage.
void SetSessionProject(std::int64_t id, std::string context);

// Root directory that holds all crash report folders.
std::filesystem::path CrashesRoot();

} // namespace XER::App::Crash
