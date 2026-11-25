/**
 * This file is part of XER, Tools open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "commands.h"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <vector>

#include "analyzer.h"
#include "archive_manager.h"
#include "cmake_gen.h"
#include "common.h"
#include "git_ops.h"
#include "process_util.h"
#include "state.h"

namespace xer_sync_dev {
namespace {

const char* KindName(DepKind k) {
    return k == DepKind::kGit ? "github" : "binary";
}

std::string StatePathFor(const Manifest& m) {
    return GenericPath(m.output_dir / ".xersync-state.json");
}

bool DepSelected(const Options& o, const Dependency& d) {
    return o.dep_filter.empty() || o.dep_filter == d.name;
}

// Candidate include dirs a dep exposes to the others (existence
// checked — cheap fs probes, used both for -I flags and edge match).
std::vector<fs::path> CandidateIncludeDirs(const fs::path& root,
                                           const std::string& name) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const fs::path& c : {root / "include", root / "inc",
                              root / "includes", root / "src", root,
                              root / "include" / name}) {
        if (fs::is_directory(c, ec)) out.push_back(c);
    }
    return out;
}

std::string FirstLineOf(const std::string& s) {
    const size_t nl = s.find('\n');
    return Trim(s.substr(0, nl));
}

std::string AppliedVersion(const Manifest& m, const GitProbe& p) {
    // For vendored dirs the applied version is the outer repo's sha —
    // "XER-<hash>" is shorter than "XER-vendored@<hash>".
    std::string v = p.vendored ? p.head : p.describe;
    if (v.empty() && !p.head.empty()) {
        v = p.head.substr(0, std::min<size_t>(10, p.head.size()));
    }
    if (v.empty()) v = "unknown";
    return m.prefix_new_version_all_dependencies + v;
}

void PrintDepLine(const Dependency& d, const std::string& status,
                  const std::string& rev, const std::string& staleness,
                  const std::string& pending) {
    std::printf("%-14s %-7s %-9s %-22s %-14s %s\n", d.name.c_str(),
                KindName(d.kind), status.c_str(), rev.c_str(),
                staleness.c_str(), pending.c_str());
}

}  // namespace

std::map<std::string, std::vector<fs::path>> BuildIncludeDirsOfDeps(
    const Manifest& m) {
    std::map<std::string, std::vector<fs::path>> out;
    for (const auto& d : m.deps) {
        const fs::path root = DepDir(m, d);
        std::error_code ec;
        if (fs::is_directory(root, ec)) {
            out[d.name] = CandidateIncludeDirs(root, d.name);
        }
    }
    return out;
}

fs::path DepDir(const Manifest& m, const Dependency& d) {
    return m.output_dir / d.name;
}

fs::path PatchPath(const Manifest& m, const Dependency& d) {
    return DepDir(m, d) / ("xer-" + d.name + ".cmake");
}

// --- status ------------------------------------------------------------

int CmdStatus(const Manifest& m, const Options& o) {
    SyncState state;
    std::string serr;
    state.Load(StatePathFor(m), &serr);
    if (!serr.empty()) std::cerr << "note: " << serr << "\n";

    std::printf("manifest : %s\n", GenericPath(m.manifest_path).c_str());
    std::printf("output   : %s\n", GenericPath(m.output_dir).c_str());
    std::printf("policy   : auto_update=%s interval=%dd check_on_start=%s "
                "auto_cmake_patch=%s prefix=%s\n",
                m.automatic_update ? "on" : "off",
                m.automatic_update_interval_days,
                m.automatic_update_check_on_start ? "on" : "off",
                m.automatic_create_patch_for_cmake_all_dependencies
                    ? "on" : "off",
                m.prefix_new_version_all_dependencies.c_str());
    std::printf("\n%-14s %-7s %-9s %-22s %-14s %s\n", "dep", "kind",
                "present", "upstream rev", "check", "pending actions");
    std::printf("%s\n", std::string(96, '-').c_str());

    int missing = 0;
    for (const auto& d : m.deps) {
        if (!DepSelected(o, d)) continue;
        const fs::path dir = DepDir(m, d);
        std::error_code ec;
        const bool present = fs::is_directory(dir, ec);

        std::string rev = "-", staleness = "-";
        std::vector<std::string> pending;

        if (!present) {
            pending.push_back(d.kind == DepKind::kGit ? "clone"
                                                      : "download");
            ++missing;
        } else {
            const GitProbe p = d.kind == DepKind::kGit
                                   ? ProbeRepo(dir)
                                   : GitProbe{};
            if (d.kind == DepKind::kGit) {
                rev = p.is_repo ? p.describe : "(not a git repo)";
            } else {
                rev = state.GetString(d.name, "version");
                if (rev.empty()) rev = "(binary)";
            }

            const std::string last = state.GetString(d.name, "last_check");
            const int days = DaysSinceIso(last);
            if (days < 0) {
                staleness = "never checked";
                pending.push_back("update-check");
            } else if (days >= m.automatic_update_interval_days) {
                staleness = "stale " + std::to_string(days) + "d";
                pending.push_back("update-check");
            } else {
                staleness = "fresh " + std::to_string(days) + "d";
            }

            const int pend = state.GetInt(d.name, "pending_updates", 0);
            if (pend > 0) {
                pending.push_back(std::to_string(pend) +
                                  " upstream commit(s)");
            }
            std::error_code ec2;
            if (!fs::exists(PatchPath(m, d), ec2)) {
                pending.push_back("cmake-gen");
            }
            const std::string av = state.GetString(d.name,
                                                   "applied_version");
            if (!av.empty()) rev += " [" + av + "]";
        }

        PrintDepLine(d, present ? "yes" : "NO", rev, staleness,
                     [&] {
                         std::string s;
                         for (size_t i = 0; i < pending.size(); ++i) {
                             s += (i ? ", " : "") + pending[i];
                         }
                         return s;
                     }());
    }
    std::printf("\n%d dep(s) missing from %s\n", missing,
                GenericPath(m.output_dir).c_str());
    return 0;
}

// --- sync --------------------------------------------------------------

namespace {

struct SyncOutcome {
    std::string status;   // one-liner for the report
    bool hard_error = false;
};

SyncOutcome SyncGitDep(const Manifest& m, const Dependency& d,
                       const Options& o, SyncState* state) {
    SyncOutcome r;
    const fs::path dir = DepDir(m, d);
    std::error_code ec;
    const bool present = fs::is_directory(dir, ec);

    if (!present) {
        if (o.check_only) {
            r.status = "missing — would clone " + d.url;
            return r;
        }
        std::string log;
        std::printf("  cloning %s -> %s ...\n", d.url.c_str(),
                    GenericPath(dir).c_str());
        if (!GitClone(d.url, dir, /*shallow=*/true, &log)) {
            r.status = "CLONE FAILED";
            r.hard_error = true;
            std::cerr << log;
            return r;
        }
        const GitProbe p = ProbeRepo(dir);
        state->SetString(d.name, "kind", "github");
        state->SetString(d.name, "url", d.url);
        state->SetString(d.name, "head", p.head);
        state->SetString(d.name, "version", p.describe);
        state->SetString(d.name, "applied_version",
                         AppliedVersion(m, p));
        state->SetString(d.name, "last_check", NowIso8601());
        state->SetInt(d.name, "pending_updates", 0);
        r.status = "cloned (" + p.describe + ") applied=" +
                   AppliedVersion(m, p);
        return r;
    }

    const GitProbe p = ProbeRepo(dir);
    if (!p.is_repo) {
        // Vendored subtree (no own .git) — nothing to fetch, but record
        // what we saw so status/state stay accurate.
        state->SetString(d.name, "kind", "github");
        state->SetString(d.name, "url", d.url);
        state->SetString(d.name, "last_check", NowIso8601());
        state->SetInt(d.name, "pending_updates", 0);
        if (p.vendored) {
            state->SetString(d.name, "head", p.head);
            state->SetString(d.name, "version", p.describe);
            state->SetString(d.name, "applied_version",
                             AppliedVersion(m, p));
            r.status = "vendored " + p.describe +
                       " — no repo metadata, fetch n/a";
        } else {
            r.status = "present but not a git repo — skipped";
        }
        return r;
    }

    const std::string last = state->GetString(d.name, "last_check");
    const int days = DaysSinceIso(last);
    const bool due = days < 0 ||
                     days >= m.automatic_update_interval_days ||
                     o.force;
    if (!due) {
        r.status = "fresh (checked " + std::to_string(days) +
                   "d ago) — skipped per interval";
        return r;
    }
    if (o.check_only) {
        r.status = "would fetch + count upstream commits";
        return r;
    }

    std::string log;
    if (!GitFetch(dir, &log)) {
        r.status = "fetch FAILED: " + FirstLineOf(log);
        r.hard_error = true;
        return r;
    }
    const int behind = CountBehindUpstream(dir);
    state->SetString(d.name, "last_check", NowIso8601());
    state->SetInt(d.name, "pending_updates", std::max(behind, 0));
    state->SetString(d.name, "head", p.head);
    state->SetString(d.name, "version", p.describe);
    state->SetString(d.name, "applied_version", AppliedVersion(m, p));
    if (behind > 0) {
        r.status = std::to_string(behind) +
                   " upstream commit(s) available (fetch done, pull is "
                   "a manual decision)";
    } else if (behind == 0) {
        r.status = "up to date";
    } else {
        r.status = "fetched (count n/a)";
    }
    return r;
}

SyncOutcome SyncBinaryDep(const Manifest& m, const Dependency& d,
                          const Options& o, SyncState* state) {
    SyncOutcome r;
    const fs::path dir = DepDir(m, d);
    std::error_code ec;
    if (fs::is_directory(dir, ec)) {
        state->SetString(d.name, "kind", "binary");
        state->SetString(d.name, "url", d.url);
        state->SetString(d.name, "last_check", NowIso8601());
        r.status = "present — binary deps are not update-checked";
        return r;
    }
    if (o.check_only) {
        r.status = "missing — would download " + d.url;
        return r;
    }

    const fs::path dl_dir = m.output_dir / ".xersync-downloads";
    const fs::path staging = dl_dir / ("extract-" + d.name);
    fs::create_directories(dl_dir, ec);
    fs::create_directories(staging, ec);

    std::string fname = d.url.substr(d.url.find_last_of('/') + 1);
    const size_t q = fname.find_first_of("?#");
    if (q != std::string::npos) fname.resize(q);
    const fs::path tarball = dl_dir / fname;

    if (!fs::exists(tarball, ec)) {
        std::printf("  downloading %s ...\n", d.url.c_str());
        std::string err;
        if (!HttpDownload(d.url, GenericPath(tarball), &err)) {
            r.status = "DOWNLOAD FAILED: " + err;
            r.hard_error = true;
            return r;
        }
    } else {
        std::printf("  using cached %s\n",
                    GenericPath(tarball).c_str());
    }

    std::printf("  extracting -> %s ...\n",
                GenericPath(staging).c_str());
    int64_t extracted = 0;
    std::string err;
    if (!XER::Content::Archive::ArchiveManager::Extract(
            GenericPath(tarball), GenericPath(staging), {}, &extracted,
            &err)) {
        r.status = "EXTRACT FAILED: " + err;
        r.hard_error = true;
        return r;
    }

    // Typical tarballs have one top-level dir; move it into place.
    fs::create_directories(dir.parent_path(), ec);
    fs::path payload = staging;
    {
        int entries = 0;
        fs::path single;
        for (const auto& e : fs::directory_iterator(staging, ec)) {
            ++entries;
            single = e.path();
        }
        if (entries == 1 && fs::is_directory(single, ec)) {
            payload = single;
        }
    }
    if (fs::exists(dir, ec)) fs::remove_all(dir, ec);
    if (payload == staging) {
        fs::rename(staging, dir, ec);
    } else {
        fs::rename(payload, dir, ec);
        fs::remove_all(staging, ec);
    }
    if (ec) {
        r.status = "MOVE FAILED: " + ec.message();
        r.hard_error = true;
        return r;
    }

    std::string ver = fname;
    state->SetString(d.name, "kind", "binary");
    state->SetString(d.name, "url", d.url);
    state->SetString(d.name, "version", ver);
    state->SetString(d.name, "applied_version",
                     m.prefix_new_version_all_dependencies + ver);
    state->SetString(d.name, "last_check", NowIso8601());
    r.status = "extracted " + std::to_string(extracted) +
               " entries -> " + GenericPath(dir);
    return r;
}

}  // namespace

int CmdSync(const Manifest& m, const Options& o) {
    SyncState state;
    std::string serr;
    state.Load(StatePathFor(m), &serr);
    if (!serr.empty()) std::cerr << "note: " << serr << "\n";

    if (!o.check_only && !GitAvailable() &&
        !DepsOfKind(m, DepKind::kGit).empty()) {
        std::cerr << "warning: git not found on PATH — git deps cannot "
                     "be cloned/fetched\n";
    }

    int errors = 0;
    for (const auto& d : m.deps) {
        if (!DepSelected(o, d)) continue;
        const SyncOutcome r =
            d.kind == DepKind::kGit ? SyncGitDep(m, d, o, &state)
                                    : SyncBinaryDep(m, d, o, &state);
        std::printf("%-14s %s\n", d.name.c_str(), r.status.c_str());
        if (r.hard_error) ++errors;
    }

    if (!o.check_only) {
        std::string werr;
        if (!state.Save(&werr)) {
            std::cerr << "warning: " << werr << "\n";
        }
    }
    std::printf("%s sync %s: %d dep(s), %d error(s)\n",
                o.check_only ? "[check-only]" : "", "done",
                static_cast<int>(o.dep_filter.empty() ? m.deps.size() : 1),
                errors);
    return errors ? 1 : 0;
}

// --- cmake-gen ---------------------------------------------------------

int RunCmakeGenForDep(const Manifest& m, const Dependency& d,
                      const Options& o, std::string* detail) {
    const fs::path dir = DepDir(m, d);
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        if (detail) *detail = "dep dir missing — sync it first";
        return 1;
    }

    AnalyzeOptions ao;
    ao.include_dirs_of_deps = BuildIncludeDirsOfDeps(m);
    ao.extra_include_dirs = o.extra_includes;
#ifdef XER_SYNC_DEV_SYSROOT_INCLUDE
    ao.sysroot_include = XER_SYNC_DEV_SYSROOT_INCLUDE;
#endif

    DepAnalysis a;
    AnalyzeDep(d, dir, ao, &a);

    GitProbe p;
    std::string rev = "binary";
    std::string binary_version;
    if (d.kind == DepKind::kGit) {
        p = ProbeRepo(dir);
        rev = p.describe.empty() ? "(no rev)" : p.describe;
    } else {
        // Version from the tarball filename: first \d+.\d+ run
        // (cef_binary_158.0.6+..._windows64.tar.bz2 -> "158.0.6").
        std::string fname = d.url.substr(d.url.find_last_of('/') + 1);
        static const std::regex ver_re(R"((\d+\.\d+[\d.]*))");
        std::smatch vm;
        if (std::regex_search(fname, vm, ver_re)) {
            binary_version = vm[1].str();
            rev = binary_version;
        }
    }

    GenContext ctx;
    ctx.manifest = &m;
    ctx.upstream_url = d.url;
    ctx.upstream_rev = rev;
    ctx.applied_version =
        d.kind == DepKind::kGit
            ? AppliedVersion(m, p)
            : m.prefix_new_version_all_dependencies +
                  (binary_version.empty() ? "binary" : binary_version);
    ctx.generated_at = NowIso8601();
    ctx.libclang_used = a.libclang_used;

    const std::string content = GeneratePatch(d, a, ctx);
    fs::path written;
    std::string err;
    if (!WritePatch(dir, d.name, content, &written, &err)) {
        if (detail) *detail = err;
        return 1;
    }
    if (detail) {
        *detail = GenericPath(written) +
                  (a.libclang_used ? " [libclang]" : " [fs-scan]") +
                  (a.has_upstream_cmake  ? " [wrapper]" :
                   a.header_only         ? " [interface]" :
                   d.kind == DepKind::kBinary ? " [binary-shim]"
                                              : " [static-lib]");
        if (!a.inter_dep_includes.empty()) {
            *detail += " edges:";
            for (const auto& [dd, n] : a.inter_dep_includes) {
                *detail += " " + dd;
            }
        }
        if (!a.warning.empty()) *detail += " warn:" + a.warning;
    }
    return 0;
}

int CmdCmakeGen(const Manifest& m, const Options& o) {
    int errors = 0, generated = 0;
    for (const auto& d : m.deps) {
        if (!DepSelected(o, d)) continue;
        const fs::path patch = PatchPath(m, d);
        std::error_code ec;
        // Bulk runs skip existing patches unless --force; an explicit
        // --dep request always regenerates.
        if (!o.force && o.dep_filter.empty() && fs::exists(patch, ec)) {
            if (o.verbose) {
                std::printf("%-14s patch exists — skipped (--force to "
                            "regenerate)\n", d.name.c_str());
            }
            continue;
        }
        std::string detail;
        const int rc = RunCmakeGenForDep(m, d, o, &detail);
        if (rc == 0) {
            ++generated;
            std::printf("%-14s %s\n", d.name.c_str(), detail.c_str());
        } else {
            ++errors;
            std::printf("%-14s FAILED: %s\n", d.name.c_str(),
                        detail.c_str());
        }
    }
    std::printf("cmake-gen: %d generated, %d skipped/failed\n",
                generated, errors);
    return errors ? 1 : 0;
}

// --- verify ------------------------------------------------------------

int CmdVerify(const Manifest& m, const Options& o) {
    int checked = 0, failed = 0, missing = 0;
    for (const auto& d : m.deps) {
        if (!DepSelected(o, d)) continue;
        const fs::path patch = PatchPath(m, d);
        std::error_code ec;
        if (!fs::exists(patch, ec)) {
            ++missing;
            std::printf("%-14s no patch (run cmake-gen)\n",
                        d.name.c_str());
            continue;
        }
        ++checked;
        std::vector<std::string> problems;
        const bool ok = VerifyPatch(patch, &problems);
        if (ok) {
            std::printf("%-14s OK  %s\n", d.name.c_str(),
                        GenericPath(patch).c_str());
        } else {
            ++failed;
            std::printf("%-14s FAIL %s\n", d.name.c_str(),
                        GenericPath(patch).c_str());
            for (const auto& p : problems) {
                std::printf("    - %s\n", p.c_str());
            }
        }
    }
    std::printf("verify: %d checked, %d failed, %d without patch\n",
                checked, failed, missing);
    return failed ? 1 : 0;
}

// --- auto --------------------------------------------------------------

int CmdAuto(const Manifest& m, const Options& o) {
    SyncState state;
    std::string serr;
    state.Load(StatePathFor(m), &serr);
    if (!serr.empty()) std::cerr << "note: " << serr << "\n";

    std::printf("auto: check_on_start=%s auto_update=%s interval=%dd "
                "auto_cmake_patch=%s\n",
                m.automatic_update_check_on_start ? "on" : "off",
                m.automatic_update ? "on" : "off",
                m.automatic_update_interval_days,
                m.automatic_create_patch_for_cmake_all_dependencies
                    ? "on" : "off");

    // 1) Cheap version check — what `check_on_start` means: report
    //    presence + recorded revision; no network.
    if (m.automatic_update_check_on_start) {
        std::printf("\n[start check]\n");
        for (const auto& d : m.deps) {
            if (!DepSelected(o, d)) continue;
            std::error_code ec;
            const bool present = fs::is_directory(DepDir(m, d), ec);
            const std::string ver =
                state.GetString(d.name, "applied_version");
            std::printf("  %-14s %s%s\n", d.name.c_str(),
                        present ? "present" : "MISSING",
                        ver.empty() ? "" : ("  " + ver).c_str());
        }
    }

    // 2) Interval-elapsed update pass.
    const int days = DaysSinceIso(state.last_auto_run());
    const bool interval_due =
        days < 0 || days >= m.automatic_update_interval_days;
    if (m.automatic_update) {
        if (!interval_due) {
            std::printf("\n[update] skipped — last run %dd ago "
                        "(interval %dd)\n", days,
                        m.automatic_update_interval_days);
        } else {
            std::printf("\n[update] interval elapsed — checking deps\n");
            for (const auto& d : m.deps) {
                if (!DepSelected(o, d)) continue;
                std::error_code ec;
                const bool present = fs::is_directory(DepDir(m, d), ec);
                if (!present) {
                    if (o.fetch && d.kind == DepKind::kGit) {
                        const SyncOutcome r =
                            SyncGitDep(m, d, o, &state);
                        std::printf("  %-14s %s\n", d.name.c_str(),
                                    r.status.c_str());
                    } else if (o.fetch) {
                        const SyncOutcome r =
                            SyncBinaryDep(m, d, o, &state);
                        std::printf("  %-14s %s\n", d.name.c_str(),
                                    r.status.c_str());
                    } else {
                        std::printf(
                            "  %-14s MISSING (clone needs --fetch)\n",
                            d.name.c_str());
                    }
                    continue;
                }
                if (d.kind == DepKind::kGit) {
                    const SyncOutcome r = SyncGitDep(m, d, o, &state);
                    std::printf("  %-14s %s\n", d.name.c_str(),
                                r.status.c_str());
                } else {
                    std::printf("  %-14s present (binary)\n",
                                d.name.c_str());
                }
            }
            state.set_last_auto_run(NowIso8601());
        }
    }

    // 3) automatic_create_patch_for_cmake_all_dependencies.
    if (m.automatic_create_patch_for_cmake_all_dependencies) {
        std::printf("\n[cmake patches]\n");
        for (const auto& d : m.deps) {
            if (!DepSelected(o, d)) continue;
            std::error_code ec;
            if (!fs::is_directory(DepDir(m, d), ec)) continue;
            std::string detail;
            if (RunCmakeGenForDep(m, d, o, &detail) == 0) {
                std::printf("  %-14s %s\n", d.name.c_str(),
                            detail.c_str());
            } else {
                std::printf("  %-14s FAILED: %s\n", d.name.c_str(),
                            detail.c_str());
            }
        }
    }

    std::string werr;
    if (!state.Save(&werr)) std::cerr << "warning: " << werr << "\n";
    std::printf("auto: done\n");
    return 0;
}

}  // namespace xer_sync_dev
