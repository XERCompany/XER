#include "text_normalizer.h"

#include <absl/strings/ascii.h>
#include <absl/strings/str_replace.h>
#include <absl/strings/strip.h>

#if ENGINE_HAS_ICU
#include <unicode/unistr.h>
#endif

namespace XER::JavaScript::Common {

namespace {

std::string CollapseWhitespace(const std::string& input) {
    std::string text(absl::StripAsciiWhitespace(input));
    text = absl::StrReplaceAll(text, {{"\t", " "}, {"\n", " "}, {"\r", " "}});

    std::string out;
    out.reserve(text.size());

    bool last_space = false;
    for (char c : text) {
        if (c == ' ') {
            if (!last_space) {
                out.push_back(c);
            }
            last_space = true;
            continue;
        }
        out.push_back(c);
        last_space = false;
    }
    return out;
}

}  // namespace

std::string TextNormalizer::NormalizeTopic(const std::string& value) {
    std::string normalized = CollapseWhitespace(value);

#if ENGINE_HAS_ICU
    icu::UnicodeString utext = icu::UnicodeString::fromUTF8(normalized);
    utext.toLower();
    std::string utf8;
    utext.toUTF8String(utf8);
    return utf8;
#else
    absl::AsciiStrToLower(&normalized);
    return normalized;
#endif
}

std::string TextNormalizer::NormalizeText(const std::string& value) {
    return CollapseWhitespace(value);
}

}  // namespace XER::JavaScript::Common
