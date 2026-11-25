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
 *   dependencies-opensource[]   {package_name,url}  binary archives;
 *                               optional "mirrors": [url,...] tried in
 *                               order after `url`, and optional
 *                               "extract_map": [{from,to},...] that
 *                               flattens matching staging subtrees into
 *                               <dep>/<to>/ after extraction (e.g. a
 *                               NuGet .nupkg's runtimes/ tree).
 */
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace xer_sync_dev {

enum class DepKind { kGit, kBinary };

// One extract_map rule: the extracted subtree `from` (archive-relative,
// '/'-separated) is moved — contents flattened — into `<dep_dir>/<to>/`.
struct ExtractMapEntry {
    std::string from;   // e.g. "runtimes/win-x64/native/"
    std::string to;     // e.g. "win-x64/"
};

struct Dependency {
    DepKind kind = DepKind::kGit;
    std::string name;   // "name" (github) or "package_name" (opensource)
    std::string url;
    std::vector<std::string> mirrors;          // kBinary only
    std::vector<ExtractMapEntry> extract_map;  // kBinary only
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

// Appends `dep` to the manifest file on disk, into the array matching
// `dep.kind` ("dependencies-from-github" / "dependencies-opensource").
// The edit is surgical: the `-- format: json` comment header and the
// existing formatting are preserved byte-for-byte; the new entry is
// emitted in the file's 4-space-indent style. `mirrors`/`extract_map`
// are only written when non-empty (kBinary entries).
// Fails when the target array can't be located or the file can't be
// rewritten; the file is left untouched on failure.
bool AppendDepToManifest(const std::filesystem::path& path,
                         const Dependency& dep, std::string* error);

// Derives a default dep name from a URL: basename minus a trailing
// ".git" (github) or a known archive extension (.nupkg/.zip/.tar.*)
// for opensource entries. "" when nothing sensible can be derived.
std::string DefaultNameFromUrl(const std::string& url, DepKind kind);

}  // namespace xer_sync_dev
