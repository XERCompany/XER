/**
 * This file is part of XER, Tools open source project.
 * Copyright (C) 2026 Yoshi A.
 *
 * xer_sync_dev selftest — fully offline. Builds a synthetic dep tree
 * in a temp dir (one compiled lib "minilib" that #includes a header
 * of a header-only dep "otherhdr"), writes a synthetic manifest,
 * then exercises: manifest parse -> status -> cmake-gen -> verify ->
 * state round-trip. Exit 0 on all checks passing.
 */
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "analyzer.h"
#include "archive_manager.h"
#include "cmake_gen.h"
#include "commands.h"
#include "common.h"
#include "manifest.h"
#include "process_util.h"
#include "state.h"

namespace xer_sync_dev {
namespace {

int g_pass = 0, g_fail = 0;

void Check(bool cond, const std::string& what) {
    if (cond) {
        ++g_pass;
        std::printf("  PASS %s\n", what.c_str());
    } else {
        ++g_fail;
        std::printf("  FAIL %s\n", what.c_str());
    }
}

bool Write(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
    return static_cast<bool>(f);
}

}  // namespace

int CmdSelftest(const Options& o) {
    std::error_code ec;
    const fs::path root =
        fs::temp_directory_path(ec) /
        ("xer_sync_dev_selftest_" +
         std::to_string(std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count() %
                        1000000000));
    fs::remove_all(root, ec);
    const fs::path outdir = root / "third_party";

    std::printf("selftest workspace: %s\n", GenericPath(root).c_str());

    // --- synthetic deps -------------------------------------------------
    // otherhdr: header-only, include/otherhdr/other.h
    Check(Write(outdir / "otherhdr/include/otherhdr/other.h",
                "#ifndef OTHERHDR_OTHER_H\n"
                "#define OTHERHDR_OTHER_H\n"
                "inline int other_answer() { return 42; }\n"
                "#endif\n"),
          "write otherhdr header");

    // minilib: include/minilib/mini.h (includes otherhdr/other.h +
    //          <vector>), src/mini.cc, src/mini_linux.cc (must be
    //          platform-excluded), no CMakeLists.txt.
    Check(Write(outdir / "minilib/include/minilib/mini.h",
                "#pragma once\n"
                "#include <vector>\n"
                "#include <otherhdr/other.h>\n"
                "#ifdef MINILIB_ENABLE_EXTRAS\n"
                "int mini_extras();\n"
                "#endif\n"
                "int mini_value();\n"),
          "write minilib header");
    Check(Write(outdir / "minilib/src/mini.cc",
                "#include <minilib/mini.h>\n"
                "int mini_value() { return other_answer(); }\n"),
          "write minilib source");
    Check(Write(outdir / "minilib/src/mini_linux.cc",
                "// posix-only source — must be filtered out\n"
                "int mini_linux_only() { return 0; }\n"),
          "write minilib linux source");
    Check(Write(outdir / "minilib/tests/mini_test.cc",
                "int main() { return 1; }\n"),
          "write minilib test source (should be skipped)");

    // --- synthetic manifest ---------------------------------------------
    const fs::path mpath = root / "TEST.xersyunc";
    Check(Write(mpath,
                "-- format: json\n"
                "{\n"
                "  \"automatic_update\": true,\n"
                "  \"automatic_update_interval_days\": 7,\n"
                "  \"automatic_update_check_on_start\": true,\n"
                "  \"automatic_create_patch_for_cmake_all_dependencies\":"
                " true,\n"
                "  \"prefix_new_version_all_dependencies\": \"XER-\",\n"
                "  \"output_directory\": \"third_party\",\n"
                "  \"build_mode\": \"static\",\n"
                "  \"dependencies-from-github\": [\n"
                "    {\"name\": \"minilib\", \"url\": "
                "\"https://example.invalid/minilib\",\n"
                "     \"ref\": \"v1.2.3\",\n"
                "     \"notes\": \"test note\"},\n"
                "    {\"name\": \"otherhdr\", \"url\": "
                "\"https://example.invalid/otherhdr\",\n"
                "     \"build_mode\": \"shared\"}\n"
                "  ]\n"
                "}\n"),
          "write synthetic manifest");

    Manifest m;
    std::string err;
    Check(LoadManifest(mpath, &m, &err), "parse manifest" +
          (err.empty() ? "" : " (" + err + ")"));
    Check(m.deps.size() == 2, "manifest has 2 deps");
    Check(m.output_dir == fs::weakly_canonical(outdir),
          "output_directory resolved against manifest dir");
    Check(m.prefix_new_version_all_dependencies == "XER-",
          "prefix parsed");
    Check(m.build_mode == "static", "manifest build_mode parsed");
    {
        const Dependency* mini = FindDep(m, "minilib");
        const Dependency* oh = FindDep(m, "otherhdr");
        Check(mini && mini->ref == "v1.2.3", "dep ref parsed");
        Check(mini && mini->notes == "test note", "dep notes parsed");
        Check(oh && EffectiveBuildMode(m, *oh) == "shared",
              "per-dep build_mode overrides manifest");
        Check(mini && EffectiveBuildMode(m, *mini) == "static",
              "dep build_mode inherits manifest");
    }

    // --- status ----------------------------------------------------------
    std::printf("\n[status]\n");
    Check(CmdStatus(m, o) == 0, "status runs");

    // --- cmake-gen -------------------------------------------------------
    std::printf("\n[cmake-gen]\n");
    {
        Options gen = o;
        gen.force = true;
        std::string detail;
        const Dependency* mini = FindDep(m, "minilib");
        Check(mini != nullptr, "find dep minilib");
        Check(mini && RunCmakeGenForDep(m, *mini, gen, &detail) == 0,
              "cmake-gen minilib" + (detail.empty() ? "" : " — " + detail));

        const Dependency* oh = FindDep(m, "otherhdr");
        Check(oh && RunCmakeGenForDep(m, *oh, gen, &detail) == 0,
              "cmake-gen otherhdr" +
                  (detail.empty() ? "" : " — " + detail));
    }

    const fs::path mini_patch = outdir / "minilib/xer-minilib.cmake";
    const fs::path oh_patch = outdir / "otherhdr/xer-otherhdr.cmake";
    Check(fs::exists(mini_patch, ec), "xer-minilib.cmake exists");
    Check(fs::exists(oh_patch, ec), "xer-otherhdr.cmake exists");

    {
        std::ifstream in(mini_patch);
        const std::string body((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        Check(body.find("add_library(XER-minilib STATIC") !=
                  std::string::npos,
              "minilib emits STATIC target");
        Check(body.find("src/mini.cc") != std::string::npos,
              "minilib includes mini.cc");
        Check(body.find("mini_linux.cc") == std::string::npos ||
                  body.find("#   src/mini_linux.cc") != std::string::npos,
              "mini_linux.cc excluded or comment-listed");
        Check(body.find("XER-otherhdr") != std::string::npos,
              "inter-dep edge to otherhdr emitted");
        Check(body.find("MINILIB_ENABLE_EXTRAS") != std::string::npos,
              "config macro reported in header block");
    }
    {
        std::ifstream in(oh_patch);
        const std::string body((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        Check(body.find("add_library(XER-otherhdr INTERFACE)") !=
                  std::string::npos,
              "otherhdr emits INTERFACE target");
    }

    // --- verify -----------------------------------------------------------
    std::printf("\n[verify]\n");
    Check(CmdVerify(m, o) == 0, "verify all generated patches pass");

    {
        std::vector<std::string> problems;
        Check(VerifyPatch(mini_patch, &problems) && problems.empty(),
              "xer-minilib.cmake verifies clean");
    }

    // --- state round-trip --------------------------------------------------
    {
        SyncState st;
        std::string se;
        Check(st.Load(GenericPath(outdir / ".xersync-state.json"), &se),
              "state load (fresh)");
        st.SetString("minilib", "head", "deadbeef");
        st.SetString("minilib", "applied_version", "XER-deadbee");
        st.SetInt("minilib", "pending_updates", 3);
        st.set_last_auto_run(NowIso8601());
        Check(st.Save(&se), "state save" +
              (se.empty() ? "" : " (" + se + ")"));

        SyncState st2;
        Check(st2.Load(GenericPath(outdir / ".xersync-state.json"), &se),
              "state reload");
        Check(st2.GetString("minilib", "applied_version") ==
                  "XER-deadbee",
              "applied_version round-trips");
        Check(st2.GetInt("minilib", "pending_updates") == 3,
              "pending_updates round-trips");
        Check(DaysSinceIso(st2.last_auto_run()) == 0,
              "last_auto_run is today");
    }

    // --- manifest mutation round-trip ------------------------------------
    // --addfromgit/--addopensource append entries surgically; the file
    // must stay parseable and keep the `-- format: json` header.
    std::printf("\n[manifest append]\n");
    {
        Dependency g;
        g.kind = DepKind::kGit;
        g.name = "testgit";
        g.url = "https://example.invalid/testgit.git";
        g.ref = "deadbeef";
        g.notes = "hard: custom build";
        std::string ae;
        Check(AppendDepToManifest(mpath, g, &ae),
              "append github entry" + (ae.empty() ? "" : " — " + ae));

        Dependency b;
        b.kind = DepKind::kBinary;
        b.name = "testos";
        b.url = "https://example.invalid/testos.1.2.3.zip";
        b.mirrors = {"https://mirror.invalid/testos.1.2.3.zip"};
        b.extract_map = {{"runtimes/win-x64/native/", "win-x64/"},
                         {"build/native/include/", "include/"}};
        Check(AppendDepToManifest(mpath, b, &ae),
              "append opensource entry" +
                  (ae.empty() ? "" : " — " + ae));

        std::ifstream head(mpath);
        std::string first_line;
        std::getline(head, first_line);
        Check(Trim(first_line) == "-- format: json",
              "manifest keeps '-- format: json' header");

        Manifest m2;
        Check(LoadManifest(mpath, &m2, &ae),
              "manifest re-parses after appends" +
                  (ae.empty() ? "" : " — " + ae));
        Check(m2.deps.size() == 4, "manifest now has 4 deps");
        const Dependency* t = FindDep(m2, "testgit");
        Check(t && t->kind == DepKind::kGit &&
                  t->url == g.url &&
                  t->ref == "deadbeef" &&
                  t->notes == "hard: custom build",
              "github entry + ref + notes round-trips");
        const Dependency* t2 = FindDep(m2, "testos");
        Check(t2 && t2->kind == DepKind::kBinary &&
                  t2->mirrors.size() == 1 &&
                  t2->extract_map.size() == 2 &&
                  t2->extract_map[0].from ==
                      "runtimes/win-x64/native/" &&
                  t2->extract_map[0].to == "win-x64/",
              "opensource entry + mirrors + extract_map round-trip");
        Check(DefaultNameFromUrl("https://github.com/foo/bar.git",
                                 DepKind::kGit) == "bar" &&
                  DefaultNameFromUrl(
                      "https://x/microsoft.ml.onnxruntime.1.32.0.nupkg",
                      DepKind::kBinary) == "microsoft.ml.onnxruntime" &&
                  DefaultNameFromUrl("https://x/y.tar.bz2",
                                     DepKind::kBinary) == "y",
              "DefaultNameFromUrl git/nupkg/tar.bz2");
    }

    // --- extract_map on a locally-built zip ------------------------------
    // Mirrors the .nupkg flow: build a real zip with `cmake -E tar`,
    // "download" it via a file:// URL, extract with ArchiveManager,
    // then flatten via ApplyExtractMap.
    std::printf("\n[extract_map pipeline]\n");
    {
        const fs::path zsrc = root / "zipsrc";
        Check(Write(zsrc / "runtimes/win-x64/native/ort.dll",
                    "MZ-fake-dll"),
              "write fixture ort.dll");
        Check(Write(zsrc / "build/native/include/ort.h",
                    "#pragma once\n"),
              "write fixture ort.h");
        Check(Write(zsrc / "junk/skip.me", "x"), "write unmapped file");

        const fs::path zip = root / "pkg.zip";
        const ProcResult tar = RunProcess(
            "cd /d " + ShellQuote(GenericPath(zsrc)) +
                " && cmake -E tar cf " +
                ShellQuote(GenericPath(zip)) +
                " --format=zip -- runtimes build junk",
            true);
        Check(tar.ran && tar.exit_code == 0 &&
                  fs::is_regular_file(zip, ec),
              "cmake -E tar builds zip");

        const fs::path dldir = root / "dl";
        const fs::path dl = dldir / "pkg.zip";
        std::string derr;
        Check(HttpDownload("file://" + GenericPath(zip),
                           GenericPath(dl), &derr),
              "HttpDownload handles file:// url" +
                  (derr.empty() ? "" : " — " + derr));

        const fs::path staging = root / "staging";
        int64_t n = 0;
        std::string xerr;
        {
            // Extract needs a relative dest — anchor CWD at `root`.
            const fs::path prev_cwd = fs::current_path(ec);
            fs::current_path(root, ec);
            Check(XER::Content::Archive::ArchiveManager::Extract(
                      GenericPath(dl), "staging", {}, &n, &xerr) &&
                      n > 0,
                  "zip extracts" +
                      (xerr.empty() ? "" : " — " + xerr));
            fs::current_path(prev_cwd, ec);
        }
        Check(fs::is_regular_file(
                  staging / "runtimes/win-x64/native/ort.dll", ec),
              "staging has runtimes/ tree");

        const fs::path depdir = root / "third_party/onnxruntime";
        Check(ApplyExtractMap(
                  staging, depdir,
                  {{"runtimes/win-x64/native/", "win-x64/"},
                   {"build/native/include/", "include/"}},
                  &xerr),
              "ApplyExtractMap runs" +
                  (xerr.empty() ? "" : " — " + xerr));
        Check(fs::is_regular_file(depdir / "win-x64/ort.dll", ec),
              "flattened: <dep>/win-x64/ort.dll exists");
        Check(fs::is_regular_file(depdir / "include/ort.h", ec),
              "flattened: <dep>/include/ort.h exists");
        Check(!fs::exists(depdir / "junk", ec) &&
                  !fs::exists(depdir / "skip.me", ec),
              "unmapped subtree not copied");
        std::string merr;
        Check(!ApplyExtractMap(staging, depdir,
                               {{"does/not/exist/", "x/"}}, &merr) &&
                  !merr.empty(),
              "ApplyExtractMap errors on missing 'from'");
    }

    // --- CA bundle resolution --------------------------------------------
    std::printf("\n[ca resolution]\n");
    {
        const fs::path probe = root / "probe-ca.crt";
        Check(Write(probe, "-----BEGIN CERTIFICATE-----\n"),
              "write probe CA file");
        const std::string probe_s = GenericPath(probe);
        Check(ResolveCaBundle("", {"C:/nonexistent/ca.crt",
                                   probe_s}) == probe_s,
              "ResolveCaBundle picks first existing candidate");
        Check(ResolveCaBundle("C:/explicit/my-ca.pem", {probe_s}) ==
                  "C:/explicit/my-ca.pem",
              "explicit --cacert wins verbatim");

        // env override: CURL_CA_BUNDLE is honored before candidates.
        const fs::path envca = root / "env-ca.crt";
        Check(Write(envca, "-----BEGIN CERTIFICATE-----\n"),
              "write env CA file");
        const std::string env_s = GenericPath(envca);
#ifdef _WIN32
        _putenv_s("CURL_CA_BUNDLE", env_s.c_str());
#else
        setenv("CURL_CA_BUNDLE", env_s.c_str(), 1);
#endif
        Check(ResolveCaBundle("", {probe_s}) == env_s,
              "CURL_CA_BUNDLE env override wins over candidates");
#ifdef _WIN32
        _putenv_s("CURL_CA_BUNDLE", "");
#else
        unsetenv("CURL_CA_BUNDLE");
#endif
    }

    // --- download retry seam ---------------------------------------------
    // http://127.0.0.1:9 is the discard port — connection refused is
    // instant, and the attempts out-param exposes the retry plumbing.
    std::printf("\n[retry seam]\n");
    {
        const fs::path dest = root / "never.bin";
        DownloadOptions dopt;
        dopt.max_retries = 3;
        dopt.retry_backoff_ms = 1;
        int attempts = 0;
        dopt.attempts = &attempts;
        std::string derr;
        Check(!HttpDownload("http://127.0.0.1:9/xer_sync_dev_selftest",
                            GenericPath(dest), &derr, dopt) &&
                  attempts == 3,
              "retry loop runs max_retries attempts (attempts=" +
                  std::to_string(attempts) + ")");
        Check(!fs::exists(dest, ec), "no partial file left behind");
    }

    if (!o.keep) {
        fs::remove_all(root, ec);
    } else {
        std::printf("keeping workspace: %s\n",
                    GenericPath(root).c_str());
    }
    std::printf("\nselftest: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

}  // namespace xer_sync_dev
