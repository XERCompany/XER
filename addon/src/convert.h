#pragma once

// JS -> C++ argument converters shared by all widget bindings.

#include <string>
#include <vector>

#include "util.h"
#include "layout/geometry.h"
#include "helper/string_utils.h"

namespace xer::addon {

namespace UIL = XER::UI::Layout;
namespace Str = XER::Helper::String;

inline v8::Local<v8::Value> GetProp(v8::Local<v8::Object> obj,
                                    const char* key) {
    return Nan::Get(obj, Nan::New(key).ToLocalChecked())
        .FromMaybe(Nan::Undefined());
}

inline std::string StrProp(v8::Local<v8::Object> obj, const char* key) {
    v8::Local<v8::Value> v = GetProp(obj, key);
    if (!v->IsString()) return {};
    Nan::Utf8String s(v);
    return std::string(*s, static_cast<std::size_t>(s.length()));
}

inline bool BoolProp(v8::Local<v8::Object> obj, const char* key,
                     bool fallback) {
    v8::Local<v8::Value> v = GetProp(obj, key);
    return v->IsBoolean() ? v.As<v8::Boolean>()->Value() : fallback;
}

inline double NumProp(v8::Local<v8::Object> obj, const char* key,
                      double fallback) {
    v8::Local<v8::Value> v = GetProp(obj, key);
    return v->IsNumber() ? v.As<v8::Number>()->Value() : fallback;
}

// String array from a JS array argument (non-array -> empty).
inline std::vector<std::string> StringArray(v8::Local<v8::Value> v) {
    std::vector<std::string> out;
    if (v.IsEmpty() || !v->IsArray()) return out;
    v8::Local<v8::Array> arr = v.As<v8::Array>();
    const uint32_t n = arr->Length();
    out.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        v8::Local<v8::Value> item =
            arr->Get(Nan::GetCurrentContext(), i).FromMaybe(Nan::Undefined());
        if (item->IsString()) {
            Nan::Utf8String s(item);
            out.emplace_back(*s, static_cast<std::size_t>(s.length()));
        } else {
            out.emplace_back();
        }
    }
    return out;
}

// Int array from a JS array argument (non-array -> empty).
inline std::vector<int> IntArray(v8::Local<v8::Value> v) {
    std::vector<int> out;
    if (v.IsEmpty() || !v->IsArray()) return out;
    v8::Local<v8::Array> arr = v.As<v8::Array>();
    const uint32_t n = arr->Length();
    out.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        v8::Local<v8::Value> item =
            arr->Get(Nan::GetCurrentContext(), i).FromMaybe(Nan::Undefined());
        out.push_back(item->IsNumber()
                          ? static_cast<int>(item.As<v8::Number>()->Value())
                          : 0);
    }
    return out;
}

inline double NumberOr(v8::Local<v8::Value> v, double fallback) {
    return v->IsNumber() ? v.As<v8::Number>()->Value() : fallback;
}

inline bool BoolOr(v8::Local<v8::Value> v, bool fallback) {
    return v->IsBoolean() ? v.As<v8::Boolean>()->Value() : fallback;
}

inline UIL::Point PointFromJS(v8::Local<v8::Value> obj) {
    UIL::Point p{};
    if (obj.IsEmpty() || !obj->IsObject()) return p;
    v8::Local<v8::Object> o = obj.As<v8::Object>();
    p.x = static_cast<float>(
        NumberOr(GetProp(o, "x"), 0));
    p.y = static_cast<float>(
        NumberOr(GetProp(o, "y"), 0));
    return p;
}

inline UIL::Rect RectFromJS(v8::Local<v8::Value> obj) {
    UIL::Rect r{};
    if (obj.IsEmpty() || !obj->IsObject()) return r;
    v8::Local<v8::Object> o = obj.As<v8::Object>();
    r.x = static_cast<float>(NumberOr(GetProp(o, "x"), 0));
    r.y = static_cast<float>(NumberOr(GetProp(o, "y"), 0));
    r.width = static_cast<float>(NumberOr(GetProp(o, "width"), 0));
    r.height = static_cast<float>(NumberOr(GetProp(o, "height"), 0));
    return r;
}

// EnumArg — EnumFromJS for a FunctionCallbackInfo argument slot.
template <typename T>
inline T EnumArg(const Nan::FunctionCallbackInfo<v8::Value>& info, int index,
                 T fallback,
                 std::initializer_list<std::pair<const char*, T>> names) {
    return index < info.Length() ? EnumFromJS(info[index], fallback, names)
                                 : fallback;
}

// Enum parse: accepts the C++ enumerator name (any case) or its ordinal.
// `names` lists {"Primary", T::Primary}, ... in declaration order.
template <typename T>
inline T EnumFromJS(v8::Local<v8::Value> v, T fallback,
                    std::initializer_list<std::pair<const char*, T>> names) {
    if (v.IsEmpty()) return fallback;
    if (v->IsNumber()) {
        const int i = static_cast<int>(v.As<v8::Number>()->Value());
        int k = 0;
        for (const auto& [name, val] : names) {
            (void)name;
            if (k++ == i) return val;
        }
        return fallback;
    }
    if (v->IsString()) {
        Nan::Utf8String s(v);
        std::string lower = Str::ToLowerAscii(std::string(*s, s.length()));
        for (const auto& [name, val] : names) {
            if (Str::ToLowerAscii(name) == lower) return val;
        }
    }
    return fallback;
}

}  // namespace xer::addon
