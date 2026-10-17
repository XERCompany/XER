#pragma once
 
#include "node_graph/node_graph_json.h"
 
#include <string>
#include <variant>
#include <vector>
 
namespace XER::NodeGraph {
 
enum class ValueType { kAny, kNumber, kBool, kString, kArray };
 
const char* ValueTypeName(ValueType type);
ValueType ValueTypeFromName(const std::string& name);
 
// Data-flow compatibility: any <-> *, number <-> bool, number|bool -> string.
bool AreTypesCompatible(ValueType from, ValueType to);
 
class Value {
public:
    using Array = std::vector<double>;
 
    Value() = default;
    Value(double v) : data_(v) {}
    Value(int v) : data_(static_cast<double>(v)) {}
    Value(bool v) : data_(v) {}
    Value(const char* v) : data_(std::string(v)) {}
    Value(std::string v) : data_(std::move(v)) {}
    Value(Array v) : data_(std::move(v)) {}
 
    bool IsNone() const { return std::holds_alternative<std::monostate>(data_); }
    ValueType Type() const;
 
    double AsNumber(double fallback = 0.0) const;
    bool AsBool(bool fallback = false) const;
    std::string AsString() const;
    Array AsArray() const;
 
    // Coerces to the requested type following the compatibility rules.
    Value ConvertTo(ValueType type) const;
 
    Json ToJson() const;
    static Value FromJson(const Json& json);
 
    bool operator==(const Value& other) const { return data_ == other.data_; }
    bool operator!=(const Value& other) const { return !(*this == other); }
 
private:
    std::variant<std::monostate, double, bool, std::string, Array> data_;
};
 
}  // namespace XER::NodeGraph