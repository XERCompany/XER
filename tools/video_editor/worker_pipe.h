// worker_pipe.h — generic spawned-subprocess JSON-lines client shared
// by every named editor worker role. One WorkerPipe owns one spawned
// child: requests are single JSON lines on the child's stdin, replies
// single JSON lines on its stdout, correlated by "id" (the caller
// allocates ids via NextId and embeds them in the encoded request).
//
// Spawn strategy (WorkerPipeSpec): the dedicated exe sitting BESIDE
// this process's module wins — never PATH/CWD, so an unrelated same-
// named binary can never be picked up; `self_spawn_arg` optionally
// falls back to re-execing THIS exe with a worker flag (dev builds).
// A request that times out or a dead process kills the child; the
// next request respawns on demand, and after kMaxDeaths the pipe
// stays down so callers degrade to their in-process path instead of
// crash-looping a subprocess.
//
// Cancel semantics: KillWorker() terminates the child WITHOUT the
// crash-death penalty (the reader exit that normally counts a death
// is suppressed) — in-flight RoundTrips return false immediately and
// the next request respawns. This is how the editor cancels a remote
// job: the worker is job-grouped and disposable by design.
//
// Windows-only today — other platforms get stubs where Available()
// is always false and callers run their in-process path.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace XER::VideoEditor {

struct WorkerPipeSpec {
    const char* dedicated_exe = nullptr;   // "XERSyncMedia.exe"
    const char* self_spawn_arg = nullptr;  // "--media-worker"
    const char* env_disable = nullptr;     // "XER_VE_MEDIA_WORKER"
};

class WorkerPipe {
public:
    explicit WorkerPipe(WorkerPipeSpec spec);
    ~WorkerPipe();
    WorkerPipe(const WorkerPipe&) = delete;
    WorkerPipe& operator=(const WorkerPipe&) = delete;

    // false → caller runs its in-process path (env kill-switch, spawn
    // failure, or the worker died too many times). Spawns lazily on
    // first call.
    bool Available();

    // Monotonic request id allocator (thread-safe).
    std::uint64_t NextId();

    // Sends `request_line` (one complete JSON object whose "id" equals
    // `id`) and waits up to timeout_ms for the reply line carrying the
    // same id. false → timeout / dead worker / broken transport; the
    // child is torn down so the next call respawns. Concurrent callers
    // are serialized by the id-keyed reply routing.
    bool RoundTrip(std::uint64_t id, const std::string& request_line,
                   int timeout_ms, std::string* reply_line);

    // Liveness probe: sends {"kind":"ping"} and decodes the pong.
    // role_out receives the worker's role string ("XERSyncMedia"...)
    // so callers can prove WHICH binary answered.
    bool Ping(std::string* role_out, int timeout_ms = 5000);

    // Deliberate kill — cancel semantics: the child dies with no
    // crash-death penalty, in-flight RoundTrips fail fast, and the
    // next request respawns cleanly.
    void KillWorker();

    int Deaths() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Role singletons — dedicated engine exes with NO self-spawn fallback
// (a missing exe degrades the caller to its in-process path, matching
// the editor's safety contract). The env vars force-disable the role;
// the worker processes set their own var at startup so in-process
// calls inside a worker can never recurse into another subprocess.
WorkerPipe& SyncMediaPipe();   // XERSyncMedia.exe — XER_VE_SYNC_WORKER=0
WorkerPipe& FxScriptPipe();    // XERFxScript.exe  — XER_VE_FX_WORKER=0

}  // namespace XER::VideoEditor
