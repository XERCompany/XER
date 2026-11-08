#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XER::JavaScript::Common {

struct TaskPayload {
    std::string topic;
    std::string text;
    std::vector<std::uint8_t> binary;
    std::uint64_t sequence = 0;
};

}  // namespace XER::JavaScript::Common
