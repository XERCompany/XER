// XERSyncMedia.exe — media sync engine worker for the XER video
// editor. Runs the shared JSON-lines worker loop serving "sync_plan":
// the editor ships the clip list (path/in/out/rate), THIS process
// decodes the audio peaks through its own FFmpeg path and runs the
// bounded envelope correlation, then replies with the per-clip shift
// plan — the whole math+decode off the editor process so a heavy
// multi-cam sync (or a decoder crash) never touches the UI.
//
// Cancel: there is no cancel message. The editor kills this process
// (WorkerPipe::KillWorker — it is job-grouped and disposable) to abort
// a running plan; the parent respawns it on the next request.
//
// stdin/stdout are the protocol pipe; diagnostics go to stderr.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "media_worker_server.h"

namespace {

// The sync client singleton must be dead inside THIS process: if any
// in-process code path ever reached for it, it must stay in-process
// rather than spawn a grandchild worker.
void DisableNestedWorkers() {
#if defined(_WIN32)
    SetEnvironmentVariableA("XER_VE_SYNC_WORKER", "0");
#else
    setenv("XER_VE_SYNC_WORKER", "0", 1);
#endif
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--help") == 0 ||
            std::strcmp(arg, "-h") == 0 ||
            std::strcmp(arg, "/?") == 0) {
            std::fputs(
                "XERSyncMedia - XER media sync engine worker\n"
                "Serves sync_plan over the JSON-lines worker\n"
                "protocol until stdin EOF.\n",
                stderr);
            return 0;
        }
    }
    DisableNestedWorkers();
    return XER::VideoEditor::RunSyncMediaWorker("XERSyncMedia");
}
