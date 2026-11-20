#pragma once

#include "config.h"
#include "xer/xer_ast.h"
#include "xer/xer_tokenzer.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Xer {

struct XerCompileResult {
    std::string normalized_source;
    std::vector<XerToken> tokens;
    XerAstProgram program;
};

class XerCompiler {
public:
    XerCompileResult CompileFromText(std::string_view source, std::string* error_out = nullptr) const;
    XerCompileResult CompileFromFile(const std::filesystem::path& path, std::string* error_out = nullptr) const;
};

}  // namespace Xer