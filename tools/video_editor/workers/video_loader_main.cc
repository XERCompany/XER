// XERVideoLoader.exe — dedicated named worker process for the XER
// video editor. video_editor.exe (MediaWorkerClient) spawns this
// binary with stdin/stdout redirected to pipes and routes all media
// decode work to it over the JSON-lines protocol in
// media_worker_protocol.h:
//   req : {"id":N,"kind":"poster"|"peaks"|"ping"|"info",...}
//   rsp : {"id":N,"ok":true|false, ...payload...}
//
// The process exists for crash isolation: a decoder fault (malformed
// media, FFmpeg edge cases) kills THIS worker, never the editor. The
// editor re-spawns it on demand and never re-sends the path that just
// killed it (the in-process fallback decodes that one). This exe runs
// the SAME server loop as `video_editor --media-worker` — the legacy
// self-spawn path that stays as the dev-build fallback when
// XERVideoLoader.exe is absent.
//
// Lifetime: the editor closes our stdin when it shuts down; the read
// loop sees EOF and exits. The parent's WinJobGroup also reaps us on
// a parent crash — no Job handling is needed here (we are the
// assigned child, not the assigner).
//
// Manual debugging:
//   XERVideoLoader.exe            speaks JSON lines until EOF
//   XERVideoLoader.exe --help     usage on stderr, then exit
//   XERVideoLoader.exe --version  version on stderr, then exit

#include <cstdio>
#include <cstring>

#include "media_worker_server.h"

namespace {

void PrintUsage() {
    // stdout is the protocol pipe in worker mode — diagnostics go to
    // stderr so a stray --help can never corrupt a reply stream.
    std::fputs(
        "XERVideoLoader - XER video editor media decode worker\n"
        "\n"
        "stdin : one JSON request per line "
        "(poster/peaks/ping/info)\n"
        "stdout: one JSON reply per line\n"
        "\n"
        "Runs until stdin hits EOF (the editor closes the pipe on\n"
        "shutdown). Spawned by video_editor.exe; see\n"
        "tools/video_editor/media_worker_protocol.h for the wire\n"
        "format.\n",
        stderr);
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--help") == 0 ||
            std::strcmp(arg, "-h") == 0 ||
            std::strcmp(arg, "/?") == 0) {
            PrintUsage();
            return 0;
        }
        if (std::strcmp(arg, "--version") == 0) {
            std::fputs("XERVideoLoader 1.0.0.0\n", stderr);
            return 0;
        }
        // Unknown args are ignored — the editor owns the command line
        // and may add flags the worker does not need.
    }
    // The role string rides every ping/info reply so the parent can
    // prove the dedicated binary — not a self-spawn fallback —
    // answered. Blocks until the editor closes stdin.
    return XER::VideoEditor::RunMediaWorker("XERVideoLoader");
}
