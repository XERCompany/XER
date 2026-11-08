#include "preprocess_text_tokenizer.h"
#include <cctype>

namespace XER::ML::Preprocessing {

bool PreprocessTextTokenizer::IsSeparator(char c) {
    return std::isspace(static_cast<unsigned char>(c)) ||
           std::ispunct(static_cast<unsigned char>(c));
}

std::vector<std::string> PreprocessTextTokenizer::Tokenize(
        const std::string& text) const {
    std::vector<std::string> tokens;
    std::string token;
    for (char c : text) {
        if (IsSeparator(c)) {
            if (!token.empty()) { tokens.push_back(token); token.clear(); }
        } else {
            token += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    if (!token.empty()) tokens.push_back(token);
    return tokens;
}

std::vector<std::vector<std::string>> PreprocessTextTokenizer::TokenizeBatch(
        const std::vector<std::string>& texts) const {
    std::vector<std::vector<std::string>> out;
    out.reserve(texts.size());
    for (const auto& t : texts) out.push_back(Tokenize(t));
    return out;
}

}  // namespace XER::ML::Preprocessing
