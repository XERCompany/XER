// Embedded V8 engine for the app-runtime assets layer. One instance per
// process lifetime (init/shutdown of the platform is global); cheap to
// keep alive for the whole app — script handlers registered by
// runtime.js are invoked later via CallHandler.
//
// Deliberately small surface:
//   Create()            — init platform + isolate + context with the
//                         globals NativeImport / exports / console
//   RunFile/RunSource   — compile + execute, error text on failure
//   RegisterModule      — make `NativeImport("<name>")` return an object
//   SetOutData/GetOutData — the exports.setOutAppData() registry
//   CallHandler         — invoke a previously registered JS function
//
// NOT thread-safe: all calls must happen on the UI thread.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace v8 {
class Isolate;
template <class T> class Local;
class Context;
class Value;
class Object;
class Function;
template <class T> class Global;
}

namespace XER::VideoEditor::AppRuntime {

// A JS-side value passed back to C++: for our bootstrap surface only
// strings/numbers/bools and registered functions matter.
struct OutValue {
    enum class Kind {
        Undefined,
        String,
        Number,
        Bool,
        Handler,
        Json  // `str` carries raw JSON text; marshalled via v8::JSON::Parse
    } kind = Kind::Undefined;
    std::string str;
    double num = 0;
    bool boolean = false;
    int handler_id = -1;  // index into the engine's handler table
};

// A module factory builds the V8 object returned by
// NativeImport("<name>"). The factory receives the engine so it can
// attach native callbacks via engine->DefineFunction().
class JsEngine;
using ModuleFactory = std::function<void(JsEngine& engine)>;

class JsEngine {
public:
    // Initialises the V8 platform (once per process) and creates an
    // isolate + context exposing the globals:
    //   NativeImport(name), exports, console.log/console.warn
    static std::unique_ptr<JsEngine> Create(std::string* error);
    ~JsEngine();

    JsEngine() = default;  // empty shell — Create() fills it
    JsEngine(const JsEngine&) = delete;
    JsEngine& operator=(const JsEngine&) = delete;

    // Compiles and runs source text; `name` is shown in stack traces.
    bool RunSource(const std::string& source, const std::string& name,
                   std::string* error);
    bool RunFile(const std::string& path_utf8, std::string* error);

    // ---- native surface exposed to modules -------------------------

    // Attaches fn as `name` on `parent` (a V8 object handle kept by the
    // engine). Native callbacks receive args as vector<string>-coercible
    // values — we expose them to the callback as OutValue.
    using NativeFn = std::function<OutValue(const std::vector<OutValue>&)>;
    // Returns an object handle id usable with DefineFunction and
    // SetObjectProperty; -1 on failure.
    int NewObject();
    bool DefineFunction(int object_id, const std::string& name,
                        NativeFn fn);
    bool SetObjectProperty(int object_id, const std::string& key,
                           const OutValue& value);
    bool RegisterModule(const std::string& name, ModuleFactory factory);

    // ---- exports.setOutAppData registry ----------------------------

    // Called by the app_super_exportAppData module: stores key→value.
    void SetOutData(const std::string& key, const OutValue& value);
    OutValue GetOutData(const std::string& key) const;

    // Invokes handler registered under `key` (via SetOutData with a
    // function value). `args` become the JS call arguments (strings are
    // wrapped as JS strings; for structured payloads modules pass JSON
    // text — the engine parses objects given as JSON when arg_json=true).
    bool CallHandler(const std::string& key,
                     const std::vector<OutValue>& args,
                     std::string* error);

    // Serialization helper: builds a JS object literal from flat
    // key→value pairs (all strings) and returns it as an argument value.
    OutValue MakeObjectArg(
        const std::vector<std::pair<std::string, std::string>>& fields);

    // Single access point for consumers that need the raw V8 API
    // (typed arrays, function handles — things OutValue cannot carry).
    // Runs `fn` inside Isolate::Scope + HandleScope + Context::Scope;
    // the callback receives (isolate, context). Must still be called
    // only from the thread that owns this engine.
    bool WithContext(const std::function<bool(
                         v8::Isolate*, v8::Local<v8::Context>)>& fn);

    v8::Isolate* isolate() const;  // for module factories

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Helpers for module code
OutValue StrVal(std::string s);
OutValue NumVal(double n);
OutValue BoolVal(bool b);
OutValue UndefVal();

}  // namespace XER::VideoEditor::AppRuntime
