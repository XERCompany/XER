#pragma once

#include <v8.h>

#include <memory>
#include <string>

#include "flow_script_env.h" 

namespace XER {
class FlowScript {
public:
    static void Init(v8::Isolate* isolate);

    static void Shutdown();

    static v8::Isolate* CreateIsolate();

    static void DisposeIsolate(v8::Isolate* isolate);

    explicit FlowScript(std::string path);

    FlowScriptEnv* create_env();

    bool Run() const;

private:
    std::string path_;
    std::unique_ptr<FlowScriptEnv> env_;
};
} // namespace XER