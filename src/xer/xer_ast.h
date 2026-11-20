#pragma once

#include "config.h"
#include "xer/xer_tokenzer.h"
#include "xer/xer_value.h"

#include <optional>
#include <string>
#include <vector>

namespace Xer {

enum class XerAstNodeKind {
    kProgram,
    kImport,
    kFunction,
    kLocal,
    kCall,
    kLiteral,
};

struct XerAstImport {
    std::string module_name;
    std::string alias;
    XerSourceRange range;
};

struct XerAstCall {
    std::string callee;
    std::optional<std::string> receiver;
    XerSourceRange range;
};

struct XerAstFunction {
    std::string name;
    bool is_async = false;
    XerSourceRange range;
    std::vector<std::string> locals;
    std::vector<XerAstCall> calls;
};

struct XerAstProgram {
    std::vector<XerAstImport> imports;
    std::vector<XerAstFunction> functions;
    std::vector<XerAstCall> top_level_calls;
    XerValue metadata;
};

std::string XerAstNodeKindName(XerAstNodeKind kind);

}  // namespace Xer