/**
 * This file is part of XER, Tools open source project.
 * Copyright (C) 2026 Yoshi A.
 *
 * xer_sync_dev — .xersync dependency-manifest dev utility.
 * See docs/wiki/architecture/xer-sync-dev.md for design notes.
 *
 *   xer_sync_dev status    <manifest> [--dep n] [--verbose]
 *   xer_sync_dev sync      <manifest> [--dep n] [--check-only] [--force]
 *   xer_sync_dev cmake-gen <manifest> [--dep n] [--force]
 *   xer_sync_dev verify    <manifest> [--dep n]
 *   xer_sync_dev auto      <manifest> [--dep n] [--fetch]
 *   xer_sync_dev selftest  [--keep]
 *   xer_sync_dev --addfromgit <url.git> [--name n] [--manifest m]
 *   xer_sync_dev --addopensource <url> [--name n] [--manifest m]
 *                [--extract-map <from>=<to>[;<from>=<to>...]]
 */
#include <cstdio>
#include <cstring>
#include <string>

#include "commands.h"
#include "manifest.h"

namespace {

void Usage() {
    std::printf(R"(xer_sync_dev — .xersync dependency manifest utility

usage: xer_sync_dev <command> [args]

commands:
  status    <manifest>   Per-dep presence / revision / check staleness /
                         pending actions report (no network).
  sync      <manifest>   Clone missing git deps (shallow), git fetch +
                         count upstream commits for present deps
                         (respects automatic_update_interval_days via
                         third_party/.xersync-state.json), download +
                         extract binary tarballs via libarchive.
  cmake-gen <manifest>   Analyze each dep's headers/sources (libclang
                         include-graph when available) and emit a
                         xer-<name>.cmake patch inside the dep dir
                         declaring target XER-<name>.
  verify    <manifest>   Static check of generated patches: paren
                         balance, command whitelist, referenced paths.
  auto      <manifest>   automatic_* policy run: cheap start check +
                         interval-gated update + patch generation.
                         Missing deps are only fetched with --fetch.
  selftest               Offline end-to-end test on a synthetic dep
                         tree in a temp dir (no manifest, no network).
  --addfromgit <url>     append {name,url} to dependencies-from-github,
                         then run that dep's pipeline (clone -> state
                         -> cmake-gen). Name defaults to the repo
                         basename minus .git.
  --addopensource <url>  append {package_name,url,extract_map?} to
                         dependencies-opensource, then download +
                         extract + cmake-gen + state.

options:
  --dep <name>        scope to a single dependency
  --check-only        report what sync would do; no network, no writes
  --fetch             (auto) allow cloning/downloading missing deps
  --force             ignore interval freshness / regenerate patches
  --extra-include <dir>  extra -I root for include resolution
  --cacert <path>     CA bundle for downloads (overrides probing and
                      CURLSSLOPT_NATIVE_CA)
  --manifest <path>   manifest for the add* commands (default
                      third_party/3RD.xersyunc)
  --name <name>       dep name for the add* commands
  --extract-map <spec>  opensource extract_map, "from=to;from=to;..."
  --verbose           more output
  --keep              (selftest) keep the temp workspace
  --help              this text

manifest: third_party/3RD.xersyunc (JSON; '--' comment lines stripped)
)");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--help") == 0 ||
        std::strcmp(argv[1], "-h") == 0 ||
        std::strcmp(argv[1], "help") == 0) {
        Usage();
        return argc < 2 ? 1 : 0;
    }

    const std::string cmd = argv[1];
    xer_sync_dev::Options opt;
    std::string manifest_arg;
    std::string manifest_opt;

    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--dep" && i + 1 < argc) {
            opt.dep_filter = argv[++i];
        } else if (a == "--check-only") {
            opt.check_only = true;
        } else if (a == "--fetch") {
            opt.fetch = true;
        } else if (a == "--force") {
            opt.force = true;
        } else if (a == "--verbose" || a == "-v") {
            opt.verbose = true;
        } else if (a == "--keep") {
            opt.keep = true;
        } else if (a == "--extra-include" && i + 1 < argc) {
            opt.extra_includes.push_back(argv[++i]);
        } else if (a == "--cacert" && i + 1 < argc) {
            opt.cacert = argv[++i];
        } else if (a == "--manifest" && i + 1 < argc) {
            manifest_opt = argv[++i];
        } else if (a == "--name" && i + 1 < argc) {
            opt.dep_name = argv[++i];
        } else if (a == "--extract-map" && i + 1 < argc) {
            // "from=to;from=to;..."
            for (const std::string& pair :
                 xer_sync_dev::SplitTokens(argv[++i], ";")) {
                const size_t eq = pair.find('=');
                if (eq == std::string::npos) {
                    std::fprintf(stderr,
                                 "--extract-map: entry '%s' lacks "
                                 "'='\n",
                                 pair.c_str());
                    return 2;
                }
                opt.extract_map.push_back(
                    {pair.substr(0, eq), pair.substr(eq + 1)});
            }
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "unknown option: %s\n", a.c_str());
            return 2;
        } else if (manifest_arg.empty()) {
            manifest_arg = a;
        } else {
            std::fprintf(stderr, "unexpected argument: %s\n", a.c_str());
            return 2;
        }
    }

    if (cmd == "selftest") {
        return xer_sync_dev::CmdSelftest(opt);
    }

    // The add* commands take a URL positional, not a manifest —
    // --manifest picks the file (default third_party/3RD.xersyunc).
    if (cmd == "--addfromgit" || cmd == "addfromgit" ||
        cmd == "--addopensource" || cmd == "addopensource") {
        if (manifest_arg.empty()) {
            std::fprintf(stderr, "%s: url argument required\n",
                         cmd.c_str());
            return 2;
        }
        opt.add_url = manifest_arg;
        return (cmd.find("git") != std::string::npos)
                   ? xer_sync_dev::CmdAddFromGit(manifest_opt, opt)
                   : xer_sync_dev::CmdAddOpenSource(manifest_opt, opt);
    }

    if (manifest_arg.empty()) {
        std::fprintf(stderr, "%s: manifest path required\n",
                     cmd.c_str());
        return 2;
    }

    xer_sync_dev::Manifest m;
    std::string err;
    if (!xer_sync_dev::LoadManifest(manifest_arg, &m, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 2;
    }

    if (!opt.dep_filter.empty() &&
        !xer_sync_dev::FindDep(m, opt.dep_filter)) {
        std::fprintf(stderr, "error: dep '%s' not in manifest\n",
                     opt.dep_filter.c_str());
        return 2;
    }

    if (cmd == "status") return xer_sync_dev::CmdStatus(m, opt);
    if (cmd == "sync") return xer_sync_dev::CmdSync(m, opt);
    if (cmd == "cmake-gen") return xer_sync_dev::CmdCmakeGen(m, opt);
    if (cmd == "verify") return xer_sync_dev::CmdVerify(m, opt);
    if (cmd == "auto") return xer_sync_dev::CmdAuto(m, opt);

    std::fprintf(stderr, "unknown command: %s\n", cmd.c_str());
    Usage();
    return 2;
}
