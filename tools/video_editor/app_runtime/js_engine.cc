// Embedded V8 engine for the app-runtime assets layer — implements the
// contract declared in js_engine.h. One isolate + one context per engine;
// the V8 platform is initialized once per process in Create().
//
// Embedder plumbing notes:
//   - The Impl pointer is stashed in isolate embedder data slot 0 so the
//     v8::FunctionCallback trampolines (captureless lambdas defined inside
//     member functions, the only place the private nested type
//     JsEngine::Impl is nameable) can reach the engine state.
//   - A NativeFn lives in impl->native_fns; the FunctionTemplate's data is
//     a v8::External wrapping that pointer.
//   - NativeImport resolves through impl->module_factories; the factory
//     builds its object via NewObject()/DefineFunction()/SetObjectProperty()
//     and the first object created during the call is returned (and cached
//     so repeated imports get the same module object).

#include "js_engine.h"

#if defined(XER_VE_HAS_V8)
#include <libplatform/libplatform.h>
#include <v8.h>
#endif

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <utility>

namespace XER::VideoEditor::AppRuntime {
namespace {

#if defined(XER_VE_HAS_V8)

// Single handle table for BOTH function handlers and module objects —
// OutValue::Handler carries an index here regardless of the underlying
// V8 type (export_appdata encodes NewObject ids through the same field).
using HandlerTable = std::vector<v8::Global<v8::Value>>;

v8::Platform* g_platform = nullptr;  // intentionally leaks; lives for process

v8::Local<v8::String> V8Str(v8::Isolate* isolate, const std::string& s) {
    return v8::String::NewFromUtf8(isolate, s.data(),
                                   v8::NewStringType::kNormal,
                                   static_cast<int>(s.size()))
        .ToLocalChecked();
}

std::string FromV8Str(v8::Isolate* isolate, v8::Local<v8::Value> v) {
    const v8::String::Utf8Value utf8(isolate, v);
    return *utf8 ? std::string(*utf8, static_cast<size_t>(utf8.length()))
                 : std::string();
}

void Throw(v8::Isolate* isolate, const std::string& message) {
    isolate->ThrowException(V8Str(isolate, message));
}

std::string ArgToPrintable(v8::Isolate* isolate, v8::Local<v8::Context> ctx,
                           v8::Local<v8::Value> v) {
    if (v->IsString()) return FromV8Str(isolate, v);
    v8::Local<v8::String> s;
    if (v->ToDetailString(ctx).ToLocal(&s)) return FromV8Str(isolate, s);
    return std::string();
}

void ConsolePrint(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    const v8::HandleScope scope(isolate);
    const v8::Local<v8::Context> ctx = isolate->GetCurrentContext();
    std::string line;
    for (int i = 0; i < info.Length(); ++i) {
        if (i) line += ' ';
        line += ArgToPrintable(isolate, ctx, info[i]);
    }
    line += '\n';
    fputs(line.c_str(), stdout);
    fflush(stdout);
}

std::string FormatException(v8::Isolate* isolate, v8::Local<v8::Context> ctx,
                            v8::TryCatch& try_catch) {
    const v8::HandleScope scope(isolate);
    std::string out;
    const v8::Local<v8::Message> message = try_catch.Message();
    if (!message.IsEmpty()) {
        out = FromV8Str(isolate, message->Get());
        int line = 0;
        if (message->GetLineNumber(ctx).To(&line)) {
            out += " (" + FromV8Str(isolate, message->GetScriptResourceName()) +
                   ":" + std::to_string(line) + ")";
        }
    } else {
        out = FromV8Str(isolate, try_catch.Exception());
    }
    v8::Local<v8::Value> stack;
    if (try_catch.StackTrace(ctx).ToLocal(&stack) && stack->IsString()) {
        const std::string s = FromV8Str(isolate, stack);
        if (!s.empty()) {
            out += '\n';
            out += s;
        }
    }
    return out.empty() ? std::string("unknown script error") : out;
}

#endif  // XER_VE_HAS_V8

void JsonEscapeInto(std::string& out, const std::string& s) {
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x",
                             static_cast<unsigned int>(
                                 static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
}

#if defined(XER_VE_HAS_V8)

// Structured JS values (plain objects/arrays) round-trip into C++ as
// JSON text under Kind::Json — e.g. concat's {join: "/"} options object.
// `free_slots` carries indices released by FreeHandle — see Impl below;
// they are reused before the table is grown so long sessions don't
// accumulate V8 Globals without bound.
OutValue ToOutValue(v8::Isolate* isolate, v8::Local<v8::Context> ctx,
                    HandlerTable& handlers, std::vector<int>& free_slots,
                    v8::Local<v8::Value> v) {
    if (v.IsEmpty() || v->IsUndefined() || v->IsNull()) return {};
    if (v->IsString()) return StrVal(FromV8Str(isolate, v));
    if (v->IsNumber())
        return NumVal(v->NumberValue(ctx).FromMaybe(0));
    if (v->IsBoolean()) return BoolVal(v->BooleanValue(isolate));
    if (v->IsFunction()) {
        OutValue out;
        out.kind = OutValue::Kind::Handler;
        // Function-identity dedupe: re-registering the same JS function
        // (e.g. setOutAppData called every frame with the same callback)
        // reuses its slot instead of pushing a new Global per call.
        for (int i = 0; i < static_cast<int>(handlers.size()); ++i) {
            if (!handlers[static_cast<size_t>(i)].IsEmpty() &&
                handlers[static_cast<size_t>(i)]
                    .Get(isolate)
                    ->StrictEquals(v)) {
                out.handler_id = i;
                return out;
            }
        }
        if (!free_slots.empty()) {
            out.handler_id = free_slots.back();
            free_slots.pop_back();
            handlers[static_cast<size_t>(out.handler_id)].Reset(isolate, v);
        } else {
            out.handler_id = static_cast<int>(handlers.size());
            handlers.emplace_back(isolate, v);
        }
        return out;
    }
    v8::Local<v8::String> json;
    if (v8::JSON::Stringify(ctx, v).ToLocal(&json)) {
        OutValue out;
        out.kind = OutValue::Kind::Json;
        out.str = FromV8Str(isolate, json);
        return out;
    }
    return {};
}

v8::Local<v8::Value> ToV8Value(v8::Isolate* isolate,
                               v8::Local<v8::Context> ctx,
                               HandlerTable& handlers, const OutValue& v) {
    switch (v.kind) {
        case OutValue::Kind::String:
            return V8Str(isolate, v.str);
        case OutValue::Kind::Number:
            return v8::Number::New(isolate, v.num);
        case OutValue::Kind::Bool:
            return v8::Boolean::New(isolate, v.boolean);
        case OutValue::Kind::Handler:
            // The IsEmpty guard covers reclaimed slots — a stale id
            // resolves to undefined, never a Get() on an empty Global.
            if (v.handler_id >= 0 &&
                v.handler_id < static_cast<int>(handlers.size()) &&
                !handlers[static_cast<size_t>(v.handler_id)].IsEmpty())
                return handlers[v.handler_id].Get(isolate);
            return v8::Undefined(isolate);
        case OutValue::Kind::Json: {
            // A parse failure raises a pending exception — scope it to a
            // local TryCatch so it cannot poison the caller's call.
            v8::TryCatch parse_tc(isolate);
            v8::Local<v8::Value> parsed;
            if (v8::JSON::Parse(ctx, V8Str(isolate, v.str)).ToLocal(&parsed))
                return parsed;
            parse_tc.Reset();
            return v8::Undefined(isolate);
        }
        case OutValue::Kind::Undefined:
        default:
            return v8::Undefined(isolate);
    }
}

#endif  // XER_VE_HAS_V8

}  // namespace

#if defined(XER_VE_HAS_V8)

struct JsEngine::Impl {
    JsEngine* owner = nullptr;
    v8::ArrayBuffer::Allocator* allocator = nullptr;
    v8::Isolate* isolate = nullptr;
    v8::Global<v8::Context> context;
    HandlerTable handles;
    // Reclamation: slots whose Global was Reset() by FreeHandle get
    // reused by ToOutValue/NewObject before the table grows. A slot is
    // only freed when nothing in out_data still references its id —
    // the OutValue::handler_id index space is shared with module
    // object ids, so a freed id would otherwise alias a live entry.
    std::vector<int> free_handle_slots;
    // NativeImport capture: module factories run NewObject() — with
    // slot reuse the module's id is no longer "the previous
    // handles.size()", so the first NewObject during a factory call is
    // captured here instead.
    bool capture_first_handle = false;
    int first_captured_handle = -1;
    std::vector<std::unique_ptr<NativeFn>> native_fns;
    std::unordered_map<std::string, OutValue> out_data;
    std::unordered_map<std::string, ModuleFactory> module_factories;
    std::unordered_map<std::string, v8::Global<v8::Object>> module_cache;

    // Releases a handle slot for reuse. Idempotent — an already-empty
    // slot is not queued twice (a duplicate free-list entry would hand
    // the same id to two different values).
    void FreeHandle(int id) {
        if (id < 0 || id >= static_cast<int>(handles.size()) ||
            handles[static_cast<size_t>(id)].IsEmpty()) {
            return;
        }
        handles[static_cast<size_t>(id)].Reset();
        free_handle_slots.push_back(id);
    }

    // True when any out_data entry still points at `id` — a freed slot
    // would alias that entry to whatever reuses the index next.
    bool HandleReferenced(int id) const {
        for (const auto& [key, value] : out_data) {
            (void)key;
            if (value.kind == OutValue::Kind::Handler &&
                value.handler_id == id) {
                return true;
            }
        }
        return false;
    }

    ~Impl() {
        // Persistent handles die before the isolate that owns them.
        module_cache.clear();
        handles.clear();
        context.Reset();
        if (isolate) isolate->Dispose();
        delete allocator;
    }
};

std::unique_ptr<JsEngine> JsEngine::Create(std::string* error) {
    static std::once_flag init_flag;
    static std::string init_error;
    std::call_once(init_flag, [] {
        g_platform = v8::platform::NewDefaultPlatform().release();
        v8::V8::InitializePlatform(g_platform);
        // System-ICU V8 (MSYS2): InitializeICU() not
        // InitializeICUDefaultLocation — the latter aborts on nullptr
        // and isn't needed when V8 links the shared ICU.
        v8::V8::InitializeICU();
        if (!v8::V8::Initialize())
            init_error = "v8: initialization failed";
    });
    if (!init_error.empty()) {
        if (error) *error = init_error;
        return nullptr;
    }

    auto engine = std::make_unique<JsEngine>();
    engine->impl_ = std::make_unique<Impl>();
    Impl& impl = *engine->impl_;
    impl.owner = engine.get();
    impl.allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    v8::Isolate::CreateParams params;
    params.array_buffer_allocator = impl.allocator;
    impl.isolate = v8::Isolate::New(params);
    if (!impl.isolate) {
        if (error) *error = "v8: isolate creation failed";
        return nullptr;
    }
    impl.isolate->SetData(0, &impl);

    v8::Isolate::Scope isolate_scope(impl.isolate);
    v8::HandleScope handle_scope(impl.isolate);
    const v8::Local<v8::Context> ctx = v8::Context::New(impl.isolate);
    impl.context.Reset(impl.isolate, ctx);
    const v8::Context::Scope context_scope(ctx);
    const v8::Local<v8::Object> global = ctx->Global();

    const auto native_import =
        [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            const v8::HandleScope scope(iso);
            auto* impl = static_cast<Impl*>(iso->GetData(0));
            if (info.Length() < 1 || !info[0]->IsString()) {
                Throw(iso, "NativeImport: module name expected");
                return;
            }
            const std::string name = FromV8Str(iso, info[0]);
            if (const auto cached = impl->module_cache.find(name);
                cached != impl->module_cache.end()) {
                info.GetReturnValue().Set(cached->second.Get(iso));
                return;
            }
            const auto factory = impl->module_factories.find(name);
            if (factory == impl->module_factories.end()) {
                Throw(iso, "NativeImport: unknown module '" + name + "'");
                return;
            }
            const v8::Context::Scope context_scope(
                impl->context.Get(iso));
            // The module is the first object the factory NewObject()s —
            // captured through the flag (handle slots are reused, so it
            // is no longer guaranteed to land at the previous size()).
            const bool prev_capture = impl->capture_first_handle;
            const int prev_first = impl->first_captured_handle;
            impl->capture_first_handle = true;
            impl->first_captured_handle = -1;
            try {
                factory->second(*impl->owner);
            } catch (const std::exception& e) {
                impl->capture_first_handle = prev_capture;
                impl->first_captured_handle = prev_first;
                Throw(iso, "NativeImport: factory for '" + name +
                               "' threw: " + e.what());
                return;
            } catch (...) {
                impl->capture_first_handle = prev_capture;
                impl->first_captured_handle = prev_first;
                Throw(iso, "NativeImport: factory for '" + name +
                               "' threw an unknown exception");
                return;
            }
            const int module_id = impl->first_captured_handle;
            impl->capture_first_handle = prev_capture;
            impl->first_captured_handle = prev_first;
            if (module_id < 0 ||
                module_id >= static_cast<int>(impl->handles.size()) ||
                !impl->handles[static_cast<size_t>(module_id)]
                     .Get(iso)
                     ->IsObject()) {
                Throw(iso, "NativeImport: factory for '" + name +
                               "' created no object");
                return;
            }
            const v8::Local<v8::Object> module =
                impl->handles[static_cast<size_t>(module_id)]
                    .Get(iso)
                    .As<v8::Object>();
            impl->module_cache.emplace(name,
                                       v8::Global<v8::Object>(iso, module));
            // The slot only indexed the object for factory setup;
            // module_cache keeps its own Global — reclaim the index
            // unless an out-data handler aliased it mid-factory.
            if (!impl->HandleReferenced(module_id)) {
                impl->FreeHandle(module_id);
            }
            info.GetReturnValue().Set(module);
        };

    const auto console_print =
        [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            ConsolePrint(info);
        };

    bool ok = true;
    v8::Local<v8::Function> import_fn;
    ok = ok && v8::FunctionTemplate::New(impl.isolate, native_import)
                   ->GetFunction(ctx)
                   .ToLocal(&import_fn);
    ok = ok && global->Set(ctx, V8Str(impl.isolate, "NativeImport"), import_fn)
                   .FromMaybe(false);
    ok = ok && global->Set(ctx, V8Str(impl.isolate, "exports"),
                           v8::Object::New(impl.isolate))
                   .FromMaybe(false);

    const v8::Local<v8::Object> console = v8::Object::New(impl.isolate);
    const v8::Local<v8::FunctionTemplate> print_tpl =
        v8::FunctionTemplate::New(impl.isolate, console_print);
    for (const char* level : {"log", "warn"}) {
        v8::Local<v8::Function> fn;
        ok = ok && print_tpl->GetFunction(ctx).ToLocal(&fn);
        ok = ok && console->Set(ctx, V8Str(impl.isolate, level), fn)
                       .FromMaybe(false);
    }
    ok = ok && global->Set(ctx, V8Str(impl.isolate, "console"), console)
                   .FromMaybe(false);

    if (!ok) {
        if (error) *error = "v8: failed to install global bindings";
        return nullptr;
    }
    return engine;
}

JsEngine::~JsEngine() = default;

bool JsEngine::RunSource(const std::string& source, const std::string& name,
                         std::string* error) {
    v8::Isolate* iso = impl_->isolate;
    v8::Isolate::Scope isolate_scope(iso);
    v8::HandleScope handle_scope(iso);
    const v8::Local<v8::Context> ctx = impl_->context.Get(iso);
    const v8::Context::Scope context_scope(ctx);
    v8::TryCatch try_catch(iso);
    v8::ScriptOrigin origin(V8Str(iso, name));
    v8::Local<v8::Script> script;
    if (!v8::Script::Compile(ctx, V8Str(iso, source), &origin)
             .ToLocal(&script)) {
        if (error) *error = FormatException(iso, ctx, try_catch);
        return false;
    }
    if (script->Run(ctx).IsEmpty()) {
        if (error) *error = FormatException(iso, ctx, try_catch);
        return false;
    }
    return true;
}

bool JsEngine::RunFile(const std::string& path_utf8, std::string* error) {
    std::ifstream file(
        std::filesystem::path(
            std::u8string(path_utf8.begin(), path_utf8.end())),
        std::ios::binary);
    if (!file) {
        if (error) *error = "cannot open '" + path_utf8 + "'";
        return false;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    if (file.bad()) {
        if (error) *error = "error reading '" + path_utf8 + "'";
        return false;
    }
    return RunSource(contents.str(), path_utf8, error);
}

int JsEngine::NewObject() {
    v8::Isolate* iso = impl_->isolate;
    v8::Isolate::Scope isolate_scope(iso);
    v8::HandleScope handle_scope(iso);
    const v8::Local<v8::Context> ctx = impl_->context.Get(iso);
    const v8::Context::Scope context_scope(ctx);
    const v8::Local<v8::Value> obj(v8::Object::New(iso));
    int id;
    if (!impl_->free_handle_slots.empty()) {
        id = impl_->free_handle_slots.back();
        impl_->free_handle_slots.pop_back();
        impl_->handles[static_cast<size_t>(id)].Reset(iso, obj);
    } else {
        impl_->handles.emplace_back(iso, obj);
        id = static_cast<int>(impl_->handles.size()) - 1;
    }
    if (impl_->capture_first_handle) {
        impl_->first_captured_handle = id;
        impl_->capture_first_handle = false;
    }
    return id;
}

bool JsEngine::DefineFunction(int object_id, const std::string& name,
                              NativeFn fn) {
    if (object_id < 0 ||
        object_id >= static_cast<int>(impl_->handles.size()))
        return false;
    impl_->native_fns.push_back(std::make_unique<NativeFn>(std::move(fn)));

    const auto trampoline =
        [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            v8::Isolate* iso = info.GetIsolate();
            const v8::HandleScope scope(iso);
            const v8::Local<v8::Context> ctx = iso->GetCurrentContext();
            auto* impl = static_cast<Impl*>(iso->GetData(0));
            auto* native = static_cast<JsEngine::NativeFn*>(
                v8::Local<v8::External>::Cast(info.Data())->Value());
            std::vector<OutValue> args;
            args.reserve(static_cast<size_t>(info.Length()));
            for (int i = 0; i < info.Length(); ++i)
                args.push_back(ToOutValue(iso, ctx, impl->handles,
                                          impl->free_handle_slots,
                                          info[i]));
            OutValue result;
            try {
                result = (*native)(args);
            } catch (const std::exception& e) {
                Throw(iso, std::string("native callback threw: ") + e.what());
                return;
            } catch (...) {
                Throw(iso, "native callback threw an unknown exception");
                return;
            }
            info.GetReturnValue().Set(
                ToV8Value(iso, ctx, impl->handles, result));
        };

    v8::Isolate* iso = impl_->isolate;
    v8::Isolate::Scope isolate_scope(iso);
    v8::HandleScope handle_scope(iso);
    const v8::Local<v8::Context> ctx = impl_->context.Get(iso);
    const v8::Context::Scope context_scope(ctx);
    const v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(
        iso, trampoline,
        v8::External::New(iso, impl_->native_fns.back().get()));
    v8::Local<v8::Function> func;
    if (!tpl->GetFunction(ctx).ToLocal(&func)) return false;
    func->SetName(V8Str(iso, name));
    return impl_->handles[object_id]
        .Get(iso)
        .As<v8::Object>()
        ->Set(ctx, V8Str(iso, name), func)
        .FromMaybe(false);
}

bool JsEngine::SetObjectProperty(int object_id, const std::string& key,
                                 const OutValue& value) {
    if (object_id < 0 ||
        object_id >= static_cast<int>(impl_->handles.size()))
        return false;
    v8::Isolate* iso = impl_->isolate;
    v8::Isolate::Scope isolate_scope(iso);
    v8::HandleScope handle_scope(iso);
    const v8::Local<v8::Context> ctx = impl_->context.Get(iso);
    const v8::Context::Scope context_scope(ctx);
    return impl_->handles[object_id]
        .Get(iso)
        .As<v8::Object>()
        ->Set(ctx, V8Str(iso, key),
              ToV8Value(iso, ctx, impl_->handles, value))
        .FromMaybe(false);
}

bool JsEngine::RegisterModule(const std::string& name, ModuleFactory factory) {
    if (name.empty() || !factory) return false;
    impl_->module_factories[name] = std::move(factory);
    impl_->module_cache.erase(name);  // a re-registered factory must re-run
    return true;
}

void JsEngine::SetOutData(const std::string& key, const OutValue& value) {
    // Reclaim the overwritten handler's table slot once nothing else
    // references it — keeps `handles` bounded when a key is
    // re-registered repeatedly (e.g. per-frame setOutAppData).
    const auto old = impl_->out_data.find(key);
    const int old_handler =
        old != impl_->out_data.end() &&
                old->second.kind == OutValue::Kind::Handler
            ? old->second.handler_id
            : -1;
    impl_->out_data[key] = value;
    if (old_handler >= 0 && old_handler != value.handler_id &&
        !impl_->HandleReferenced(old_handler)) {
        impl_->FreeHandle(old_handler);
    }
}

OutValue JsEngine::GetOutData(const std::string& key) const {
    const auto it = impl_->out_data.find(key);
    return it != impl_->out_data.end() ? it->second : OutValue{};
}

bool JsEngine::CallHandler(const std::string& key,
                           const std::vector<OutValue>& args,
                           std::string* error) {
    const auto it = impl_->out_data.find(key);
    // Handlers are optional: an absent or undefined key is a no-op.
    if (it == impl_->out_data.end() ||
        it->second.kind == OutValue::Kind::Undefined)
        return true;
    if (it->second.kind != OutValue::Kind::Handler ||
        it->second.handler_id < 0 ||
        it->second.handler_id >= static_cast<int>(impl_->handles.size())) {
        if (error) *error = "CallHandler: '" + key + "' is not a handler";
        return false;
    }

    v8::Isolate* iso = impl_->isolate;
    v8::Isolate::Scope isolate_scope(iso);
    v8::HandleScope handle_scope(iso);
    const v8::Local<v8::Context> ctx = impl_->context.Get(iso);
    const v8::Context::Scope context_scope(ctx);

    std::vector<v8::Local<v8::Value>> argv;
    argv.reserve(args.size());
    for (const OutValue& arg : args)
        argv.push_back(ToV8Value(iso, ctx, impl_->handles, arg));

    v8::TryCatch try_catch(iso);
    // Handler ids and object ids share the handle table — the slot may
    // hold a non-function (or a reclaimed/empty Global). An unchecked
    // As<Function> would hit a V8 CHECK and abort; validate instead.
    const v8::Local<v8::Value> fn_value =
        impl_->handles[it->second.handler_id].Get(iso);
    if (fn_value.IsEmpty() || !fn_value->IsFunction()) {
        if (error) {
            *error = "CallHandler: '" + key +
                     "' does not reference a function";
        }
        return false;
    }
    const v8::Local<v8::Function> fn = fn_value.As<v8::Function>();
    if (fn->Call(ctx, ctx->Global(), static_cast<int>(argv.size()),
                 argv.data())
            .IsEmpty()) {
        if (error) *error = FormatException(iso, ctx, try_catch);
        return false;
    }
    return true;
}

v8::Isolate* JsEngine::isolate() const { return impl_->isolate; }

bool JsEngine::WithContext(const std::function<bool(
                               v8::Isolate*, v8::Local<v8::Context>)>& fn) {
    if (impl_->isolate == nullptr || fn == nullptr) {
        return false;
    }
    v8::Isolate::Scope isolate_scope(impl_->isolate);
    v8::HandleScope handle_scope(impl_->isolate);
    const v8::Local<v8::Context> ctx = impl_->context.Get(impl_->isolate);
    const v8::Context::Scope context_scope(ctx);
    return fn(impl_->isolate, ctx);
}

#else   // !XER_VE_HAS_V8 — no system V8: inert stubs, Boot() keeps
        // compiled-in defaults (the js engine error is non-fatal).

struct JsEngine::Impl {
    std::unordered_map<std::string, OutValue> out_data;
};

std::unique_ptr<JsEngine> JsEngine::Create(std::string* error) {
    if (error) *error = "built without V8 (XER_VE_HAS_V8 undefined)";
    return nullptr;
}

JsEngine::~JsEngine() = default;

bool JsEngine::RunSource(const std::string&, const std::string&,
                         std::string* error) {
    if (error) *error = "built without V8";
    return false;
}

bool JsEngine::RunFile(const std::string&, std::string* error) {
    if (error) *error = "built without V8";
    return false;
}

int JsEngine::NewObject() { return -1; }

bool JsEngine::DefineFunction(int, const std::string&, NativeFn) {
    return false;
}

bool JsEngine::SetObjectProperty(int, const std::string&, const OutValue&) {
    return false;
}

bool JsEngine::RegisterModule(const std::string&, ModuleFactory) {
    return false;
}

void JsEngine::SetOutData(const std::string& key, const OutValue& value) {
    impl_->out_data[key] = value;
}

OutValue JsEngine::GetOutData(const std::string& key) const {
    const auto it = impl_->out_data.find(key);
    return it != impl_->out_data.end() ? it->second : OutValue{};
}

bool JsEngine::CallHandler(const std::string& key,
                           const std::vector<OutValue>&,
                           std::string* error) {
    const auto it = impl_->out_data.find(key);
    // Absent handlers stay no-ops, same contract as the V8 path.
    if (it == impl_->out_data.end() ||
        it->second.kind == OutValue::Kind::Undefined)
        return true;
    if (error) *error = "CallHandler: '" + key + "' needs V8";
    return false;
}

v8::Isolate* JsEngine::isolate() const { return nullptr; }

bool JsEngine::WithContext(const std::function<bool(
                               v8::Isolate*, v8::Local<v8::Context>)>&) {
    return false;
}

#endif  // XER_VE_HAS_V8

OutValue JsEngine::MakeObjectArg(
    const std::vector<std::pair<std::string, std::string>>& fields) {
    std::string json = "{";
    bool first = true;
    for (const auto& [key, value] : fields) {
        if (!first) json += ',';
        first = false;
        json += '"';
        JsonEscapeInto(json, key);
        json += "\":\"";
        JsonEscapeInto(json, value);
        json += '"';
    }
    json += '}';
    OutValue out;
    out.kind = OutValue::Kind::Json;
    out.str = std::move(json);
    return out;
}

OutValue StrVal(std::string s) {
    OutValue v;
    v.kind = OutValue::Kind::String;
    v.str = std::move(s);
    return v;
}

OutValue NumVal(double n) {
    OutValue v;
    v.kind = OutValue::Kind::Number;
    v.num = n;
    return v;
}

OutValue BoolVal(bool b) {
    OutValue v;
    v.kind = OutValue::Kind::Bool;
    v.boolean = b;
    return v;
}

OutValue UndefVal() { return {}; }

}  // namespace XER::VideoEditor::AppRuntime
