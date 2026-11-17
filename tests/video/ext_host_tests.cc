// ExtHost + TZ suite — the .xexrj extension package layer:
//   * src/tz container roundtrip, OVERFLOW manifest, integrity
//     verification (crc tamper, missing required members, unsafe
//     member names),
//   * tools/video_editor/extensions/ext_host: manifest.toml parsing,
//     verify-before-execute, and Run() of main.js straight out of the
//     archive inside the shared V8 engine.
//
// Temp fixtures live under Test::TempDir()/ext_host_<pid> and are
// removed at the end of the suite.
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "video_editor_test_util.h"
#include "extensions/ext_host.h"
#include "tz/tz.h"

namespace {
using XER::VideoEditor::ExtHost;
namespace Tz = XER::Tz;

std::filesystem::path TempRoot() {
#ifdef _WIN32
    const long long pid = static_cast<long long>(_getpid());
#else
    const long long pid = static_cast<long long>(getpid());
#endif
    return XER::VideoEditor::Test::TempDir() /
           ("ext_host_" + std::to_string(pid));
}

bool WriteFile(const std::filesystem::path& p,
               const std::string& text) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
    return out.good();
}

std::filesystem::path PackArchive(
    const std::map<std::string, std::vector<std::uint8_t>>& files,
    const std::string& name) {
    const auto path = TempRoot() / name;
    std::string err;
    if (!Tz::TzWrite(files, path.string(), &err)) {
        return {};
    }
    return path;
}

std::vector<std::uint8_t> Bytes(const std::string& s) {
    return {s.begin(), s.end()};
}

// ---- TZ container ---------------------------------------------------------

void TestTzRoundtrip() {
    std::map<std::string, std::vector<std::uint8_t>> files;
    files["main.js"] = Bytes("console.log('hi')");
    files["static/index.html"] = Bytes("<html></html>");
    files["scripts/a.js"] = Bytes("var a=1;");
    const auto path = PackArchive(files, "roundtrip.tz");
    XER_VE_CHECK(!path.empty());

    std::string err;
    auto arc = Tz::TzArchive::Open(path.string(), &err);
    XER_VE_CHECK(arc != nullptr);
    if (!arc) return;
    XER_VE_CHECK(arc->Entries().size() == 4);  // 3 + OVERFLOW
    XER_VE_CHECK(arc->Has("main.js"));
    XER_VE_CHECK(arc->Has("OVERFLOW"));
    std::string main_src;
    XER_VE_CHECK(arc->Read("main.js", &main_src));
    XER_VE_CHECK(main_src == "console.log('hi')");
    std::string missing;
    XER_VE_CHECK(!arc->Read("nope.js", &missing));
}

void TestTzVerifyOk() {
    std::map<std::string, std::vector<std::uint8_t>> files;
    files["a.txt"] = Bytes("alpha");
    files["dir/b.txt"] = Bytes("beta");
    const auto path = PackArchive(files, "verify_ok.tz");
    XER_VE_CHECK(!path.empty());
    const auto report = Tz::TzVerify(path.string());
    XER_VE_CHECK(report.ok);
    XER_VE_CHECK(report.errors.empty());
    XER_VE_CHECK(report.entries == 3);  // 2 + OVERFLOW
}

void TestTzVerifyRequired() {
    std::map<std::string, std::vector<std::uint8_t>> files;
    files["a.txt"] = Bytes("x");
    const auto path = PackArchive(files, "verify_req.tz");
    const auto ok = Tz::TzVerify(path.string(), {"a.txt"});
    XER_VE_CHECK(ok.ok);
    const auto missing = Tz::TzVerify(path.string(), {"missing.bin"});
    XER_VE_CHECK(!missing.ok);
    XER_VE_CHECK(!missing.errors.empty());
}

void TestTzVerifyTamper() {
    std::map<std::string, std::vector<std::uint8_t>> files;
    files["a.txt"] = Bytes("contents to corrupt");
    const auto path = PackArchive(files, "verify_tamper.tz");
    XER_VE_CHECK(!path.empty());
    // Flip a byte inside the payload (header is 24 bytes).
    {
        std::fstream f(path, std::ios::binary | std::ios::in |
                                 std::ios::out);
        f.seekp(26);
        f.put('Z');
        XER_VE_CHECK(f.good());
    }
    const auto report = Tz::TzVerify(path.string());
    XER_VE_CHECK(!report.ok);
}

void TestTzUnsafeNames() {
    XER_VE_CHECK(Tz::TzIsSafeName("scripts/helper.js"));
    XER_VE_CHECK(!Tz::TzIsSafeName("../evil"));
    XER_VE_CHECK(!Tz::TzIsSafeName("a/../../b"));
    XER_VE_CHECK(!Tz::TzIsSafeName("/abs/path"));
    XER_VE_CHECK(!Tz::TzIsSafeName("C:/windows/x.dll"));
    XER_VE_CHECK(Tz::TzNormalizeName("./a//b.js") == "a/b.js");
    XER_VE_CHECK(Tz::TzNormalizeName("a\\b.js") == "a/b.js");
    XER_VE_CHECK(Tz::TzNormalizeName("../x").empty());

    // TzWrite must refuse unsafe member names outright.
    std::map<std::string, std::vector<std::uint8_t>> bad;
    bad["../escape"] = Bytes("x");
    std::string err;
    XER_VE_CHECK(!Tz::TzWrite(bad,
                              (TempRoot() / "bad.tz").string(), &err));
    XER_VE_CHECK(!err.empty());
}

void TestTzWriteDirDeterministic() {
    const auto src = TempRoot() / "srcdir";
    XER_VE_CHECK(WriteFile(src / "b.txt", "b"));
    XER_VE_CHECK(WriteFile(src / "sub" / "a.txt", "a"));
    const auto p1 = TempRoot() / "d1.tz";
    const auto p2 = TempRoot() / "d2.tz";
    std::string err;
    XER_VE_CHECK(Tz::TzWriteDir(src.string(), p1.string(), &err));
    XER_VE_CHECK(Tz::TzWriteDir(src.string(), p2.string(), &err));
    std::ifstream f1(p1, std::ios::binary | std::ios::ate);
    std::ifstream f2(p2, std::ios::binary | std::ios::ate);
    XER_VE_CHECK(f1.tellg() == f2.tellg());
    XER_VE_CHECK(f1.tellg() > 0);
}

// ---- ExtHost: manifest + verify-before-run --------------------------------

std::map<std::string, std::vector<std::uint8_t>> DemoFiles() {
    return {
        {"manifest.toml",
         Bytes("[extension]\n"
               "id = \"test_ext\"\n"
               "name = \"Test Ext\"\n"
               "version = \"1.0\"\n"
               "entry = \"main.js\"\n"
               "ui = \"static/index.html\"\n"
               "\n"
               "[files]\n"
               "requires = [\"scripts/helper.js\", "
               "\"static/index.html\"]\n")},
        {"main.js",
         Bytes("const ext = NativeImport(\"xer_ext\");\n"
               "ext.log(\"id=\" + ext.id());\n"
               "ext.log(ext.readFile(\"scripts/helper.js\"));\n")},
        {"scripts/helper.js", Bytes("function v(){return 7;}")},
        {"static/index.html", Bytes("<html>x</html>")},
    };
}

void TestExtLoadVerify() {
    const auto path = PackArchive(DemoFiles(), "ok.xexrj");
    XER_VE_CHECK(!path.empty());
    std::string err;
    XER_VE_CHECK(ExtHost::Shared().Load(path.string(), &err));
    if (!err.empty()) std::fprintf(stderr, "load err: %s\n", err.c_str());
    const auto& entries = ExtHost::Shared().Entries();
    bool found = false;
    for (const auto& e : entries) {
        if (e.manifest.id == "test_ext") {
            found = true;
            XER_VE_CHECK(e.verified);
            XER_VE_CHECK(e.manifest.name == "Test Ext");
            XER_VE_CHECK(e.manifest.entry == "main.js");
            XER_VE_CHECK(e.manifest.ui == "static/index.html");
            XER_VE_CHECK(e.manifest.required.size() == 2);
        }
    }
    XER_VE_CHECK(found);
}

void TestExtManifestMissingId() {
    auto files = DemoFiles();
    files["manifest.toml"] = Bytes(
        "[extension]\nname = \"NoId\"\nentry = \"main.js\"\n");
    const auto path = PackArchive(files, "noid.xexrj");
    std::string err;
    XER_VE_CHECK(!ExtHost::Shared().Load(path.string(), &err));
    XER_VE_CHECK(!err.empty());
}

void TestExtManifestTraversalEntry() {
    auto files = DemoFiles();
    files["manifest.toml"] = Bytes(
        "[extension]\nid = \"bad\"\nentry = \"../escape.js\"\n");
    const auto path = PackArchive(files, "trav.xexrj");
    std::string err;
    XER_VE_CHECK(!ExtHost::Shared().Load(path.string(), &err));
    XER_VE_CHECK(!err.empty());
}

void TestExtMissingRequiredFile() {
    auto files = DemoFiles();
    files.erase("scripts/helper.js");   // manifest still requires it
    const auto path = PackArchive(files, "missreq.xexrj");
    std::string err;
    XER_VE_CHECK(!ExtHost::Shared().Load(path.string(), &err));
    XER_VE_CHECK(!err.empty());
}

void TestExtRunMainJs() {
    const auto path = PackArchive(DemoFiles(), "run.xexrj");
    XER_VE_CHECK(!path.empty());
    std::string err;
    XER_VE_CHECK(ExtHost::Shared().Load(path.string(), &err));

    int status_calls = 0;
    ExtHost::Shared().SetActionFn(
        [&status_calls](const std::string& action,
                        const std::string&) {
            if (action == "project_info") {
                return std::string("{\"clips\":3}");
            }
            if (action == "status") {
                ++status_calls;
                return std::string("{\"ok\":true}");
            }
            return std::string("{\"error\":\"?\"}");
        });

    const auto res = ExtHost::Shared().Run("test_ext");
    if (!res.ok) {
        std::fprintf(stderr, "run err: %s\n", res.error.c_str());
    }
    XER_VE_CHECK(res.ok);
    XER_VE_CHECK(res.output.find("id=test_ext") != std::string::npos);
    // ext.readFile streamed the helper member out of the TZ blob.
    XER_VE_CHECK(res.output.find("return 7") != std::string::npos);

    // xer_ext member reads never escape the archive.
    const auto data = ExtHost::Shared().ReadMember("test_ext",
                                                   "scripts/helper.js");
    XER_VE_CHECK(!data.empty());
    ExtHost::Shared().SetActionFn(nullptr);
}

void TestExtRunMissing() {
    const auto res = ExtHost::Shared().Run("no_such_ext");
    XER_VE_CHECK(!res.ok);
    XER_VE_CHECK(!res.error.empty());
}

}  // namespace

int XER_VideoEditor_RunExtHostTests() {
    const int before = XER::VideoEditor::Test::Failures();
    {
        // PackArchive writes straight into TempRoot — create it up
        // front so the first case doesn't depend on a later WriteFile.
        std::error_code ec;
        std::filesystem::create_directories(TempRoot(), ec);
    }
    const struct {
        const char* name;
        void (*fn)();
    } cases[] = {
        {"tz_roundtrip", &TestTzRoundtrip},
        {"tz_verify_ok", &TestTzVerifyOk},
        {"tz_verify_required", &TestTzVerifyRequired},
        {"tz_verify_tamper", &TestTzVerifyTamper},
        {"tz_unsafe_names", &TestTzUnsafeNames},
        {"tz_writedir_deterministic", &TestTzWriteDirDeterministic},
        {"ext_load_verify", &TestExtLoadVerify},
        {"ext_manifest_missing_id", &TestExtManifestMissingId},
        {"ext_manifest_traversal", &TestExtManifestTraversalEntry},
        {"ext_missing_required", &TestExtMissingRequiredFile},
        {"ext_run_mainjs", &TestExtRunMainJs},
        {"ext_run_missing", &TestExtRunMissing},
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
