#include "node_graph/node_graph_value.h"
 
#include <cmath>
#include <cstdio>
#include <cstdlib>
 
namespace XER::NodeGraph {
 
const char* ValueTypeName(ValueType type) {
    switch (type) {
        case ValueType::kAny: return "any";
        case ValueType::kNumber: return "number";
        case ValueType::kBool: return "bool";
        case ValueType::kString: return "string";
        case ValueType::kArray: return "array";
    }
    return "any";
}
 
ValueType ValueTypeFromName(const std::string& name) {
    if (name == "number" || name == "float" || name == "int") return ValueType::kNumber;
    if (name == "bool" || name == "boolean") return ValueType::kBool;
    if (name == "string") return ValueType::kString;
    if (name == "array") return ValueType::kArray;
    return ValueType::kAny;
}
 
bool AreTypesCompatible(ValueType from, ValueType to) {
    if (from == to || from == ValueType::kAny || to == ValueType::kAny) return true;
    if ((from == ValueType::kNumber && to == ValueType::kBool) ||
        (from == ValueType::kBool && to == ValueType::kNumber)) {
        return true;
    }
    if (to == ValueType::kString && (from == ValueType::kNumber || from == ValueType::kBool)) return true;
    return false;
}
 
ValueType Value::Type() const {
    switch (data_.index()) {
        case 1: return ValueType::kNumber;
        case 2: return ValueType::kBool;
        case 3: return ValueType::kString;
        case 4: return ValueType::kArray;
        default: return ValueType::kAny;
    }
}
 
double Value::AsNumber(double fallback) const {
    if (const auto* d = std::get_if<double>(&data_)) return *d;
    if (const auto* b = std::get_if<bool>(&data_)) return *b ? 1.0 : 0.0;
    if (const auto* s = std::get_if<std::string>(&data_)) {
        char* end = nullptr;
        const double v = std::strtod(s->c_str(), &end);
        return end != s->c_str() ? v : fallback;
    }
    if (const auto* a = std::get_if<Array>(&data_)) return a->empty() ? fallback : a->front();
    return fallback;
}
 
bool Value::AsBool(bool fallback) const {
    if (const auto* b = std::get_if<bool>(&data_)) return *b;
    if (const auto* d = std::get_if<double>(&data_)) return *d != 0.0;
    if (const auto* s = std::get_if<std::string>(&data_)) return !s->empty() && *s != "false" && *s != "0";
    if (const auto* a = std::get_if<Array>(&data_)) return !a->empty();
    return fallback;
}
 
static std::string FormatNumber(double v) {
    if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 1e15) {
        return std::to_string(static_cast<long long>(v));
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}
 
std::string Value::AsString() const {
    if (const auto* s = std::get_if<std::string>(&data_)) return *s;
    if (const auto* d = std::get_if<double>(&data_)) return FormatNumber(*d);
    if (const auto* b = std::get_if<bool>(&data_)) return *b ? "true" : "false";
    if (const auto* a = std::get_if<Array>(&data_)) {
        std::string out = "[";
        for (size_t i = 0; i < a->size(); ++i) {
            if (i) out += ", ";
            out += FormatNumber((*a)[i]);
        }
        return out + "]";
    }
    return "";
}
 
Value::Array Value::AsArray() const {
    if (const auto* a = std::get_if<Array>(&data_)) return *a;
    if (IsNone()) return {};
    return {AsNumber()};
}
 
Value Value::ConvertTo(ValueType type) const {
    switch (type) {
        case ValueType::kAny: return *this;
        case ValueType::kNumber: return Value(AsNumber());
        case ValueType::kBool: return Value(AsBool());
        case ValueType::kString: return Value(AsString());
        case ValueType::kArray: return Value(AsArray());
    }
    return *this;
}
 
Json Value::ToJson() const {
    if (const auto* d = std::get_if<double>(&data_)) return Json(*d);
    if (const auto* b = std::get_if<bool>(&data_)) return Json(*b);
    if (const auto* s = std::get_if<std::string>(&data_)) return Json(*s);
    if (const auto* a = std::get_if<Array>(&data_)) {
        Json::Array arr;
        arr.reserve(a->size());
        for (double v : *a) arr.emplace_back(v);
        return Json(std::move(arr));
    }
    return Json();
}
 
Value Value::FromJson(const Json& json) {
    if (json.IsNumber()) return Value(json.AsNumber());
    if (json.IsBool()) return Value(json.AsBool());
    if (json.IsString()) return Value(json.AsString());
    if (json.IsArray()) {
        Array arr;
        for (const auto& item : json.AsArray()) arr.push_back(item.AsNumber());
        return Value(std::move(arr));
    }
    return Value();
}
 
}  // namespace XER::NodeGraph