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
int XER_VideoEditor_RunCompositor2Tests();
int XER_VideoEditor_RunMixerTests();
int XER_VideoEditor_RunExportTests();
int XER_VideoEditor_RunExportPresetTests();
int XER_VideoEditor_RunVst3Tests();
int XER_VideoEditor_RunLabTests();
int XER_VideoEditor_RunLab2Tests();
int XER_VideoEditor_RunPlaybackTests();
int XER_VideoEditor_RunOpsTests();
int XER_VideoEditor_RunGeneratedExportTests();
int XER_VideoEditor_RunModel2Tests();
int XER_VideoEditor_RunTimecodeTests();
int XER_VideoEditor_RunAutosaveTests();

int main() {
    XER_VideoEditor_RunModelTests();
    XER_VideoEditor_RunHistoryTests();
    XER_VideoEditor_RunStoreTests();
    XER_VideoEditor_RunCatalogTests();
    XER_VideoEditor_RunPosterTests();
    XER_VideoEditor_RunDecoderTests();
    XER_VideoEditor_RunCompositorTests();
    XER_VideoEditor_RunCompositor2Tests();
    XER_VideoEditor_RunMixerTests();
    XER_VideoEditor_RunExportTests();
    XER_VideoEditor_RunExportPresetTests();
    XER_VideoEditor_RunVst3Tests();
    XER_VideoEditor_RunLabTests();
    XER_VideoEditor_RunLab2Tests();
    XER_VideoEditor_RunPlaybackTests();
    XER_VideoEditor_RunOpsTests();
    XER_VideoEditor_RunGeneratedExportTests();
    XER_VideoEditor_RunModel2Tests();
    XER_VideoEditor_RunTimecodeTests();
    XER_VideoEditor_RunAutosaveTests();
    const int failures = XER::VideoEditor::Test::Failures();
    if (failures == 0) {
        std::printf("xer_video_editor_tests: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "xer_video_editor_tests: %d check(s) failed\n",
                 failures);
    return failures > 255 ? 255 : failures;
}
