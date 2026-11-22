#include "flow_script_import_module.h"

#include <string>

// TODO(module-registry): src/modules/module_registry.h does not exist —
// the whole module layer (a name -> factory table that installs each
// builtin module's V8 object into the context, plus
// modules::ImportModule / modules::ResolveCanonicalModuleName) is
// missing. Until it is built, ImportModule() throws "unavailable in
// this build" and every demo_app JS demo that calls ImportModule("...")
// cannot run (nn-backend-stack.md dead-path item 8). The seam below is
// already wired to pick the header up automatically once it lands.
#if defined(__has_include)
#if __has_include("modules/module_registry.h")
#include "modules/module_registry.h"
#define ENGINE_FLOWSCRIPT_HAS_MODULE_REGISTRY 1
#else
#define ENGINE_FLOWSCRIPT_HAS_MODULE_REGISTRY 0
#endif
#else
#define ENGINE_FLOWSCRIPT_HAS_MODULE_REGISTRY 0
#endif

namespace XER {
namespace flow_script_detail {

void ImportModuleCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);

    if (args.Length() < 1 || !args[0]->IsString()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "ImportModule expects module name string")));
        return;
    }

    v8::String::Utf8Value module_name_utf8(isolate, args[0]);
    if (*module_name_utf8 == nullptr) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Module name is not valid UTF-8")));
        return;
    }

#if ENGINE_FLOWSCRIPT_HAS_MODULE_REGISTRY
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::string error;
    if (!modules::ImportModule(isolate, context, *module_name_utf8, &error)) {
        if (error.empty()) {
            error = "Failed to import module";
        }
        isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8(isolate, error.c_str()).ToLocalChecked()));
        return;
    }

    std::string canonical_name = modules::ResolveCanonicalModuleName(*module_name_utf8);
    if (canonical_name.empty()) {
        canonical_name = *module_name_utf8;
    }

    v8::Local<v8::Value> module_value;
    if (!context->Global()
             ->Get(context, v8::String::NewFromUtf8(isolate, canonical_name.c_str()).ToLocalChecked())
             .ToLocal(&module_value)) {
        isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8Literal(isolate, "Imported module could not be resolved")));
        return;
    }

    args.GetReturnValue().Set(module_value);
#else
    isolate->ThrowException(v8::Exception::Error(
        v8::String::NewFromUtf8Literal(isolate, "ImportModule is unavailable in this build")));
#endif
}

}  // namespace flow_script_detail
}  // namespace XER
