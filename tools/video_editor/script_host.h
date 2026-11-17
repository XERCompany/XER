// Community Scripts: user-supplied .js files under assets/scripts/,
// described by a scripts.json manifest and executed inside the app's
// embedded V8 runtime (app_runtime/JsEngine).
//
//   ScriptHost::Shared().LoadManifest("<assets>/scripts")
//       → Entries() lists the manifest for a Scripts menu / shortcuts.
//   Run("id")
//       → reads entry.file and executes it via JsEngine::RunFile.
//
// Inside a script, NativeImport("xer_editor") exposes the app surface:
//   call(action, argJson) → JSON string produced by the app's ActionFn
//   projectInfo()         → shorthand for call("project_info", "{}")
//   setStatus(text)       → routes {"text": ...} as the "status" action
//   log(message)          → appended to RunResult::output (the engine's
//                           console.log goes to stdout only, so this is
//                           the capture channel)
//
// Engine selection: Run() prefers AppRuntime::SharedBootEngine() — the
// engine Boot() created for runtime.js, i.e. the same JS context the
// rest of the app uses. When no boot engine exists (headless tools,
// unit tests) a private JsEngine is created lazily and the
// app_super_* bootstrap modules (native_modules.h RegisterAll) are
// installed on it too, so scripts see the full bootstrap surface.
//
// Module registration: JsEngine::RegisterModule is an open registry —
// the host installs "xer_editor" itself on first run per engine, so
// app_runtime/native_modules.cc needs no changes.
//
// THREADING: everything here runs on the UI thread. Run() is
// synchronous — user-invoked scripts block the UI until they return —
// matching the JsEngine contract (not thread-safe, UI thread only).
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app_runtime/js_engine.h"

namespace XER::VideoEditor {

class ScriptHost {
public:
    static ScriptHost& Shared();

    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    // One entry of scripts.json. `id` is the key Run() looks up;
    // `file` is a path relative to the scripts dir (validated: never
    // absolute, never containing a ".." component).
    struct ScriptEntry {
        std::string id;
        std::string title;        // menu label
        std::string description;
        std::string file;
        std::string category;     // optional grouping for the picker
        std::string shortcut;     // optional accelerator text
    };

    // App-side dispatcher behind xer_editor.call(action, argJson).
    // Receives the action name and a JSON argument string; must return
    // a JSON string (convention: {"ok": bool, ...}). Installed once by
    // the app at startup; scripts get an error JSON back while unset.
    using ActionFn = std::function<std::string(
        const std::string& action, const std::string& argJson)>;
    void SetActionFn(ActionFn fn);

    struct RunResult {
        bool ok = false;
        std::string output;  // text captured via xer_editor.log()
        std::string error;   // compile/runtime error text when !ok
    };

    // Parses <scripts_dir>/scripts.json — a JSON array of entry
    // objects. Soft-fail: a missing or malformed manifest returns
    // false with an explanatory `error` and leaves Entries() empty;
    // individual entries that fail validation (empty id/file,
    // unsafe file path, duplicate id) are skipped, never fatal.
    bool LoadManifest(const std::string& scripts_dir,
                      std::string* error);

    const std::vector<ScriptEntry>& Entries() const { return entries_; }
    const std::string& ScriptsDir() const { return scripts_dir_; }

    // scripts_dir + entry.file, UTF-8 with forward slashes.
    std::string ResolvePath(const ScriptEntry& entry) const;

    // Finds `id` in Entries(), resolves its file inside the scripts
    // dir and executes it in the engine. Synchronous, UI thread only.
    RunResult Run(const std::string& id);

    // The shared boot engine when present, else a lazily created
    // private engine carrying the app_super_* bootstrap modules.
    // Public so sibling hosts (extensions/ext_host) can run their
    // entry points in the same JS context as community scripts.
    AppRuntime::JsEngine* Engine(std::string* error);

private:
    ScriptHost() = default;

    // Installs the "xer_editor" module factory once per engine.
    void EnsureEditorModule(AppRuntime::JsEngine& engine);

    // ModuleFactory body — builds the NativeImport("xer_editor")
    // object: call / projectInfo / setStatus / log.
    void BuildEditorModule(AppRuntime::JsEngine& engine);

    // Routes an action through action_fn_ with exception guards and
    // a degraded error JSON when no handler is installed.
    std::string Dispatch(const std::string& action,
                         const std::string& arg_json);
    AppRuntime::OutValue EditorCall(
        const std::vector<AppRuntime::OutValue>& args);

    std::string scripts_dir_;  // UTF-8, as passed to LoadManifest
    std::vector<ScriptEntry> entries_;
    ActionFn action_fn_;
    std::unique_ptr<AppRuntime::JsEngine> owned_engine_;
    AppRuntime::JsEngine* module_engine_ = nullptr;  // xer_editor host
    std::string run_output_;  // xer_editor.log() sink, reset per Run()
};

}  // namespace XER::VideoEditor
