// media_worker_server.h — the `--media-worker` subprocess entry.
// JSON-lines loop on stdin/stdout; see media_worker_protocol.h for the
// wire contract. The process exists so a decoder crash kills the
// worker, not the editor — every line is independent (no session
// state, decoders are opened per request and discarded).
#pragma once

namespace XER::VideoEditor {

// Blocks on stdin until EOF (parent closed the pipe / died) and
// returns a process exit code. stderr gets per-request diagnostics.
int RunMediaWorker();

}  // namespace XER::VideoEditor
