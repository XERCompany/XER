// Single entry point for the VideoEditor headless-core suites. Each
// suite .cc exposes XER_VideoEditor_Run<Suite>Tests(); the shared
// XER_VE_CHECK macro counts failures in
// XER::VideoEditor::Test::g_failures (video_editor_test_util.h).
#include <cstdio>
#include <string_view>

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
int XER_VideoEditor_RunMediaPrepTests();
int XER_VideoEditor_RunSourceEditTests();
int XER_VideoEditor_RunPluginHostTests();
int XER_VideoEditor_RunAppRuntimeTests();
int XER_VideoEditor_RunNetClientTests();
int XER_VideoEditor_RunScriptHostTests();
int XER_VideoEditor_RunExtHostTests();

int main(int argc, char** argv) {
    // Keep a short feedback loop for asset/runtime regressions without
    // running the codec and real-time playback suites on every edit.
    if (argc == 3 && std::string_view(argv[1]) == "--suite" &&
        std::string_view(argv[2]) == "runtime") {
        XER_VideoEditor_RunAppRuntimeTests();
        const int failures = XER::VideoEditor::Test::Failures();
        std::printf("app_runtime_tests: %d failure(s)\n", failures);
        return failures > 255 ? 255 : failures;
    }
    if (argc == 3 && std::string_view(argv[1]) == "--suite" &&
        std::string_view(argv[2]) == "net") {
        XER_VideoEditor_RunNetClientTests();
        const int failures = XER::VideoEditor::Test::Failures();
        std::printf("net_client_tests: %d failure(s)\n", failures);
        return failures > 255 ? 255 : failures;
    }
    if (argc == 3 && std::string_view(argv[1]) == "--suite" &&
        std::string_view(argv[2]) == "mediaprep") {
        XER_VideoEditor_RunMediaPrepTests();
        const int failures = XER::VideoEditor::Test::Failures();
        std::printf("media_prep_tests: %d failure(s)\n", failures);
        return failures > 255 ? 255 : failures;
    }
    if (argc != 1) {
        std::fprintf(stderr,
                     "Usage: XER_VideoEditor_Tests"
                     " [--suite runtime|net|mediaprep]\n");
        return 2;
    }
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
    XER_VideoEditor_RunMediaPrepTests();
    XER_VideoEditor_RunSourceEditTests();
    XER_VideoEditor_RunPluginHostTests();
    XER_VideoEditor_RunAppRuntimeTests();
    XER_VideoEditor_RunScriptHostTests();
    XER_VideoEditor_RunExtHostTests();
    XER_VideoEditor_RunNetClientTests();
    const int failures = XER::VideoEditor::Test::Failures();
    if (failures == 0) {
        std::printf("xer_video_editor_tests: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "xer_video_editor_tests: %d check(s) failed\n",
                 failures);
    return failures > 255 ? 255 : failures;
}
