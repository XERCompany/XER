#pragma once

#include <v8.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace flow_script_detail {

class FlowScriptRequireRuntime {
public:
    FlowScriptRequireRuntime(v8::Isolate* isolate,
                             v8::Local<v8::Context> context,
                             std::string entry_script_path,
                             bool sandboxed = false);

    bool BindGlobals();
    bool RunEntry(std::string* error_out);

private:
    struct ScriptFrame {
        std::filesystem::path path;
        bool has_export = false;
        v8::Global<v8::Value> export_value;
    };

    static FlowScriptRequireRuntime* FromArgs(const v8::FunctionCallbackInfo<v8::Value>& args);
    static void RequireFileCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
    static void MakeExportModuleCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
    static void MakeExportModuleAllGlobalCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
    static void MakeExportModuleNowFunctionCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
    static void MakeExportModuleAsAsyncCallback(const v8::FunctionCallbackInfo<v8::Value>& args);

    bool Require(const std::string& request,
                 v8::Local<v8::Value>* export_value,
                 std::string* error_out);
    bool ExecuteScriptFile(const std::filesystem::path& path,
                           const std::string& source_kind,
                           const std::string& compiler_used,
                           v8::Local<v8::Value>* export_value,
                           std::string* error_out);

    void SetCurrentExport(v8::Local<v8::Value> value);
    bool AssignNamedGlobal(v8::Local<v8::String> name, v8::Local<v8::Value> value);
    bool AssignObjectToGlobal(v8::Local<v8::Object> object);
    std::filesystem::path ResolveRequestPath(const std::string& request) const;
    bool IsWithinSandboxRoot(const std::filesystem::path& path) const;

    v8::Isolate* isolate_;
    v8::Global<v8::Context> context_;
    std::filesystem::path entry_script_path_;
    std::filesystem::path sandbox_root_;
    bool sandboxed_;
    std::vector<ScriptFrame> stack_;
    std::unordered_map<std::string, v8::Global<v8::Value>> module_cache_;
    std::unordered_set<std::string> loading_modules_;
};

}  // namespace flow_script_detail
