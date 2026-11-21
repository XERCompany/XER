// ide_preview_probe.cc — tiny CLI harness for IdePreviewSelftest so
// the watcher + headless-CEF check can run before the IDE wires it
// into RunSelftest:
//   ide_preview_probe.exe   exit 0 = watcher pass (CEF line is
//                           informational: ok / unavailable / skipped)
#include <cstdio>
#include <string>

#include "ide_preview_widget.h"

int main() {
    std::string report;
    const bool ok = XER::IdeXejso::IdePreviewSelftest(&report);
    std::fputs(report.c_str(), stdout);
    std::fputs(ok ? "ide_preview_probe: PASS\n"
                  : "ide_preview_probe: FAIL\n",
               stdout);
    return ok ? 0 : 1;
}
