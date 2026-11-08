#include "javascript/engine/runtime_context.h"

namespace XER::JavaScript::Runtime {

void RuntimeContext::Attach(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    isolate_ = isolate;
    context_.Reset(isolate_, context);
}

void RuntimeContext::Reset() {
    context_.Reset();
    isolate_ = nullptr;
}

bool RuntimeContext::IsReady() const {
    return isolate_ != nullptr && !context_.IsEmpty();
}

v8::Isolate* RuntimeContext::isolate() const {
    return isolate_;
}

v8::Local<v8::Context> RuntimeContext::LocalContext() const {
    if (!IsReady()) {
        return v8::Local<v8::Context>();
    }
    return context_.Get(isolate_);
}

v8::Local<v8::Value> RuntimeContext::MakeString(const std::string& value) const {
    if (!IsReady()) {
        return v8::Local<v8::Value>();
    }

    v8::Local<v8::String> out;
    if (!v8::String::NewFromUtf8(
             isolate_,
             value.c_str(),
             v8::NewStringType::kNormal,
             static_cast<int>(value.size()))
             .ToLocal(&out)) {
        return v8::Local<v8::Value>();
    }

    return out;
}

v8::Local<v8::Object> RuntimeContext::MakeEventObject(
    const std::string& topic,
    const std::string& text,
    std::uint64_t sequence) const {
    if (!IsReady()) {
        return v8::Local<v8::Object>();
    }

    v8::EscapableHandleScope scope(isolate_);
    v8::Local<v8::Context> context = LocalContext();
    if (context.IsEmpty()) {
        return scope.Escape(v8::Object::New(isolate_));
    }

    v8::Local<v8::Object> event = v8::Object::New(isolate_);
    event
        ->Set(context, MakeString("topic").As<v8::String>(), MakeString(topic))
        .FromMaybe(false);
    event
        ->Set(context, MakeString("text").As<v8::String>(), MakeString(text))
        .FromMaybe(false);
    event
        ->Set(
            context,
            MakeString("sequence").As<v8::String>(),
            v8::Number::New(isolate_, static_cast<double>(sequence)))
        .FromMaybe(false);
    return scope.Escape(event);
}

}  // namespace XER::JavaScript::Runtime
