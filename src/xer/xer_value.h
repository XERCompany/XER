#pragma once

#include "config.h"
#include <absl/container/flat_hash_map.h>

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace Xer {

enum class XerValueType {
    kNull,
    kInteger,
    kString,
    kObject,
    kArray,
};

class XerValue {
public:
    using Object = absl::flat_hash_map<std::string, XerValue>;
    using Array = std::vector<XerValue>;

    XerValue();
    explicit XerValue(std::int64_t value);
    explicit XerValue(std::string value);
    explicit XerValue(const char* value);
    explicit XerValue(std::shared_ptr<Object> object);
    explicit XerValue(std::shared_ptr<Array> array);

    static XerValue MakeObject();
    static XerValue MakeArray();

    XerValueType type() const;
    bool IsNull() const;
    bool IsInteger() const;
    bool IsString() const;
    bool IsObject() const;
    bool IsArray() const;

    std::int64_t AsInteger(std::int64_t fallback = 0) const;
    const std::string& AsString() const;
    std::shared_ptr<Object> AsObject() const;
    std::shared_ptr<Array> AsArray() const;

    std::string ToString() const;

private:
    using Storage = std::variant<std::monostate, std::int64_t, std::string, std::shared_ptr<Object>, std::shared_ptr<Array>>;

    Storage data_;
};

}  // namespace Xer