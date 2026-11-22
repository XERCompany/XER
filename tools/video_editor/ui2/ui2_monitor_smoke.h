// `ui2_monitor` headless smoke check — registered exe-side from
// main.cc next to ui2_shell/ui2_timeline. Exercises the M2 monitor
// pair without a window: MonitorSurface letterbox/overlay math,
// MonitorPanel chrome composition + hook dispatch through a real
// WidgetTree, and the VeApp-level wiring (scope routing, exclusive
// playback, source marks -> three-point edit).
//
// Frame path note: surfaces are fed through SetFrame with synthetic
// BGRA — no real decode is exercised here (the PreviewEngine pair
// only runs its lifecycle on empty/synthetic projects; real-media
// playback is verified by the interactive app).
#pragma once

#include <string>

namespace XER::VideoEditor::Ui2 {

// Returns "" on success or a one-line failure reason (Smoke::CheckFn).
std::string Ui2MonitorSmoke();

}  // namespace XER::VideoEditor::Ui2
