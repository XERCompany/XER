// shell_render.cc — see shell_render.h. Compiled ONLY into
// xer_browser_shell.exe: CefExecuteProcess runs this in every CEF
// subprocess (renderer/GPU/utility); the render handler injects the
// `xer` object into each new V8 context so web pages can reach the
// src/ API commands registered in the browser process.
#include "shell_render.h"

#if defined(XER_HAS_CEF)
#include <map>

#include "include/cef_browser.h"
#include "include/cef_process_message.h"
#include "include/cef_render_process_handler.h"
#include "include/cef_v8.h"

namespace XER::Cef {
namespace {

// Outstanding xer.call promises. Everything here lives on the renderer
// thread (OnContextCreated / V8 Execute / OnProcessMessageReceived all
// fire there) — no locking needed.
struct PendingCall {
    CefRefPtr<CefV8Context> context;
    CefRefPtr<CefV8Value> promise;
};
std::map<int, PendingCall> g_pending;
int g_next_call_id = 0;

void SendCallToBrowser(CefRefPtr<CefFrame> frame, int id,
                       const CefString& command,
                       const CefString& args_json) {
    CefRefPtr<CefProcessMessage> msg =
        CefProcessMessage::Create("xer_call");
    CefRefPtr<CefListValue> a = msg->GetArgumentList();
    a->SetInt(0, id);
    a->SetString(1, command);
    a->SetString(2, args_json);
    frame->SendProcessMessage(PID_BROWSER, msg);
}

// V8 handler bound to one (browser, frame, context) triple — a fresh
// instance is created per OnContextCreated so frame identity can never
// cross wires.
class XERV8Handler final : public CefV8Handler {
public:
    XERV8Handler(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 CefRefPtr<CefV8Context> context)
        : browser_(std::move(browser)),
          frame_(std::move(frame)),
          context_(std::move(context)) {}

    bool Execute(const CefString& name, CefRefPtr<CefV8Value>,
                 const CefV8ValueList& arguments,
                 CefRefPtr<CefV8Value>& retval,
                 CefString& exception) override {
        if (name == "version") {
            retval = CefV8Value::CreateString("XERBrowserShell/1.0");
            return true;
        }
        if (name == "log") {
            // Fire-and-forget: routed through xer_call with a pending
            // id that is never resolved — the reply is dropped.
            const CefString text =
                arguments.empty() ? CefString()
                                  : JsonStringify(arguments[0]);
            SendCallToBrowser(frame_, ++g_next_call_id, "xer.log",
                              text);
            return true;
        }
        if (name == "call") {
            if (arguments.empty() || !arguments[0]->IsString()) {
                exception = "xer.call(command[, args]) — command must "
                            "be a string";
                return true;
            }
            const CefString payload =
                arguments.size() > 1 ? JsonStringify(arguments[1])
                                     : CefString("{}");
            const int id = ++g_next_call_id;
            CefRefPtr<CefV8Value> promise =
                CefV8Value::CreatePromise();
            g_pending[id] = {context_, promise};
            SendCallToBrowser(frame_, id,
                              arguments[0]->GetStringValue(), payload);
            retval = promise;
            return true;
        }
        return false;
    }

private:
    // JSON.stringify(value) through the page's own JSON global — the
    // only safe way to serialize arbitrary V8 values.
    CefString JsonStringify(CefRefPtr<CefV8Value> value) {
        CefRefPtr<CefV8Value> global = context_->GetGlobal();
        CefRefPtr<CefV8Value> json = global->GetValue("JSON");
        if (json == nullptr || !json->IsObject()) {
            return "null";
        }
        CefRefPtr<CefV8Value> fn = json->GetValue("stringify");
        if (fn == nullptr || !fn->IsFunction()) {
            return "null";
        }
        CefV8ValueList a{value};
        CefRefPtr<CefV8Value> r = fn->ExecuteFunction(nullptr, a);
        return r != nullptr && r->IsString() ? r->GetStringValue()
                                            : CefString("null");
    }

    CefRefPtr<CefBrowser> browser_;
    CefRefPtr<CefFrame> frame_;
    CefRefPtr<CefV8Context> context_;

    XERV8Handler(const XERV8Handler&) = delete;
    XERV8Handler& operator=(const XERV8Handler&) = delete;

    IMPLEMENT_REFCOUNTING(XERV8Handler);
};

class XERBrowserShellApp final : public CefApp,
                                 public CefBrowserProcessHandler,
                                 public CefRenderProcessHandler {
public:
    XERBrowserShellApp() = default;

    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler()
        override {
        return this;
    }
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler()
        override {
        return this;
    }

    // ---- browser process ----
    void OnContextInitialized() override {}

    // ---- render process ----
    void OnContextCreated(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          CefRefPtr<CefV8Context> context) override {
        CefRefPtr<XERV8Handler> handler =
            new XERV8Handler(browser, frame, context);
        CefRefPtr<CefV8Value> xer =
            CefV8Value::CreateObject(nullptr, nullptr);
        xer->SetValue("call",
                      CefV8Value::CreateFunction("call", handler),
                      V8_PROPERTY_ATTRIBUTE_NONE);
        xer->SetValue("version",
                      CefV8Value::CreateFunction("version", handler),
                      V8_PROPERTY_ATTRIBUTE_NONE);
        xer->SetValue("log",
                      CefV8Value::CreateFunction("log", handler),
                      V8_PROPERTY_ATTRIBUTE_NONE);
        context->GetGlobal()->SetValue("xer", xer,
                                       V8_PROPERTY_ATTRIBUTE_NONE);
        // Convenience wrapper: xer.callJson resolves with the result
        // already JSON.parse'd (pages get objects, not strings).
        CefRefPtr<CefV8Value> eval_ret;
        CefRefPtr<CefV8Exception> eval_exc;
        context->Eval(
            "xer.callJson=function(c,a){return xer.call(c,a).then("
            "function(s){try{return JSON.parse(s)}catch(e){return "
            "{ok:false,error:'invalid result json',raw:s}}})};",
            "xer://shell", 0, eval_ret, eval_exc);
    }

    void OnContextReleased(CefRefPtr<CefBrowser>,
                           CefRefPtr<CefFrame>,
                           CefRefPtr<CefV8Context> context) override {
        for (auto it = g_pending.begin(); it != g_pending.end();) {
            if (it->second.context->IsSame(context)) {
                it = g_pending.erase(it);
            } else {
                ++it;
            }
        }
    }

    bool OnProcessMessageReceived(
        CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
        CefProcessId, CefRefPtr<CefProcessMessage> message) override {
        if (message->GetName() != "xer_result") {
            return false;
        }
        CefRefPtr<CefListValue> a = message->GetArgumentList();
        if (!a || a->GetSize() < 3) {
            return true;
        }
        const int id = a->GetInt(0);
        const auto it = g_pending.find(id);
        if (it == g_pending.end()) {
            return true;   // xer.log fire-and-forget, or dead context
        }
        PendingCall pending = std::move(it->second);
        g_pending.erase(it);
        const bool ok = a->GetBool(1);
        const CefString payload = a->GetString(2);
        pending.context->Enter();
        if (ok) {
            pending.promise->ResolvePromise(
                CefV8Value::CreateString(payload));
        } else {
            pending.promise->RejectPromise(payload);
        }
        pending.context->Exit();
        return true;
    }

    XERBrowserShellApp(const XERBrowserShellApp&) = delete;
    XERBrowserShellApp& operator=(const XERBrowserShellApp&) = delete;

private:
    IMPLEMENT_REFCOUNTING(XERBrowserShellApp);
};

}  // namespace

CefRefPtr<CefApp> CreateBrowserShellApp() {
    return new XERBrowserShellApp();
}

}  // namespace XER::Cef
#endif  // XER_HAS_CEF
