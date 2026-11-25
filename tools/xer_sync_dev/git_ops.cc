/**
 * This file is part of XER, Tools open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "git_ops.h"

#include "common.h"
#include "process_util.h"

namespace xer_sync_dev {
namespace {

std::string FirstLine(std::string s) {
    const size_t nl = s.find('\n');
    if (nl != std::string::npos) s.resize(nl);
    return Trim(s);
}

ProcResult Git(const std::filesystem::path& dir, const std::string& args,
               bool merge = true) {
    return RunProcess("git -C " + ShellQuote(GenericPath(dir)) + " " + args,
                      merge);
}

}  // namespace

bool GitAvailable() {
    const ProcResult r = RunProcess("git --version", true);
    return r.ran && r.exit_code == 0;
}

GitProbe ProbeRepo(const std::filesystem::path& dir) {
    GitProbe p;
    p.git_available = GitAvailable();
    if (!p.git_available || !fs::is_directory(dir)) return p;

    // A vendored dir inside the outer repo would otherwise "detect" the
    // parent repository — git walks up. A dep only counts as its own
    // repo when it is the worktree toplevel itself.
    const ProcResult top = Git(dir, "rev-parse --show-toplevel", false);
    if (!top.ran || top.exit_code != 0) return p;
    std::error_code tec;
    const fs::path toplevel =
        fs::weakly_canonical(FirstLine(top.output), tec);
    const fs::path self = fs::weakly_canonical(dir, tec);
    if (tec || toplevel != self) {
        // Inside an outer worktree (vendored subtree): record the
        // outer repo's last-touch commit as the honest revision.
        const std::string vd = VendoredDescribe(dir);
        if (!vd.empty()) {
            p.vendored = true;
            p.describe = vd;
            p.head = vd.substr(vd.find('@') + 1);
        }
        return p;
    }

    const ProcResult head =
        Git(dir, "rev-parse --verify HEAD", false);
    if (head.ran && head.exit_code == 0) {
        p.is_repo = true;
        p.head = FirstLine(head.output);
    } else {
        return p;
    }

    const ProcResult desc = Git(dir, "describe --tags --always", false);
    if (desc.ran && desc.exit_code == 0) p.describe = FirstLine(desc.output);
    if (p.describe.empty()) {
        p.describe = p.head.size() > 10 ? p.head.substr(0, 10) : p.head;
    }

    const ProcResult remote =
        Git(dir, "remote get-url origin", false);
    if (remote.ran && remote.exit_code == 0) {
        p.remote_url = FirstLine(remote.output);
    }
    return p;
}

std::string VendoredDescribe(const std::filesystem::path& dir) {
    const ProcResult top = Git(dir, "rev-parse --show-toplevel", false);
    if (!top.ran || top.exit_code != 0) return {};
    const ProcResult r = Git(dir, "log -1 --format=%h -- .", false);
    if (!r.ran || r.exit_code != 0) return {};
    const std::string h = FirstLine(r.output);
    return h.empty() ? "" : "vendored@" + h;
}

bool GitClone(const std::string& url, const std::filesystem::path& dir,
              bool shallow, std::string* log) {
    std::string cmd = "git clone ";
    if (shallow) cmd += "--depth 1 ";
    cmd += ShellQuote(url) + " " + ShellQuote(GenericPath(dir));
    const ProcResult r = RunProcess(cmd, true);
    if (log) *log = r.output;
    return r.ran && r.exit_code == 0 && fs::is_directory(dir);
}

bool GitCloneRef(const std::string& url, const std::string& ref,
                 const std::filesystem::path& dir, std::string* log) {
    std::string out;
    auto run = [&out](const std::string& cmd) {
        const ProcResult r = RunProcess(cmd, true);
        out += r.output;
        return r.ran && r.exit_code == 0;
    };
    const std::string qdir = ShellQuote(GenericPath(dir));
    if (!run("git init --quiet " + qdir)) {
        if (log) *log = out;
        return false;
    }
    if (!run("git -C " + qdir + " remote add origin " +
             ShellQuote(url))) {
        if (log) *log = out;
        return false;
    }
    bool ok = run("git -C " + qdir +
                  " fetch --quiet --depth 1 origin " + ShellQuote(ref)) &&
              run("git -C " + qdir + " checkout --quiet --detach FETCH_HEAD");
    if (!ok) {
        // Server refused the direct ref fetch (no reachable-sha1
        // uploadpack permission) — widen to a full fetch and let git
        // resolve the ref name itself.
        ok = run("git -C " + qdir + " fetch --quiet origin") &&
             run("git -C " + qdir + " checkout --quiet --detach " +
                 ShellQuote(ref));
    }
    if (log) *log = out;
    return ok && fs::is_directory(dir);
}

std::string GitResolveRef(const std::filesystem::path& dir,
                          const std::string& ref) {
    // "^{commit}" goes inside the quotes — an unquoted ^ is cmd.exe's
    // escape char and gets swallowed before git sees it.
    const ProcResult r =
        Git(dir, "rev-parse --verify --quiet " +
                     ShellQuote(ref + "^{commit}"), false);
    if (!r.ran || r.exit_code != 0) return {};
    return FirstLine(r.output);
}

bool GitCheckout(const std::filesystem::path& dir,
                 const std::string& ref, std::string* log) {
    const ProcResult r = Git(dir, "checkout --quiet --detach " +
                                      ShellQuote(ref), true);
    if (log) *log = r.output;
    return r.ran && r.exit_code == 0;
}

bool GitFetch(const std::filesystem::path& dir, std::string* log) {
    const ProcResult r = Git(dir, "fetch --quiet origin", true);
    if (log) *log = r.output;
    return r.ran && r.exit_code == 0;
}

int CountBehindUpstream(const std::filesystem::path& dir) {
    const ProcResult r =
        Git(dir, "rev-list --count HEAD..FETCH_HEAD", false);
    if (!r.ran || r.exit_code != 0) return -1;
    try {
        return std::stoi(FirstLine(r.output));
    } catch (...) {
        return -1;
    }
}

std::string GitLsRemoteHead(const std::string& url) {
    const ProcResult r =
        RunProcess("git ls-remote " + ShellQuote(url) + " HEAD", false);
    if (!r.ran || r.exit_code != 0) return {};
    // Output: "<sha>\tHEAD"
    const std::string line = FirstLine(r.output);
    const size_t tab = line.find_first_of("\t ");
    return Trim(line.substr(0, tab));
}

}  // namespace xer_sync_dev
