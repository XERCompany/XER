// sync_media_server.cc — XERSyncMedia's request handler: the whole
// audio-sync compute lives in this worker process. A "sync_plan"
// request carries the clip list (path/in/out/rate + optional injected
// peaks for tests); the handler rebuilds a minimal Project, decodes
// any non-injected peaks through the local FFmpeg path, runs the same
// envelope correlation the in-process plan uses, and returns the
// per-clip shift plan. The editor then applies the moves — the worker
// is pure compute and owns no undoable state.
//
// CANCEL: there is no cancel message — the editor kills this process
// (WorkerPipe::KillWorker, job-grouped and disposable) to abort a
// running plan. Nothing here needs cleanup hooks.
#include "media_worker_server.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "audio_sync.h"
#include "media_catalog.h"
#include "media_worker_protocol.h"
#include "timeline_model.h"

namespace XER::VideoEditor {
namespace {

std::string HandleSyncPlan(const std::string& line) {
    std::uint64_t id = 0;
    MediaWorker::SyncPlanRequestData data;
    if (!MediaWorker::DecodeSyncPlanRequest(line, &id, &data)) {
        return MediaWorker::EncodeErrorReply(id, "malformed sync_plan");
    }

    // Injected peaks (smoke/relay) win over decode so the correlation
    // math can be exercised without media fixtures. Everything else
    // decodes HERE — moving that work off the editor is the point of
    // this process.
    std::map<std::string, AudioPeaks> injected;
    for (const auto& c : data.clips) {
        if (c.has_peaks) injected[c.path] = c.peaks;
    }

    // Rebuild the smallest Project that drives ComputeAudioSyncPlan:
    // media clips on one synthetic track, in request order.
    Project project;
    project.tracks.push_back(Track{TrackKind::Audio, "A1"});
    project.clips.reserve(data.clips.size());
    for (std::size_t i = 0; i < data.clips.size(); ++i) {
        const auto& c = data.clips[i];
        Clip clip;
        clip.id = c.clip_id.empty()
                      ? "clip" + std::to_string(i)
                      : c.clip_id;
        clip.source_path = c.path;
        clip.source.kind = SourceKind::Media;
        clip.track = 0;
        clip.timeline_start = c.start;
        clip.in_point = c.in_point;
        clip.playback_rate = c.rate > 0.0 ? c.rate : 1.0;
        clip.duration =
            c.out_point > c.in_point ? c.out_point - c.in_point : 0.0;
        project.clips.push_back(std::move(clip));
    }
    std::string reference_id;
    if (data.ref_index >= 0 &&
        data.ref_index < static_cast<int>(project.clips.size())) {
        reference_id =
            project.clips[static_cast<std::size_t>(data.ref_index)].id;
    }

    AudioSyncPlanOptions options;
    options.peaks = [&injected](const std::string& path,
                              AudioPeaks* out) -> bool {
        const auto it = injected.find(path);
        if (it != injected.end()) {
            *out = it->second;
            return true;
        }
        return DecodeAudioPeaks(path, out, nullptr);
    };

    AudioSyncReport report;
    std::string error;
    if (!ComputeAudioSyncPlan(project, reference_id, &report, &error,
                              options)) {
        return MediaWorker::EncodeErrorReply(
            id, error.empty() ? "sync plan failed" : error);
    }

    MediaWorker::SyncPlanResult result;
    result.reference_clip_id = report.reference_clip_id;
    result.status = report.status;
    result.entries.reserve(report.entries.size());
    for (const auto& e : report.entries) {
        MediaWorker::SyncPlanResultEntry entry;
        entry.clip_id = e.clip_id;
        entry.new_timeline_start = e.new_timeline_start;
        entry.delta_seconds = e.delta_seconds;
        entry.confidence = e.confidence;
        entry.matched = e.matched;
        entry.note = e.note;
        result.entries.push_back(std::move(entry));
    }
    return MediaWorker::EncodeSyncPlanReply(id, result);
}

}  // namespace

int RunSyncMediaWorker(const char* role) {
    return RunJsonWorkerLoop(
        role, "sync_plan,ping,info",
        [](const MediaWorker::Request& req,
           const std::string& line) -> std::string {
            if (req.kind == MediaWorker::Kind::kSyncPlan) {
                return HandleSyncPlan(line);
            }
            return MediaWorker::EncodeErrorReply(
                req.id, "kind not implemented in this worker role");
        });
}

}  // namespace XER::VideoEditor
