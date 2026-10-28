#include "glob.h"
#include "path_utils.h"
#include "regex_filter.h"
#include <algorithm>
namespace XBuild {
bool ExpandGlob(const Project& project, const std::string& input, bool recursive,
                std::vector<fs::path>& files, std::string& error) {
    auto pattern = input;
    if (pattern.starts_with("<GLOB_RECURSE:") && pattern.ends_with(">")) {
        pattern = pattern.substr(14, pattern.size() - 15);
        recursive = true;
    }
    RegexFilter filter;
    const auto regex_at = pattern.find("<REGEX:");
    const bool filtered = regex_at != std::string::npos;
    fs::path directory;
    if (filtered) {
        if (!filter.Parse(pattern.substr(regex_at), error)) return false;
        directory = AbsolutePath(project.root, regex_at == 0 ? "." : pattern.substr(0, regex_at));
        recursive = true;
    } else {
        directory = AbsolutePath(project.root, pattern);
    }
    std::error_code ec;
    if (fs::is_regular_file(directory, ec)) {
        files.push_back(directory);
        return true;
    }
    if (!recursive || !fs::is_directory(directory, ec)) {
        error = "Source does not exist or is not a supported glob: " + PathText(directory);
        return false;
    }
    fs::recursive_directory_iterator iterator(directory, ec), end;
    while (!ec && iterator != end) {
        const auto path = iterator->path();
        if (iterator->is_directory(ec)) {
            const auto name = path.filename().string();
            if (IsWithin(AbsolutePath(project.root, path), project.build_dir) ||
                name == ".git" || name == ".deps" || name == "vcpkg_installed") iterator.disable_recursion_pending();
        } else if (iterator->is_regular_file(ec) && IsSourceFile(path) &&
                   (!filtered || filter.Matches(PathText(path.filename())))) {
            files.push_back(AbsolutePath(project.root, path));
        }
        iterator.increment(ec);
    }
    if (ec) { error = "Cannot enumerate " + PathText(directory) + ": " + ec.message(); return false; }
    std::sort(files.begin(), files.end());
    if (files.empty()) { error = "No sources matched: " + input; return false; }
    return true;
}
}
