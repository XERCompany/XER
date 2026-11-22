// Text-based editing helpers for the video editor's Transcript dock
// panel — the Descript/Premiere "Paper Edit" surface: the transcript
// IS the edit surface; a text selection maps to a source-time range
// which maps (through the bound clip's in_point/rate) to a timeline
// range the timeline model can lift/extract.
//
// UI-free like the rest of the core lib: pure data transforms the
// panel widget and the smoke checks both consume. Whisper emits
// SEGMENT-level spans only (whisper_transcribe.h), so word timing is
// distributed evenly across each segment's span — true per-word
// timestamps are a deferred gap; the edit surface behaves the same.
#pragma once

#include <string>
#include <vector>

#include "ai/whisper_transcribe.h"
#include "timeline_model.h"

namespace XER::VideoEditor {

// One display word on the transcript surface. Times are SOURCE
// seconds (same domain as Clip::in_point) so the mapping survives
// re-trims and rate changes — ClipTag uses the same contract.
struct TranscriptWord {
    std::string text;          // single token, no whitespace
    double src_t0 = 0.0;
    double src_t1 = 0.0;
    bool segment_start = false;  // first word of its whisper segment
};

// Splits every segment's text on whitespace into words, distributing
// the segment's [t0,t1] across the words proportionally to text
// length (longer word -> longer span). Empty segments produce no
// words; degenerate (t1 <= t0) spans still emit words with t0 == t1.
std::vector<TranscriptWord> TranscriptWordsFromSegments(
    const std::vector<Ai::TranscriptSegment>& segments);

// Clip->timeline mapping. Consumed source span is
// [in_point, in_point + duration * playback_rate); times outside it
// have no timeline position (the transcript covers the whole media
// file while the clip may use only part of it).
// SourceToTimeline returns quiet NaN for unmapped times.
double SourceToTimeline(const Clip& clip, double src_t);
// Maps a source range to a timeline range clamped to the clip's
// consumed span; false when the range does not intersect it.
bool SourceRangeToTimeline(const Clip& clip, double src_t0,
                           double src_t1, double* tl_t0, double* tl_t1);

// ---- per-media transcript cache ------------------------------------
// Transcriptions are expensive (model load + full audio decode), so
// finished results persist per media file: an in-memory map keyed by
// TranscriptCacheKey plus an on-disk JSON file per key under the
// app's cache dir, so a transcript survives restarts without
// re-running whisper. The key folds the normalized path AND the file
// size — a replaced/re-edited source with a different size misses the
// cache instead of serving a stale transcript.

// "<fnv1a64(path|size)>". Stable across runs (FNV-1a, not std::hash).
std::string TranscriptCacheKey(const std::string& utf8_path);

// JSON round-trip for the on-disk cache files. The stored media path
// and file size are validated on load — false (no out mutation) on
// malformed JSON, missing fields, or a size mismatch.
bool SaveTranscriptCache(const std::string& utf8_json_path,
                         const std::string& media_path,
                         const std::vector<Ai::TranscriptSegment>& spans);
bool LoadTranscriptCache(const std::string& utf8_json_path,
                         const std::string& media_path,
                         std::vector<Ai::TranscriptSegment>* out);

}  // namespace XER::VideoEditor
