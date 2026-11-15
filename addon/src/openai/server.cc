// xer.OpenAI.Server — JS-callable OpenAI-compatible HTTP server.
//
// Transport: Content::OpenAI::Server::OpenAIServer (content/openai), which
// serves real HTTP via XER::Network::Http::HttpServer. Built-in OpenAI
// routes (/v1/chat/completions, /v1/images/*, /v1/audio/*, /v1/models)
// dispatch into the native OpenAIServer request handlers; routes registered
// from JS via on*() are matched first and handled by JS callbacks.
//
// Threading model: HttpServer dispatches each request on a detached worker
// thread, but JS callbacks may only run on the Node main thread. Every
// call is marshalled through a uv_async queue: the worker posts a JsCall
// and blocks on a condition variable until the main-thread drain executes
// the handler and fills the response. Handlers must therefore be fast and
// must not block — a handler that never calls res.json/send/end produces a
// 500, and a handler that calls back into the same server deadlocks.
//
// Routes registered via on*() must be set before start(); the router is
// snapshotted when the listen socket opens.

#include "util.h"
#include "callback.h"

#include "content/network/http/http_message.h"
#include "content/network/http/http_router.h"
#include "content/network/http/http_server.h"
#include "server/openai_server.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace H = XER::Network::Http;
namespace OAI = Content::OpenAI::Server;

// Response assembled by the res.* methods on the JS side.
struct PendingResponse {
    enum class Kind { kNone, kText, kJson, kBinary };
    int status = 200;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    Kind kind = Kind::kNone;
    bool done = false;
};

struct RouteEntry {
    std::string method;  // uppercase verb, or "*" for any
    std::string pattern;
    Nan::Persistent<v8::Function> fn;
};

// One request/response round-trip through the JS main thread.
struct JsCall {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    H::HttpResponse response;

    std::string route_key;  // "METHOD /path", or "@auth" for the auth gate
    H::HttpRequest request;
    std::map<std::string, std::string> path_params;
};
using JsCallPtr = std::shared_ptr<JsCall>;

// Shared between the wrap and detached HttpServer worker threads. Contains
// no V8 handles — safe to destroy on any thread.
struct ServerState {
    std::shared_ptr<OAI::OpenAIServer> openai;
    std::string host = "127.0.0.1";
    std::uint16_t port = 8080;
    std::atomic<bool> stopping{false};
    std::atomic<bool> running{false};

    std::mutex queue_mutex;
    std::deque<JsCallPtr> queue;
    // Heap-allocated so uv_close's callback can delete it; data = ServerWrap*.
    uv_async_t* async = nullptr;
};

struct AuthDecision {
    bool called = false;
    bool allowed = false;
};

bool IEquals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb + 32);
        if (ca != cb) return false;
    }
    return true;
}

std::string Utf8(v8::Local<v8::Value> v) {
    Nan::Utf8String s(v);
    return std::string(*s ? *s : "");
}

// Extracts the API key the same way OpenAI clients send it:
// "Authorization: Bearer k", x-api-key/api-key header, ?api_key=, ?key=.
std::string ExtractApiKey(const H::HttpRequest& req) {
    const std::string auth = req.headers.Get("authorization");
    if (auth.size() > 7 && IEquals(auth.substr(0, 7), "bearer ")) {
        return auth.substr(7);
    }
    for (const char* name : {"x-api-key", "api-key"}) {
        if (req.headers.Contains(name)) return req.headers.Get(name);
    }
    std::string key = req.QueryParam("api_key");
    if (key.empty()) key = req.QueryParam("key");
    return key;
}

v8::Local<v8::Value> BytesToUint8Array(std::string_view bytes) {
    v8::Isolate* iso = v8::Isolate::GetCurrent();
    if (bytes.empty()) {
        return v8::Uint8Array::New(v8::ArrayBuffer::New(iso, 0), 0, 0);
    }
    v8::Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(iso, bytes.size());
    auto store = ab->GetBackingStore();
    std::memcpy(store->Data(), bytes.data(), bytes.size());
    return v8::Uint8Array::New(ab, 0, bytes.size());
}

// verify(ok) function handed to the onHandlerKey callback. Must be invoked
// synchronously while the auth callback runs.
v8::Local<v8::Value> MakeVerifyFunction(AuthDecision* decision) {
    v8::Isolate* iso = v8::Isolate::GetCurrent();
    v8::Local<v8::Context> context = iso->GetCurrentContext();
    v8::Local<v8::Value> data = v8::External::New(iso, decision);
    v8::Local<v8::Function> fn;
    auto maybe = v8::Function::New(
        context,
        [](const v8::FunctionCallbackInfo<v8::Value>& info) {
            auto* d = static_cast<AuthDecision*>(
                info.Data().As<v8::External>()->Value());
            d->called = true;
            d->allowed =
                info.Length() > 0 &&
                info[0]
                    ->BooleanValue(info.GetIsolate()->GetCurrentContext())
                    .FromMaybe(false);
        },
        data);
    if (!maybe.ToLocal(&fn)) return v8::Local<v8::Value>();
    return fn;
}

H::HttpResponse BuildHttpResponse(const PendingResponse& ps) {
    H::HttpResponse resp;
    resp.status = ps.status;
    resp.body = ps.body;
    bool has_content_type = false;
    for (const auto& [name, value] : ps.headers) {
        if (IEquals(name, "content-type")) has_content_type = true;
        resp.headers.Add(name, value);
    }
    if (!has_content_type) {
        switch (ps.kind) {
            case PendingResponse::Kind::kJson:
                resp.headers.Set("Content-Type",
                                  "application/json; charset=utf-8");
                break;
            case PendingResponse::Kind::kBinary:
                resp.headers.Set("Content-Type", "application/octet-stream");
                break;
            case PendingResponse::Kind::kText:
                resp.headers.Set("Content-Type", "text/plain; charset=utf-8");
                break;
            case PendingResponse::Kind::kNone:
                break;
        }
    }
    return resp;
}

// res.* methods — unwrap the ResponseWrap on info.This().
class ResponseWrap;

ResponseWrap* UnwrapResponse(v8::Local<v8::Value> v) {
    if (v.IsEmpty() || !v->IsObject()) return nullptr;
    return Nan::ObjectWrap::Unwrap<ResponseWrap>(v.As<v8::Object>());
}

class ResponseWrap : public Nan::ObjectWrap {
public:
    explicit ResponseWrap(std::shared_ptr<PendingResponse> ps)
        : ps_(std::move(ps)) {}

    static v8::Local<v8::Object> Create(std::shared_ptr<PendingResponse> ps) {
        auto* wrap = new ResponseWrap(std::move(ps));
        v8::Local<v8::Context> context =
            v8::Isolate::GetCurrent()->GetCurrentContext();
        v8::Local<v8::Object> obj;
        Template()
            ->InstanceTemplate()
            ->NewInstance(context)
            .ToLocal(&obj);
        wrap->Wrap(obj);
        return obj;
    }

    static v8::Local<v8::FunctionTemplate> Template() {
        // Leaked on purpose: a static Nan::Persistent would be destroyed
        // after the isolate is gone (same hazard as WindowWrap).
        static auto* tpl = new Nan::Persistent<v8::FunctionTemplate>();
        if (tpl->IsEmpty()) {
            auto t = Nan::New<v8::FunctionTemplate>(
                [](const Nan::FunctionCallbackInfo<v8::Value>&) {});
            t->InstanceTemplate()->SetInternalFieldCount(1);
            t->SetClassName(Nan::New("ServerResponse").ToLocalChecked());
            auto proto = t->PrototypeTemplate();
            Nan::SetMethod(proto, "status", ResponseStatus);
            Nan::SetMethod(proto, "setHeader", ResponseSetHeader);
            Nan::SetMethod(proto, "json", ResponseJson);
            Nan::SetMethod(proto, "send", ResponseSend);
            Nan::SetMethod(proto, "end", ResponseEnd);
            tpl->Reset(t);
        }
        return Nan::New(*tpl);
    }

private:
    friend ResponseWrap* UnwrapResponse(v8::Local<v8::Value> v);

    static void ResponseStatus(const Nan::FunctionCallbackInfo<v8::Value>& info) {
        auto* self = UnwrapResponse(info.This());
        if (!self) return;
        self->ps_->status = Nan::To<int>(info[0]).FromMaybe(200);
        info.GetReturnValue().Set(info.This());
    }

    static void ResponseSetHeader(const Nan::FunctionCallbackInfo<v8::Value>& info) {
        auto* self = UnwrapResponse(info.This());
        if (!self || info.Length() < 2) return;
        self->ps_->headers.emplace_back(Utf8(info[0]), Utf8(info[1]));
        info.GetReturnValue().Set(info.This());
    }

    static void ResponseJson(const Nan::FunctionCallbackInfo<v8::Value>& info) {
        auto* self = UnwrapResponse(info.This());
        if (!self) return;
        v8::Isolate* iso = v8::Isolate::GetCurrent();
        v8::Local<v8::Value> s;
        if (v8::JSON::Stringify(iso->GetCurrentContext(), info[0]).ToLocal(&s)) {
            self->ps_->body = Utf8(s);
            self->ps_->kind = PendingResponse::Kind::kJson;
            self->ps_->done = true;
        }
        info.GetReturnValue().Set(info.This());
    }

    static void ResponseSend(const Nan::FunctionCallbackInfo<v8::Value>& info) {
        auto* self = UnwrapResponse(info.This());
        if (!self || info.Length() < 1) return;
        v8::Local<v8::Value> v = info[0];
        if (v->IsString()) {
            self->ps_->body = Utf8(v);
            self->ps_->kind = PendingResponse::Kind::kText;
        } else if (v->IsUint8Array() || v->IsArrayBuffer()) {
            v8::Local<v8::ArrayBuffer> ab;
            size_t offset = 0;
            if (v->IsUint8Array()) {
                auto ta = v.As<v8::Uint8Array>();
                ab = ta->Buffer();
                offset = ta->ByteOffset();
            } else {
                ab = v.As<v8::ArrayBuffer>();
            }
            auto store = ab->GetBackingStore();
            const char* src =
                static_cast<const char*>(store->Data()) + offset;
            self->ps_->body.assign(src, store->ByteLength() - offset);
            self->ps_->kind = PendingResponse::Kind::kBinary;
        } else if (v->IsObject() && !v->IsNull()) {
            v8::Isolate* iso = v8::Isolate::GetCurrent();
            v8::Local<v8::Value> s;
            if (v8::JSON::Stringify(iso->GetCurrentContext(), v).ToLocal(&s)) {
                self->ps_->body = Utf8(s);
                self->ps_->kind = PendingResponse::Kind::kJson;
            }
        }
        self->ps_->done = true;
        info.GetReturnValue().Set(info.This());
    }

    static void ResponseEnd(const Nan::FunctionCallbackInfo<v8::Value>& info) {
        auto* self = UnwrapResponse(info.This());
        if (!self) return;
        // res.end(body) is allowed as a send()+finish shortcut.
        if (info.Length() > 0 && info[0]->IsString()) {
            self->ps_->body = Utf8(info[0]);
            self->ps_->kind = PendingResponse::Kind::kText;
        }
        self->ps_->done = true;
        info.GetReturnValue().Set(info.This());
    }

    std::shared_ptr<PendingResponse> ps_;
};

// Runs on an HttpServer worker thread: enqueue the call for the main
// thread and block until ExecuteCall fills the response.
H::HttpResponse DispatchCall(const std::shared_ptr<ServerState>& state,
                             const JsCallPtr& call) {
    {
        std::lock_guard<std::mutex> lk(state->queue_mutex);
        if (state->stopping) {
            return H::HttpResponse::Make(503, "server is stopping",
                                         "text/plain; charset=utf-8");
        }
        state->queue.push_back(call);
    }
    uv_async_send(state->async);

    std::unique_lock<std::mutex> lk(call->mutex);
    call->cv.wait(lk, [&call] { return call->done; });
    return call->response;
}

class ServerWrap : public Nan::ObjectWrap {
public:
    ServerWrap() : state_(std::make_shared<ServerState>()) {}
    ~ServerWrap() override { Shutdown(); }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
        auto* wrap = new ServerWrap();
        if (xer::addon::HasArg(info, 0) && info[0]->IsObject()) {
            // Optional {host, port} config object.
            v8::Local<v8::Object> cfg = info[0].As<v8::Object>();
            v8::Local<v8::Value> hv, pv;
            if (Nan::Get(cfg, Nan::New("host").ToLocalChecked()).ToLocal(&hv) &&
                hv->IsString()) {
                wrap->state_->host = Utf8(hv);
            }
            if (Nan::Get(cfg, Nan::New("port").ToLocalChecked()).ToLocal(&pv) &&
                pv->IsNumber()) {
                wrap->state_->port = static_cast<std::uint16_t>(
                    pv.As<v8::Number>()->Value());
            }
        }
        wrap->Wrap(info.This());
        info.GetReturnValue().Set(info.This());
    }

    // Registry — touched only on the Node main thread.
    std::map<std::string, RouteEntry> routes_;
    Nan::Persistent<v8::Function> auth_fn_;
    bool shutdown_done_ = false;

    void Shutdown() {
        if (shutdown_done_) return;
        shutdown_done_ = true;
        auto& st = *state_;
        if (st.openai) {
            st.openai->Stop();
            st.openai.reset();
        }
        {
            // Release every blocked worker with a 503 so detached threads
            // cannot outlive the isolate.
            std::lock_guard<std::mutex> lk(st.queue_mutex);
            st.stopping = true;
            for (const auto& c : st.queue) {
                {
                    std::lock_guard<std::mutex> clk(c->mutex);
                    c->response = H::HttpResponse::Make(
                        503, "server stopped", "text/plain; charset=utf-8");
                    c->done = true;
                }
                c->cv.notify_one();
            }
            st.queue.clear();
        }
        for (auto& [key, entry] : routes_) entry.fn.Reset();
        routes_.clear();
        if (!auth_fn_.IsEmpty()) auth_fn_.Reset();
        if (st.async != nullptr) {
            uv_close(st.async,
                     [](uv_handle_t* h) {
                         delete static_cast<uv_async_t*>(h);
                     });
            st.async = nullptr;
        }
    }

    static void AsyncCallback(uv_async_t* handle) {
        auto* self = static_cast<ServerWrap*>(handle->data);
        if (self == nullptr) return;
        std::deque<JsCallPtr> batch;
        {
            std::lock_guard<std::mutex> lk(self->state_->queue_mutex);
            batch.swap(self->state_->queue);
        }
        if (batch.empty()) return;
        v8::HandleScope hs(v8::Isolate::GetCurrent());
        for (const auto& call : batch) self->ExecuteCall(call);
    }

    void ExecuteCall(const JsCallPtr& call) {
        auto finish = [call](H::HttpResponse resp) {
            {
                std::lock_guard<std::mutex> lk(call->mutex);
                call->response = std::move(resp);
                call->done = true;
            }
            call->cv.notify_one();
        };

        v8::Isolate* iso = v8::Isolate::GetCurrent();
        v8::Local<v8::Context> context = iso->GetCurrentContext();
        v8::Local<v8::Value> global = context->Global();

        // Auth gate probe: the native auth validator marshals one of these
        // per request. route_key "@auth" runs the onHandlerKey callback and
        // reports the verdict as a 200/401 response.
        if (call->route_key == "@auth") {
            if (auth_fn_.IsEmpty()) {
                finish(H::HttpResponse::Make(200, "", "text/plain"));
                return;
            }
            AuthDecision decision;
            v8::Local<v8::Value> verify = MakeVerifyFunction(&decision);
            if (verify.IsEmpty()) {
                finish(H::HttpResponse::JsonError(500, "auth setup failed"));
                return;
            }
            v8::Local<v8::Value> argv[] = {
                Nan::New(ExtractApiKey(call->request)), verify};
            {
                v8::TryCatch tc(iso);
                Nan::Call(Nan::New(auth_fn_), global, 2, argv);
                (void)tc;  // auth errors are treated as rejections below
            }
            finish(decision.called && decision.allowed
                       ? H::HttpResponse::Make(200, "", "text/plain")
                       : H::HttpResponse::JsonError(401, "unauthorized"));
            return;
        }

        auto it = routes_.find(call->route_key);
        if (it == routes_.end() || it->second.fn.IsEmpty()) {
            finish(H::HttpResponse::JsonError(
                404, "no handler for " + call->route_key));
            return;
        }

        auto pending = std::make_shared<PendingResponse>();
        v8::Local<v8::Value> argv[] = {BuildRequestValue(call),
                                     ResponseWrap::Create(pending)};
        v8::Local<v8::Value> result;
        {
            v8::TryCatch tc(iso);
            if (!Nan::Call(Nan::New(it->second.fn), global, 2, argv)
                     .ToLocal(&result) ||
                tc.HasCaught()) {
                std::string msg = "handler threw an exception";
                if (tc.HasCaught() && tc.Message()) {
                    msg = Utf8(tc.Message());
                }
                finish(H::HttpResponse::JsonError(500, msg));
                return;
            }
        }

        // Handlers may also return a value instead of calling res.*:
        // object -> JSON, string -> text.
        if (!pending->done) {
            if (result->IsString()) {
                pending->body = Utf8(result);
                pending->kind = PendingResponse::Kind::kText;
                pending->done = true;
            } else if (result->IsObject() && !result->IsNull() &&
                       !result->IsFunction()) {
                v8::Local<v8::Value> s;
                if (v8::JSON::Stringify(context, result).ToLocal(&s)) {
                    pending->body = Utf8(s);
                    pending->kind = PendingResponse::Kind::kJson;
                    pending->done = true;
                }
            }
        }
        if (!pending->done) {
            finish(H::HttpResponse::JsonError(
                500, "handler did not send a response"));
            return;
        }
        finish(BuildHttpResponse(*pending));
    }

    v8::Local<v8::Value> BuildRequestValue(const JsCallPtr& call) {
        const H::HttpRequest& req = call->request;
        v8::Local<v8::Context> context =
            v8::Isolate::GetCurrent()->GetCurrentContext();
        auto obj = Nan::New<v8::Object>();
        auto set = [&obj](const char* name, v8::Local<v8::Value> v) {
            Nan::Set(obj, Nan::New(name).ToLocalChecked(), v);
        };
        set("method", Nan::New(req.method_string));
        set("path", Nan::New(req.path));
        set("query", Nan::New(req.query));
        set("version", Nan::New(req.version));
        set("remote", Nan::New(req.remote));

        auto headers = Nan::New<v8::Object>();
        for (const auto& [name, value] : req.headers.MapView()) {
            auto key = Nan::New(name).ToLocalChecked();
            v8::Local<v8::Value> cur;
            if (Nan::Get(headers, key).ToLocal(&cur) && cur->IsString()) {
                Nan::Set(headers, key,
                         Nan::New(Utf8(cur) + ", " + value).ToLocalChecked());
            } else {
                Nan::Set(headers, key, Nan::New(value).ToLocalChecked());
            }
        }
        set("headers", headers);

        auto qp = Nan::New<v8::Object>();
        for (const auto& [k, v] : req.QueryParams()) {
            Nan::Set(qp, Nan::New(k).ToLocalChecked(),
                     Nan::New(v).ToLocalChecked());
        }
        set("queryParams", qp);

        auto pp = Nan::New<v8::Object>();
        for (const auto& [k, v] : call->path_params) {
            Nan::Set(pp, Nan::New(k).ToLocalChecked(),
                     Nan::New(v).ToLocalChecked());
        }
        set("params", pp);

        set("body", Nan::New(req.body));
        if (!req.body.empty()) set("rawBody", BytesToUint8Array(req.body));

        std::string ct = req.headers.Get("content-type");
        for (char& c : ct) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        }
        if (ct.find("json") != std::string::npos && !req.body.empty()) {
            v8::Local<v8::Value> parsed;
            if (v8::JSON::Parse(context, Nan::New(req.body)).ToLocal(&parsed)) {
                set("json", parsed);
            }
        }
        return obj;
    }

    std::shared_ptr<ServerState> state_;
};

ServerWrap* UnwrapServer(v8::Local<v8::Value> v) {
    if (v.IsEmpty() || !v->IsObject()) return nullptr;
    return Nan::ObjectWrap::Unwrap<ServerWrap>(v.As<v8::Object>());
}

// --- NAN_METHODs ------------------------------------------------------------

NAN_METHOD(ServerStart) {
    auto* self = UnwrapServer(info.This());
    if (!self) return;
    auto& st = *self->state_;
    if (st.running) {
        Nan::ThrowError("OpenAI.Server: already running");
        return;
    }

    std::uint16_t port = st.port;
    std::string host = st.host;
    if (xer::addon::HasArg(info, 0) && info[0]->IsNumber()) {
        port = static_cast<std::uint16_t>(
            info[0].As<v8::Number>()->Value());
    }
    std::string host_arg;
    if (xer::addon::ArgUtf8(info, 1, &host_arg) && !host_arg.empty()) {
        host = host_arg;
    }

    auto openai = std::make_shared<OAI::OpenAIServer>(OAI::ServerConfig{});
    OAI::ServerConfig cfg = openai->GetConfig();
    cfg.host = host;
    cfg.port = port;
    cfg.max_connections = 256;
    cfg.enable_cors = true;
    openai->SetConfig(cfg);

    // JS routes are registered on the native server before Start() so they
    // are matched ahead of the built-in OpenAI endpoints.
    for (const auto& [key, entry] : self->routes_) {
        const std::string method = entry.method;
        const std::string pattern = entry.pattern;
        auto state = self->state_;
        const std::string route_key = key;
        if (!openai->AddRoute(
                method, pattern,
                [state, route_key](const H::HttpRequest& req,
                                   const std::map<std::string, std::string>& params) {
                    auto call = std::make_shared<JsCall>();
                    call->route_key = route_key;
                    call->request = req;
                    call->path_params = params;
                    return DispatchCall(state, call);
                })) {
            Nan::ThrowError("OpenAI.Server: failed to register route");
            return;
        }
    }

    // Auth gate: route every request through the onHandlerKey callback.
    // The callback runs on the main thread via the same JsCall marshalling;
    // its verdict comes back as a synthetic 200/401 response.
    if (!self->auth_fn_.IsEmpty()) {
        auto state = self->state_;
        openai->SetAuthValidator(
            [state](const H::HttpRequest& req) -> bool {
                auto call = std::make_shared<JsCall>();
                call->route_key = "@auth";
                call->request = req;
                const H::HttpResponse resp = DispatchCall(state, call);
                return resp.status == 200;
            });
    }

    if (st.async == nullptr) {
        st.async = new uv_async_t;
        uv_async_init(uv_default_loop(), st.async, &ServerWrap::AsyncCallback);
        st.async->data = self;
        uv_ref(reinterpret_cast<uv_handle_t*>(st.async));
    }

    if (!openai->Start()) {
        Nan::ThrowError("OpenAI.Server: failed to start (port in use?)");
        return;
    }
    st.openai = std::move(openai);
    st.host = host;
    st.port = port;
    st.stopping = false;
    st.running = true;
    info.GetReturnValue().Set(true);
}

NAN_METHOD(ServerStop) {
    auto* self = UnwrapServer(info.This());
    if (!self) return;
    auto& st = *self->state_;
    if (st.openai) st.openai->Stop();
    st.running = false;
    info.GetReturnValue().Set(true);
}

NAN_METHOD(ServerIsRunning) {
    auto* self = UnwrapServer(info.This());
    info.GetReturnValue().Set(
        Nan::New(self != nullptr && self->state_->openai &&
                 self->state_->openai->IsRunning()));
}

// Registers `fn` under method+pattern in the route table.
void RegisterRoute(ServerWrap* self, const std::string& method,
                   const std::string& pattern,
                   v8::Local<v8::Function> fn) {
    if (self->state_->running) {
        Nan::ThrowError(
            "OpenAI.Server: routes must be registered before start()");
        return;
    }
    if (pattern.empty() || pattern.front() != '/') {
        Nan::ThrowError("OpenAI.Server: route pattern must start with '/'");
        return;
    }
    const std::string key = method + " " + pattern;
    auto& entry = self->routes_[key];
    entry.method = method;
    entry.pattern = pattern;
    entry.fn.Reset(fn);
}

NAN_METHOD(ServerOn) {
    auto* self = UnwrapServer(info.This());
    if (!self) return;
    std::string method, pattern;
    v8::Local<v8::Function> fn;
    if (xer::addon::HasArg(info, 2) && info[2]->IsFunction()) {
        // on(method, path, handler)
        method = xer::addon::HasArg(info, 0) && info[0]->IsString()
                     ? Utf8(info[0])
                     : "*";
        for (char& c : method) {
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
        }
        if (!xer::addon::ArgUtf8(info, 1, &pattern)) {
            Nan::ThrowError("OpenAI.Server.on: path must be a string");
            return;
        }
        fn = info[2].As<v8::Function>();
    } else if (xer::addon::HasArg(info, 1) && info[1]->IsFunction()) {
        // on(path, handler) — any method
        if (!xer::addon::ArgUtf8(info, 0, &pattern)) {
            Nan::ThrowError("OpenAI.Server.on: path must be a string");
            return;
        }
        method = "*";
        fn = info[1].As<v8::Function>();
    } else {
        Nan::ThrowError(
            "OpenAI.Server.on: expected on(method, path, fn) or on(path, fn)");
        return;
    }
    RegisterRoute(self, method, pattern, fn);
}

void RegisterNamed(ServerWrap* self,
                    std::initializer_list<const char*> patterns,
                    v8::Local<v8::Function> fn) {
    for (const char* p : patterns) {
        RegisterRoute(self, "POST", p, fn);
    }
}

NAN_METHOD(ServerOnHandlerKey) {
    auto* self = UnwrapServer(info.This());
    if (!self) return;
    if (xer::addon::HasArg(info, 0) && info[0]->IsFunction()) {
        self->auth_fn_.Reset(info[0].As<v8::Function>());
    } else {
        if (!self->auth_fn_.IsEmpty()) self->auth_fn_.Reset();
    }
}

NAN_METHOD(ServerOnHandleCompletion) {
    auto* self = UnwrapServer(info.This());
    if (!self || !xer::addon::HasArg(info, 0) || !info[0]->IsFunction()) {
        return;
    }
    RegisterNamed(self, {"/v1/chat/completions", "/chat/completions"},
                  info[0].As<v8::Function>());
}

NAN_METHOD(ServerOnHandleAudio) {
    auto* self = UnwrapServer(info.This());
    if (!self || !xer::addon::HasArg(info, 0) || !info[0]->IsFunction()) {
        return;
    }
    RegisterNamed(self, {"/v1/audio/transcriptions", "/v1/audio/speech"},
                  info[0].As<v8::Function>());
}

NAN_METHOD(ServerOnHandleImage) {
    auto* self = UnwrapServer(info.This());
    if (!self || !xer::addon::HasArg(info, 0) || !info[0]->IsFunction()) {
        return;
    }
    RegisterNamed(self, {"/v1/images/generations"},
                  info[0].As<v8::Function>());
}

}  // namespace

namespace xer::addon::openai {

void RegisterServer(v8::Local<v8::Object> exports) {
    v8::Local<v8::FunctionTemplate> tpl =
        Nan::New<v8::FunctionTemplate>(ServerWrap::New);
    tpl->InstanceTemplate()->SetInternalFieldCount(1);
    tpl->SetClassName(Nan::New("Server").ToLocalChecked());
    auto proto = tpl->PrototypeTemplate();
    Nan::SetMethod(proto, "start", ServerStart);
    Nan::SetMethod(proto, "stop", ServerStop);
    Nan::SetMethod(proto, "isRunning", ServerIsRunning);
    Nan::SetMethod(proto, "on", ServerOn);
    Nan::SetMethod(proto, "onHandlerKey", ServerOnHandlerKey);
    Nan::SetMethod(proto, "onHandleCompletion", ServerOnHandleCompletion);
    Nan::SetMethod(proto, "onHandleAudio", ServerOnHandleAudio);
    Nan::SetMethod(proto, "onHandleImage", ServerOnHandleImage);
    Nan::Set(exports, Nan::New("Server").ToLocalChecked(),
             Nan::GetFunction(tpl).ToLocalChecked());
}

}  // namespace xer::addon::openai
