#include <iostream>

#include "app/app_main.h"
#include "app/crash_handler.h"

namespace {

bool HasFlag(int argc, char** argv, const char* flag) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr && std::string(argv[i]) == flag) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char **argv) {
    // Crash handlers go first — before FluxCore, cache, UI or anything that
    // can fail. The report path resolves lazily inside the handler.
    XER::App::Crash::Install();

    XER::App::Application app;

    if (!app.Initialize(argc, argv)) {
        return 1;
    }

    // --selftest-crash: deliberate AV to verify dump + auto-restart +
    // startup dialog end-to-end. Only fires at depth 0 so the restarted
    // instance boots cleanly and shows the report dialog.
    if (HasFlag(argc, argv, "--selftest-crash") &&
        XER::App::Crash::RestartDepth(argc, argv) == 0) {
        *static_cast<volatile int*>(nullptr) = 0;
    }

    return app.Run();
}
