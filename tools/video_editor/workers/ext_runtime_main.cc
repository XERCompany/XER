// VE.ExtensionsRuntime.exe — the out-of-process native-extension
// runtime for the XER video editor. JIT-links mobjective/*.xsjo
// payloads from verified .xexrj archives (ORC LLJIT) and serves
// ext_load / ext_call / ext_unload / ext_list / ext_events over the
// JSON-lines worker protocol — a crashing native extension kills
// THIS worker, never the editor.
//
// LLVM is initialized lazily inside the first native ext_load
// (XsjoJit::Create) — ext_list and ping alone never touch libLLVM,
// which is also delay-loaded so a missing runtime degrades cleanly
// instead of aborting process start.
//
// stdin/stdout are the protocol pipe; diagnostics go to stderr.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "extensions/ext_runtime_server.h"

namespace {

// ESSENTIAL: inside this worker, the editor-side subprocess path for
// extensions must stay in-process — otherwise the worker would spawn
// a grandchild VE.ExtensionsRuntime and recurse. XER_VE_EXT_WORKER=0
// is the kill-switch the WorkerPipeSpec honors.
void DisableNestedWorkers() {
#if defined(_WIN32)
    SetEnvironmentVariableA("XER_VE_EXT_WORKER", "0");
#else
    setenv("XER_VE_EXT_WORKER", "0", 1);
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
                "VE.ExtensionsRuntime - XER native-extension JIT "
                "runtime\n"
                "Serves ext_load/ext_call/ext_unload/ext_list/"
                "ext_events over the JSON-lines worker protocol "
                "until stdin EOF.\n",
                stderr);
            return 0;
        }
    }
    DisableNestedWorkers();
    return XER::VideoEditor::RunExtRuntime("VE.ExtensionsRuntime");
}
