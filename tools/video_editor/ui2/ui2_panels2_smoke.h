// `ui2_panels2` headless smoke check — registered exe-side from
// main.cc next to `ui2_panels`. Exercises the M3 wave-3 right-dock
// panels without a window or renderer:
//   * MarkersPanel — model sync (add/remove/update through the real
//     VeApp hooks -> TimelineHistory -> project_.markers), search
//     filtering, the dbl-click seek funnel, and the session
//     annotation store;
//   * HistoryPanel — UndoLabels/RedoLabels enumeration, the undo /
//     divider / redo row ordering, and click -> repeated
//     Undo()/Redo() jump semantics;
//   * QueuePanel — the deterministic simulated-drive lifecycle
//     (Queued -> Running -> Paused -> Running -> Done, plus cancel +
//     retry + ClearFinished) that stands in for the single-job
//     ExportEngine when no on_submit driver is installed;
//   * ExtensionsPanel — enumerate-hook feed, session enable/disable,
//     verified-gating for Run/OpenUi, and the settings seam;
//   * TranscriptPanel — segment->word feed, click->seek through the
//     bound clip's source->timeline map, word-range selection ->
//     marker / captions / ripple-delete verbs through the real
//     shell hooks (fake transcript data — no whisper needed).
#pragma once

#include <string>

namespace XER::VideoEditor::Ui2 {

// Returns "" on success or a one-line failure reason (Smoke::CheckFn).
std::string Ui2Panels2Smoke();

}  // namespace XER::VideoEditor::Ui2
