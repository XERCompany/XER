#pragma once

#include <string>
#include <string_view>

namespace XER::flux::terminal {

std::string StripAnsi(std::string_view text);

}  // namespace XER::flux::terminal
