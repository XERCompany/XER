#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace XER::Engine::SQL::Content {

using Blob = std::vector<std::uint8_t>;
using Value = std::variant<std::monostate, std::int64_t, double, std::string, Blob>;

}  // namespace XER::Engine::SQL::Content