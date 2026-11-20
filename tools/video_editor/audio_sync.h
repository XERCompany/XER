// One-click audio sync — aligns every audio-bearing clip on the
// timeline to a reference clip by cross-correlating waveform
// envelopes (PluralEyes-style). Built for wedding/event/documentary
// multi-cam: the correlation runs on a smoothed ENERGY envelope at
// ~21ms resolution, which is robust to quiet rooms, camera mics vs
// recorders at different distances, and partially overlapping takes.
//
// Flow: each unique source file is decoded once to peak buckets
// (media_prep / DecodeAudioPeaks — subprocess-isolated, disk-cached),
// envelopes are normalized and correlated, and the caller applies
// every confident match through TimelineHistory::MoveClip so the
// whole operation stays a normal undoable edit.
#pragma once

#include <string>
#include <vector>

#include "timeline_model.h"

namespace XER::VideoEditor {

struct AudioSyncEntry {
    std::string clip_id;
    double new_timeline_start = 0.0;  // where the clip should move
    double delta_seconds = 0.0;       // applied shift (signed)
    double confidence = 0.0;          // normalized correlation [0,1]
    bool matched = false;             // false → leave the clip in place
    std::string note;                 // human reason when !matched
};

struct AudioSyncReport {
    std::string reference_clip_id;
    std::vector<AudioSyncEntry> entries;
    std::string status;               // summary for the status bar
};

// Pure analysis — reads the project, decodes peak envelopes, fills
// `report` with per-clip moves. Does NOT mutate the project: the
// caller applies entries (history-wrapped MoveClip or plain
// TimelineModel::MoveClip for batch/headless use) and decides what a
// failed move means.
//
// If `reference_clip_id` is empty the earliest clip on the lowest
// track index is used. Clips sharing the reference's source_path are
// skipped (already the same take). Returns false only when there is
// nothing to correlate at all (no reference / no decodable audio).
bool ComputeAudioSyncPlan(const Project& project,
                          const std::string& reference_clip_id,
                          AudioSyncReport* report,
                          std::string* error_out);

}  // namespace XER::VideoEditor
