// media_worker_client.cc — see media_worker_client.h for the contract.
//
// The transport machinery lives in worker_pipe.cc (WorkerPipe); this
// file adds the media-role specifics on top: spawn spec (dedicated
// XERVideoLoader.exe beside this exe, self-spawn `--media-worker`
// fallback, XER_VE_MEDIA_WORKER env kill-switch), per-kind timeouts,
// and the suspect-path negative cache — a path that times out or
// kills the worker is remembered so a respawn is never asked to
// re-decode the file that just crashed it (the in-process fallback
// handles that one).

#include "media_worker_client.h"

#include "media_worker_protocol.h"
#include "worker_pipe.h"

#include <mutex>
#include <set>
#include <string>

namespace XER::VideoEditor {
namespace {

constexpr int kPosterTimeoutMs = 15000;
constexpr int kPeaksTimeoutMs = 120000;
constexpr int kPingTimeoutMs = 5000;
constexpr std::size_t kMaxSuspects = 256;

}  // namespace

struct MediaWorkerClient::Impl {
    WorkerPipe pipe{WorkerPipeSpec{"XERVideoLoader.exe",
                                   "--media-worker",
                                   "XER_VE_MEDIA_WORKER"}};
    std::mutex suspects_mu;
    std::set<std::string> suspects;   // paths that timed out / died

    bool Suspected(const std::string& path) {
        std::lock_guard<std::mutex> lock(suspects_mu);
        return suspects.count(path) != 0u;
    }

    void Suspect(const std::string& path) {
        if (path.empty()) return;
        std::lock_guard<std::mutex> lock(suspects_mu);
        if (suspects.size() < kMaxSuspects) {
            suspects.insert(path);
        }
    }
};

MediaWorkerClient& MediaWorkerClient::Shared() {
    static MediaWorkerClient inst;
    return inst;
}

MediaWorkerClient::~MediaWorkerClient() {
    delete impl_;
}

bool MediaWorkerClient::Available() {
    if (impl_ == nullptr) {
        impl_ = new Impl();
    }
    return impl_->pipe.Available();
}

bool MediaWorkerClient::PosterAt(const std::string& path,
                                 double seconds, video::Frame* out) {
    if (impl_ != nullptr && impl_->Suspected(path)) return false;
    if (!Available()) return false;
    MediaWorker::Request req;
    req.kind = MediaWorker::Kind::kPosterAt;
    req.path = path;
    req.seconds = seconds;
    const std::uint64_t id = impl_->pipe.NextId();
    req.id = id;
    std::string reply;
    if (!impl_->pipe.RoundTrip(id, MediaWorker::EncodeRequest(req),
                               kPosterTimeoutMs, &reply)) {
        impl_->Suspect(path);
        return false;
    }
    std::uint64_t rid = 0;
    return MediaWorker::DecodePosterReply(reply, &rid, out);
}

bool MediaWorkerClient::Peaks(const std::string& path,
                              AudioPeaks* out) {
    if (impl_ != nullptr && impl_->Suspected(path)) return false;
    if (!Available()) return false;
    MediaWorker::Request req;
    req.kind = MediaWorker::Kind::kPeaks;
    req.path = path;
    const std::uint64_t id = impl_->pipe.NextId();
    req.id = id;
    std::string reply;
    if (!impl_->pipe.RoundTrip(id, MediaWorker::EncodeRequest(req),
                               kPeaksTimeoutMs, &reply)) {
        impl_->Suspect(path);
        return false;
    }
    std::uint64_t rid = 0;
    return MediaWorker::DecodePeaksReply(reply, &rid, out);
}

bool MediaWorkerClient::Ping(std::string* role_out) {
    if (!Available()) return false;
    return impl_->pipe.Ping(role_out, kPingTimeoutMs);
}

}  // namespace XER::VideoEditor
