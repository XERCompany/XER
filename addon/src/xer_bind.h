#pragma once

// Shared helpers for the xer.node NAN bindings.
// nan.h / node.h must be included before any XER header: they pull in
// windows.h and v8.h, which can collide with project headers.
// NOMINMAX must be defined *before* the first windows.h inclusion or the
// min/max macros break std::min/std::max inside the V8 headers.

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <nan.h>

// Undo windows.h min/max macros if some earlier include already defined them.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <string>

namespace xer::addon {

// Declarations implemented in the bind_*.cc files.
void InitImage(v8::Local<v8::Object> exports);
void InitAudio(v8::Local<v8::Object> exports);

// Returns true and fills `out` when info[index] is a string.
inline bool ArgUtf8(const Nan::FunctionCallbackInfo<v8::Value>& info,
                    int index, std::string* out) {
    if (index >= info.Length() || !info[index]->IsString()) return false;
    Nan::Utf8String s(info[index]);
    if (s.length() < 0) return false;
    *out = std::string(*s, static_cast<std::size_t>(s.length()));
    return true;
}

inline bool HasArg(const Nan::FunctionCallbackInfo<v8::Value>& info,
                   int index) {
    return index < info.Length() && !info[index]->IsUndefined() &&
           !info[index]->IsNull();
}

inline void ThrowUsage(const char* what) {
    Nan::ThrowError((std::string("xer.") + what).c_str());
}

}  // namespace xer::addon
