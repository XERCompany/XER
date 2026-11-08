#include "rm_path_resolver.h"
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace XER::ModelsBuilder::Reader::Utils {

std::string RmPathResolver::Resolve(const std::string& path,
                                     const std::vector<std::string>& search_dirs) {
    fs::path p(path);
    if (p.is_absolute()) return fs::exists(p) ? p.string() : "";

    if (fs::exists(p)) return fs::absolute(p).string();

    for (const auto& dir : search_dirs) {
        fs::path candidate = fs::path(dir) / p;
        if (fs::exists(candidate)) return fs::absolute(candidate).string();
    }
    return "";
}

std::string RmPathResolver::Extension(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

std::string RmPathResolver::Stem(const std::string& path) {
    return fs::path(path).stem().string();
}

std::string RmPathResolver::ParentDir(const std::string& path) {
    return fs::path(path).parent_path().string();
}

std::string RmPathResolver::Join(const std::string& base, const std::string& rel) {
    return (fs::path(base) / rel).string();
}

bool RmPathResolver::Exists(const std::string& path) {
    return fs::exists(path) && fs::is_regular_file(path);
}

}  // namespace XER::ModelsBuilder::Reader::Utils
