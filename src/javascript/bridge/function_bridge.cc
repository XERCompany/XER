#include "javascript/bridge/function_bridge.h"

#include "javascript/common/text_normalizer.h"

namespace XER::JavaScript::Bridge {

FunctionBridge::FunctionBridge(XER::JavaScript::Runtime::RuntimeContext* runtime)
    : runtime_(runtime) {}

FunctionBridge::~FunctionBridge() {
    for (auto& [_, fn] : handlers_) {
        fn.Reset();
    }
}

bool FunctionBridge::Register(std::string_view handler_name, v8::Local<v8::Function> fn) {
    if (runtime_ == nullptr || !runtime_->IsReady() || fn.IsEmpty()) {
        return false;
    }

    const std::string key = XER::JavaScript::Common::TextNormalizer::NormalizeTopic(std::string(handler_name));
    auto it = handlers_.find(key);
    if (it != handlers_.end()) {
        it->second.Reset();
        it->second.Reset(runtime_->isolate(), fn);
        return true;
    }

    handlers_[key].Reset(runtime_->isolate(), fn);
    return true;
}

bool FunctionBridge::Unregister(std::string_view handler_name) {
    const std::string key = XER::JavaScript::Common::TextNormalizer::NormalizeTopic(std::string(handler_name));
    auto it = handlers_.find(key);
    if (it == handlers_.end()) {
        return false;
    }

    it->second.Reset();
    handlers_.erase(it);
    return true;
}

bool FunctionBridge::Invoke(
    std::string_view handler_name,
    const std::string& topic,
    const std::string& text,
    std::uint64_t sequence) {
    if (runtime_ == nullptr || !runtime_->IsReady()) {
        return false;
    }

    const std::string key = XER::JavaScript::Common::TextNormalizer::NormalizeTopic(std::string(handler_name));
    auto it = handlers_.find(key);
    if (it == handlers_.end()) {
        return false;
    }

    v8::Isolate* isolate = runtime_->isolate();
    v8::HandleScope handle_scope(isolate);

    v8::Local<v8::Context> context = runtime_->LocalContext();
    if (context.IsEmpty()) {
        return false;
    }

    v8::Context::Scope context_scope(context);
    v8::Local<v8::Function> fn = it->second.Get(isolate);
    v8::Local<v8::Value> argv[] = {runtime_->MakeEventObject(topic, text, sequence)};
    if (argv[0].IsEmpty()) {
        return false;
    }

    v8::TryCatch try_catch(isolate);
    v8::MaybeLocal<v8::Value> result = fn->Call(context, context->Global(), 1, argv);
    return !result.IsEmpty() && !try_catch.HasCaught();
}

}  // namespace XER::JavaScript::Bridge
