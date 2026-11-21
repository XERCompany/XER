// media_worker_server.h — the worker-subprocess entries. JSON-lines
// loops on stdin/stdout; see media_worker_protocol.h for the wire
// contract. The processes exist so a decode/script crash kills the
// worker, not the editor — every line is independent (no session
// state shared across requests beyond a role's own engine cache).
//
// Roles:
//   RunMediaWorker(role)   — the decode worker: poster/peaks plus the
//                            role-agnostic ping/info probes. Serves
//                            video_editor --media-worker (self-spawn
//                            fallback, role "media-worker") and the
//                            dedicated XERVideoLoader.exe.
//   RunSyncMediaWorker(role) — XERSyncMedia.exe: the audio-sync engine
//                            (peak decode + envelope correlation) runs
//                            here, off the editor process.
//   RunFxScriptWorker(role) — XERFxScript.exe: the sandboxed JS/V8
//                            script-fx host (list/validate/audio/
//                            transition).
//   RunStubWorker(role)    — liveness-only placeholder for future
//                            roles: ping/info answer normally, every
//                            other kind gets an explicit
//                            "not implemented" error reply.
#pragma once

#include <functional>
#include <string>

#include "media_worker_protocol.h"   // MediaWorker::Request

namespace XER::VideoEditor {

// Blocks on stdin until EOF (parent closed the pipe / died) and
// returns a process exit code. stderr gets per-request diagnostics.
// `role` rides every ping/info reply so the parent can identify which
// binary answered (dedicated exe vs self-spawn).
int RunMediaWorker(const char* role = "media-worker");
int RunSyncMediaWorker(const char* role);
int RunFxScriptWorker(const char* role);

// Same loop, minus the engines — liveness only (see file comment).
int RunStubWorker(const char* role);

// The shared JSON-lines loop every role runs: malformed lines get an
// error reply, ping/info are answered by the loop itself (with `role`
// and the `serves` csv it advertises on info), and everything else is
// handed to `handler` — which receives both the parsed envelope and
// the raw line for structured-body kinds.
using MediaRequestHandler = std::function<std::string(
    const MediaWorker::Request& request, const std::string& raw_line)>;
int RunJsonWorkerLoop(const char* role, const char* serves,
                      const MediaRequestHandler& handler);

}  // namespace XER::VideoEditor
