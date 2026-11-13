#include "watch/watch_file_scanner.h"
#include "helper/string_utils.h"

#include <re2/re2.h>
#include <range/v3/algorithm/sort.hpp>

#include <algorithm>
#include <cctype>
#include <system_error>

WatchFileScanner::WatchFileScanner(std::filesystem::path root)
    : root_(std::move(root)) {}

std::vector<WatchFileSnapshot> WatchFileScanner::Scan() const {
    std::vector<WatchFileSnapshot> out;
    std::error_code ec;

    if (root_.empty() || !std::filesystem::exists(root_, ec)) {
        return out;
    }

    ec.clear();
    if (std::filesystem::is_regular_file(root_, ec)) {
        if (!ec && IsWatchedExtension(root_)) {
            out.push_back(WatchFileSnapshot::Capture(root_));
        }
        return out;
    }

    std::filesystem::recursive_directory_iterator it(
        root_,
        std::filesystem::directory_options::skip_permission_denied,
        ec);
    std::filesystem::recursive_directory_iterator end;

    if (ec) {
        return out;
    }

    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }

        const auto& entry = *it;
        if (!entry.is_regular_file(ec)) {
            ec.clear();
            continue;
        }

        const auto p = entry.path();
        if (IsWatchedExtension(p)) {
            out.push_back(WatchFileSnapshot::Capture(p));
        }
    }

    ranges::sort(out, [](const WatchFileSnapshot& a, const WatchFileSnapshot& b) {
        return a.path.string() < b.path.string();
    });

    return out;
}

bool WatchFileScanner::IsWatchedExtension(const std::filesystem::path& path) {
    const std::string name = XER::Helper::String::ToLowerAscii(path.filename().string());
    static const re2::RE2 kIgnoredPattern(R"(\.flowcache\.|\.flowcache$)");
    if (RE2::PartialMatch(name, kIgnoredPattern)) {
        return false;
    }

    const std::string ext = XER::Helper::String::ToLowerAscii(path.extension().string());
    static const re2::RE2 kExtPattern(R"(\.(js|ts|mjs|cjs|json|xml))");
    return RE2::FullMatch(ext, kExtPattern);
}
