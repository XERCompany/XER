// ext_runtime_server.cc — see ext_runtime_server.h for the wire
// protocol and the ve_* bridge design (v1 event queue + v2
// synchronous host channel; v3 widens the served ve_call table
// editor-side — this file only forwards names, plus the live
// ve_project_info upgrade). Compiled ONLY into
// VE.ExtensionsRuntime.exe (excluded from the core glob) — the
// editor talks to this over worker_pipe, it never links the server.
//
// Layout of this file:
//   1. ve_ctx + the ve_* host implementations (sdk/ve.core.h) — the
//      addresses of these functions are what the JIT's SymbolMap
//      binds the extension's undefined ve_* references to — plus the
//      reverse-channel HostBridge ve_call uses.
//   2. ExtRuntime — process state: the XsjoJit instance, the loaded
//      extensions and their event queues.
//   3. Request handlers + the JSON-lines loop.
#include "ext_runtime_server.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <json/json.h>

#include "ext_host.h"            // ExtManifest + ExtManifestParse
#include "media_worker_protocol.h"  // EncodePongReply/EncodeErrorReply
#include "tz/tz.h"
#include "xsjo_jit.h"

#include "sdk/ve.core.h"

#ifdef _WIN32
#include <windows.h>   // GetModuleFileNameW — default extensions dir
#endif

// Completes the opaque type from sdk/ve.core.h. `owner` points back
// at the LoadedExt the context belongs to (void* so the type can be
// defined before LoadedExt's anonymous-namespace declaration).
struct ve_ctx {
    void* owner = nullptr;
};

namespace XER::VideoEditor {
namespace {

// ---- ve_ctx + host services ------------------------------------------

// One LoadedExt per ext_load — heap-stable so the ve_ctx embedded in
// it never moves while JIT'd extension code holds the pointer.
struct LoadedExt {
    std::string ext_id;
    std::string name;
    std::string path;
    std::shared_ptr<Tz::TzArchive> archive;
    std::vector<std::uint64_t> modules;    // XsjoJit module ids
    std::string project_json = "{}";       // snapshot from ext_load
    std::deque<Json::Value> events;        // the v1 bridge queue
    ve_ctx ctx;                            // ctx.owner == this
};

LoadedExt* ExtOwner(ve_ctx* ctx) {
    return ctx == nullptr
               ? nullptr
               : static_cast<LoadedExt*>(ctx->owner);
}

Json::Value MakeEvent(const LoadedExt* ext, const char* type) {
    Json::Value e(Json::objectValue);
    e["ext"] = ext != nullptr ? ext->ext_id : std::string();
    e["type"] = type;
    return e;
}

void PushEvent(LoadedExt* ext, Json::Value&& e) {
    if (ext != nullptr) {
        ext->events.push_back(std::move(e));
    }
}

char* DupString(const std::string& s) {
    char* p = static_cast<char*>(std::malloc(s.size() + 1));
    if (p != nullptr) {
        std::memcpy(p, s.c_str(), s.size() + 1);
    }
    return p;
}

// Defined in the JSON plumbing block further down (same anonymous
// namespace — all anonymous blocks in one TU are one namespace).
bool ParseJsonLine(const std::string& line, Json::Value* out);
std::string WriteLine(const Json::Value& v);

#ifdef _WIN32
// ---- the reverse host channel (SDK v2) -----------------------------
// The editor's WorkerPipe spawns this role with spec.callback_env =
// "XER_VE_EXT_HOST", so two inherited anonymous-pipe handle values
// arrive through the environment (see the header for the wire
// contract). ve_call uses them to request editor services
// synchronously — the only ve_* surface that needs an answer.
struct HostBridge {
    HANDLE req = nullptr;            // write: host_call -> editor
    HANDLE rsp = nullptr;            // read:  editor's replies
    std::uint64_t next_req = 1;
    bool probed = false;
    bool available = false;
    // Serializes the whole write-request/read-reply transaction —
    // extension code can legally call ve_call from threads it
    // spawned itself, and interleaved lines would corrupt the
    // correlation. (The main request thread is naturally serial;
    // this is for extension-owned threads.)
    std::mutex mu;
};

HostBridge& TheBridge() {
    static HostBridge bridge;
    return bridge;
}

HANDLE EnvHandle(const char* var) {
    const char* v = std::getenv(var);
    if (v == nullptr || *v == '\0') return nullptr;
    char* end = nullptr;
    const unsigned long long h = std::strtoull(v, &end, 10);
    if (end == v || h == 0) return nullptr;
    return reinterpret_cast<HANDLE>(
        static_cast<std::uintptr_t>(h));
}

bool BridgeAvailable() {
    HostBridge& b = TheBridge();
    if (b.probed) return b.available;
    b.probed = true;
    b.req = EnvHandle("XER_VE_EXT_HOST_REQ");
    b.rsp = EnvHandle("XER_VE_EXT_HOST_RSP");
    b.available = b.req != nullptr && b.rsp != nullptr;
    return b.available;
}

enum class BridgeResult {
    kAnswer,      // editor replied — result_json holds the payload
    kNoChannel,   // spawned without the channel -> v1 fallback
    kTransport    // channel died mid-call -> error result, no requeue
};

// Writes one host_call request and blocks on the reply line whose
// req_id matches. There is NO timeout: anonymous pipes don't support
// overlapped reads, so a ReadFile here parks until the editor
// answers or the pipe breaks. An editor that never services
// host_call stalls the extension — the recovery path is the editor
// killing this worker (the whole runtime is disposable by design),
// never a half-completed in-process teardown.
BridgeResult BridgeCall(const std::string& ext_id,
                        const std::string& name,
                        const std::string& args,
                        std::string* result_json) {
    HostBridge& b = TheBridge();
    if (!BridgeAvailable()) return BridgeResult::kNoChannel;
    std::lock_guard<std::mutex> lock(b.mu);
    const std::uint64_t req_id = b.next_req++;
    Json::Value req(Json::objectValue);
    req["kind"] = "host_call";
    req["req_id"] = Json::UInt64(req_id);
    req["ext"] = ext_id;
    req["name"] = name;
    req["args"] = args;   // stays a string containing JSON — the
                          // editor parses it like ext_call's args
    const std::string wire = WriteLine(req) + "\n";
    DWORD wrote = 0;
    if (!WriteFile(b.req, wire.data(),
                   static_cast<DWORD>(wire.size()), &wrote,
                   nullptr) ||
        wrote != wire.size()) {
        return BridgeResult::kTransport;
    }
    std::string carry;
    char buf[8192];
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(b.rsp, buf, sizeof(buf), &got, nullptr) ||
            got == 0) {
            return BridgeResult::kTransport;
        }
        carry.append(buf, got);
        for (;;) {
            const auto nl = carry.find('\n');
            if (nl == std::string::npos) break;
            const std::string line = carry.substr(0, nl);
            carry.erase(0, nl + 1);
            Json::Value root;
            if (!ParseJsonLine(line, &root)) continue;
            if (root.get("req_id", 0).asUInt64() != req_id) {
                continue;   // stray line — single-threaded so none
                            // are expected; skip defensively
            }
            if (!root.get("ok", false).asBool()) {
                // Dispatch-level failure travels back as an error
                // RESULT (ve_call keeps the {"ok":false,...}
                // contract instead of adding error codes).
                Json::Value r(Json::objectValue);
                r["ok"] = false;
                r["error"] = root.get("error", "editor dispatch failed")
                                 .asString();
                *result_json = WriteLine(r);
                return BridgeResult::kAnswer;
            }
            *result_json = root.get("result", "{}").asString();
            return BridgeResult::kAnswer;
        }
    }
}
#endif  // _WIN32

}  // namespace
}  // namespace XER::VideoEditor

// The ve.core.h host implementation — plain C functions whose
// addresses go into the JIT SymbolMap. Everything the editor must
// see fire-and-forget is queued as an event on the owning
// extension's deque; ve_call is the one synchronous service and
// rides the reverse channel (or the queued call_request when the
// runtime was spawned without one).

void ve_log(ve_ctx* ctx, const char* message) {
    auto* ext = XER::VideoEditor::ExtOwner(ctx);
    if (ext == nullptr || message == nullptr) return;
    auto e = XER::VideoEditor::MakeEvent(ext, "log");
    e["msg"] = message;
    XER::VideoEditor::PushEvent(ext, std::move(e));
}

void ve_set_status(ve_ctx* ctx, const char* message) {
    auto* ext = XER::VideoEditor::ExtOwner(ctx);
    if (ext == nullptr || message == nullptr) return;
    auto e = XER::VideoEditor::MakeEvent(ext, "status");
    e["msg"] = message;
    XER::VideoEditor::PushEvent(ext, std::move(e));
}

int ve_project_info(ve_ctx* ctx, char** out_json) {
    if (out_json == nullptr) return VE_ERR_BAD_ARG;
    *out_json = nullptr;
    auto* ext = XER::VideoEditor::ExtOwner(ctx);
    if (ext == nullptr) return VE_ERR_GENERIC;
#ifdef _WIN32
    // v3: prefer the LIVE project — forward as ve_call("project.info")
    // on the reverse channel. Blocking is legal for exactly the same
    // reason ve_call blocks (extension code, never the reply-pump
    // thread). A dead or absent channel falls through to the static
    // load-time snapshot — a stale answer beats none for a read-only
    // convenience API.
    if (XER::VideoEditor::BridgeAvailable()) {
        std::string live;
        if (XER::VideoEditor::BridgeCall(ext->ext_id, "project.info",
                                         "{}", &live) ==
            XER::VideoEditor::BridgeResult::kAnswer) {
            *out_json = XER::VideoEditor::DupString(live);
            return *out_json != nullptr ? VE_OK : VE_ERR_GENERIC;
        }
    }
#endif
    *out_json = XER::VideoEditor::DupString(ext->project_json);
    return *out_json != nullptr ? VE_OK : VE_ERR_GENERIC;
}

int ve_add_clip(ve_ctx* ctx, const char* kind, const char* param,
                double start_seconds, double duration_seconds) {
    auto* ext = XER::VideoEditor::ExtOwner(ctx);
    if (ext == nullptr || kind == nullptr) return VE_ERR_BAD_ARG;
    auto e = XER::VideoEditor::MakeEvent(ext, "add_clip");
    e["kind"] = kind;
    e["param"] = param != nullptr ? param : "";
    e["start"] = start_seconds;
    e["duration"] = duration_seconds;
    XER::VideoEditor::PushEvent(ext, std::move(e));
    // Accepted for async delivery — the editor fulfills it when it
    // drains the event queue (v1 bridge, see the header comment).
    return VE_OK;
}

int ve_call(ve_ctx* ctx, const char* name, const char* args_json,
            char** out_json) {
    if (out_json == nullptr || name == nullptr) return VE_ERR_BAD_ARG;
    *out_json = nullptr;
    auto* ext = XER::VideoEditor::ExtOwner(ctx);
    if (ext == nullptr) return VE_ERR_GENERIC;
    const std::string args =
        args_json != nullptr ? args_json : "{}";
#ifdef _WIN32
    // v2: the synchronous host channel — see the header for why this
    // is a second transport instead of a reply on the request pipe.
    std::string result;
    switch (XER::VideoEditor::BridgeCall(ext->ext_id, name, args,
                                       &result)) {
        case XER::VideoEditor::BridgeResult::kAnswer:
            *out_json = XER::VideoEditor::DupString(result);
            return *out_json != nullptr ? VE_OK : VE_ERR_GENERIC;
        case XER::VideoEditor::BridgeResult::kTransport:
            // The channel died mid-call: do NOT requeue the request
            // as an event — the editor may have partially applied it
            // and a queued copy would double-apply on the next drain.
            *out_json = XER::VideoEditor::DupString(
                "{\"ok\":false,\"error\":\"host channel lost\"}");
            return *out_json != nullptr ? VE_OK : VE_ERR_GENERIC;
        case XER::VideoEditor::BridgeResult::kNoChannel:
            break;   // spawned without the channel -> v1 fallback
    }
#endif
    // v1 fallback: the call is queued as a "call_request" event so it
    // at least reaches the editor; the extension gets an explicit
    // error result rather than a hang.
    auto e = XER::VideoEditor::MakeEvent(ext, "call_request");
    e["name"] = name;
    e["args"] = args;
    XER::VideoEditor::PushEvent(ext, std::move(e));
    *out_json = XER::VideoEditor::DupString(
        "{\"ok\":false,\"error\":\"not bridged yet\"}");
    return *out_json != nullptr ? VE_OK : VE_ERR_GENERIC;
}

void ve_post(ve_ctx* ctx, const char* event_json) {
    auto* ext = XER::VideoEditor::ExtOwner(ctx);
    if (ext == nullptr || event_json == nullptr) return;
    auto e = XER::VideoEditor::MakeEvent(ext, "event");
    Json::Value parsed;
    std::string errs;
    Json::CharReaderBuilder r;
    std::istringstream in(event_json);
    if (Json::parseFromStream(r, in, &parsed, &errs) &&
        (parsed.isObject() || parsed.isArray())) {
        e["event"] = parsed;
    } else {
        e["event"] = event_json;   // non-JSON payloads ride as strings
    }
    XER::VideoEditor::PushEvent(ext, std::move(e));
}

void ve_free(ve_ctx* /*ctx*/, void* ptr) {
    std::free(ptr);
}

namespace XER::VideoEditor {
namespace {

// ---- ExtRuntime -------------------------------------------------------

const std::vector<XsjoJit::HostSymbol>& HostSymbols() {
    static const std::vector<XsjoJit::HostSymbol> symbols = {
        {"ve_log", reinterpret_cast<void*>(&ve_log)},
        {"ve_set_status", reinterpret_cast<void*>(&ve_set_status)},
        {"ve_project_info",
         reinterpret_cast<void*>(&ve_project_info)},
        {"ve_add_clip", reinterpret_cast<void*>(&ve_add_clip)},
        {"ve_call", reinterpret_cast<void*>(&ve_call)},
        {"ve_post", reinterpret_cast<void*>(&ve_post)},
        {"ve_free", reinterpret_cast<void*>(&ve_free)},
    };
    return symbols;
}

struct ExtRuntime {
    // Lazily built on the first native ext_load — a JS-only workload
    // never pays for LLVM startup (or the delay-load probe).
    std::unique_ptr<XsjoJit> jit;
    std::string jit_error;
    bool jit_tried = false;
    std::vector<std::unique_ptr<LoadedExt>> loaded;

    XsjoJit* Jit() {
        if (!jit_tried) {
            jit_tried = true;
            jit = XsjoJit::Create(&jit_error);
        }
        return jit.get();
    }

    LoadedExt* Find(const std::string& ext_id) {
        for (auto& e : loaded) {
            if (e->ext_id == ext_id) return e.get();
        }
        return nullptr;
    }

    // Moves the queued events into `arr` (a JSON array the reply then
    // carries — see the header's event-envelope contract).
    void DrainEvents(LoadedExt* ext, Json::Value* arr) {
        if (ext == nullptr || arr == nullptr) return;
        while (!ext->events.empty()) {
            arr->append(std::move(ext->events.front()));
            ext->events.pop_front();
        }
    }

    void UnloadModules(LoadedExt& ext) {
        if (jit == nullptr) return;
        for (const std::uint64_t mid : ext.modules) {
            static_cast<void>(jit->UnloadModule(mid));
        }
        ext.modules.clear();
    }
};

// ---- JSON plumbing ----------------------------------------------------

bool ParseJsonLine(const std::string& line, Json::Value* out) {
    std::string errs;
    Json::CharReaderBuilder r;
    std::istringstream in(line);
    return Json::parseFromStream(r, in, out, &errs) &&
           out->isObject();
}

std::string WriteLine(const Json::Value& v) {
    Json::StreamWriterBuilder w;
    w["indentation"] = "";
    return Json::writeString(w, v);
}

std::string Reply(std::uint64_t id, Json::Value&& payload) {
    payload["id"] = Json::UInt64(id);
    payload["ok"] = true;
    return WriteLine(payload);
}

std::string ReplyErr(std::uint64_t id, const std::string& err) {
    return MediaWorker::EncodeErrorReply(id, err);
}

// "args" may arrive as a string (already JSON) or a JSON value to be
// serialized — both land as the args_json string the callable sees.
std::string ArgsText(const Json::Value& root) {
    const Json::Value& a = root["args"];
    if (a.isNull()) {
        const Json::Value& aj = root["args_json"];
        return aj.isString() ? aj.asString() : std::string("{}");
    }
    return a.isString() ? a.asString() : WriteLine(a);
}

bool HasXsjoMembers(const Tz::TzArchive& archive,
                    std::vector<std::string>* names) {
    names->clear();
    for (const Tz::TzEntry& e : archive.Entries()) {
        if (e.name.size() > 11 &&
            e.name.compare(0, 11, "mobjective/") == 0 &&
            e.name.size() > 5 &&
            e.name.compare(e.name.size() - 5, 5, ".xsjo") == 0) {
            names->push_back(e.name);
        }
    }
    return !names->empty();
}

std::string DefaultExtensionsDir() {
#ifdef _WIN32
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return (std::filesystem::path(exe).parent_path() / "assets" /
            "extensions")
        .string();
#else
    return {};
#endif
}

// Opens + parses + verifies one archive — the same sequence
// ExtHost::Load runs, minus the CEF registration (this process has
// no browser).
bool OpenVerified(const std::string& path,
                  std::shared_ptr<Tz::TzArchive>* archive_out,
                  ExtManifest* manifest, std::string* error) {
    std::string open_err;
    auto archive = Tz::TzArchive::Open(path, &open_err);
    if (!archive) {
        if (error) *error = open_err;
        return false;
    }
    std::string manifest_text;
    if (!archive->Read("manifest.toml", &manifest_text)) {
        if (error) *error = "manifest.toml missing";
        return false;
    }
    if (!ExtManifestParse(manifest_text, manifest, error)) {
        return false;
    }
    std::vector<std::string> required = manifest->required;
    required.push_back(manifest->entry);
    auto report = Tz::TzVerify(path, required);
    if (!report.ok) {
        if (error) {
            *error = report.errors.empty()
                         ? "verification failed"
                         : report.errors.front();
        }
        return false;
    }
    *archive_out = std::move(archive);
    return true;
}

// ---- request handlers -------------------------------------------------

std::string HandleExtList(ExtRuntime& /*rt*/, std::uint64_t id,
                          const Json::Value& root) {
    std::string dir = root.get("dir", "").asString();
    if (dir.empty()) dir = DefaultExtensionsDir();
    Json::Value exts(Json::arrayValue);
    std::error_code ec;
    if (std::filesystem::is_directory(dir, ec)) {
        for (const auto& de :
             std::filesystem::directory_iterator(dir, ec)) {
            if (!de.is_regular_file(ec) ||
                de.path().extension() != ".xexrj") {
                continue;
            }
            Json::Value item(Json::objectValue);
            item["path"] = de.path().string();
            std::string open_err;
            auto archive =
                Tz::TzArchive::Open(item["path"].asString(), &open_err);
            bool has_static = false;
            if (archive) {
                std::vector<std::string> xsjo;
                item["has_native"] = HasXsjoMembers(*archive, &xsjo);
                for (const Tz::TzEntry& e : archive->Entries()) {
                    if (e.name.compare(0, 7, "static/") == 0) {
                        has_static = true;
                        break;
                    }
                }
                std::string manifest_text;
                ExtManifest manifest;
                std::string parse_err;
                if (archive->Read("manifest.toml", &manifest_text) &&
                    ExtManifestParse(manifest_text, &manifest,
                                     &parse_err)) {
                    item["id"] = manifest.id;
                    item["name"] = manifest.name;
                } else {
                    item["id"] = "";
                    item["name"] = "";
                    item["error"] =
                        parse_err.empty() ? "manifest unreadable"
                                          : parse_err;
                }
            } else {
                item["has_native"] = false;
                item["error"] =
                    open_err.empty() ? "archive unreadable" : open_err;
            }
            item["has_static"] = has_static;
            exts.append(std::move(item));
        }
    }
    Json::Value p(Json::objectValue);
    p["exts"] = std::move(exts);
    p["dir"] = dir;
    return Reply(id, std::move(p));
}

std::string HandleExtLoad(ExtRuntime& rt, std::uint64_t id,
                          const Json::Value& root) {
    const std::string path = root.get("path", "").asString();
    if (path.empty()) {
        return ReplyErr(id, "ext_load requires \"path\"");
    }
    std::shared_ptr<Tz::TzArchive> archive;
    ExtManifest manifest;
    std::string error;
    if (!OpenVerified(path, &archive, &manifest, &error)) {
        return ReplyErr(id, error);
    }

    std::vector<std::string> xsjo_names;
    const bool native = HasXsjoMembers(*archive, &xsjo_names);
    if (!native) {
        // JS/static-only extension — nothing for the JIT; the
        // in-editor ExtHost owns this side. Report success so the
        // caller can route instead of treating it as a failure.
        Json::Value p(Json::objectValue);
        p["ext_id"] = manifest.id;
        p["name"] = manifest.name;
        p["native"] = false;
        p["modules"] = 0;
        p["note"] = "no mobjective members — JS/static extension "
                    "belongs to the in-editor host";
        p["events"] = Json::Value(Json::arrayValue);
        return Reply(id, std::move(p));
    }

    XsjoJit* jit = rt.Jit();
    if (jit == nullptr) {
        return ReplyErr(id, "JIT unavailable: " + rt.jit_error);
    }

    auto ext = std::make_unique<LoadedExt>();
    ext->ext_id = manifest.id;
    ext->name = manifest.name;
    ext->path = path;
    ext->archive = archive;
    const Json::Value& pj = root["project_json"];
    if (pj.isString() && !pj.asString().empty()) {
        ext->project_json = pj.asString();
    } else if (root["project"].isObject()) {
        ext->project_json = WriteLine(root["project"]);
    }
    ext->ctx.owner = ext.get();

    for (const std::string& member : xsjo_names) {
        std::vector<std::uint8_t> bytes;
        if (!archive->Read(member, &bytes)) {
            rt.UnloadModules(*ext);
            return ReplyErr(id, "cannot read member " + member);
        }
        std::uint64_t mid = 0;
        auto res = jit->LoadModule(bytes.data(), bytes.size(),
                                   manifest.id + ":" + member,
                                   HostSymbols(), &mid);
        if (!res) {
            rt.UnloadModules(*ext);
            return ReplyErr(id, member + ": " + res.error);
        }
        ext->modules.push_back(mid);
    }

    // Mesh the extension's module dylibs: each module's undefined
    // symbols resolve against every other module of the SAME
    // extension — this is what links spliced-in library objects
    // (mobjective/lib_*.xsjo, produced from .xejslib deps by
    // ztzmake) to the extension's own code. The mesh is built
    // lazily-safe: ORC resolves at materialization time, and every
    // dylib already exists here.
    for (const std::uint64_t mid : ext->modules) {
        auto res = jit->LinkModules(mid, ext->modules);
        if (!res) {
            rt.UnloadModules(*ext);
            return ReplyErr(id, "module link failed: " + res.error);
        }
    }

    rt.loaded.push_back(std::move(ext));
    LoadedExt& le = *rt.loaded.back();

    // ve_ext_api_version is optional; report it so the editor can
    // warn on ABI drift without failing the load.
    int api_version = VE_CORE_API_VERSION;
    for (const std::uint64_t mid : le.modules) {
        void* addr = nullptr;
        if (jit->FindSymbol(mid, "ve_ext_api_version", &addr)) {
            api_version =
                reinterpret_cast<ve_api_version_fn>(addr)();
            break;
        }
    }

    // ve_ext_entry is called on the first module that exports it —
    // one entry call per extension. A payload that only hosts
    // callables (no lifecycle hook) is legal: reported as
    // entry:"missing" instead of failing the load.
    int entry_rc = 0;
    bool entry_ran = false;
    for (const std::uint64_t mid : le.modules) {
        void* addr = nullptr;
        if (!jit->FindSymbol(mid, "ve_ext_entry", &addr)) continue;
        auto entry = jit->CallEntry(mid, "ve_ext_entry", &le.ctx,
                                    &entry_rc);
        if (!entry) {
            rt.UnloadModules(le);
            rt.loaded.pop_back();
            return ReplyErr(id, "ve_ext_entry call failed: " +
                                entry.error);
        }
        entry_ran = true;
        break;
    }
    if (entry_ran && entry_rc != VE_OK) {
        auto e = MakeEvent(&le, "entry_failed");
        e["rc"] = entry_rc;
        PushEvent(&le, std::move(e));
    }
    Json::Value events(Json::arrayValue);
    rt.DrainEvents(&le, &events);

    Json::Value p(Json::objectValue);
    p["ext_id"] = le.ext_id;
    p["name"] = le.name;
    p["native"] = true;
    p["modules"] = static_cast<Json::UInt64>(le.modules.size());
    p["entry_rc"] = entry_ran ? Json::Value(entry_rc) : Json::Value();
    p["api_version"] = api_version;
    p["events"] = std::move(events);
    return Reply(id, std::move(p));
}

std::string HandleExtCall(ExtRuntime& rt, std::uint64_t id,
                          const Json::Value& root) {
    const std::string ext_id = root.get("ext_id", "").asString();
    const std::string fn = root.get("fn", "").asString();
    if (ext_id.empty() || fn.empty()) {
        return ReplyErr(id,
                        "ext_call requires \"ext_id\" and \"fn\"");
    }
    LoadedExt* ext = rt.Find(ext_id);
    if (ext == nullptr) {
        return ReplyErr(id, "extension '" + ext_id +
                            "' is not loaded (or has no native "
                            "modules)");
    }
    XsjoJit* jit = rt.Jit();
    if (jit == nullptr) {
        return ReplyErr(id, "JIT unavailable: " + rt.jit_error);
    }
    const std::string args = ArgsText(root);
    // Search every module of the extension — the callable may live
    // in any of its objects.
    for (const std::uint64_t mid : ext->modules) {
        void* addr = nullptr;
        if (!jit->FindSymbol(mid, fn, &addr)) continue;
        std::string result;
        auto call = jit->CallJson(mid, fn, &ext->ctx, args, &result);
        if (!call) {
            return ReplyErr(id, fn + ": " + call.error);
        }
        Json::Value events(Json::arrayValue);
        rt.DrainEvents(ext, &events);
        Json::Value p(Json::objectValue);
        p["result_json"] = result;
        p["events"] = std::move(events);
        return Reply(id, std::move(p));
    }
    return ReplyErr(id, "symbol not found: " + fn);
}

std::string HandleExtUnload(ExtRuntime& rt, std::uint64_t id,
                            const Json::Value& root) {
    const std::string ext_id = root.get("ext_id", "").asString();
    if (ext_id.empty()) {
        return ReplyErr(id, "ext_unload requires \"ext_id\"");
    }
    for (auto it = rt.loaded.begin(); it != rt.loaded.end(); ++it) {
        if ((*it)->ext_id != ext_id) continue;
        LoadedExt& ext = **it;
        // Optional shutdown hook — absence is not an error.
        if (rt.jit != nullptr && !ext.modules.empty()) {
            void* addr = nullptr;
            if (rt.jit->FindSymbol(ext.modules.front(),
                                   "ve_ext_shutdown", &addr)) {
                reinterpret_cast<ve_shutdown_fn>(addr)(&ext.ctx);
            }
        }
        rt.UnloadModules(ext);
        rt.loaded.erase(it);
        return Reply(id, Json::Value(Json::objectValue));
    }
    return ReplyErr(id, "extension '" + ext_id + "' is not loaded");
}

std::string HandleExtEvents(ExtRuntime& rt, std::uint64_t id,
                            const Json::Value& root) {
    const std::string filter = root.get("ext_id", "").asString();
    Json::Value events(Json::arrayValue);
    for (auto& e : rt.loaded) {
        if (!filter.empty() && e->ext_id != filter) continue;
        rt.DrainEvents(e.get(), &events);
    }
    Json::Value p(Json::objectValue);
    p["events"] = std::move(events);
    return Reply(id, std::move(p));
}

}  // namespace

int RunExtRuntime(const char* role) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    const std::string role_str =
        (role != nullptr && role[0] != '\0') ? role : "ext-runtime";
    ExtRuntime rt;
    std::string line;
    while (std::getline(std::cin, line)) {
        Json::Value root;
        std::string reply;
        if (!ParseJsonLine(line, &root)) {
            reply = MediaWorker::EncodeErrorReply(
                0, "malformed request");
        } else {
            const std::uint64_t id =
                root.get("id", 0).asUInt64();
            const std::string kind =
                root.get("kind", "").asString();
            if (kind == "ping") {
                reply = MediaWorker::EncodePongReply(id, role_str);
            } else if (kind == "info") {
                // Same {role,serves} shape as EncodeInfoReply plus
                // "sdk" — the ve.core generation this runtime was
                // built against so clients can probe the service
                // table level (v3: the full ve_call surface).
                Json::Value p(Json::objectValue);
                p["role"] = role_str;
                p["serves"] =
                    "ext_load,ext_call,ext_unload,ext_list,"
                    "ext_events,ping,info";
                p["sdk"] = VE_CORE_API_VERSION;
                reply = Reply(id, std::move(p));
            } else if (kind == "ext_list") {
                reply = HandleExtList(rt, id, root);
            } else if (kind == "ext_load") {
                reply = HandleExtLoad(rt, id, root);
            } else if (kind == "ext_call") {
                reply = HandleExtCall(rt, id, root);
            } else if (kind == "ext_unload") {
                reply = HandleExtUnload(rt, id, root);
            } else if (kind == "ext_events") {
                reply = HandleExtEvents(rt, id, root);
            } else {
                reply = ReplyErr(id, "unknown kind '" + kind +
                                     "' for this worker role");
            }
        }
        std::cout << reply << '\n' << std::flush;
    }
    // EOF = parent closed stdin (shutdown or crash) — exit quietly.
    return 0;
}

}  // namespace XER::VideoEditor
