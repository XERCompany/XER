#pragma once

#include <string>

namespace XER::AI::SDBase {

class SdXlPromptExpander {
public:
    std::string Expand(const std::string& prompt) const;
};

}  // namespace XER::AI::SDBase
