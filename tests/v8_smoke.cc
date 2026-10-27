#include <libplatform/libplatform.h>
#include <v8.h>

#include <iostream>
#include <memory>

int main(int argc, char** argv) {
    if (!v8::V8::InitializeICUDefaultLocation(argc ? argv[0] : nullptr)) return 1;
    auto platform = v8::platform::NewDefaultPlatform();
    v8::V8::InitializePlatform(platform.get());
    if (!v8::V8::Initialize()) return 2;
    auto allocator = std::unique_ptr<v8::ArrayBuffer::Allocator>(
        v8::ArrayBuffer::Allocator::NewDefaultAllocator());
    v8::Isolate::CreateParams params;
    params.array_buffer_allocator = allocator.get();
    auto* isolate = v8::Isolate::New(params);
    if (!isolate) return 3;
    int result = 4;
    {
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handles(isolate);
        auto context = v8::Context::New(isolate);
        v8::Context::Scope context_scope(context);
        v8::TryCatch errors(isolate);
        auto source = v8::String::NewFromUtf8Literal(
            isolate, "21 * 2 === 42 && new Intl.NumberFormat('en-US').format(1234) === '1,234'");
        v8::Local<v8::Script> script;
        v8::Local<v8::Value> value;
        if (v8::Script::Compile(context, source).ToLocal(&script) &&
            script->Run(context).ToLocal(&value) && value->IsTrue()) {
            result = 0;
        } else if (errors.HasCaught()) {
            v8::String::Utf8Value message(isolate, errors.Exception());
            std::cerr << (*message ? *message : "V8 exception") << '\n';
        }
    }
    isolate->Dispose();
    v8::V8::Dispose();
    v8::V8::DisposePlatform();
    return result;
}
