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

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "media_catalog.h"   // AudioPeaks (provider seam)
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

// Peaks-provider seam: called once per unique source path, must fill
// `out` and return true when the file has usable audio. MAY BLOCK —
// the plan invokes it inline, so callers that need the decode off the
// UI thread run the whole plan on a worker. Production code wires
// MediaPrep::LookupPeaks (bounded wait; subprocess-isolated and
// disk-cached decodes); tests inject synthetic peaks and never touch
// a decoder. Empty → the built-in DecodeAudioPeaks subprocess path.
using AudioSyncPeaksProvider =
    std::function<bool(const std::string& canon_path, AudioPeaks* out)>;

struct AudioSyncPlanOptions {
    AudioSyncPeaksProvider peaks;
    // Polled between clips and inside the correlation sweep. Flipping
    // it aborts the plan (returns false, error "audio sync
    // cancelled") — nothing is half-applied since the plan never
    // mutates the project.
    std::atomic<bool>* cancel = nullptr;
    // Called on the plan thread after each candidate clip resolves:
    // (done, total). Keep it cheap — store to atomics; marshal UI
    // updates from the polling side.
    std::function<void(int done, int total)> progress;
};

// Pure analysis — reads the project, obtains peak envelopes (through
// `options.peaks` when set, else DecodeAudioPeaks), fills `report`
// with per-clip moves. Does NOT mutate the project: the caller
// applies entries (history-wrapped MoveClip or plain
// TimelineModel::MoveClip for batch/headless use) and decides what a
// failed move means.
//
// If `reference_clip_id` is empty the earliest clip on the lowest
// track index is used. Clips sharing the reference's source_path are
// skipped (already the same take). Returns false only when there is
// nothing to correlate at all (no reference / no decodable audio) or
// the run was cancelled.
bool ComputeAudioSyncPlan(const Project& project,
                          const std::string& reference_clip_id,
                          AudioSyncReport* report,
                          std::string* error_out,
                          const AudioSyncPlanOptions& options = {});

// Headless/test convenience: the plan driven by `peaks` instead of a
// decoder — no media files, no catalog, no subprocess. Exercises the
// envelope math, the lag search, and the lag→timeline-shift sign.
bool ComputeAudioSyncPlanWithPeaks(
    const Project& project, const std::string& reference_clip_id,
    const AudioSyncPeaksProvider& peaks, AudioSyncReport* report,
    std::string* error_out);

// Out-of-process variant: routes the SAME plan through
// XERSyncMedia.exe (SyncMediaPipe) — the worker decodes the peaks and
// runs the correlation off the editor process. Tri-state result:
//   kPlanned     — report filled from the worker reply
//   kUnavailable — worker not spawnable / transport failed / garbage
//                  reply: the caller should run the in-process plan
//   kFailed      — genuine failure (no clips, worker-reported error,
//                  cancelled): error_out filled; do NOT fall back
// Windows-only: always kUnavailable elsewhere.
// CANCEL: a remote request cannot be polled mid-flight — the caller
// cancels by killing the worker (WorkerPipe::KillWorker, job-grouped
// and disposable); this call then returns kUnavailable/kFailed on its
// own. See worker_pipe.h for the no-death-penalty kill semantics.
enum class AudioSyncRoute { kPlanned, kUnavailable, kFailed };
AudioSyncRoute ComputeAudioSyncPlanViaWorker(
    const Project& project, const std::string& reference_clip_id,
    AudioSyncReport* report, std::string* error_out,
    std::atomic<bool>* cancel = nullptr);

}  // namespace XER::VideoEditor
