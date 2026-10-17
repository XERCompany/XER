#include "flow_script.h"

#include <libplatform/libplatform.h>

#include <cstdlib>
#include <mutex>
#include <utility>

#include <absl/strings/str_cat.h>

#include "error_handler/err_capture.h"
#include "flow_script_buffer.h"
#include "engine_params.h"
#include "flow_script_console.h"
#include "flow_script_event_bus.h"
#include "flow_script_event_bus_static.h"
#include "flow_script_import_module.h"
#include "flow_script_require.h"
#include "flux/terminal/terminal_output_renderer.h"
#include "resource_guard.h"
#include "runtime_safety/safe_sandbox.h"
#include "v8/v8_engine.h"
#include "v8/v8_initializer.h"


namespace {

void WriteFlowScriptLine(flux::terminal::OutputStream stream, const std::string& text) {
    flux::terminal::WriteLine(stream, text);
}
 
}  // namespace

void FlowScript::Init(v8::Isolate* isolate) {
    (void)isolate;
    const EngineParams params = EngineParamsFromEnv();
    Engine::V8Runtime::EnsureInitialized(params.v8_platform_workers);  
}

void FlowScript::Shutdown() {
    Engine::V8Runtime::Shutdown();
}

v8::Isolate* FlowScript::CreateIsolate() {
    const EngineParams params = EngineParamsFromEnv();
    std::string warning;
    v8::Isolate* isolate = Engine::V8Runtime::CreateManagedIsolate(params, &warning);
    if (!warning.empty()) {
        WriteFlowScriptLine(flux::terminal::OutputStream::kStderr,
            absl::StrCat("[flow_script] ", warning));
    }
    return isolate;
}

void FlowScript::DisposeIsolate(v8::Isolate* isolate) {
    if (isolate == nullptr) {
        return;
    }

    Engine::ErrorHandler::BindIsolate(nullptr);
    Engine::V8Runtime::DisposeManagedIsolate(isolate);
}

FlowScript::FlowScript(std::string path) : path_(std::move(path)), env_(nullptr) {}

FlowScriptEnv* FlowScript::create_env() {
    if (!env_) {
        auto data = std::make_unique<FlowScriptEnvData>();
        data->env["script_path"] = path_;
        env_ = std::make_unique<FlowScriptEnv>(data.get());
    }

    return env_.get();
}

bool FlowScript::Run() const {
    v8::Isolate* isolate = CreateIsolate();
    if (isolate == nullptr) {
        WriteFlowScriptLine(flux::terminal::OutputStream::kStderr, "Failed to create V8 isolate");
        return false;
    }
    Engine::ErrorHandler::BindIsolate(isolate);

    const EngineParams params = EngineParamsFromEnv();
    const Engine::RuntimeSafety::SandboxSettings sandbox_settings = Engine::RuntimeSafety::ResolveSandboxSettings(params);

    if (params.is_verbose()) {
        WriteFlowScriptLine(flux::terminal::OutputStream::kStderr, absl::StrCat("[flow_script] run: ", path_));
        if (sandbox_settings.enabled) {
            WriteFlowScriptLine(flux::terminal::OutputStream::kStderr,
                absl::StrCat("[flow_script]   ", Engine::RuntimeSafety::BuildSandboxSummary(sandbox_settings)));
        }
        if (params.max_memory_used > 0) {
            WriteFlowScriptLine(flux::terminal::OutputStream::kStderr,
                absl::StrCat("[flow_script]   max_memory_used=", params.max_memory_used, " MiB"));
        }
        if (params.noemit) {
            WriteFlowScriptLine(flux::terminal::OutputStream::kStderr, "[flow_script]   noemit (dry-run)");
        }
    }

    if (!flow_script_detail::StaticEvent::state) {
        flow_script_detail::StaticEvent::state = std::make_unique<flow_script_detail::JsEventBusState>();
    }

    bool success = false;
    {
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = v8::Context::New(isolate);
        v8::Context::Scope context_scope(context); 
        flow_script_detail::StaticEvent::state->runtime.Attach(isolate, context);

        do {
            // In sandbox mode, skip ImportModule binding (no dynamic module loading).
            if (sandbox_settings.allow_dynamic_modules) {
                bool import_bound = context->Global()
                                        ->Set(
                                            context,
                                            v8::String::NewFromUtf8Literal(isolate, "ImportModule"),
                                            v8::Function::New(context, flow_script_detail::ImportModuleCallback)
                                                .ToLocalChecked())
                                        .FromMaybe(false);
                if (!import_bound) {
					WriteFlowScriptLine(flux::terminal::OutputStream::kStderr, "Failed to bind ImportModule");
                    break;
                }
            } else if (params.is_verbose()) {
				WriteFlowScriptLine(flux::terminal::OutputStream::kStderr,
					"[flow_script]   sandbox: ImportModule binding skipped");
            }

            if (!flow_script_detail::BindConsoleGlobals(isolate, context)) {
				WriteFlowScriptLine(flux::terminal::OutputStream::kStderr, "Failed to bind console");
                break;
            }

            if (!flow_script_detail::BindBuffer(isolate, context)) {
				WriteFlowScriptLine(flux::terminal::OutputStream::kStderr, "Failed to bind Buffer");
				break;
			}

            if (!flow_script_detail::BindEventBus(isolate, context, flow_script_detail::StaticEvent::state.get())) {
				WriteFlowScriptLine(flux::terminal::OutputStream::kStderr, "Failed to bind EventBus");
                break;
            }

            flow_script_detail::FlowScriptRequireRuntime require_runtime(
                isolate, context, path_, sandbox_settings.enabled);
            if (!require_runtime.BindGlobals()) {
				WriteFlowScriptLine(flux::terminal::OutputStream::kStderr,
					"Failed to bind RequireFile/MakeExportModule APIs");
                break;
            }

            std::string run_error;
            // Start resource monitor: enforces memory hard limit and timeout.
            ResourceGuard guard(isolate, ResourceGuardOptionsFromEnv());
            guard.Start();

            const bool run_ok = require_runtime.RunEntry(&run_error);

            guard.Stop();

            if (!run_ok) {
                if (guard.was_killed() && !guard.kill_reason().empty()) {
                    WriteFlowScriptLine(flux::terminal::OutputStream::kStderr,
                        absl::StrCat("[resource_guard] script aborted: ", guard.kill_reason()));
                } else {
                    WriteFlowScriptLine(flux::terminal::OutputStream::kStderr, run_error);
                }
                break;
            }

            success = true;
        } while (false);

        flow_script_detail::StaticEvent::state->runtime.Reset();
    }

    DisposeIsolate(isolate);
    return success;
}
