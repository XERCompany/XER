// ext_host.h — .xexrj extension host for the VideoEditor.
//
// An extension is a TZ archive (built by ztzmake.exe, verified by
// ztzvertfy.exe / TzVerify) living under assets/extensions/. The host
//   1. opens the archive in memory (no extraction),
//   2. parses manifest.toml — [extension] id/name/version/entry/ui,
//      [files] requires=[...], [native] dlls/lua/py=[...],
//   3. verifies integrity: every member's crc32, the OVERFLOW
//      manifest, and every manifest-declared path must exist,
//   4. registers the archive with the xer-ext:// CEF scheme so the
//      extension's HTML/CSS is served straight from the TZ blob,
//   5. runs main.js inside the shared V8 engine with a "xer_ext"
//      module whose surface is scoped to THIS extension (log, setStatus,
//      projectInfo, call, listFiles, readFile, openUi, loadNative,
//      runLua, runPython).
//
// Native payloads (.dll/.node/.lua/.py) are only reachable when the
// manifest's [native] section allowlists them, and .dll/.node binaries
// are extracted into a crc-checked cache directory before LoadLibrary
// touches them — the archive is always verified first.
//
// THREADING: same contract as ScriptHost — everything runs on the UI
// thread; Run() is synchronous.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace XER::Tz { class TzArchive; }

namespace XER::VideoEditor {

// Parsed manifest.toml. Unknown sections/keys are ignored; malformed
// input makes Parse fail and the extension is rejected before any JS
// runs — matching the "verify before execute" contract.
struct ExtManifest {
    std::string id;                 // [extension] id  (required)
    std::string name;               // [extension] name
    std::string version;            // [extension] version
    std::string entry = "main.js";  // [extension] entry
    std::string ui;                 // [extension] ui — served via xer-ext://
    std::vector<std::string> required;   // [files] requires = [...]
    std::vector<std::string> native_dll; // [native] dlls = [...]
    std::vector<std::string> native_lua; // [native] lua = [...]
    std::vector<std::string> native_py;  // [native] py = [...]
};

struct Extension {
    std::string path;               // absolute path of the .xexrj
    ExtManifest manifest;
    bool verified = false;
    std::vector<std::string> verify_errors;
};

// Parses manifest.toml text into `out` — the same strict parser
// ExtHost::Load runs. Exposed for the out-of-process extension
// runtime (VE.ExtensionsRuntime.exe / ext_runtime_server.cc), which
// opens the same .xexrj archives in its own address space and must
// evaluate the manifest identically without dragging in the CEF/V8
// host machinery.
bool ExtManifestParse(const std::string& text, ExtManifest* out,
                      std::string* error);

class ExtHost {
public:
    static ExtHost& Shared();

    ExtHost(const ExtHost&) = delete;
    ExtHost& operator=(const ExtHost&) = delete;

    // App integration — installed once by the editor.
    //  action_fn   routes "call(action, json)" / projectInfo / setStatus
    //              into the app's action dispatcher (JSON in/out).
    //  open_ui_fn  receives a fully-formed xer-ext:// URL to display in
    //              the CEF Web panel; may be null in headless contexts.
    using ActionFn = std::function<std::string(
        const std::string& action, const std::string& arg_json)>;
    using OpenUiFn = std::function<void(const std::string& url)>;
    void SetActionFn(ActionFn fn) { action_fn_ = std::move(fn); }
    void SetOpenUiFn(OpenUiFn fn) { open_ui_fn_ = std::move(fn); }

    // Rescans assets/extensions/, opens + verifies every *.xexrj.
    // Entries() reflects the last scan; failed archives appear with
    // verified=false and populated verify_errors.
    void Scan(const std::string& extensions_dir);
    const std::vector<Extension>& Entries() const { return entries_; }

    // Opens + verifies one archive; index into Entries() via
    // manifest.id on success. error receives the first problem.
    bool Load(const std::string& path, std::string* error);

    struct RunResult {
        bool ok = false;
        std::string error;
        std::string output;    // ext.log() capture
    };
    // Executes the extension's entry point inside the V8 engine.
    // The archive must already be loaded + verified — Load() again for
    // a scanned entry or Scan() first.
    RunResult Run(const std::string& id);

    // Serves one member for callers that cannot use the CEF scheme
    // (tests, diagnostics). Empty vector on failure.
    std::vector<std::uint8_t> ReadMember(const std::string& id,
                                         const std::string& member);

    // Directory where verified native payloads are extracted before
    // loading — <temp>/xer_ext_cache/<id>/.
    static std::string NativeCacheDir(const std::string& ext_id);

private:
    ExtHost() = default;

    struct Loaded {
        Extension ext;
        std::shared_ptr<Tz::TzArchive> archive;
        // HMODULEs (as void*) opened via xer_ext.loadNative — tracked
        // so the unload path can FreeLibrary each one instead of
        // leaking a module refcount per load for the process life.
        std::vector<void*> native_modules;
    };
    Loaded* FindLoaded(const std::string& id);
    // FreeLibrary's every native module the Loaded entry holds.
    void FreeNativeModules(Loaded& loaded);
    // Frees every loaded extension's native modules and clears
    // loaded_ — called by Scan() so extensions removed from disk can
    // no longer be Run().
    void UnloadAll();
    // Registers the xer_ext module bound to `loaded` on the shared
    // engine (rebound before every Run — the module captures which
    // extension is executing).
    void InstallModule(Loaded& loaded, RunResult* sink);
    // Extracts a verified member into the cache dir (skipped when the
    // cached file already matches the archive crc) and returns its
    // path; "" on failure.
    std::string ExtractForNative(Loaded& loaded,
                                 const std::string& member,
                                 std::string* error);

    ActionFn action_fn_;
    OpenUiFn open_ui_fn_;
    std::vector<Extension> entries_;
    std::vector<Loaded> loaded_;
    Loaded* running_ = nullptr;     // extension currently executing
    RunResult* sink_ = nullptr;     // its RunResult::output target
};

}  // namespace XER::VideoEditor
