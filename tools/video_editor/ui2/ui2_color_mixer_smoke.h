// `ui2_color_mixer` headless smoke check — registered exe-side from
// main.cc next to `ui2_panels`. Exercises the M3 wave-2 panels
// without a window or renderer:
//   * ColorPanel wheel hit mapping — real pointer events on the
//     wheel widget map to ColorParams wheel offsets + the
//     on_color_changed commit hook fires;
//   * eyedropper arm + sample callback round trip —
//     ArmWhiteBalance arms, ApplyPickedColor commits the
//     neutralizing temperature/tint correction;
//   * ScopesPanel pull path — a synthetic BGRA frame through
//     SetFrameSource produces a non-empty luma histogram + sane
//     waveform envelope (vectorscope/parade stay honest stubs);
//   * MixerPanel strip set from a seeded project + the fader verb
//     -> TimelineHistory commit -> Undo round trip (track gain +
//     master gain + mute);
//   * VeApp wiring — the right-dock Color/Mix/Scopes tabs hold the
//     real panels (structure accessors).
#pragma once

#include <string>

namespace XER::VideoEditor::Ui2 {

// Returns "" on success or a one-line failure reason (Smoke::CheckFn).
std::string Ui2ColorMixerSmoke();

}  // namespace XER::VideoEditor::Ui2
