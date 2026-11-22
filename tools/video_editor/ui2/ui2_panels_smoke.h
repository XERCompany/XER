// `ui2_panels` headless smoke check — registered exe-side from
// main.cc next to `ui2_timeline`. Exercises the M3 wave-1 panels
// without a window or renderer:
//   * BinGridSurface virtualization rectangle math (columns/rows,
//     CardRect/ItemAtPoint round trip, VisibleIndexRange) plus
//     click/marquee state and the drag handoff hooks;
//   * MediaPanel search filtering + deterministic sort modes;
//   * EffectsPanel tree population from BuildFxRegistryEntries
//     (registry incl. track_mosaic / smart_reframe) + search;
//   * InspectorPanel selection-provider round trip;
//   * Inspector edit -> TimelineHistory::ReplaceClip -> Undo (the
//     "one gesture = one undo" contract through the real history).
#pragma once

#include <string>

namespace XER::VideoEditor::Ui2 {

// Returns "" on success or a one-line failure reason (Smoke::CheckFn).
std::string Ui2PanelsSmoke();

}  // namespace XER::VideoEditor::Ui2
