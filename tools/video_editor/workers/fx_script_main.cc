// XERFxScript.exe — FX script engine worker for the XER video editor.
// Hosts the sandboxed JS/V8 effects runtime OUT of the editor process
// and serves fx_list / fx_validate / fx_audio / fx_transition over the
// JSON-lines worker protocol — a throwing or runaway script kills
// this worker (after the in-engine watchdog's terminate attempt), not
// the editor.
//
// The V8 platform is initialized lazily per process inside the first
// JsFxRuntime::EnsureLoaded (JsEngine::Create's std::call_once) —
// fx_list alone never touches V8. Nothing else needs process init:
// FFmpeg-free, COM-free.
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

// ESSENTIAL: inside this worker, the editor-side subprocess paths
// (ProcessAudio / LoadCatalog remote attempts) must stay in-process —
// otherwise the worker would spawn a grandchild XERFxScript and
// recurse. XER_VE_FX_WORKER=0 disables FxScriptPipe here.
void DisableNestedWorkers() {
#if defined(_WIN32)
    SetEnvironmentVariableA("XER_VE_FX_WORKER", "0");
#else
    setenv("XER_VE_FX_WORKER", "0", 1);
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
                "XERFxScript - XER FX script engine worker\n"
                "Serves fx_list/fx_validate/fx_audio/fx_transition\n"
                "over the JSON-lines worker protocol until stdin\n"
                "EOF.\n",
                stderr);
            return 0;
        }
    }
    DisableNestedWorkers();
    return XER::VideoEditor::RunFxScriptWorker("XERFxScript");
}
