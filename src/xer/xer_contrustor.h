#pragma once

#include "config.h"
#include "xer/xer_ast.h"

#include <string>

namespace Xer {

class XerContrustor {
public:
    XerAstImport MakeImport(std::string module_name, std::string alias, XerSourceRange range) const;
    XerAstFunction MakeFunction(std::string name, bool is_async, XerSourceRange range) const;
    XerAstCall MakeCall(std::string callee, std::optional<std::string> receiver, XerSourceRange range) const;

    void AddLocal(XerAstFunction* function, std::string name) const;
    void AddCall(XerAstFunction* function, XerAstCall call) const;
    void AddTopLevelCall(XerAstProgram* program, XerAstCall call) const;
};

}  // namespace Xer