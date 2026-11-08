#pragma once
#include <vector>
#include <string>
#include <map>

namespace XER::ML::Preprocessing {

/// Simple whitespace/punctuation tokenizer for raw text
class PreprocessTextTokenizer {
public:
    /// Tokenize a string into lowercase word tokens
    std::vector<std::string> Tokenize(const std::string& text) const;
    /// Tokenize a batch
    std::vector<std::vector<std::string>> TokenizeBatch(
        const std::vector<std::string>& texts) const;
private:
    static bool IsSeparator(char c);
};

}  // namespace XER::ML::Preprocessing
