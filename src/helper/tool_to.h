#pragma once

#include <filesystem>
#include <string>

namespace XER::Engine::Helper {

class ToolTo {
public:
    static bool CopyBinary(const std::filesystem::path& source,
                           const std::filesystem::path& destination,
                           std::string* error_message);

    static std::string ReadTextFile(const std::filesystem::path& path);
    static bool WriteTextFile(const std::filesystem::path& path,
                              const std::string& text,
                              bool append,
                              std::string* error_message);
};

}

using ToolTo = XER::Engine::Helper::ToolTo;