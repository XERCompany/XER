#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace XER::ModelsBuilder::Reader::Utils {

/// Path resolution helpers for model file loading.
class RmPathResolver {
public:
    /// Return absolute path; if relative, resolve against search_dirs in order.
    /// Returns empty string if not found.
    static std::string Resolve(const std::string& path,
                                const std::vector<std::string>& search_dirs = {});

    /// Return the extension in lowercase (e.g. ".onnx")
    static std::string Extension(const std::string& path);

    /// Return filename without extension
    static std::string Stem(const std::string& path);

    /// Return parent directory path
    static std::string ParentDir(const std::string& path);

    /// Join two path segments
    static std::string Join(const std::string& base, const std::string& rel);

    /// True if the path exists and is a regular file
    static bool Exists(const std::string& path);
};

}  // namespace XER::ModelsBuilder::Reader::Utils
