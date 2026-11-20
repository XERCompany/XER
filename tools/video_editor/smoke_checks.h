// Repeatable machine-checkable smoke mode for video_editor.exe —
// `video_editor --smoke` runs every registered self-check without
// opening a window, prints one line per check
//   SMOKE <name> OK
//   SMOKE <name> FAIL <reason>
// to stdout and exits 0 on all-OK / 1 on any FAIL. `--smoke-list`
// prints the check names one per line (same order as the run).
//
// Every check is synchronous and bounded; a watchdog caps the whole
// run (~15 s) so a hung subsystem still exits non-zero instead of
// blocking a harness. All checks are headless by design — the only
// surfaces exercised are the UI-free core (timeline model, history,
// store, catalog, presets, generated sources, compositor, mixer,
// autosave) plus the assets-layer boot and a tiny FFmpeg export.
#pragma once

#include <string>
#include <vector>

namespace XER::VideoEditor::Smoke {

// Names of every registered check, in run order. Stable across runs —
// automation can diff `--smoke-list` output.
std::vector<std::string> CheckNames();

// Runs every check in order, printing the SMOKE lines above to
// stdout. Returns the process exit code: 0 when all checks pass,
// 1 when any check fails or the watchdog fires.
int RunAll();

}  // namespace XER::VideoEditor::Smoke
