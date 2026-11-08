#pragma once

#include <string>

namespace XER::JavaScript::Common {

class TextNormalizer {
public:
    static std::string NormalizeTopic(const std::string& value);
    static std::string NormalizeText(const std::string& value);
};

}  // namespace XER::JavaScript::Common
