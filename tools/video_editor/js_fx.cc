// js_fx.cc — see js_fx.h for the contract.
#include "js_fx.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include <json/json.h>

#include "app_runtime/js_engine.h"
#include "frame.h"

#if defined(XER_VE_HAS_V8)
#include <libplatform/libplatform.h>
#include <v8.h>
#endif

namespace XER::VideoEditor {
namespace {

// ---- process-wide catalog (JSON only — no V8) ----------------------

std::string g_fx_dir;
std::vector<JsFxRuntime::FxDesc> g_video_fx;
std::vector<JsFxRuntime::FxDesc> g_audio_fx;
std::vector<JsFxRuntime::FxDesc> g_transitions;
bool g_catalog_loaded = false;

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

}  // namespace

bool JsFxRuntime::LoadCatalog(const std::string& fx_dir,
                              std::string* error) {
    const std::filesystem::path manifest =
        std::filesystem::path(fx_dir) / "fx.json";
    std::ifstream in(manifest, std::ios::binary);
    if (!in) {
        if (error) *error = "fx.json not found: " + manifest.string();
        g_video_fx.clear();
        g_audio_fx.clear();
        g_transitions.clear();
        g_fx_dir = fx_dir;
        g_catalog_loaded = false;
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    Json::Value root;
    std::string errs;
    Json::CharReaderBuilder builder;
    if (!Json::parseFromStream(builder, ss, &root, &errs)) {
        if (error) *error = "fx.json parse: " + errs;
        return false;
    }
    g_fx_dir = fx_dir;
    g_video_fx.clear();
    g_audio_fx.clear();
    g_transitions.clear();
    ParseDescList(root["video"], &g_video_fx);
    ParseDescList(root["audio"], &g_audio_fx);
    ParseDescList(root["transitions"], &g_transitions);
    g_catalog_loaded = true;
    return true;
}

const std::vector<JsFxRuntime::FxDesc>& JsFxRuntime::VideoEffects() {
    return g_video_fx;
}
const std::vector<JsFxRuntime::FxDesc>& JsFxRuntime::AudioEffects() {
    return g_audio_fx;
}
const std::vector<JsFxRuntime::FxDesc>& JsFxRuntime::ScriptTransitions() {
    return g_transitions;
}
const std::string& JsFxRuntime::FxDir() { return g_fx_dir; }

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
        out = V8Str(iso, msg->GetSourceLine(ctx).ToLocalChecked());
        out += " : ";
    }
    out += V8Str(iso, try_catch.Exception());
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

}  // namespace

JsFxRuntime::JsFxRuntime() : impl_(std::make_unique<Impl>()) {}
JsFxRuntime::~JsFxRuntime() = default;

bool JsFxRuntime::EnsureLoaded(std::string* error) {
    if (!impl_->engine) {
        impl_->engine = AppRuntime::JsEngine::Create(error);
        if (!impl_->engine) return false;
    }
    if (impl_->load_attempted) {
        return true;
    }
    impl_->load_attempted = true;
    if (!g_catalog_loaded) {
        if (error) *error = "js fx catalog not loaded";
        return false;
    }
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
    // Evaluate every file the catalog lists — each registers its own
    // descriptor. A failing file reports but does not stop the others.
    std::string first_error;
    auto run_files = [&](const std::vector<FxDesc>& list) {
        for (const auto& d : list) {
            const std::string path =
                (std::filesystem::path(g_fx_dir) / d.file).string();
            std::string run_error;
            if (!impl_->engine->RunFile(path, &run_error)) {
                if (first_error.empty()) {
                    first_error = d.file + ": " + run_error;
                }
            }
        }
    };
    run_files(g_video_fx);
    run_files(g_audio_fx);
    run_files(g_transitions);
    if (!first_error.empty() && error != nullptr) {
        *error = first_error;   // reported once; fns that loaded work
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
                             double time_s, std::string* error) {
    if (frame == nullptr || !frame->IsValid() ||
        frame->Format() != video::PixelFormat::BGRA8) {
        return false;
    }
    const auto it = impl_->video_fns.find(id);
    if (it == impl_->video_fns.end()) return false;
    Impl* impl = impl_.get();
    const v8::Global<v8::Function>* fn = &it->second;
    const FxDesc* desc = FindDesc(g_video_fx, id);
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
            const auto fn_local =
                v8::Local<v8::Function>::New(iso, *fn);
            v8::Local<v8::Value> argv[] = {
                frame_obj, ParamsObject(iso, ctx, desc, params),
                ctx_obj};
            v8::MaybeLocal<v8::Value> result =
                fn_local->Call(ctx, ctx->Global(), 3, argv);
            if (result.IsEmpty() || try_catch.HasCaught()) {
                if (error != nullptr) {
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
                               double time_s, std::string* error) {
    if (interleaved == nullptr || frames <= 0) return false;
    const auto it = impl_->audio_fns.find(id);
    if (it == impl_->audio_fns.end()) return false;
    Impl* impl = impl_.get();
    const v8::Global<v8::Function>* fn = &it->second;
    const FxDesc* desc = FindDesc(g_audio_fx, id);
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
            v8::MaybeLocal<v8::Value> result =
                fn_local->Call(ctx, ctx->Global(), 3, argv);
            if (result.IsEmpty() || try_catch.HasCaught()) {
                if (error != nullptr) {
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
            if (!fn_local->Call(ctx, ctx->Global(), 2, argv)
                     .ToLocal(&result) ||
                !result->IsNumber() || try_catch.HasCaught()) {
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
bool JsFxRuntime::EnsureLoaded(std::string* error) {
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
                             std::string*) {
    return false;
}
bool JsFxRuntime::ProcessAudio(const std::string&, float*, int, int,
                               const std::map<std::uint32_t, float>&,
                               double, std::string*) {
    return false;
}
double JsFxRuntime::TransitionWeight(const std::string&, double progress) {
    return progress;
}

#endif  // XER_VE_HAS_V8

}  // namespace XER::VideoEditor
