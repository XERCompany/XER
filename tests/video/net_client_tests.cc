// NetClient suite — validation and failure paths of the src/network
// facade (network/net_client.h) used by the app_super_net V8 module.
// No live network: the only transport case targets 127.0.0.1:1, which
// refuses connections immediately on every platform.
#include <filesystem>
#include <memory>
#include <string>

#include "video_editor_test_util.h"
#include "network/net_client.h"
#include "app_runtime/js_engine.h"
#include "app_runtime/native_modules.h"

namespace {
namespace net = XER::Network;
namespace apprt = XER::VideoEditor::AppRuntime;
using apprt::JsEngine;
using apprt::OutValue;

// Empty url must be rejected before the transport is touched: ok is
// false and the error explains why.
void TestFetchEmptyUrl() {
    const net::NetResponse r = net::Fetch("GET", "");
    XER_VE_CHECK(!r.ok);
    XER_VE_CHECK(!r.error.empty());
    XER_VE_CHECK(r.status == 0);
}

// Empty method and a non-positive timeout are likewise pre-flight
// rejections.
void TestFetchEmptyMethodAndBadTimeout() {
    XER_VE_CHECK(!net::Fetch("", "http://example.invalid/").ok);
    const net::NetResponse r =
        net::Fetch("GET", "http://example.invalid/", {}, {}, 0);
    XER_VE_CHECK(!r.ok);
    XER_VE_CHECK(!r.error.empty());
}

// Get/Post conveniences inherit the same validation.
void TestConvenienceValidation() {
    XER_VE_CHECK(!net::Get("").ok);
    XER_VE_CHECK(!net::Post("", "x", "text/plain").ok);
}

// 127.0.0.1:1 refuses instantly — Fetch must propagate the transport
// error (ok false, status 0, non-empty error carrying the url).
void TestFetchUnreachableHost() {
    const net::NetResponse r = net::Get("http://127.0.0.1:1/x", 3000);
    XER_VE_CHECK(!r.ok);
    XER_VE_CHECK(r.status == 0);
    XER_VE_CHECK(!r.error.empty());
    XER_VE_CHECK(r.error.find("127.0.0.1") != std::string::npos);
}

// DownloadFile: an empty destination is rejected with context, and a
// fetch failure surfaces instead of writing an empty file.
void TestDownloadFileValidation() {
    const auto dir = XER::VideoEditor::Test::TempDir();
    const auto dest = (dir / "net_client_dl.bin").string();

    std::string error;
    XER_VE_CHECK(!net::DownloadFile("http://127.0.0.1/x", "", 1000,
                                    &error));
    XER_VE_CHECK(!error.empty());

    error.clear();
    XER_VE_CHECK(!net::DownloadFile("http://127.0.0.1:1/x", dest, 3000,
                                    &error));
    XER_VE_CHECK(!error.empty());
    XER_VE_CHECK(!std::filesystem::exists(dest));
}

// End-to-end through NativeImport("app_super_net"): fetch("") must
// surface the validation error as {ok:false, status:0, error:"..."}
// and download() to an unreachable host as {ok:false, error:"..."} —
// results are ferried back via the out-data registry.
void TestJsModuleValidation() {
    static std::string create_error;
    static std::unique_ptr<JsEngine> engine =
        JsEngine::Create(&create_error);
    if (engine == nullptr) {
        XER_VE_CHECK(engine != nullptr);
        return;
    }
    apprt::RegisterAll(*engine);
    std::string error;
    // Top-level consts persist in the shared context across scripts —
    // the "nc_" prefixes avoid a re-declaration SyntaxError.
    XER_VE_CHECK(engine->RunSource(
        "const nc_m = NativeImport(\"app_super_exportAppData\");\n"
        "const nc_n = NativeImport(\"app_super_net\");\n"
        "const nc_r = nc_n.fetch(\"\");\n"
        "const nc_d = nc_n.download(\"http://127.0.0.1:1/x\", "
        "\"nc.bin\");\n"
        "nc_m.setOutAppData({ \"nc_ok\": nc_r.ok, "
        "\"nc_status\": nc_r.status, \"nc_err\": nc_r.error, "
        "\"nc_dok\": nc_d.ok, \"nc_derr\": nc_d.error });\n",
        "net_client.js", &error));

    const OutValue ok = engine->GetOutData("nc_ok");
    XER_VE_CHECK(ok.kind == OutValue::Kind::Bool);
    XER_VE_CHECK(!ok.boolean);
    const OutValue status = engine->GetOutData("nc_status");
    XER_VE_CHECK(status.kind == OutValue::Kind::Number);
    XER_VE_CHECK(status.num == 0);
    const OutValue err = engine->GetOutData("nc_err");
    XER_VE_CHECK(err.kind == OutValue::Kind::String);
    XER_VE_CHECK(!err.str.empty());
    const OutValue dok = engine->GetOutData("nc_dok");
    XER_VE_CHECK(dok.kind == OutValue::Kind::Bool);
    XER_VE_CHECK(!dok.boolean);
    const OutValue derr = engine->GetOutData("nc_derr");
    XER_VE_CHECK(derr.kind == OutValue::Kind::String);
    XER_VE_CHECK(!derr.str.empty());
}

}  // namespace

int XER_VideoEditor_RunNetClientTests() {
    const struct {
        const char* name;
        void (*fn)();
    } cases[] = {
        {"fetch_empty_url", &TestFetchEmptyUrl},
        {"fetch_empty_method_and_bad_timeout",
         &TestFetchEmptyMethodAndBadTimeout},
        {"convenience_validation", &TestConvenienceValidation},
        {"fetch_unreachable_host", &TestFetchUnreachableHost},
        {"download_file_validation", &TestDownloadFileValidation},
        {"js_module_validation", &TestJsModuleValidation},
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
    return 0;
}
