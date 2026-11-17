// ScriptHost suite — the Community Scripts layer in
// tools/video_editor/script_host.h: scripts.json manifest parsing
// (validation, unsafe-file rejection, graceful missing-file degrade)
// and Run() over the embedded V8 engine.
//
// The run cases exercise whatever engine ScriptHost picks:
// SharedBootEngine() when the app_runtime boot tests already ran, or
// the host's lazily created private engine otherwise. Both register
// the "xer_editor" module on first use, so NativeImport resolves in
// either context.
//
// Temp fixtures live under Test::TempDir()/script_host and are
// removed at the end of the suite.
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "video_editor_test_util.h"
#include "script_host.h"
#include "app_runtime/app_runtime.h"

namespace {
using XER::VideoEditor::ScriptHost;
namespace apprt = XER::VideoEditor::AppRuntime;

// Per-suite scratch directory inside the shared test temp area.
std::filesystem::path TempRoot() {
#ifdef _WIN32
    const long long pid = static_cast<long long>(_getpid());
#else
    const long long pid = static_cast<long long>(getpid());
#endif
    return XER::VideoEditor::Test::TempDir() /
           ("script_host_" + std::to_string(pid));
}

bool WriteFile(const std::filesystem::path& path,
               const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
    return f.good();
}

// SharedBootEngine() is a borrowed pointer owned by whichever
// BootResult the last Boot() caller kept alive — the app_runtime
// suite lets its results die at function scope, leaving the pointer
// stale by the time this suite runs (main.cc stashes its BootResult
// in a static; we mirror that here). Returns the pinned result; when
// Boot fails the shared pointer is cleared so ScriptHost falls back
// to its private engine instead of chasing a dangling one.
apprt::BootResult& SharedBoot() {
    static std::string status;
    static apprt::BootResult result = apprt::Boot(&status);
    if (!result.ok) apprt::SharedBootEngine() = nullptr;
    return result;
}

// ------------------------------------------------------------------
// Cases
// ------------------------------------------------------------------

// A well-formed scripts.json loads every field of every entry.
void TestManifestParse() {
    const auto dir = TempRoot() / "parse";
    std::filesystem::create_directories(dir);
    XER_VE_CHECK(WriteFile(
        dir / "scripts.json",
        "[{\"id\":\"hello\",\"title\":\"Hello Script\","
        "\"description\":\"Demo\",\"file\":\"hello.js\","
        "\"category\":\"Utility\",\"shortcut\":\"Ctrl+Alt+1\"},"
        "{\"id\":\"sub/nested\",\"file\":\"sub/x.js\"}]"));
    std::string error;
    ScriptHost& host = ScriptHost::Shared();
    XER_VE_CHECK(host.LoadManifest(dir.string(), &error));
    const auto& entries = host.Entries();
    XER_VE_CHECK(entries.size() == 2);
    if (entries.size() == 2) {
        XER_VE_CHECK(entries[0].id == "hello");
        XER_VE_CHECK(entries[0].title == "Hello Script");
        XER_VE_CHECK(entries[0].description == "Demo");
        XER_VE_CHECK(entries[0].file == "hello.js");
        XER_VE_CHECK(entries[0].category == "Utility");
        XER_VE_CHECK(entries[0].shortcut == "Ctrl+Alt+1");
        // Subdirectory files are allowed — only .. / absolute paths
        // are unsafe.
        XER_VE_CHECK(entries[1].file == "sub/x.js");
    }
    // ResolvePath joins the scripts dir and the entry file.
    if (!entries.empty()) {
        const std::string resolved = host.ResolvePath(entries[0]);
        XER_VE_CHECK(resolved.find("hello.js") != std::string::npos);
        XER_VE_CHECK(resolved.find(dir.filename().string()) !=
                     std::string::npos);
    }
}

// Missing scripts.json must degrade: false return, explanatory
// error, empty Entries() — never a crash.
void TestManifestMissing() {
    const auto dir = TempRoot() / "missing";
    std::filesystem::create_directories(dir);
    std::string error;
    XER_VE_CHECK(!ScriptHost::Shared().LoadManifest(dir.string(),
                                                    &error));
    XER_VE_CHECK(!error.empty());
    XER_VE_CHECK(ScriptHost::Shared().Entries().empty());
}

// Entries with an unsafe or incomplete file are rejected while valid
// siblings still load: "../evil.js" (escapes the dir), an absolute
// path, an empty file, an empty id and a duplicate id are all
// skipped, leaving only the good entries.
void TestManifestBadFileRejected() {
    const auto dir = TempRoot() / "rejected";
    std::filesystem::create_directories(dir);
    XER_VE_CHECK(WriteFile(
        dir / "scripts.json",
        "[{\"id\":\"evil\",\"file\":\"../evil.js\"},"
        "{\"id\":\"abs\",\"file\":\"C:/Windows/evil.js\"},"
        "{\"id\":\"nofile\"},"
        "{\"file\":\"no_id.js\"},"
        "{\"id\":\"good\",\"file\":\"good.js\"},"
        "{\"id\":\"good\",\"file\":\"dup.js\"}]"));
    std::string error;
    XER_VE_CHECK(ScriptHost::Shared().LoadManifest(dir.string(),
                                                   &error));
    const auto& entries = ScriptHost::Shared().Entries();
    XER_VE_CHECK(entries.size() == 1);
    if (entries.size() == 1) {
        XER_VE_CHECK(entries[0].id == "good");
        XER_VE_CHECK(entries[0].file == "good.js");
    }
}

// Malformed JSON fails cleanly with a non-empty error.
void TestManifestMalformed() {
    const auto dir = TempRoot() / "malformed";
    std::filesystem::create_directories(dir);
    XER_VE_CHECK(WriteFile(dir / "scripts.json", "{ not json"));
    std::string error;
    XER_VE_CHECK(!ScriptHost::Shared().LoadManifest(dir.string(),
                                                    &error));
    XER_VE_CHECK(!error.empty());
}

// Run() on an id absent from the manifest reports an error without
// touching the engine — this case stays hermetic even when V8 is
// unavailable.
void TestRunMissingId() {
    const auto dir = TempRoot() / "run_missing";
    std::filesystem::create_directories(dir);
    XER_VE_CHECK(WriteFile(
        dir / "scripts.json",
        "[{\"id\":\"present\",\"file\":\"present.js\"}]"));
    std::string error;
    XER_VE_CHECK(ScriptHost::Shared().LoadManifest(dir.string(),
                                                   &error));
    const ScriptHost::RunResult result =
        ScriptHost::Shared().Run("does_not_exist");
    XER_VE_CHECK(!result.ok);
    XER_VE_CHECK(!result.error.empty());
}

// End-to-end: a temp script that calls xer_editor.log() and
// xer_editor.call("noop","{}") must run ok, land its log text in
// RunResult::output and route the action through the installed
// ActionFn.
void TestRunHello() {
    const auto dir = TempRoot() / "run_hello";
    std::filesystem::create_directories(dir);
    XER_VE_CHECK(WriteFile(
        dir / "scripts.json",
        "[{\"id\":\"hello\",\"title\":\"Hello\",\"file\":\"hello.js\"}]"));
    XER_VE_CHECK(WriteFile(
        dir / "hello.js",
        "const editor = NativeImport(\"xer_editor\");\n"
        "editor.log(\"hi from test\");\n"
        "const reply = JSON.parse(editor.call(\"noop\", \"{}\"));\n"
        "editor.log(\"reply_ok=\" + reply.ok);\n"));

    // Pin a live boot engine before Run() consults SharedBootEngine().
    (void)SharedBoot();

    std::string error;
    ScriptHost& host = ScriptHost::Shared();
    XER_VE_CHECK(host.LoadManifest(dir.string(), &error));

    std::string seen_action;
    host.SetActionFn(
        [&seen_action](const std::string& action,
                       const std::string& arg_json) {
            seen_action = action;
            return std::string("{\"ok\":true,\"echo\":") + arg_json +
                   "}";
        });

    const ScriptHost::RunResult result = host.Run("hello");
    XER_VE_CHECK(result.ok);
    if (!result.ok) {
        std::fprintf(stderr, "script_run_hello error: %s\n",
                     result.error.c_str());
    }
    XER_VE_CHECK(result.output.find("hi from test") !=
                 std::string::npos);
    XER_VE_CHECK(result.output.find("reply_ok=true") !=
                 std::string::npos);
    XER_VE_CHECK(seen_action == "noop");
}

// A script with a syntax error fails Run() but reports the V8 error
// text rather than crashing.
void TestRunScriptError() {
    const auto dir = TempRoot() / "run_bad";
    std::filesystem::create_directories(dir);
    XER_VE_CHECK(WriteFile(
        dir / "scripts.json",
        "[{\"id\":\"bad\",\"file\":\"bad.js\"}]"));
    XER_VE_CHECK(
        WriteFile(dir / "bad.js", "syntax error("));

    (void)SharedBoot();

    std::string error;
    ScriptHost& host = ScriptHost::Shared();
    XER_VE_CHECK(host.LoadManifest(dir.string(), &error));
    const ScriptHost::RunResult result = host.Run("bad");
    XER_VE_CHECK(!result.ok);
    XER_VE_CHECK(!result.error.empty());
}

}  // namespace

int XER_VideoEditor_RunScriptHostTests() {
    const int before = XER::VideoEditor::Test::Failures();
    const struct {
        const char* name;
        void (*fn)();
    } cases[] = {
        {"script_manifest_parse", &TestManifestParse},
        {"script_manifest_missing", &TestManifestMissing},
        {"script_manifest_bad_file_rejected",
         &TestManifestBadFileRejected},
        {"script_manifest_malformed", &TestManifestMalformed},
        {"script_run_missing_id", &TestRunMissingId},
        {"script_run_hello", &TestRunHello},
        {"script_run_script_error", &TestRunScriptError},
    };
    for (const auto& c : cases) {
        std::printf("[RUN] %s\n", c.name);
        std::fflush(stdout);
        const int case_before = XER::VideoEditor::Test::Failures();
        c.fn();
        const bool passed =
            XER::VideoEditor::Test::Failures() == case_before;
        std::printf("[%s] %s\n", passed ? "PASS" : "FAIL", c.name);
        std::fflush(stdout);
    }
    std::error_code ec;
    std::filesystem::remove_all(TempRoot(), ec);
    return XER::VideoEditor::Test::Failures() - before;
}
