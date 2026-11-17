#include "flow_script_require.h"

#include "flow_script_require_support.h"


namespace XER {
namespace flow_script_detail {

using require_support::ToUtf8;

bool FlowScriptRequireRuntime::BindGlobals() {
    auto context = context_.Get(isolate_);

    auto bind = [&](const char* name,
                    v8::FunctionCallback callback) -> bool {
        v8::Local<v8::External> data = v8::External::New(isolate_, this);
        v8::Local<v8::Function> fn;
        if (!v8::Function::New(context, callback, data).ToLocal(&fn)) {
            return false;
        }
        return context->Global()
            ->Set(context,
                  v8::String::NewFromUtf8(isolate_, name).ToLocalChecked(),
                  fn)
            .FromMaybe(false);
    };

    bool ok = true;
    ok = ok && bind("RequireFile", &FlowScriptRequireRuntime::RequireFileCallback);
    ok = ok && bind("MakeExportModule", &FlowScriptRequireRuntime::MakeExportModuleCallback);
    ok = ok && bind("MakeExportModuleAllGlobal", &FlowScriptRequireRuntime::MakeExportModuleAllGlobalCallback);
    ok = ok && bind("MakeExportModuleNowFunction", &FlowScriptRequireRuntime::MakeExportModuleNowFunctionCallback);
    ok = ok && bind("MakeExportModuleAsAsync", &FlowScriptRequireRuntime::MakeExportModuleAsAsyncCallback);
    return ok;
}

FlowScriptRequireRuntime* FlowScriptRequireRuntime::FromArgs(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
    if (args.Data().IsEmpty() || !args.Data()->IsExternal()) {
        return nullptr;
    }
    return static_cast<FlowScriptRequireRuntime*>(
        v8::Local<v8::External>::Cast(args.Data())->Value());
}

void FlowScriptRequireRuntime::RequireFileCallback(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto* runtime = FromArgs(args);
    if (runtime == nullptr) {
        return;
    }

    if (args.Length() < 1 || !args[0]->IsString()) {
        runtime->isolate_->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(runtime->isolate_, "RequireFile expects file path string")));
        return;
    }

    std::string request = ToUtf8(runtime->isolate_, args[0]);
    std::string error;
    v8::Local<v8::Value> export_value;
    if (!runtime->Require(request, &export_value, &error)) {
        if (error.empty()) {
            error = "RequireFile failed";
        }
        runtime->isolate_->ThrowException(
            v8::Exception::Error(v8::String::NewFromUtf8(runtime->isolate_, error.c_str()).ToLocalChecked()));
        return;
    }

    args.GetReturnValue().Set(export_value);
}

void FlowScriptRequireRuntime::MakeExportModuleCallback(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto* runtime = FromArgs(args);
    if (runtime == nullptr || args.Length() < 1) {
        return;
    }

    auto context = runtime->context_.Get(runtime->isolate_);
    v8::Context::Scope context_scope(context);

    v8::Local<v8::Value> value = args[0];
    runtime->SetCurrentExport(value);

    if (value->IsFunction()) {
        auto fn = value.As<v8::Function>();
        v8::Local<v8::Value> fn_name_value = fn->GetName();
        if (fn_name_value->IsString()) {
            runtime->AssignNamedGlobal(fn_name_value.As<v8::String>(), value);
        }
    }

    args.GetReturnValue().Set(value);
}

void FlowScriptRequireRuntime::MakeExportModuleAllGlobalCallback(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto* runtime = FromArgs(args);
    if (runtime == nullptr || args.Length() < 1 || !args[0]->IsObject()) {
        return;
    }

    auto context = runtime->context_.Get(runtime->isolate_);
    v8::Context::Scope context_scope(context);

    v8::Local<v8::Object> payload = args[0].As<v8::Object>();
    runtime->SetCurrentExport(payload);

    v8::Local<v8::Value> global_this_value;
    if (payload
            ->Get(context, v8::String::NewFromUtf8Literal(runtime->isolate_, "global_this"))
            .ToLocal(&global_this_value)
        && global_this_value->IsObject()) {
        runtime->AssignObjectToGlobal(global_this_value.As<v8::Object>());
    }

    args.GetReturnValue().Set(payload);
}

void FlowScriptRequireRuntime::MakeExportModuleNowFunctionCallback(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto* runtime = FromArgs(args);
    if (runtime == nullptr || args.Length() < 1) {
        return;
    }

    runtime->SetCurrentExport(args[0]);
    args.GetReturnValue().Set(args[0]);
}

void FlowScriptRequireRuntime::MakeExportModuleAsAsyncCallback(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto* runtime = FromArgs(args);
    if (runtime == nullptr || args.Length() < 1) {
        return;
    }

    auto context = runtime->context_.Get(runtime->isolate_);
    v8::Context::Scope context_scope(context);

    v8::Local<v8::Value> value = args[0];
    runtime->SetCurrentExport(value);

    if (args.Length() >= 2 && args[1]->IsString()) {
        runtime->AssignNamedGlobal(args[1].As<v8::String>(), value);
    }

    args.GetReturnValue().Set(value);
}

}  // namespace flow_script_detail
}  // namespace XER
