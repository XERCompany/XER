#pragma once

#include "config.h"
#include "helper/tool_to.h"
#include "xer/xer_tokenzer.h"

#include <filesystem>
#include <string>

namespace Xer {

class XerUtil {
public:
    static std::string ReadSourceFile(const std::filesystem::path& path, std::string* error_out = nullptr);
    static std::string ExtensionOf(const std::filesystem::path& path);
    static bool IsXerPath(const std::filesystem::path& path);
    static std::string FormatLocation(const XerSourceLocation& location);
};

}  // namespace Xer