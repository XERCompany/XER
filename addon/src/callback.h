#pragma once

// CallbackStore — bridges C++ std::function callbacks to JS functions.
//
// Each widget wrap owns one store. A SetOn* binding stores the JS function
// under a name and installs a C++ lambda capturing the *store* (shared_ptr),
// never the wrap — so the callback stays valid while the C++ widget lives
// (e.g. as a child of another widget) even if the JS object is collected.
// WidgetWrap's destructor calls Clear(), turning dead callbacks into no-ops.

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "util.h"

#include "layout/geometry.h"

namespace xer::addon {

namespace UIL = XER::UI::Layout;

// --- C++ -> JS value marshalling ---------------------------------------------

inline v8::Local<v8::Value> ToV8(bool v) { return Nan::New(v); }
inline v8::Local<v8::Value> ToV8(int v) { return Nan::New(v); }
inline v8::Local<v8::Value> ToV8(std::int64_t v) {
    return Nan::New(static_cast<double>(v));
}
inline v8::Local<v8::Value> ToV8(std::size_t v) {
    return Nan::New(static_cast<double>(v));
}
inline v8::Local<v8::Value> ToV8(float v) {
    return Nan::New(static_cast<double>(v));
}
inline v8::Local<v8::Value> ToV8(double v) { return Nan::New(v); }
inline v8::Local<v8::Value> ToV8(const char* v) {
    return Nan::New(v).ToLocalChecked();
}
inline v8::Local<v8::Value> ToV8(const std::string& v) {
    return Nan::New(v).ToLocalChecked();
}
inline v8::Local<v8::Value> ToV8(const std::vector<int>& v) {
    v8::Local<v8::Array> arr = Nan::New<v8::Array>(v.size());
    for (std::size_t i = 0; i < v.size(); ++i)
        Nan::Set(arr, i, Nan::New(v[i]));
    return arr;
}
inline v8::Local<v8::Value> ToV8(const std::vector<float>& v) {
    v8::Local<v8::Array> arr = Nan::New<v8::Array>(v.size());
    for (std::size_t i = 0; i < v.size(); ++i)
        Nan::Set(arr, i, Nan::New(v[i]));
    return arr;
}
inline v8::Local<v8::Value> ToV8(const std::vector<std::string>& v) {
    v8::Local<v8::Array> arr = Nan::New<v8::Array>(v.size());
    for (std::size_t i = 0; i < v.size(); ++i)
        Nan::Set(arr, i, Nan::New(v[i]).ToLocalChecked());
    return arr;
}
inline v8::Local<v8::Value> ToV8(const UIL::Point& p) {
    v8::Local<v8::Object> obj = Nan::New<v8::Object>();
    Nan::Set(obj, Nan::New("x").ToLocalChecked(), Nan::New(p.x));
    Nan::Set(obj, Nan::New("y").ToLocalChecked(), Nan::New(p.y));
    return obj;
}
inline v8::Local<v8::Value> ToV8(const UIL::Rect& r) {
    v8::Local<v8::Object> obj = Nan::New<v8::Object>();
    Nan::Set(obj, Nan::New("x").ToLocalChecked(), Nan::New(r.x));
    Nan::Set(obj, Nan::New("y").ToLocalChecked(), Nan::New(r.y));
    Nan::Set(obj, Nan::New("width").ToLocalChecked(), Nan::New(r.width));
    Nan::Set(obj, Nan::New("height").ToLocalChecked(), Nan::New(r.height));
    return obj;
}
inline v8::Local<v8::Value> ToV8(std::nullptr_t) {
    return Nan::Undefined();
}
// Passthrough for pre-built JS values (objects/arrays created by the
// binding itself before Emit).
inline v8::Local<v8::Value> ToV8(v8::Local<v8::Value> v) { return v; }

class CallbackStore {
public:
    ~CallbackStore() { Clear(); }

    // name must stay alive — callers use string literals or stable keys.
    void Set(const char* name, v8::Local<v8::Function> fn) {
        auto& slot = fns_[name];
        if (!slot) slot = std::make_unique<Nan::Persistent<v8::Function>>();
        slot->Reset(fn);
    }

    void Remove(const char* name) {
        auto it = fns_.find(name);
        if (it != fns_.end()) fns_.erase(it);
    }

    void Clear() {
        for (auto& [name, fn] : fns_) fn->Reset();
        fns_.clear();
    }

    // Unique stable key for repeated registrations (e.g. menu item actions).
    std::string Unique(const char* prefix) {
        return std::string(prefix) + "_" + std::to_string(next_key_++);
    }

    template <typename... Args>
    void Emit(const char* name, Args&&... args) {
        auto it = fns_.find(name);
        if (it == fns_.end() || it->second->IsEmpty()) return;
        v8::Local<v8::Value> argv[sizeof...(Args) > 0 ? sizeof...(Args) : 1] = {
            ToV8(std::forward<Args>(args))...};
        Nan::Call(Nan::New(*it->second), Nan::GetCurrentContext()->Global(),
                  static_cast<int>(sizeof...(Args)), argv);
    }

    // Like Emit but returns the JS result (for bool-returning callbacks).
    template <typename... Args>
    v8::Local<v8::Value> Call(const char* name, Args&&... args) {
        auto it = fns_.find(name);
        if (it == fns_.end() || it->second->IsEmpty()) return Nan::Undefined();
        v8::Local<v8::Value> argv[sizeof...(Args) > 0 ? sizeof...(Args) : 1] = {
            ToV8(std::forward<Args>(args))...};
        return Nan::Call(Nan::New(*it->second),
                         Nan::GetCurrentContext()->Global(),
                         static_cast<int>(sizeof...(Args)), argv)
            .ToLocalChecked();
    }

private:
    std::unordered_map<std::string,
                       std::unique_ptr<Nan::Persistent<v8::Function>>>
        fns_;
    int next_key_ = 0;
};

}  // namespace xer::addon
