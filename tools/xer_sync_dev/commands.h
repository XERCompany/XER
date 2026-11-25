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
};

int CmdStatus(const Manifest& m, const Options& o);
int CmdSync(const Manifest& m, const Options& o);
int CmdCmakeGen(const Manifest& m, const Options& o);
int CmdVerify(const Manifest& m, const Options& o);
int CmdAuto(const Manifest& m, const Options& o);
int CmdSelftest(const Options& o);

// Shared helpers exposed for selftest.
std::map<std::string, std::vector<fs::path>> BuildIncludeDirsOfDeps(
    const Manifest& m);
fs::path DepDir(const Manifest& m, const Dependency& d);
fs::path PatchPath(const Manifest& m, const Dependency& d);
int RunCmakeGenForDep(const Manifest& m, const Dependency& d,
                      const Options& o, std::string* detail);

}  // namespace xer_sync_dev
