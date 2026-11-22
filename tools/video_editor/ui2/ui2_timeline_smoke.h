// `ui2_timeline` headless smoke check — registered exe-side from
// main.cc next to `ui2_shell`. Exercises the M2 timeline without a
// window: time/track geometry, clip hit-testing, select/move/split
// gestures through TimelineHooks, snapping, tool switching,
// marker/work-area/track-control hooks, the gesture->history-macro
// bracket through TimelineBridge, and the non-blocking async media
// lookup seam.
#pragma once

#include <string>

namespace XER::VideoEditor::Ui2 {

// Returns "" on success or a one-line failure reason (Smoke::CheckFn).
std::string Ui2TimelineSmoke();

}  // namespace XER::VideoEditor::Ui2
