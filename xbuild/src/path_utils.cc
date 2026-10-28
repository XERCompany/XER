#include "path_utils.h"
#include <absl/strings/ascii.h>
#include <absl/strings/str_split.h>
#include <algorithm>

namespace XBuild {
std::string PathText(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}
std::filesystem::path AbsolutePath(const std::filesystem::path& root, const std::filesystem::path& path) {
    std::error_code ec;
    const auto absolute = (path.is_absolute() ? path : root / path).lexically_normal();
    auto canonical = std::filesystem::weakly_canonical(absolute, ec);
    return ec ? absolute : canonical;
}
bool IsWithin(const std::filesystem::path& path, const std::filesystem::path& root) {
    auto p = path.lexically_normal();
    auto r = root.lexically_normal();
    auto pi = p.begin();
    for (auto ri = r.begin(); ri != r.end(); ++ri, ++pi) {
        if (pi == p.end()) return false;
#ifdef _WIN32
        if (!absl::EqualsIgnoreCase(PathText(*pi), PathText(*ri))) return false;
#else
        if (*pi != *ri) return false;
#endif
    }
    return true;
}
bool IsSourceFile(const std::filesystem::path& path) {
    const auto ext = absl::AsciiStrToLower(path.extension().string());
    return ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx";
}
bool IsHeaderFile(const std::filesystem::path& path) {
    const auto ext = absl::AsciiStrToLower(path.extension().string());
    return ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx" || ext == ".inc";
}
bool EnsureDirectory(const std::filesystem::path& path, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (!ec) return true;
    error = "Cannot create " + PathText(path) + ": " + ec.message();
    return false;
}
std::vector<std::string> SplitPaths(const std::string& text) {
#ifdef _WIN32
    return absl::StrSplit(text, ';', absl::SkipEmpty());
#else
    return absl::StrSplit(text, ':', absl::SkipEmpty());
#endif
}
std::string SafeName(const std::string& name) {
    std::string result;
    for (unsigned char c : name) result += absl::ascii_isalnum(c) || c == '_' || c == '-' ? static_cast<char>(c) : '_';
    return result.empty() ? "target" : result;
}
}
