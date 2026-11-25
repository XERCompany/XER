/**
 * This file is part of XER, Tools open source project.
 * Copyright (C) 2026 Yoshi A.
 *
 * xer_sync_dev — .xersync manifest model + parser.
 *
 * Manifest format (see third_party/3RD.xersyunc): JSON, optionally
 * prefixed with `-- format: json` style comment lines (any line whose
 * first non-space chars are `--` or `//` is stripped before parsing).
 *
 *   automatic_update                                bool
 *   automatic_update_interval_days                  int  (default 7)
 *   automatic_update_check_on_start                 bool
 *   automatic_create_patch_for_cmake_all_dependencies bool
 *   prefix_new_version_all_dependencies             string ("XER-")
 *   output_directory                                string ("third_party")
 *   dependencies-from-github[]  {name, url}         git-cloned deps
 *   dependencies-opensource[]   {package_name,url}  binary tarballs
 */
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace xer_sync_dev {

enum class DepKind { kGit, kBinary };

struct Dependency {
    DepKind kind = DepKind::kGit;
    std::string name;   // "name" (github) or "package_name" (opensource)
    std::string url;
};

struct Manifest {
    bool automatic_update = false;
    int automatic_update_interval_days = 7;
    bool automatic_update_check_on_start = false;
    bool automatic_create_patch_for_cmake_all_dependencies = false;
    std::string prefix_new_version_all_dependencies = "XER-";
    std::string output_directory = "third_party";

    std::vector<Dependency> deps;

    // Derived at load time:
    std::filesystem::path manifest_path;   // absolute path of the file
    std::filesystem::path output_dir;      // output_directory resolved
                                           // against the manifest dir
};

// Reads + parses `path`. `output_dir` resolves relative to the
// manifest's own directory so the tool works from any CWD.
bool LoadManifest(const std::filesystem::path& path, Manifest* out,
                  std::string* error);

// Convenience: all deps filtered by kind.
std::vector<const Dependency*> DepsOfKind(const Manifest& m, DepKind k);

const Dependency* FindDep(const Manifest& m, const std::string& name);

}  // namespace xer_sync_dev
