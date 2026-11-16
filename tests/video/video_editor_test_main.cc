// Single entry point for the VideoEditor headless-core suites. Each
// suite .cc exposes XER_VideoEditor_Run<Suite>Tests(); the shared
// XER_VE_CHECK macro counts failures in
// XER::VideoEditor::Test::g_failures (video_editor_test_util.h).
#include <cstdio>

#include "video_editor_test_util.h"

int XER_VideoEditor_RunModelTests();
int XER_VideoEditor_RunHistoryTests();
int XER_VideoEditor_RunStoreTests();
int XER_VideoEditor_RunCatalogTests();
int XER_VideoEditor_RunPosterTests();
int XER_VideoEditor_RunDecoderTests();
int XER_VideoEditor_RunCompositorTests();
int XER_VideoEditor_RunMixerTests();

int main() {
    XER_VideoEditor_RunModelTests();
    XER_VideoEditor_RunHistoryTests();
    XER_VideoEditor_RunStoreTests();
    XER_VideoEditor_RunCatalogTests();
    XER_VideoEditor_RunPosterTests();
    XER_VideoEditor_RunDecoderTests();
    XER_VideoEditor_RunCompositorTests();
    XER_VideoEditor_RunMixerTests();
    const int failures = XER::VideoEditor::Test::Failures();
    if (failures == 0) {
        std::printf("xer_video_editor_tests: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "xer_video_editor_tests: %d check(s) failed\n",
                 failures);
    return failures > 255 ? 255 : failures;
}
