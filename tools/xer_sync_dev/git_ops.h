/**
 * This file is part of XER, Tools open source project.
 * Copyright (C) 2026 Yoshi A.
 *
 * xer_sync_dev — git operations, implemented by shelling out to the
 * `git` CLI (must be on PATH; git-for-windows / MSYS2 both work).
 * This is deliberate: the repo has no libgit2 dependency and the CLI
 * gives honest clone/fetch semantics for free.
 */
#pragma once

#include <filesystem>
#include <string>

namespace xer_sync_dev {

struct GitProbe {
    bool git_available = false;
    bool is_repo = false;
    bool vendored = false;   // not a repo, but inside an outer worktree
    std::string head;        // full sha of HEAD ("" when none);
                             // for vendored dirs: outer repo sha
    std::string describe;    // `git describe --tags --always` ("" on fail);
                             // for vendored dirs: "vendored@<sha>"
    std::string remote_url;  // origin url ("" when none)
};

bool GitAvailable();

GitProbe ProbeRepo(const std::filesystem::path& dir);

// For a directory that is not itself a repo but lives inside an outer
// worktree: the outer repo's last commit touching that subtree is the
// honest "vendored at" revision. describe = "vendored@<sha>".
std::string VendoredDescribe(const std::filesystem::path& dir);

// Clones `url` into `dir` (must not exist). `shallow` adds --depth 1 —
// the default because some manifest deps (ffmpeg) are multi-GB clones.
// Fetch still works on shallow clones (they deepen on demand).
bool GitClone(const std::string& url, const std::filesystem::path& dir,
              bool shallow, std::string* log);

// `git -C dir fetch --quiet origin`; FETCH_HEAD is left for
// CountBehindUpstream().
bool GitFetch(const std::filesystem::path& dir, std::string* log);

// Commits on FETCH_HEAD not on HEAD after a fetch; -1 when unknown
// (e.g. shallow boundary already contains everything fetched).
int CountBehindUpstream(const std::filesystem::path& dir);

// `git ls-remote <url> HEAD` -> sha, "" on failure. Cheap remote probe
// used by --check-only-free status and auto mode's "cheap version
// check" when we deliberately avoid a full fetch.
std::string GitLsRemoteHead(const std::string& url);

}  // namespace xer_sync_dev
