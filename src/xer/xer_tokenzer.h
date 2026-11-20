#pragma once

#include "config.h"
#include "xer/xer_string.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Xer {

struct XerSourceLocation {
    std::size_t line = 1;
    std::size_t column = 1;
};

struct XerSourceRange {
    XerSourceLocation begin;
    XerSourceLocation end;
};

enum class XerTokenKind {
    kEndOfFile,
    kIdentifier,
    kNumber,
    kString,
    kImport,
    kAs,
    kAsync,
    kLocal,
    kThen,
    kEnd,
    kLParen,
    kRParen,
    kLBrace,
    kRBrace,
    kComma,
    kColon,
    kSemicolon,
    kEqual,
    kArrow,
};

struct XerToken {
    XerTokenKind kind = XerTokenKind::kEndOfFile;
    std::string text;
    XerSourceRange range;
};

std::string XerTokenKindName(XerTokenKind kind);

class XerTokenzer {
public:
    std::vector<XerToken> Tokenize(std::string_view source, std::string* error_out = nullptr) const;
};

}  // namespace Xer