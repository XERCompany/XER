// `ui2_lifecycle` headless smoke check — registered exe-side from
// main.cc next to `ui2_panels2`. Exercises the application-level
// dialogs + lifecycle guards without a window or renderer:
//   * Export dialog: the form seeds from the document, validates a
//     missing output path, and SubmitExportForm() composes a real
//     QueuePanel row (the same code the dialog's Export button runs);
//   * Project settings: ApplyProjectSettings() commits through
//     TimelineHistory so the canvas/fps/proxy fields undo cleanly;
//   * Dirty flag: a document mutation arms it, SaveProjectAs()
//     clears it;
//   * Autosave: a Tick pump past the poll cadence writes a slot in
//     <project dir>/autosave (interval forced to 0 via
//     SetAutosaveIntervalMs);
//   * Close guard: RequestClose() on a dirty project returns false
//     and opens the prompt; ResolveCloseGuard(Cancel) keeps it
//     blocked, Discard approves, and Save writes + clears dirty.
#pragma once

#include <string>

namespace XER::VideoEditor::Ui2 {

// Returns "" on success or a one-line failure reason (Smoke::CheckFn).
std::string Ui2LifecycleSmoke();

}  // namespace XER::VideoEditor::Ui2
