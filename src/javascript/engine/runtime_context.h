#pragma once

#include <v8.h>

#include <cstdint>
#include <string>

namespace XER::JavaScript::Runtime {

class RuntimeContext {
public:
    RuntimeContext() = default;
    ~RuntimeContext() = default;

    RuntimeContext(const RuntimeContext&) = delete;
    RuntimeContext& operator=(const RuntimeContext&) = delete;

    void Attach(v8::Isolate* isolate, v8::Local<v8::Context> context);
    void Reset();
    bool IsReady() const;

    v8::Isolate* isolate() const;
    v8::Local<v8::Context> LocalContext() const;

    v8::Local<v8::Value> MakeString(const std::string& value) const;
    v8::Local<v8::Object> MakeEventObject(
        const std::string& topic,
        const std::string& text,
        std::uint64_t sequence) const;

private:
    v8::Isolate* isolate_ = nullptr;
    v8::Global<v8::Context> context_;
};

}  // namespace XER::JavaScript::Runtime
