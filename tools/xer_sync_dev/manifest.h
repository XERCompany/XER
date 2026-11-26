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
 *   build_mode                                      string ("static"):
 *                               default build flavor applied to every dep
 *                               that lacks its own "build_mode" override.
 *                               "static" and "shared" force
 *                               BUILD_SHARED_LIBS OFF/ON (scoped, saved +
 *                               restored) around the add_subdirectory() of
 *                               an upstream-CMake wrapper patch and pick
 *                               the STATIC/SHARED keyword for
 *                               source-list-generated libraries.
 *                               "default" leaves upstream's own choice
 *                               untouched. "interface" forces a
 *                               header-only INTERFACE target even when
 *                               the dep tree carries sources (boost).
 *                               Header-only and binary deps are
 *                               unaffected.
 *   dependencies-from-github[]  {name, url}         git-cloned deps;
 *                               optional "ref" pins the checkout (tag,
 *                               branch or commit sha — sync fetches the
 *                               ref shallowly and checks it out detached;
 *                               a pinned dep never gets the update-count
 *                               pass, pin drift is reported/re-pinned),
 *                               optional "build_mode" overrides the
 *                               manifest default, optional "notes" is a
 *                               free-form annotation surfaced by status
 *                               (used to flag 'hard' deps needing custom
 *                               build scripts).
 *   dependencies-opensource[]   {package_name,url}  binary archives;
 *                               optional "mirrors": [url,...] tried in
 *                               order after `url`, optional
 *                               "extract_map": [{from,to},...] that
 *                               flattens matching staging subtrees into
 *                               <dep>/<to>/ after extraction (e.g. a
 *                               NuGet .nupkg's runtimes/ tree), and the
 *                               same optional "build_mode"/"notes" fields
 *                               as github deps.
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
    std::string ref;    // kGit only: pin — tag, branch or commit sha.
                        // Clones fetch the ref shallowly and check it out
                        // detached; sync reports and re-pins drift instead
                        // of counting upstream commits.
    std::string notes;  // free-form annotation (surfaced by `status`);
                        // used to flag 'hard' deps that need custom build
                        // scripts — see msys2-exit-plan.md route H.
    bool toolchain = false;  // compiler/tool package (llvm-mingw,
                             // w64devkit, pkg-config-lite): excluded
                             // from inter-dep include scanning so every
                             // other dep doesn't edge against it.
    std::string build_mode;  // "" -> inherit Manifest::build_mode;
                             // "static"|"shared"|"default".
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
    // Default build flavor for cmake-gen ("static" per the MSYS2-exit
    // directive — every dep builds as a static lib). See the file-top
    // comment for the accepted values.
    std::string build_mode = "static";

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

// dep.build_mode when set, else the manifest-level default
// ("static"). "static"/"shared"/"default" are the meaningful values —
// anything else is treated as "default".
std::string EffectiveBuildMode(const Manifest& m, const Dependency& d);

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
