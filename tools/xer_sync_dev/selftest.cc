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
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "analyzer.h"
#include "cmake_gen.h"
#include "commands.h"
#include "common.h"
#include "manifest.h"
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
                "  \"dependencies-from-github\": [\n"
                "    {\"name\": \"minilib\", \"url\": "
                "\"https://example.invalid/minilib\"},\n"
                "    {\"name\": \"otherhdr\", \"url\": "
                "\"https://example.invalid/otherhdr\"}\n"
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
