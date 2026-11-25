/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "packaging/dependency_scanner.h"

#include "packaging/pe_imports.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <map>
#include <set>

namespace XER::Packaging {

namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

// Look up a bare DLL name first beside the scanned binary, then in each
// search dir in order. filename() strips any directory part the import
// table may carry (import names are bare in practice; stay defensive).
bool ResolveDll(const std::string& name,
                const std::vector<std::filesystem::path>& dirs,
                std::filesystem::path* out_path, std::size_t* out_dir) {
    const std::filesystem::path fname =
        std::filesystem::path(name).filename();
    std::error_code ec;
    for (std::size_t i = 0; i < dirs.size(); ++i) {
        const std::filesystem::path cand = dirs[i] / fname;
        if (std::filesystem::exists(cand, ec) &&
            !std::filesystem::is_directory(cand, ec)) {
            *out_path = cand;
            *out_dir = i;
            return true;
        }
    }
    return false;
}

}  // namespace

bool ScanDependencies(const std::filesystem::path& exe,
                      const std::vector<std::filesystem::path>& search_dirs,
                      DependencyScanResult* out, std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) *error = "out parameter is null";
        return false;
    }
    *out = DependencyScanResult{};

    std::error_code ec;
    if (!std::filesystem::exists(exe, ec)) {
        if (error != nullptr)
            *error = "cannot read '" + exe.string() + "'";
        return false;
    }
    const std::filesystem::path exe_dir =
        std::filesystem::absolute(exe, ec).parent_path();
    out->exe_dir = exe_dir;

    // dir 0 = exe dir (resolved deps here are never re-copied by the
    // packager: they already sit next to the app).
    std::vector<std::filesystem::path> dirs{exe_dir};
    dirs.insert(dirs.end(), search_dirs.begin(), search_dirs.end());

    // BFS keyed by lowercase DLL name so LIBFOO.dll/libfoo.dll collapse.
    std::set<std::string> visited;
    visited.insert(ToLower(exe.filename().string()));
    std::deque<std::filesystem::path> worklist{exe};

    // name -> resolved entry (dedup keeps the first — i.e. highest
    // priority — resolution).
    std::map<std::string, ResolvedDependency> resolved;
    std::set<std::string> unresolved;

    bool first = true;
    while (!worklist.empty()) {
        const std::filesystem::path cur = worklist.front();
        worklist.pop_front();

        PeImportInfo info;
        std::string err;
        if (!ReadPeImports(cur, &info, &err, &out->warnings)) {
            if (first) {
                if (error != nullptr) *error = err;
                return false;
            }
            out->warnings.push_back("skipping " + err);
            continue;
        }
        first = false;

        std::set<std::string> delay_set;
        for (const std::string& n : info.delay_imports)
            delay_set.insert(ToLower(
                std::filesystem::path(n).filename().string()));

        std::vector<std::string> names = info.imports;
        names.insert(names.end(), info.delay_imports.begin(),
                     info.delay_imports.end());
        for (const std::string& name : names) {
            const std::string key =
                ToLower(std::filesystem::path(name).filename().string());
            if (key.empty() || visited.count(key)) continue;
            visited.insert(key);

            std::filesystem::path hit;
            std::size_t dir_index = 0;
            if (!ResolveDll(name, dirs, &hit, &dir_index)) {
                unresolved.insert(key);
                continue;
            }
            ResolvedDependency dep;
            dep.path = hit;
            dep.origin =
                dir_index == 0 ? DepOrigin::ExeDir : DepOrigin::SearchDir;
            dep.delay_loaded = delay_set.count(key) != 0;
            resolved.emplace(key, std::move(dep));
            worklist.push_back(hit);
        }
    }

    out->dlls.reserve(resolved.size());
    for (auto& [key, dep] : resolved) out->dlls.push_back(std::move(dep));
    out->unresolved.assign(unresolved.begin(), unresolved.end());
    return true;
}

}  // namespace XER::Packaging
