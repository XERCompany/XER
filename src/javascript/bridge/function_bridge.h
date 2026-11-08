#pragma once

#include "javascript/engine/runtime_context.h"

#include <v8.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace XER::JavaScript::Bridge {

class FunctionBridge {
public:
    explicit FunctionBridge(XER::JavaScript::Runtime::RuntimeContext* runtime);
    ~FunctionBridge();

    FunctionBridge(const FunctionBridge&) = delete;
    FunctionBridge& operator=(const FunctionBridge&) = delete;

    bool Register(std::string_view handler_name, v8::Local<v8::Function> fn);
    bool Unregister(std::string_view handler_name);
    bool Invoke(std::string_view handler_name, const std::string& topic, const std::string& text, std::uint64_t sequence);

private:
    XER::JavaScript::Runtime::RuntimeContext* runtime_;
    std::unordered_map<std::string, v8::Global<v8::Function>> handlers_;
};

}  // namespace XER::JavaScript::Bridge
