/**
 * This file is part of XER, Tools open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "commands.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
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

// CA bundle candidates for ResolveCaBundle(): the linked MSYS2
// OpenSSL curl has a compiled-in default path that's meaningless to a
// native Windows exe, so we probe real locations — msys roots, the
// POSIX spellings (harmless when missing) and a repo-local
// third_party/cacert.pem escape hatch.
std::vector<std::string> DefaultCaCandidates(const Manifest& m) {
    return {
        "C:/tools/msys64/usr/ssl/certs/ca-bundle.crt",
        "C:/tools/msys64/clang64/etc/ssl/certs/ca-bundle.crt",
        "C:/tools/msys64/etc/ssl/certs/ca-bundle.crt",
        "C:/Program Files/Git/usr/ssl/certs/ca-bundle.crt",
        "C:/Program Files/Git/mingw64/etc/ssl/certs/ca-bundle.crt",
        "/etc/ssl/certs/ca-bundle.crt",
        "/usr/ssl/certs/ca-bundle.crt",
        GenericPath(m.output_dir / "cacert.pem"),
    };
}

// RAII chdir — restored even when the guarded call throws/returns
// early. Needed because ArchiveManager::Extract pipes every entry
// through ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS: the destination it
// prepends must therefore be *relative*, with the CWD as the anchor.
struct ScopedCwd {
    fs::path prev;
    explicit ScopedCwd(const fs::path& to) {
        std::error_code ec;
        prev = fs::current_path(ec);
        fs::current_path(to, ec);
    }
    ~ScopedCwd() {
        std::error_code ec;
        fs::current_path(prev, ec);
    }
    ScopedCwd(const ScopedCwd&) = delete;
    ScopedCwd& operator=(const ScopedCwd&) = delete;
};

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
        if (d.toolchain) continue;  // compilers aren't include targets
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
                if (!d.ref.empty() && p.is_repo) {
                    rev += " [pin " + d.ref + "]";
                    const std::string resolved =
                        GitResolveRef(dir, d.ref);
                    if (resolved.empty()) {
                        pending.push_back("pin-fetch");
                    } else if (resolved != p.head) {
                        pending.push_back("re-pin");
                    }
                }
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
        if (!d.notes.empty()) {
            std::printf("%-14s %s\n", "", d.notes.c_str());
        }
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
            r.status = "missing — would clone " + d.url +
                       (d.ref.empty() ? "" : " @ " + d.ref);
            return r;
        }
        std::string log;
        bool cloned = false;
        // Shallow clones die often enough on flaky networks that one
        // immediate retry is cheap insurance; a failed clone leaves a
        // partial dir behind, so it is removed before the retry.
        for (int attempt = 1; attempt <= 2 && !cloned; ++attempt) {
            std::printf("  cloning %s%s -> %s ...%s\n", d.url.c_str(),
                        d.ref.empty() ? ""
                                      : (" @ " + d.ref).c_str(),
                        GenericPath(dir).c_str(),
                        attempt > 1 ? " (retry)" : "");
            cloned = d.ref.empty()
                         ? GitClone(d.url, dir, /*shallow=*/true, &log)
                         : GitCloneRef(d.url, d.ref, dir, &log);
            if (!cloned) fs::remove_all(dir, ec);
        }
        if (!cloned) {
            r.status = "CLONE FAILED";
            r.hard_error = true;
            std::cerr << log;
            return r;
        }
        const GitProbe p = ProbeRepo(dir);
        state->SetString(d.name, "kind", "github");
        state->SetString(d.name, "url", d.url);
        if (!d.ref.empty()) state->SetString(d.name, "ref", d.ref);
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

    // Pinned dep ("ref"): the pin wins over the update machinery — no
    // fetch/behind-count pass. Drift (HEAD != ref) is re-checked out.
    if (!d.ref.empty()) {
        state->SetString(d.name, "kind", "github");
        state->SetString(d.name, "url", d.url);
        state->SetString(d.name, "ref", d.ref);
        const std::string resolved = GitResolveRef(dir, d.ref);
        if (resolved == p.head) {
            state->SetString(d.name, "last_check", NowIso8601());
            state->SetInt(d.name, "pending_updates", 0);
            r.status = "pinned " + p.describe + " (ref " + d.ref + ")";
            return r;
        }
        if (o.check_only) {
            r.status = resolved.empty()
                           ? "pinned ref " + d.ref +
                                 " not fetched — would fetch + checkout"
                           : "pin drift — would re-checkout " + d.ref;
            return r;
        }
        if (resolved.empty()) {
            std::string log;
            const ProcResult fr = RunProcess(
                "git -C " + ShellQuote(GenericPath(dir)) +
                    " fetch --quiet --depth 1 origin " + ShellQuote(d.ref),
                true);
            if (!fr.ran || fr.exit_code != 0) {
                r.status = "PIN FETCH FAILED: " + FirstLineOf(fr.output);
                r.hard_error = true;
                return r;
            }
        }
        std::string log;
        if (GitCheckout(dir, d.ref, &log) ||
            GitCheckout(dir, "FETCH_HEAD", &log)) {
            const GitProbe np = ProbeRepo(dir);
            state->SetString(d.name, "head", np.head);
            state->SetString(d.name, "version", np.describe);
            state->SetString(d.name, "applied_version",
                             AppliedVersion(m, np));
            state->SetString(d.name, "last_check", NowIso8601());
            state->SetInt(d.name, "pending_updates", 0);
            r.status = "re-pinned to " + d.ref + " (" + np.describe + ")";
            return r;
        }
        r.status = "PIN CHECKOUT FAILED: " + FirstLineOf(log);
        r.hard_error = true;
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
    fs::remove_all(staging, ec);
    fs::create_directories(staging, ec);

    // URL chain: the canonical `url` first, then `mirrors` in order.
    std::vector<std::string> urls{d.url};
    urls.insert(urls.end(), d.mirrors.begin(), d.mirrors.end());

    fs::path tarball;
    std::string fname;
    std::string err;
    bool got = false;
    for (const std::string& u : urls) {
        std::string f = u.substr(u.find_last_of('/') + 1);
        const size_t q = f.find_first_of("?#");
        if (q != std::string::npos) f.resize(q);
        const fs::path cand = dl_dir / f;
        // A finished download gets a sibling ".done" marker; a bare
        // file may be a partial from a killed run — HttpDownload
        // resumes it via CURLOPT_RESUME_FROM_LARGE.
        const fs::path done_marker =
            fs::path(cand.string() + ".done");
        if (fs::is_regular_file(cand, ec) &&
            fs::is_regular_file(done_marker, ec)) {
            std::printf("  using cached %s\n",
                        GenericPath(cand).c_str());
            tarball = cand;
            fname = f;
            got = true;
            break;
        }

        std::printf("  downloading %s ...\n", u.c_str());
        DownloadOptions dopt;
        dopt.cacert = o.cacert;
        dopt.ca_candidates = DefaultCaCandidates(m);
        dopt.max_retries = 3;
        if (o.verbose) {
            dopt.progress = [](double total, double now) {
                if (total > 0) {
                    std::printf("\r    %.1f / %.1f MB", now / 1e6,
                                total / 1e6);
                    std::fflush(stdout);
                }
            };
        }
        if (!HttpDownload(u, GenericPath(cand), &err, dopt)) {
            if (o.verbose && dopt.progress) std::printf("\n");
            std::printf("  source failed: %s\n", err.c_str());
            continue;  // next mirror
        }
        if (o.verbose && dopt.progress) std::printf("\n");
        {
            std::ofstream mark(done_marker, std::ios::binary |
                                                std::ios::trunc);
            mark << NowIso8601() << "\n";
        }
        tarball = cand;
        fname = f;
        got = true;
        break;
    }
    if (!got) {
        r.status = "DOWNLOAD FAILED: " + err;
        r.hard_error = true;
        return r;
    }

    std::printf("  extracting -> %s ...\n",
                GenericPath(staging).c_str());
    int64_t extracted = 0;
    err.clear();
    {
        // Extract needs a *relative* dest (absolute paths are blocked
        // by SECURE_NOABSOLUTEPATHS) — anchor the CWD at the download
        // dir so "extract-<name>" lands in `staging`.
        const ScopedCwd cwd(dl_dir);
        if (!XER::Content::Archive::ArchiveManager::Extract(
                GenericPath(tarball), "extract-" + d.name, {},
                &extracted, &err)) {
            r.status = "EXTRACT FAILED: " + err;
            r.hard_error = true;
            return r;
        }
    }

    fs::create_directories(dir.parent_path(), ec);
    if (fs::exists(dir, ec)) fs::remove_all(dir, ec);

    if (!d.extract_map.empty()) {
        // NuGet-style payload: only the mapped subtrees land in the
        // dep dir (<dep>/<to>/...).
        if (!ApplyExtractMap(staging, dir, d.extract_map, &err)) {
            r.status = "EXTRACT-MAP FAILED: " + err;
            r.hard_error = true;
            return r;
        }
        fs::remove_all(staging, ec);
    } else {
        // Typical tarballs have one top-level dir; move it into place.
        fs::path payload = staging;
        {
            int nentries = 0;
            fs::path single;
            for (const auto& e : fs::directory_iterator(staging, ec)) {
                ++nentries;
                single = e.path();
            }
            if (nentries == 1 && fs::is_directory(single, ec)) {
                payload = single;
            }
        }
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

// Moves the extracted subtree `staging/<from>` (or
// `staging/<single-top-dir>/<from>`) — contents flattened — into
// `<dep_dir>/<to>/`, per entry. `from` missing in the staging tree is
// a hard error; the staging top-level listing is included for
// debugging.
bool ApplyExtractMap(const fs::path& staging, const fs::path& dep_dir,
                     const std::vector<ExtractMapEntry>& entries,
                     std::string* error) {
    auto fail = [error](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    auto norm = [](std::string s) {
        std::replace(s.begin(), s.end(), '\\', '/');
        while (!s.empty() && s.front() == '/') s.erase(s.begin());
        while (!s.empty() && s.back() == '/') s.pop_back();
        return s;
    };

    std::error_code ec;
    for (const auto& e : entries) {
        const std::string from = norm(e.from);
        const std::string to = norm(e.to);
        if (from.empty() || to.empty()) {
            return fail("ApplyExtractMap: empty from/to in entry '" +
                        e.from + "' -> '" + e.to + "'");
        }

        fs::path src = staging / fs::path(from);
        if (!fs::exists(src, ec)) {
            // Tarballs usually wrap everything in one top dir — probe
            // one level down before giving up.
            for (const auto& top : fs::directory_iterator(staging, ec)) {
                if (ec) break;
                if (!top.is_directory()) continue;
                const fs::path cand = top.path() / fs::path(from);
                if (fs::exists(cand, ec)) {
                    src = cand;
                    break;
                }
            }
        }
        if (!fs::exists(src, ec)) {
            std::string listing;
            std::error_code lec;
            for (const auto& t : fs::directory_iterator(staging, lec)) {
                listing += " " + t.path().filename().string();
            }
            if (listing.empty()) listing = " <empty>";
            return fail("ApplyExtractMap: '" + e.from +
                        "' not found under staging — top-level:" +
                        listing);
        }

        const fs::path dst_dir = dep_dir / fs::path(to);
        fs::create_directories(dst_dir, ec);
        if (ec) {
            return fail("ApplyExtractMap: cannot create " +
                        GenericPath(dst_dir) + ": " + ec.message());
        }

        if (fs::is_regular_file(src, ec)) {
            const fs::path dst = dst_dir / src.filename();
            fs::remove_all(dst, ec);
            ec.clear();
            fs::rename(src, dst, ec);
            if (ec) {
                ec.clear();
                fs::copy_file(
                    src, dst, fs::copy_options::overwrite_existing, ec);
                fs::remove(src, ec);
            }
            if (ec) {
                return fail("ApplyExtractMap: move " +
                            GenericPath(src) + " -> " +
                            GenericPath(dst) + ": " + ec.message());
            }
            continue;
        }

        for (const auto& child : fs::directory_iterator(src, ec)) {
            if (ec) break;
            const fs::path dst = dst_dir / child.path().filename();
            fs::remove_all(dst, ec);
            ec.clear();
            fs::rename(child.path(), dst, ec);
            if (ec) {
                // rename across dirs should be same-volume, but fall
                // back to a recursive copy so partial moves still win.
                ec.clear();
                fs::copy(child.path(), dst,
                         fs::copy_options::recursive |
                             fs::copy_options::overwrite_existing,
                         ec);
                if (ec) {
                    return fail("ApplyExtractMap: move " +
                                GenericPath(child.path()) + " -> " +
                                GenericPath(dst) + ": " + ec.message());
                }
                fs::remove_all(child.path(), ec);
            }
        }
    }
    return true;
}

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
    if (ao.sysroot_include.empty()) {
        // In-repo llvm-mingw toolchain is the preferred sysroot — its
        // headers keep system spellings from resolving into random dep
        // dirs (see analyzer -I ordering).
        std::error_code ec;
        const fs::path mm = m.output_dir / "llvm-mingw" / "include";
        if (fs::is_directory(mm, ec)) {
            ao.sysroot_include = GenericPath(mm);
        }
    }

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
        // Patch shape is content-sniffed — opensource-route deps can
        // land in any of the four emissions now (soxr -> wrapper,
        // sqlite3 -> static-lib, boost -> interface, cef -> shim).
        const char* shape =
            content.find("Binary distribution shim") !=
                    std::string::npos
                ? " [binary-shim]"
            : a.has_upstream_cmake ? " [wrapper]"
            : content.find("INTERFACE") != std::string::npos
                ? " [interface]"
                : " [static-lib]";
        *detail = GenericPath(written) +
                  (a.libclang_used ? " [libclang]" : " [fs-scan]") +
                  shape +
                  (a.cmake_subdir.empty()
                       ? ""
                       : " (cmake in " + a.cmake_subdir + ")");
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

// --- addfromgit / addopensource -----------------------------------------

namespace {

// Shared tail of both add commands: run the dep's full pipeline
// (clone or download+extract, then cmake-gen) and persist state.
int RunDepPipeline(const Manifest& m, const Dependency& d,
                   const Options& o) {
    SyncState state;
    std::string serr;
    state.Load(StatePathFor(m), &serr);
    if (!serr.empty()) std::cerr << "note: " << serr << "\n";

    const SyncOutcome r =
        d.kind == DepKind::kGit ? SyncGitDep(m, d, o, &state)
                                : SyncBinaryDep(m, d, o, &state);
    std::printf("%-14s %s\n", d.name.c_str(), r.status.c_str());
    int errors = r.hard_error ? 1 : 0;

    if (!r.hard_error) {
        std::string detail;
        if (RunCmakeGenForDep(m, d, o, &detail) == 0) {
            std::printf("%-14s %s\n", d.name.c_str(), detail.c_str());
        } else {
            std::printf("%-14s cmake-gen FAILED: %s\n", d.name.c_str(),
                        detail.c_str());
            ++errors;
        }
    }

    std::string werr;
    if (!state.Save(&werr)) std::cerr << "warning: " << werr << "\n";
    return errors ? 1 : 0;
}

int CmdAddImpl(const std::string& manifest_arg, const Options& o,
               DepKind kind) {
    const std::string manifest_path =
        manifest_arg.empty() ? "third_party/3RD.xersyunc" : manifest_arg;
    const std::string url = o.add_url;
    if (url.empty()) {
        std::fprintf(stderr, "add: url argument required\n");
        return 2;
    }
    const std::string name =
        o.dep_name.empty() ? DefaultNameFromUrl(url, kind) : o.dep_name;
    if (name.empty()) {
        std::fprintf(stderr,
                     "add: cannot derive a dep name from '%s' — pass "
                     "--name\n",
                     url.c_str());
        return 2;
    }

    Manifest m;
    std::string err;
    if (!LoadManifest(manifest_path, &m, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 2;
    }

    const Dependency* dep = FindDep(m, name);
    if (dep) {
        if (dep->kind != kind || dep->url != url) {
            std::fprintf(stderr,
                         "add: dep '%s' already in manifest with a "
                         "different kind/url — not overwriting\n",
                         name.c_str());
            return 2;
        }
        std::printf("dep '%s' already in manifest — running pipeline\n",
                    name.c_str());
    } else {
        Dependency d;
        d.kind = kind;
        d.name = name;
        d.url = url;
        if (kind == DepKind::kGit) d.ref = o.ref;
        if (kind == DepKind::kBinary) d.extract_map = o.extract_map;
        if (!AppendDepToManifest(m.manifest_path, d, &err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
        std::printf("manifest: appended '%s' to %s\n", name.c_str(),
                    GenericPath(m.manifest_path).c_str());
        // Re-read so `dep` points at the on-disk truth.
        Manifest m2;
        if (!LoadManifest(manifest_path, &m2, &err)) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 2;
        }
        m = std::move(m2);
        dep = FindDep(m, name);
        if (!dep) {
            std::fprintf(stderr,
                         "add: appended dep '%s' but reload can't see "
                         "it\n",
                         name.c_str());
            return 1;
        }
    }
    return RunDepPipeline(m, *dep, o);
}

}  // namespace

int CmdAddFromGit(const std::string& manifest_arg, const Options& o) {
    return CmdAddImpl(manifest_arg, o, DepKind::kGit);
}

int CmdAddOpenSource(const std::string& manifest_arg, const Options& o) {
    return CmdAddImpl(manifest_arg, o, DepKind::kBinary);
}

}  // namespace xer_sync_dev
