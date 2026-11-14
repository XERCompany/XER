#pragma once

// Shared helpers for the xer.audio.* class bindings.
// nan.h / node.h must be included before any XER header (see util.h).

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "util.h"

namespace xer::addon::audio {

// Generic unwrap for any Nan::ObjectWrap with >=1 internal field.
template <typename W>
inline W* UnwrapT(v8::Local<v8::Value> v) {
    if (v.IsEmpty() || !v->IsObject()) return nullptr;
    v8::Local<v8::Object> obj = v.As<v8::Object>();
    if (obj->InternalFieldCount() < 1) return nullptr;
    return Nan::ObjectWrap::Unwrap<W>(obj);
}

inline double NumArg(const Nan::FunctionCallbackInfo<v8::Value>& info,
                     int index, double fallback = 0.0) {
    if (index < info.Length() && info[index]->IsNumber()) {
        return info[index].As<v8::Number>()->Value();
    }
    return fallback;
}

inline int IntArg(const Nan::FunctionCallbackInfo<v8::Value>& info,
                  int index, int fallback = 0) {
    if (index < info.Length() && info[index]->IsNumber()) {
        return static_cast<int>(info[index].As<v8::Number>()->Value());
    }
    return fallback;
}

inline bool BoolArg(const Nan::FunctionCallbackInfo<v8::Value>& info,
                    int index, bool fallback = false) {
    if (index < info.Length()) {
        if (info[index]->IsBoolean()) {
            return info[index].As<v8::Boolean>()->Value();
        }
        if (info[index]->IsNumber()) {
            return info[index].As<v8::Number>()->Value() != 0.0;
        }
    }
    return fallback;
}

inline std::string StrArg(const Nan::FunctionCallbackInfo<v8::Value>& info,
                          int index, const char* fallback = "") {
    std::string out;
    return ArgUtf8(info, index, &out) ? out : std::string(fallback);
}

// Enum args arrive as numbers (ordinal) — scoped enums cast cleanly.
template <typename E>
inline E EnumArg(const Nan::FunctionCallbackInfo<v8::Value>& info, int index,
                 E fallback) {
    if (index < info.Length() && info[index]->IsNumber()) {
        return static_cast<E>(
            static_cast<int>(info[index].As<v8::Number>()->Value()));
    }
    return fallback;
}

// --- float32 arrays ----------------------------------------------------------

// Accepts Float32Array | Array<number> | ArrayBuffer.
inline bool F32FromJS(v8::Local<v8::Value> v, std::vector<float>* out) {
    if (v.IsEmpty()) return false;
    if (v->IsFloat32Array()) {
        auto arr = v.As<v8::Float32Array>();
        const size_t n = arr->Length();
        out->resize(n);
        if (n > 0) {
            const char* src = static_cast<const char*>(
                arr->Buffer()->GetBackingStore()->Data()) +
                static_cast<size_t>(arr->ByteOffset());
            std::memcpy(out->data(), src, n * sizeof(float));
        }
        return true;
    }
    if (v->IsArray()) {
        auto arr = v.As<v8::Array>();
        const size_t n = arr->Length();
        out->resize(n);
        for (size_t i = 0; i < n; ++i) {
            v8::Local<v8::Value> e;
            if (!arr->Get(Nan::GetCurrentContext(),
                          static_cast<uint32_t>(i)).ToLocal(&e) ||
                !e->IsNumber()) {
                out->assign(n, 0.0f);
                return false;
            }
            (*out)[i] = static_cast<float>(e.As<v8::Number>()->Value());
        }
        return true;
    }
    if (v->IsArrayBuffer()) {
        auto ab = v.As<v8::ArrayBuffer>();
        const size_t n = ab->ByteLength() / sizeof(float);
        out->resize(n);
        if (n > 0) {
            std::memcpy(out->data(), ab->GetBackingStore()->Data(),
                        n * sizeof(float));
        }
        return true;
    }
    return false;
}

// Non-allocating view for hot paths; caller must keep `v` alive.
inline const float* F32Data(v8::Local<v8::Value> v, size_t* count) {
    if (v.IsEmpty() || !v->IsFloat32Array()) return nullptr;
    auto arr = v.As<v8::Float32Array>();
    *count = arr->Length();
    return reinterpret_cast<const float*>(
        static_cast<const char*>(
            arr->Buffer()->GetBackingStore()->Data()) +
        static_cast<size_t>(arr->ByteOffset()));
}

inline v8::Local<v8::Value> F32ToJS(const float* data, size_t n) {
    auto ab = v8::ArrayBuffer::New(v8::Isolate::GetCurrent(),
                                   n * sizeof(float));
    if (n > 0 && data != nullptr) {
        std::memcpy(ab->GetBackingStore()->Data(), data, n * sizeof(float));
    }
    return v8::Float32Array::New(ab, 0, n);
}

inline v8::Local<v8::Value> F32ToJS(const std::vector<float>& v) {
    return F32ToJS(v.data(), v.size());
}

inline v8::Local<v8::Value> D64ToJS(const double* data, size_t n) {
    auto ab = v8::ArrayBuffer::New(v8::Isolate::GetCurrent(),
                                   n * sizeof(double));
    if (n > 0 && data != nullptr) {
        std::memcpy(ab->GetBackingStore()->Data(), data, n * sizeof(double));
    }
    return v8::Float64Array::New(ab, 0, n);
}

inline v8::Local<v8::Value> D64ToJS(const std::vector<double>& v) {
    return D64ToJS(v.data(), v.size());
}

inline v8::Local<v8::Value> D64ToJS(const std::vector<size_t>& v) {
    auto arr = Nan::New<v8::Array>(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        Nan::Set(arr, static_cast<uint32_t>(i), Nan::New<v8::Number>(
            static_cast<double>(v[i])));
    }
    return arr;
}

// --- byte arrays -------------------------------------------------------------

// Accepts Uint8Array | Buffer | Array<number> | ArrayBuffer.
inline bool U8FromJS(v8::Local<v8::Value> v, std::vector<uint8_t>* out) {
    if (v.IsEmpty()) return false;
    if (v->IsUint8Array()) {
        auto arr = v.As<v8::Uint8Array>();
        const size_t n = arr->Length();
        out->resize(n);
        if (n > 0) {
            const char* src = static_cast<const char*>(
                arr->Buffer()->GetBackingStore()->Data()) +
                static_cast<size_t>(arr->ByteOffset());
            std::memcpy(out->data(), src, n);
        }
        return true;
    }
    if (v->IsArrayBuffer()) {
        auto ab = v.As<v8::ArrayBuffer>();
        const size_t n = ab->ByteLength();
        out->resize(n);
        if (n > 0) {
            std::memcpy(out->data(), ab->GetBackingStore()->Data(), n);
        }
        return true;
    }
    if (v->IsArray()) {
        auto arr = v.As<v8::Array>();
        const size_t n = arr->Length();
        out->resize(n);
        for (size_t i = 0; i < n; ++i) {
            v8::Local<v8::Value> e;
            if (!arr->Get(Nan::GetCurrentContext(),
                          static_cast<uint32_t>(i)).ToLocal(&e) ||
                !e->IsNumber()) {
                return false;
            }
            (*out)[i] = static_cast<uint8_t>(e.As<v8::Number>()->Value());
        }
        return true;
    }
    return false;
}

inline v8::Local<v8::Value> U8ToJS(const uint8_t* data, size_t n) {
    auto ab = v8::ArrayBuffer::New(v8::Isolate::GetCurrent(), n);
    if (n > 0 && data != nullptr) {
        std::memcpy(ab->GetBackingStore()->Data(), data, n);
    }
    return v8::Uint8Array::New(ab, 0, n);
}

inline v8::Local<v8::Value> U8ToJS(const std::vector<uint8_t>& v) {
    return U8ToJS(v.data(), v.size());
}

// --- int16 arrays (codec bit-depth converters) --------------------------------

inline bool I16FromJS(v8::Local<v8::Value> v, std::vector<int16_t>* out) {
    if (v.IsEmpty()) return false;
    if (v->IsInt16Array()) {
        auto arr = v.As<v8::Int16Array>();
        const size_t n = arr->Length();
        out->resize(n);
        if (n > 0) {
            const char* src = static_cast<const char*>(
                arr->Buffer()->GetBackingStore()->Data()) +
                static_cast<size_t>(arr->ByteOffset());
            std::memcpy(out->data(), src, n * sizeof(int16_t));
        }
        return true;
    }
    if (v->IsArray()) {
        auto arr = v.As<v8::Array>();
        const size_t n = arr->Length();
        out->resize(n);
        for (size_t i = 0; i < n; ++i) {
            v8::Local<v8::Value> e;
            if (!arr->Get(Nan::GetCurrentContext(),
                          static_cast<uint32_t>(i)).ToLocal(&e) ||
                !e->IsNumber()) {
                return false;
            }
            (*out)[i] = static_cast<int16_t>(e.As<v8::Number>()->Value());
        }
        return true;
    }
    return false;
}

inline v8::Local<v8::Value> I16ToJS(const std::vector<int16_t>& v) {
    auto ab = v8::ArrayBuffer::New(v8::Isolate::GetCurrent(),
                                   v.size() * sizeof(int16_t));
    if (!v.empty()) {
        std::memcpy(ab->GetBackingStore()->Data(), v.data(),
                    v.size() * sizeof(int16_t));
    }
    return v8::Int16Array::New(ab, 0, v.size());
}

// --- string arrays -----------------------------------------------------------

inline std::vector<std::string> StrArray(v8::Local<v8::Value> v) {
    std::vector<std::string> out;
    if (v.IsEmpty() || !v->IsArray()) return out;
    auto arr = v.As<v8::Array>();
    const size_t n = arr->Length();
    for (size_t i = 0; i < n; ++i) {
        v8::Local<v8::Value> e;
        if (arr->Get(Nan::GetCurrentContext(), static_cast<uint32_t>(i))
                .ToLocal(&e) &&
            e->IsString()) {
            Nan::Utf8String s(e);
            out.emplace_back(*s, static_cast<std::size_t>(s.length()));
        } else {
            out.emplace_back();
        }
    }
    return out;
}

inline v8::Local<v8::Value> StrVecToJS(const std::vector<std::string>& v) {
    auto arr = Nan::New<v8::Array>(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        Nan::Set(arr, static_cast<uint32_t>(i),
                 Nan::New(v[i]).ToLocalChecked());
    }
    return arr;
}

// --- registration helper -----------------------------------------------------

// Builds a FunctionTemplate with one internal field, installs prototype
// methods, and returns the JS constructor function.
inline v8::Local<v8::Function> MakeAudioClass(
    const char* name, Nan::FunctionCallback ctor,
    std::initializer_list<
        std::pair<const char*, Nan::FunctionCallback>> methods) {
    v8::Local<v8::FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(ctor);
    tpl->InstanceTemplate()->SetInternalFieldCount(1);
    tpl->SetClassName(Nan::New(name).ToLocalChecked());
    for (const auto& m : methods) {
        Nan::SetMethod(tpl->PrototypeTemplate(), m.first, m.second);
    }
    return Nan::GetFunction(tpl).ToLocalChecked();
}

// Registers `ctor` as `ns.name` and returns the class function.
inline v8::Local<v8::Function> RegisterAudioClass(
    v8::Local<v8::Object> ns, const char* name,
    Nan::FunctionCallback ctor,
    std::initializer_list<
        std::pair<const char*, Nan::FunctionCallback>> methods) {
    v8::Local<v8::Function> fn = MakeAudioClass(name, ctor, methods);
    Nan::Set(ns, Nan::New(name).ToLocalChecked(), fn);
    return fn;
}

} // namespace xer::addon::audio
