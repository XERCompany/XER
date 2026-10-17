#pragma once
 
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
 
namespace XER::NodeGraph {
 
// Minimal ordered JSON document used for graph serialization.
class Json {
public:
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;
 
    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool v) : data_(v) {}
    Json(int v) : data_(static_cast<double>(v)) {}
    Json(int64_t v) : data_(static_cast<double>(v)) {}
    Json(uint64_t v) : data_(static_cast<double>(v)) {}
    Json(double v) : data_(v) {}
    Json(const char* v) : data_(std::string(v)) {}
    Json(std::string v) : data_(std::move(v)) {}
    Json(Array v) : data_(std::move(v)) {}
    Json(Object v) : data_(std::move(v)) {}
 
    static Json MakeArray() { return Json(Array{}); }
    static Json MakeObject() { return Json(Object{}); }
 
    bool IsNull() const { return std::holds_alternative<std::monostate>(data_); }
    bool IsBool() const { return std::holds_alternative<bool>(data_); }
    bool IsNumber() const { return std::holds_alternative<double>(data_); }
    bool IsString() const { return std::holds_alternative<std::string>(data_); }
    bool IsArray() const { return std::holds_alternative<Array>(data_); }
    bool IsObject() const { return std::holds_alternative<Object>(data_); }
 
    bool AsBool(bool fallback = false) const;
    double AsNumber(double fallback = 0.0) const;
    int64_t AsInt(int64_t fallback = 0) const;
    const std::string& AsString() const;
    const Array& AsArray() const;
    const Object& AsObject() const;
    Array& MutableArray();
    Object& MutableObject();
 
    // Object helpers.
    bool Has(const std::string& key) const;
    const Json& operator[](const std::string& key) const;
    Json& operator[](const std::string& key);
    void Push(Json value);
 
    std::string Dump(int indent = -1) const;
    static bool Parse(std::string_view text, Json* out, std::string* error_out = nullptr);
 
private:
    void DumpTo(std::string& out, int indent, int depth) const;
 
    std::variant<std::monostate, bool, double, std::string, Array, Object> data_;
};
 
}  // namespace XER::NodeGraph