// Shared plain-assert harness for XER_VideoEditor_Tests — same style
// as tests/video/xer_video_smoke.cc (process exit code = failure count)
// but header-only/inline so multiple suite .cc files share one counter.
#pragma once

#include <cstdio>
#include <filesystem>

namespace XER::VideoEditor::Test {

inline int g_failures = 0;

inline void Check(bool condition, const char* expression,
                  const char* file, int line) {
    if (condition) return;
    ++g_failures;
    std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
}

inline int Failures() { return g_failures; }

inline std::filesystem::path TempDir() {
    const auto dir = std::filesystem::temp_directory_path() /
                     "xer_video_editor_tests";
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace XER::VideoEditor::Test

#define XER_VE_CHECK(cond) \
    XER::VideoEditor::Test::Check(static_cast<bool>(cond), #cond, \
                                  __FILE__, __LINE__)
