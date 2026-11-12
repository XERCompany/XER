#pragma once

#include "config.h"
#include "helper/string_utils.h"

#include <absl/container/flat_hash_map.h>

#include <string>
#include <string_view>

namespace Xer {

class XerString {
public:
    static std::string NormalizeSource(std::string_view source, std::string* error_out = nullptr);
    static std::string ToLowerAscii(std::string_view text);
    static std::string ExpandTemplate(
        std::string_view format,
        const absl::flat_hash_map<std::string, std::string>& replacements);
};

}  // namespace Xer