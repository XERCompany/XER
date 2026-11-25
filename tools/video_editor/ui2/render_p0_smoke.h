// `render_p0` headless smoke check — registered exe-side from
// main.cc next to the other ui2 checks. Covers the P0
// render-pipeline tickets from
// docs/wiki/research/2026-11-24-adobe-render-pipeline.md §6:
//   T1  dropped-frame counter + monitor stoplight badge
//   T2  timeline render-status strip (ClipCostClass green/yellow/red)
//   T3  paused-vs-playing quality split (paused = full res)
//   T4  measured adaptive ladder (auto tier step under overload)
//
// A synthetic PreviewEngine is driven by an injected decoder that
// sleeps ~1.4x the frame budget per frame — that both skips
// project-frame indices (drops) and saturates the measured render
// time (adaptive), so the counters exercise real engine paths.
#pragma once

#include <string>

namespace XER::VideoEditor::Ui2 {

// Returns "" on success or a one-line failure reason (Smoke::CheckFn).
std::string RenderP0Smoke();

}  // namespace XER::VideoEditor::Ui2
