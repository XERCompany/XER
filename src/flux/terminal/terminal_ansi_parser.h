#pragma once

#include <string>
#include <string_view>

namespace XER::Flux::Terminal {

std::string StripAnsi(std::string_view text);

}  // namespace XER::Flux::Terminal
