// js_fx.cc — see js_fx.h for the contract.
#include "js_fx.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

#include <json/json.h>

#include "app_runtime/js_engine.h"
#include "frame.h"
#include "media_worker_protocol.h"
#include "worker_pipe.h"

#if defined(XER_VE_HAS_V8)
#include <libplatform/libplatform.h>
#include <v8.h>
#endif

namespace XER::VideoEditor {
namespace {

// ---- process-wide catalog (JSON only — no V8) ----------------------
//
// LoadCatalog can run on any thread while compositor/mixer workers
// read the lists, so access goes through an immutable-snapshot
// scheme: each load builds a Catalog locally and publishes it under
// an exclusive lock. Snapshots live in an APPEND-ONLY deque — deque
// elements never move and entries are never erased, so a reader that
// grabbed `g_catalog` (or a reference the accessors returned) keeps a
// valid view even if a later load supersedes it. Superseded snapshots
// stay allocated; loads are rare (once per process in practice) and
// each snapshot is a few KiB.

struct Catalog {
    std::string fx_dir;
    std::vector<JsFxRuntime::FxDesc> video_fx;
    std::vector<JsFxRuntime::FxDesc> audio_fx;
    std::vector<JsFxRuntime::FxDesc> transitions;
    bool loaded = false;
};

std::shared_mutex g_catalog_mu;
std::deque<Catalog> g_catalogs;       // append-only — stable refs
const Catalog* g_catalog = nullptr;   // latest published snapshot

// Snapshot the current catalog. The returned pointer stays valid for
// the rest of the process (see the deque contract above); nullptr
// means LoadCatalog was never called.
const Catalog* CurrentCatalog() {
    std::shared_lock lock(g_catalog_mu);
    return g_catalog;
}

void ParseParamDefs(const Json::Value& arr,
                    std::vector<JsFxRuntime::ParamDef>* out) {
    if (!arr.isArray()) return;
    for (const auto& p : arr) {
        JsFxRuntime::ParamDef def;
        def.id = p.get("id", "").asString();
        def.label = p.get("label", def.id).asString();
        def.def = static_cast<float>(p.get("def", 0.0).asDouble());
        def.min = static_cast<float>(p.get("min", 0.0).asDouble());
        def.max = static_cast<float>(p.get("max", 1.0).asDouble());
        if (!def.id.empty()) out->push_back(def);
    }
}

void ParseDescList(const Json::Value& arr,
                   std::vector<JsFxRuntime::FxDesc>* out) {
    if (!arr.isArray()) return;
    for (const auto& e : arr) {
        JsFxRuntime::FxDesc desc;
        desc.id = e.get("id", "").asString();
        desc.title = e.get("title", desc.id).asString();
        desc.file = e.get("file", "").asString();
        ParseParamDefs(e["params"], &desc.params);
        // Path safety mirrors ScriptHost: never absolute, never "..".
        const std::filesystem::path f =
            std::filesystem::path(desc.file).lexically_normal();
        if (desc.id.empty() || desc.file.empty() || f.is_absolute() ||
            desc.file.find("..") != std::string::npos) {
            continue;
        }
        out->push_back(std::move(desc));
    }
}

const JsFxRuntime::FxDesc* FindDesc(
    const std::vector<JsFxRuntime::FxDesc>& list,
    const std::string& id) {
    for (const auto& d : list) {
        if (d.id == id) return &d;
    }
    return nullptr;
}

// Appends `next` to the append-only snapshot deque and publishes it.
void PublishCatalog(Catalog next) {
    std::unique_lock lock(g_catalog_mu);
    g_catalogs.push_back(std::move(next));
    g_catalog = &g_catalogs.back();
}

constexpr int kFxListTimeoutMs = 30000;
constexpr int kFxAudioTimeoutMs = 15000;

#if defined(_WIN32)
// Out-of-process manifest listing via XERFxScript.exe — the worker
// answers fx_list with plain JSON (no V8), so this stays cheap while
// still proving/warming the remote fx host before audio-eval calls
// rely on it. Any failure → caller runs the local manifest parse.
// The worker process itself disables FxScriptPipe (XER_VE_FX_WORKER)
// so its own LoadCatalog can never recurse into a grandchild.
bool LoadCatalogViaWorker(const std::string& fx_dir, Catalog* out) {
    WorkerPipe& pipe = FxScriptPipe();
    if (!pipe.Available()) return false;
    const std::uint64_t id = pipe.NextId();
    std::string reply;
    if (!pipe.RoundTrip(
            id, MediaWorker::EncodeFxListRequest(id, fx_dir),
            kFxListTimeoutMs, &reply)) {
        return false;
    }
    MediaWorker::FxListReply list;
    std::uint64_t rid = 0;
    if (!MediaWorker::DecodeFxListReply(reply, &rid, &list)) {
        return false;
    }
    auto convert = [](const std::vector<MediaWorker::FxListItem>& src,
                      std::vector<JsFxRuntime::FxDesc>* dst) {
        for (const auto& item : src) {
            JsFxRuntime::FxDesc desc;
            desc.id = item.id;
            desc.title = item.title;
            desc.file = item.file;
            for (const auto& p : item.params) {
                JsFxRuntime::ParamDef def;
                def.id = p.id;
                def.label = p.label;
                def.def = p.def;
                def.min = p.min;
                def.max = p.max;
                desc.params.push_back(std::move(def));
            }
            dst->push_back(std::move(desc));
        }
    };
    out->fx_dir = fx_dir;
    convert(list.video, &out->video_fx);
    convert(list.audio, &out->audio_fx);
    convert(list.transitions, &out->transitions);
    out->loaded = true;
    return true;
}
#endif  // _WIN32

}  // namespace

bool JsFxRuntime::LoadCatalog(const std::string& fx_dir,
                              std::string* error) {
#if defined(_WIN32)
    // Prefer the worker's parse of the same manifest — identical
    // contents, off-process. Falls through to the local read on any
    // transport/spawn trouble.
    {
        Catalog remote;
        if (LoadCatalogViaWorker(fx_dir, &remote)) {
            PublishCatalog(std::move(remote));
            return true;
        }
    }
#endif
    Catalog next;
    next.fx_dir = fx_dir;
    const std::filesystem::path manifest =
        std::filesystem::path(fx_dir) / "fx.json";
    std::ifstream in(manifest, std::ios::binary);
    if (!in) {
        if (error) *error = "fx.json not found: " + manifest.string();
        // Publish the empty snapshot — readers see "loaded=false",
        // never a torn list.
        PublishCatalog(std::move(next));
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    Json::Value root;
    std::string errs;
    Json::CharReaderBuilder builder;
    if (!Json::parseFromStream(builder, ss, &root, &errs)) {
        if (error) *error = "fx.json parse: " + errs;
        return false;   // keep the previous snapshot on parse failure
    }
    ParseDescList(root["video"], &next.video_fx);
    ParseDescList(root["audio"], &next.audio_fx);
    ParseDescList(root["transitions"], &next.transitions);
    next.loaded = true;
    PublishCatalog(std::move(next));
    return true;
}

const std::vector<JsFxRuntime::FxDesc>& JsFxRuntime::VideoEffects() {
    static const std::vector<FxDesc> kEmpty;
    const Catalog* cat = CurrentCatalog();
    return cat == nullptr ? kEmpty : cat->video_fx;
}
const std::vector<JsFxRuntime::FxDesc>& JsFxRuntime::AudioEffects() {
    static const std::vector<FxDesc> kEmpty;
    const Catalog* cat = CurrentCatalog();
    return cat == nullptr ? kEmpty : cat->audio_fx;
}
const std::vector<JsFxRuntime::FxDesc>& JsFxRuntime::ScriptTransitions() {
    static const std::vector<FxDesc> kEmpty;
    const Catalog* cat = CurrentCatalog();
    return cat == nullptr ? kEmpty : cat->transitions;
}
const std::string& JsFxRuntime::FxDir() {
    static const std::string kEmpty;
    const Catalog* cat = CurrentCatalog();
    return cat == nullptr ? kEmpty : cat->fx_dir;
}

// ------------------------------------------------------------------
#if defined(XER_VE_HAS_V8)

struct JsFxRuntime::Impl {
    std::unique_ptr<AppRuntime::JsEngine> engine;
    bool load_attempted = false;
    std::unordered_map<std::string, v8::Global<v8::Function>> video_fns;
    std::unordered_map<std::string, v8::Global<v8::Function>> audio_fns;
    std::unordered_map<std::string, v8::Global<v8::Function>>
        transition_fns;

    ~Impl() {
        // Globals die before the engine's isolate.
        video_fns.clear();
        audio_fns.clear();
        transition_fns.clear();
    }
};

namespace {

std::string V8Str(v8::Isolate* iso, v8::Local<v8::Value> v) {
    v8::String::Utf8Value s(iso, v);
    return s.length() != 0 ? std::string(*s, s.length()) : std::string();
}

// Extracts {id, fn_property} from the descriptor object a script
// passed to xerFxVideo/xerFxAudio/xerFxTransition and stores the
// function in `table`.
void RegisterFromArgs(
    const v8::FunctionCallbackInfo<v8::Value>& info,
    const char* fn_prop,
    std::unordered_map<std::string, v8::Global<v8::Function>>* table) {
    v8::Isolate* iso = info.GetIsolate();
    if (info.Length() < 1 || !info[0]->IsObject()) {
        iso->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(iso,
                "xerFx*: descriptor object expected")));
        return;
    }
    const auto ctx = iso->GetCurrentContext();
    const v8::Local<v8::Object> obj = info[0].As<v8::Object>();
    const auto id_key =
        v8::String::NewFromUtf8Literal(iso, "id");
    v8::Local<v8::Value> id_val;
    if (!obj->Get(ctx, id_key).ToLocal(&id_val) || !id_val->IsString()) {
        iso->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(iso,
                "xerFx*: descriptor.id string required")));
        return;
    }
    const std::string id = V8Str(iso, id_val);
    const auto fn_key =
        v8::String::NewFromUtf8(iso, fn_prop).ToLocalChecked();
    v8::Local<v8::Value> fn_val;
    if (!obj->Get(ctx, fn_key).ToLocal(&fn_val) ||
        !fn_val->IsFunction()) {
        iso->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(
                iso, "xerFx*: descriptor function missing")));
        return;
    }
    (*table)[id].Reset(
        iso, fn_val.As<v8::Function>());
}

std::string FormatV8Error(v8::Isolate* iso, v8::TryCatch& try_catch) {
    if (!try_catch.HasCaught()) return {};
    const v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    const v8::Local<v8::Message> msg = try_catch.Message();
    std::string out;
    if (!msg.IsEmpty()) {
        // GetSourceLine can legitimately return empty (compiled code
        // without an attached source, terminated execution) — a
        // ToLocalChecked() here would CHECK-fail *while reporting the
        // error*. Fall back to the message text instead.
        v8::Local<v8::String> line;
        if (msg->GetSourceLine(ctx).ToLocal(&line)) {
            out = V8Str(iso, line);
        }
        if (out.empty()) {
            out = V8Str(iso, msg->Get());
        }
        out += " : ";
    }
    out += V8Str(iso, try_catch.Exception());
    if (out.empty()) out = "(unknown script error)";
    return out;
}

v8::Local<v8::Object> ParamsObject(
    v8::Isolate* iso, const v8::Local<v8::Context>& ctx,
    const JsFxRuntime::FxDesc* desc,
    const std::vector<float>& positional) {
    const v8::Local<v8::Object> obj = v8::Object::New(iso);
    if (desc == nullptr) return obj;
    for (std::size_t i = 0; i < desc->params.size(); ++i) {
        const float v = i < positional.size()
                            ? positional[i]
                            : desc->params[i].def;
        obj->Set(ctx,
                 v8::String::NewFromUtf8(iso, desc->params[i].id.c_str())
                     .ToLocalChecked(),
                 v8::Number::New(iso, static_cast<double>(v)))
            .Check();
    }
    return obj;
}

v8::Local<v8::Object> ParamsObject(
    v8::Isolate* iso, const v8::Local<v8::Context>& ctx,
    const JsFxRuntime::FxDesc* desc,
    const std::map<std::uint32_t, float>& by_ordinal) {
    std::vector<float> positional;
    if (desc != nullptr) {
        positional.resize(desc->params.size());
        for (std::size_t i = 0; i < desc->params.size(); ++i) {
            const auto it = by_ordinal.find(
                static_cast<std::uint32_t>(i));
            positional[i] = it != by_ordinal.end()
                                ? it->second
                                : desc->params[i].def;
        }
    }
    return ParamsObject(iso, ctx, desc, positional);
}

// Typed array the script mutates. V8_ENABLE_SANDBOX forbids
// non-owning backing stores, so the buffer is V8-allocated and the
// native data round-trips: memcpy in before the call, memcpy out
// after (one copy per invocation — bounded by the frame size).
struct SandboxView {
    v8::Local<v8::Value> view;
    void* store = nullptr;   // backing bytes — stable until view dies
    std::size_t bytes = 0;
};

SandboxView BufferView(v8::Isolate* iso, const void* data,
                       std::size_t bytes, bool is_float) {
    SandboxView out;
    const auto buffer = v8::ArrayBuffer::New(iso, bytes);
    out.store = buffer->GetBackingStore()->Data();
    out.bytes = bytes;
    std::memcpy(out.store, data, bytes);
    if (is_float) {
        out.view = v8::Float32Array::New(buffer, 0,
                                         bytes / sizeof(float))
                       .As<v8::Value>();
    } else {
        out.view = v8::Uint8ClampedArray::New(buffer, 0, bytes)
                       .As<v8::Value>();
    }
    return out;
}

// ---- runaway-script watchdog ----------------------------------------
//
// One leaked worker thread process-wide. Each Arm() registers the
// (isolate, deadline) pair; the worker sleeps until the earliest
// deadline and calls Isolate::TerminateExecution() on any entry still
// armed — TerminateExecution is documented as callable from any
// thread without a Locker (v8-isolate.h). A terminated Call() surfaces
// as TryCatch::HasTerminated(); the caller then runs
// CancelTerminateExecution() so the isolate keeps working.
// Runtimes are thread-confined, so armed entries never contend with
// each other — the mutex below only serializes the registry itself.
constexpr auto kVideoApplyBudget = std::chrono::milliseconds(500);
constexpr auto kAudioProcessBudget = std::chrono::milliseconds(100);
constexpr auto kTransitionBudget = std::chrono::milliseconds(500);
constexpr auto kScriptLoadBudget = std::chrono::milliseconds(1000);

class FxWatchdog {
public:
    struct Entry {
        v8::Isolate* iso = nullptr;
        std::chrono::steady_clock::time_point deadline;
        std::atomic<bool> done{false};
        bool fired = false;
    };

    static FxWatchdog& Get() {
        // Intentionally leaked: the worker sleeps on the CV for the
        // whole process; a destructed static would have to join it.
        static FxWatchdog* watchdog = new FxWatchdog();
        return *watchdog;
    }

    // RAII guard — arm on the isolate-owning thread, disarm as soon
    // as the JS call returns (destructor covers early exits).
    class Arm {
    public:
        Arm(v8::Isolate* iso, std::chrono::milliseconds budget)
            : entry_(std::make_shared<Entry>()) {
            entry_->iso = iso;
            entry_->deadline =
                std::chrono::steady_clock::now() + budget;
            FxWatchdog::Get().Add(entry_);
        }
        ~Arm() { FxWatchdog::Get().Remove(entry_); }
        Arm(const Arm&) = delete;
        Arm& operator=(const Arm&) = delete;
        // Stop the watchdog firing while C++ epilogue work (e.g. the
        // framebuffer memcpy-back) is still running; entry removal
        // stays for the destructor.
        void Disarm() {
            entry_->done.store(true, std::memory_order_release);
        }
    private:
        std::shared_ptr<Entry> entry_;
    };

private:
    FxWatchdog() : worker_([this] { Run(); }) { worker_.detach(); }

    void Add(const std::shared_ptr<Entry>& entry) {
        {
            const std::lock_guard lock(mu_);
            entries_.push_back(entry);
        }
        cv_.notify_one();
    }

    void Remove(const std::shared_ptr<Entry>& entry) {
        {
            const std::lock_guard lock(mu_);
            entry->done.store(true, std::memory_order_release);
            std::erase(entries_, entry);
        }
        cv_.notify_one();
    }

    void Run() {
        std::unique_lock lock(mu_);
        for (;;) {
            auto nearest =
                std::chrono::steady_clock::time_point::max();
            for (const auto& e : entries_) {
                if (!e->fired &&
                    !e->done.load(std::memory_order_acquire) &&
                    e->deadline < nearest) {
                    nearest = e->deadline;
                }
            }
            if (nearest ==
                std::chrono::steady_clock::time_point::max()) {
                cv_.wait(lock);
                continue;
            }
            cv_.wait_until(lock, nearest);
            const auto now = std::chrono::steady_clock::now();
            for (const auto& e : entries_) {
                if (!e->fired &&
                    !e->done.load(std::memory_order_acquire) &&
                    now >= e->deadline) {
                    e->fired = true;
                    e->iso->TerminateExecution();
                }
            }
        }
    }

    std::mutex mu_;
    std::condition_variable cv_;
    std::vector<std::shared_ptr<Entry>> entries_;
    std::thread worker_;
};

// Message text for a watchdog-terminated call — carries the script id
// so the callers' warn-once dedup reports it once per effect.
std::string BudgetError(const std::string& id,
                        std::chrono::milliseconds budget) {
    return "script '" + id + "' exceeded the " +
           std::to_string(budget.count()) +
           "ms execution budget (terminated)";
}

#if defined(_WIN32)
// Out-of-process script eval via XERFxScript.exe — see ProcessAudio.
//   kDone        — worker applied the fx; pcm holds the result
//   kRejected    — worker answered ok:false (`error` filled —
//                  authoritative: it loaded the same catalog)
//   kUnavailable — transport failure → caller falls back in-process
enum class RemoteAudio { kDone, kRejected, kUnavailable };
RemoteAudio ProcessAudioViaWorker(
    const std::string& id, float* interleaved, int frames,
    int sample_rate, const std::map<std::uint32_t, float>& params,
    double time_s, const std::string& clip_id, std::string* error) {
    const std::string dir = JsFxRuntime::FxDir();
    if (dir.empty()) {
        return RemoteAudio::kUnavailable;   // worker needs a catalog
    }
    WorkerPipe& pipe = FxScriptPipe();
    if (!pipe.Available()) return RemoteAudio::kUnavailable;
    const std::uint64_t rid = pipe.NextId();
    std::string reply;
    if (!pipe.RoundTrip(
            rid,
            MediaWorker::EncodeFxAudioRequest(
                rid, dir, id, clip_id, interleaved, frames, 2,
                sample_rate, params, time_s),
            kFxAudioTimeoutMs, &reply)) {
        return RemoteAudio::kUnavailable;
    }
    std::uint64_t env_id = 0;
    bool ok = false;
    std::string err;
    if (!MediaWorker::DecodeReplyEnvelope(reply, &env_id, &ok,
                                          &err)) {
        return RemoteAudio::kUnavailable;   // garbage on the wire
    }
    if (!ok) {
        if (error != nullptr) {
            *error = err.empty() ? "fx worker failed" : err;
        }
        return RemoteAudio::kRejected;
    }
    std::vector<float> out_pcm;
    if (!MediaWorker::DecodeFxAudioReply(reply, &env_id, &out_pcm) ||
        out_pcm.size() != static_cast<std::size_t>(frames) * 2u) {
        return RemoteAudio::kUnavailable;
    }
    std::memcpy(interleaved, out_pcm.data(),
                out_pcm.size() * sizeof(float));
    return RemoteAudio::kDone;
}
#endif  // _WIN32

}  // namespace

JsFxRuntime::JsFxRuntime() : impl_(std::make_unique<Impl>()) {}
JsFxRuntime::~JsFxRuntime() = default;

bool JsFxRuntime::EnsureEngineReady(std::string* error) {
    if (impl_->engine) return true;
    impl_->engine = AppRuntime::JsEngine::Create(error);
    if (!impl_->engine) return false;
    Impl* impl = impl_.get();
    const bool installed = impl->engine->WithContext(
        [impl](v8::Isolate* iso, v8::Local<v8::Context> ctx) {
            const v8::Local<v8::Object> global = ctx->Global();
            auto install = [&](const char* name, const char* fn_prop,
                               auto* table) {
                v8::Local<v8::Array> data = v8::Array::New(iso, 2);
                data->Set(ctx, 0,
                          v8::External::New(iso, table)).Check();
                data->Set(ctx, 1,
                          v8::External::New(
                              iso, const_cast<char*>(fn_prop))).Check();
                const auto tmpl = v8::FunctionTemplate::New(
                    iso,
                    [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                        v8::Isolate* iso2 = info.GetIsolate();
                        if (!info.Data()->IsArray()) return;
                        const auto arr = info.Data().As<v8::Array>();
                        const auto ctx2 = iso2->GetCurrentContext();
                        v8::Local<v8::Value> ext_t, ext_p;
                        if (!arr->Get(ctx2, 0).ToLocal(&ext_t) ||
                            !arr->Get(ctx2, 1).ToLocal(&ext_p)) {
                            return;
                        }
                        auto* table = static_cast<
                            std::unordered_map<
                                std::string,
                                v8::Global<v8::Function>>*>(
                            ext_t.As<v8::External>()->Value());
                        const char* fn_prop =
                            static_cast<const char*>(
                                ext_p.As<v8::External>()->Value());
                        RegisterFromArgs(info, fn_prop, table);
                    },
                    data);
                global
                    ->Set(ctx,
                          v8::String::NewFromUtf8(iso, name)
                              .ToLocalChecked(),
                          tmpl->GetFunction(ctx).ToLocalChecked())
                    .Check();
            };
            install("xerFxVideo", "apply", &impl->video_fns);
            install("xerFxAudio", "process", &impl->audio_fns);
            install("xerFxTransition", "weight", &impl->transition_fns);
            return true;
        });
    if (!installed) {
        if (error) *error = "js fx: could not install globals";
        return false;
    }
    return true;
}

bool JsFxRuntime::EnsureLoaded(std::string* error) {
    if (!EnsureEngineReady(error)) return false;
    if (impl_->load_attempted) {
        return true;
    }
    impl_->load_attempted = true;
    const Catalog* catalog = CurrentCatalog();
    if (catalog == nullptr || !catalog->loaded) {
        if (error) *error = "js fx catalog not loaded";
        return false;
    }
    // Evaluate every file the catalog lists — each registers its own
    // descriptor. A failing file reports but does not stop the others.
    std::string first_error;
    auto run_files = [&](const std::vector<FxDesc>& list) {
        for (const auto& d : list) {
            const std::string path =
                (std::filesystem::path(catalog->fx_dir) / d.file)
                    .string();
            // A while(1){} at file scope would hang the loader — the
            // same budget mechanism that guards apply()/process().
            FxWatchdog::Arm guard(impl_->engine->isolate(),
                                  kScriptLoadBudget);
            std::string run_error;
            const bool ran =
                impl_->engine->RunFile(path, &run_error);
            guard.Disarm();
            v8::Isolate* iso = impl_->engine->isolate();
            if (iso->IsExecutionTerminating()) {
                // The file's own TryCatch already unwound the
                // termination; resume execution capability so the
                // remaining files still evaluate.
                iso->CancelTerminateExecution();
                run_error = "execution budget exceeded (terminated)";
            }
            if (!ran && first_error.empty()) {
                first_error = d.file + ": " + run_error;
            }
        }
    };
    run_files(catalog->video_fx);
    run_files(catalog->audio_fx);
    run_files(catalog->transitions);
    if (!first_error.empty() && error != nullptr) {
        *error = first_error;   // reported once; fns that loaded work
    }
    return true;
}

bool JsFxRuntime::ValidateSource(const std::string& source,
                                 std::string* error) {
    if (!EnsureEngineReady(error)) return false;
    const auto registered = [this] {
        return impl_->video_fns.size() + impl_->audio_fns.size() +
               impl_->transition_fns.size();
    };
    const std::size_t before = registered();
    bool ran = false;
    std::string run_error;
    {
        // Same budget as catalog loading — a file-scope while(1){}
        // must terminate, not hang the validator.
        FxWatchdog::Arm guard(impl_->engine->isolate(),
                              kScriptLoadBudget);
        ran = impl_->engine->RunSource(source, "fx_validate",
                                       &run_error);
        guard.Disarm();
    }
    v8::Isolate* iso = impl_->engine->isolate();
    if (iso->IsExecutionTerminating()) {
        iso->CancelTerminateExecution();
        if (error != nullptr) {
            *error = "execution budget exceeded (terminated)";
        }
        return false;
    }
    if (!ran) {
        if (error != nullptr) *error = run_error;
        return false;
    }
    if (registered() == before) {
        if (error != nullptr) {
            *error = "script registered no effect";
        }
        return false;
    }
    return true;
}

bool JsFxRuntime::HasVideoEffect(const std::string& id) const {
    return impl_->video_fns.count(id) != 0u;
}
bool JsFxRuntime::HasAudioEffect(const std::string& id) const {
    return impl_->audio_fns.count(id) != 0u;
}
bool JsFxRuntime::HasTransition(const std::string& id) const {
    return impl_->transition_fns.count(id) != 0u;
}

bool JsFxRuntime::ApplyVideo(const std::string& id, video::Frame* frame,
                             const std::vector<float>& params,
                             double time_s, const std::string& clip_id,
                             std::string* error) {
    if (frame == nullptr || !frame->IsValid() ||
        frame->Format() != video::PixelFormat::BGRA8) {
        return false;
    }
    const auto it = impl_->video_fns.find(id);
    if (it == impl_->video_fns.end()) return false;
    Impl* impl = impl_.get();
    const v8::Global<v8::Function>* fn = &it->second;
    const Catalog* catalog = CurrentCatalog();
    const FxDesc* desc =
        catalog != nullptr ? FindDesc(catalog->video_fx, id) : nullptr;
    bool ok = false;
    impl->engine->WithContext(
        [&](v8::Isolate* iso, v8::Local<v8::Context> ctx) {
            v8::TryCatch try_catch(iso);
            const v8::Local<v8::Object> frame_obj =
                v8::Object::New(iso);
            SandboxView frame_view =
                BufferView(iso, frame->Data(), frame->DataSize(),
                           false);
            frame_obj
                ->Set(ctx, v8::String::NewFromUtf8Literal(iso, "data"),
                      frame_view.view)
                .Check();
            frame_obj
                ->Set(ctx, v8::String::NewFromUtf8Literal(iso, "width"),
                      v8::Number::New(iso, frame->Width()))
                .Check();
            frame_obj
                ->Set(ctx, v8::String::NewFromUtf8Literal(iso, "height"),
                      v8::Number::New(iso, frame->Height()))
                .Check();
            frame_obj
                ->Set(ctx, v8::String::NewFromUtf8Literal(iso, "stride"),
                      v8::Number::New(iso, frame->Stride()))
                .Check();
            const v8::Local<v8::Object> ctx_obj = v8::Object::New(iso);
            ctx_obj
                ->Set(ctx, v8::String::NewFromUtf8Literal(iso, "time"),
                      v8::Number::New(iso, time_s))
                .Check();
            ctx_obj
                ->Set(ctx,
                      v8::String::NewFromUtf8Literal(iso, "clip_id"),
                      v8::String::NewFromUtf8(iso, clip_id.c_str())
                          .ToLocalChecked())
                .Check();
            const auto fn_local =
                v8::Local<v8::Function>::New(iso, *fn);
            v8::Local<v8::Value> argv[] = {
                frame_obj, ParamsObject(iso, ctx, desc, params),
                ctx_obj};
            FxWatchdog::Arm guard(iso, kVideoApplyBudget);
            v8::MaybeLocal<v8::Value> result =
                fn_local->Call(ctx, ctx->Global(), 3, argv);
            guard.Disarm();
            if (result.IsEmpty() || try_catch.HasCaught()) {
                if (try_catch.HasTerminated()) {
                    // Unwind complete — resume execution capability
                    // so later calls into this isolate still work.
                    iso->CancelTerminateExecution();
                    if (error != nullptr) {
                        *error = BudgetError(id, kVideoApplyBudget);
                    }
                } else if (error != nullptr) {
                    *error = FormatV8Error(iso, try_catch);
                }
                return false;
            }
            // Write the script-mutated pixels back into the frame.
            std::memcpy(frame->Data(), frame_view.store,
                        frame_view.bytes);
            ok = true;
            return true;
        });
    return ok;
}

bool JsFxRuntime::ProcessAudio(const std::string& id, float* interleaved,
                               int frames, int sample_rate,
                               const std::map<std::uint32_t, float>& params,
                               double time_s, const std::string& clip_id,
                               std::string* error) {
    if (interleaved == nullptr || frames <= 0) return false;
#if defined(_WIN32)
    // Subprocess first — a runaway or crashing script then kills
    // XERFxScript.exe, never the editor. kRejected is authoritative
    // (the worker loaded the same catalog); only transport failures
    // fall through to the in-process isolate below. Inside the worker
    // itself the pipe is env-disabled, so this can never recurse.
    std::string remote_error;
    switch (ProcessAudioViaWorker(id, interleaved, frames, sample_rate,
                                  params, time_s, clip_id,
                                  &remote_error)) {
        case RemoteAudio::kDone:
            return true;
        case RemoteAudio::kRejected:
            if (error != nullptr) *error = remote_error;
            return false;
        case RemoteAudio::kUnavailable:
            break;
    }
#endif
    const auto it = impl_->audio_fns.find(id);
    if (it == impl_->audio_fns.end()) return false;
    Impl* impl = impl_.get();
    const v8::Global<v8::Function>* fn = &it->second;
    const Catalog* catalog = CurrentCatalog();
    const FxDesc* desc =
        catalog != nullptr ? FindDesc(catalog->audio_fx, id) : nullptr;
    bool ok = false;
    impl->engine->WithContext(
        [&](v8::Isolate* iso, v8::Local<v8::Context> ctx) {
            v8::TryCatch try_catch(iso);
            const v8::Local<v8::Object> ctx_obj = v8::Object::New(iso);
            ctx_obj
                ->Set(ctx, v8::String::NewFromUtf8Literal(iso, "time"),
                      v8::Number::New(iso, time_s))
                .Check();
            ctx_obj
                ->Set(ctx,
                      v8::String::NewFromUtf8Literal(iso, "sample_rate"),
                      v8::Number::New(iso, sample_rate))
                .Check();
            ctx_obj
                ->Set(ctx,
                      v8::String::NewFromUtf8Literal(iso, "clip_id"),
                      v8::String::NewFromUtf8(iso, clip_id.c_str())
                          .ToLocalChecked())
                .Check();
            const auto fn_local =
                v8::Local<v8::Function>::New(iso, *fn);
            SandboxView pcm_view =
                BufferView(iso, interleaved,
                           static_cast<std::size_t>(frames) * 2u *
                               sizeof(float),
                           true);
            v8::Local<v8::Value> argv[] = {
                pcm_view.view,
                ParamsObject(iso, ctx, desc, params), ctx_obj};
            FxWatchdog::Arm guard(iso, kAudioProcessBudget);
            v8::MaybeLocal<v8::Value> result =
                fn_local->Call(ctx, ctx->Global(), 3, argv);
            guard.Disarm();
            if (result.IsEmpty() || try_catch.HasCaught()) {
                if (try_catch.HasTerminated()) {
                    iso->CancelTerminateExecution();
                    if (error != nullptr) {
                        *error = BudgetError(id, kAudioProcessBudget);
                    }
                } else if (error != nullptr) {
                    *error = FormatV8Error(iso, try_catch);
                }
                return false;
            }
            std::memcpy(interleaved, pcm_view.store, pcm_view.bytes);
            ok = true;
            return true;
        });
    return ok;
}

double JsFxRuntime::TransitionWeight(const std::string& id,
                                     double progress) {
    const auto it = impl_->transition_fns.find(id);
    if (it == impl_->transition_fns.end()) {
        return std::clamp(progress, 0.0, 1.0);
    }
    Impl* impl = impl_.get();
    const v8::Global<v8::Function>* fn = &it->second;
    double w = progress;
    impl->engine->WithContext(
        [&](v8::Isolate* iso, v8::Local<v8::Context> ctx) {
            v8::TryCatch try_catch(iso);
            const v8::Local<v8::Object> ctx_obj = v8::Object::New(iso);
            const auto fn_local =
                v8::Local<v8::Function>::New(iso, *fn);
            v8::Local<v8::Value> argv[] = {
                v8::Number::New(iso, progress), ctx_obj};
            v8::Local<v8::Value> result;
            FxWatchdog::Arm guard(iso, kTransitionBudget);
            const bool called =
                fn_local->Call(ctx, ctx->Global(), 2, argv)
                    .ToLocal(&result);
            guard.Disarm();
            if (try_catch.HasTerminated()) {
                iso->CancelTerminateExecution();
                return false;
            }
            if (!called || !result->IsNumber() ||
                try_catch.HasCaught()) {
                return false;
            }
            w = result.As<v8::Number>()->Value();
            return true;
        });
    return std::clamp(w, 0.0, 1.0);
}

#else   // !XER_VE_HAS_V8 — catalog still works; runtimes stay inert.

struct JsFxRuntime::Impl {};

JsFxRuntime::JsFxRuntime() : impl_(std::make_unique<Impl>()) {}
JsFxRuntime::~JsFxRuntime() = default;
bool JsFxRuntime::EnsureEngineReady(std::string* error) {
    if (error) *error = "built without V8";
    return false;
}
bool JsFxRuntime::EnsureLoaded(std::string* error) {
    if (error) *error = "built without V8";
    return false;
}
bool JsFxRuntime::ValidateSource(const std::string&,
                                 std::string* error) {
    if (error) *error = "built without V8";
    return false;
}
bool JsFxRuntime::HasVideoEffect(const std::string&) const {
    return false;
}
bool JsFxRuntime::HasAudioEffect(const std::string&) const {
    return false;
}
bool JsFxRuntime::HasTransition(const std::string&) const {
    return false;
}
bool JsFxRuntime::ApplyVideo(const std::string&, video::Frame*,
                             const std::vector<float>&, double,
                             const std::string&, std::string*) {
    return false;
}
bool JsFxRuntime::ProcessAudio(const std::string&, float*, int, int,
                               const std::map<std::uint32_t, float>&,
                               double, const std::string&,
                               std::string*) {
    return false;
}
double JsFxRuntime::TransitionWeight(const std::string&, double progress) {
    return progress;
}

#endif  // XER_VE_HAS_V8

}  // namespace XER::VideoEditor
