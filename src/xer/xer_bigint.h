#pragma once

#include "config.h"
#include <cstdint>
#include <string>

namespace Xer {

class XerBigInt {
public:
    XerBigInt();
    explicit XerBigInt(std::string decimal);

    static XerBigInt FromString(const std::string& text, std::string* error_out = nullptr);

    bool FitsInt64() const;
    std::int64_t ToInt64(std::int64_t fallback = 0) const;
    const std::string& ToString() const { return decimal_; }

private:
    std::string decimal_;
};

}  // namespace Xer