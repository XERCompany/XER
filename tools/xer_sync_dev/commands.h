/**
 * This file is part of XER, Tools open source project.
 * Copyright (C) 2026 Yoshi A.
 *
 * xer_sync_dev — subcommand implementations.
 */
#pragma once

#include <map>
#include <string>
#include <vector>

#include "common.h"
#include "manifest.h"

namespace xer_sync_dev {

struct Options {
    std::string dep_filter;              // --dep <name>
    bool check_only = false;             // --check-only (no net/no writes)
    bool fetch = false;                  // --fetch (auto: allow clones)
    bool force = false;                  // --force (regen / ignore interval)
    bool verbose = false;                // --verbose
    bool keep = false;                   // selftest: keep temp dir
    std::vector<std::string> extra_includes;  // --extra-include <dir>
    std::string cacert;                  // --cacert <path> (CAINFO)
    std::string dep_name;                // --name <n> (add* commands)
    std::string add_url;                 // positional url (add* commands)
    std::string ref;                     // --ref <tag|sha> (addfromgit)
    std::vector<ExtractMapEntry> extract_map;  // --extract-map f=t;...
};

int CmdStatus(const Manifest& m, const Options& o);
int CmdSync(const Manifest& m, const Options& o);
int CmdCmakeGen(const Manifest& m, const Options& o);
int CmdVerify(const Manifest& m, const Options& o);
int CmdAuto(const Manifest& m, const Options& o);
int CmdSelftest(const Options& o);

// --addfromgit / --addopensource: append the dep to the manifest file
// (`manifest_arg` may be a path or empty -> third_party/3RD.xersyunc),
// then run that dep's pipeline (clone or download+extract, then
// cmake-gen, then state). `o.add_url`/`o.dep_name`/`o.extract_map`
// carry the CLI payload.
int CmdAddFromGit(const std::string& manifest_arg, const Options& o);
int CmdAddOpenSource(const std::string& manifest_arg, const Options& o);

// Shared helpers exposed for selftest.
std::map<std::string, std::vector<fs::path>> BuildIncludeDirsOfDeps(
    const Manifest& m);
fs::path DepDir(const Manifest& m, const Dependency& d);
fs::path PatchPath(const Manifest& m, const Dependency& d);
int RunCmakeGenForDep(const Manifest& m, const Dependency& d,
                      const Options& o, std::string* detail);
// Moves extracted subtrees into the dep dir per an extract_map
// (post-extract flattening; see manifest.h). Exposed for selftest.
bool ApplyExtractMap(const fs::path& staging, const fs::path& dep_dir,
                     const std::vector<ExtractMapEntry>& entries,
                     std::string* error);

}  // namespace xer_sync_dev
