/**
 * This file is part of XER, Tools open source project.
 * Copyright (C) 2026 Yoshi A.
 *
 * xer_sync_dev — dependency source analysis.
 *
 * Two layers:
 *  1. Filesystem scan (always): enumerate headers/sources, guess the
 *     public include dirs, apply a platform filter (this tool targets
 *     Windows first), detect upstream CMake/autotools/meson files and
 *     generated-header templates.
 *  2. clang API pass (when built with XER_SYNC_DEV_HAVE_LIBCLANG):
 *     each public header is parsed as a translation unit through
 *     libclang (clang-c/Index.h); clang_getInclusions yields the real
 *     resolved include graph, which we classify into internal /
 *     inter-dep (another manifest dependency) / system buckets.
 *     The textual #include scan below is the documented fallback for
 *     builds without libclang — it resolves the same names against the
 *     deps' include dirs, less accurately but deterministically.
 *
 * Output feeds cmake_gen.cc, which emits the xer-<name>.cmake patch.
 */
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "common.h"
#include "manifest.h"

namespace xer_sync_dev {

struct DepAnalysis {
    std::string name;
    fs::path root;

    std::vector<std::string> include_dirs;      // rel. to root, "/"-sep
    std::vector<std::string> public_headers;    // rel. to root
    std::vector<std::string> sources;           // kept for this platform
    std::vector<std::string> excluded_sources;  // platform/test-filtered
    bool header_only = false;

    // Upstream build files.
    bool has_upstream_cmake = false;
    // "" -> CMakeLists.txt at dep root; otherwise the probed subdir
    // that carries the real upstream project (lz4 "build/cmake",
    // expat "expat", x265 "source", mpg123 "ports/cmake").
    std::string cmake_subdir;
    std::vector<std::string> upstream_lib_targets;  // add_library() names
    // Declared library kind per target (static/shared/interface/object/
    // module/normal) — lets the wrapper demote INTERFACE/OBJECT aliases
    // (lz4's `lz4` is an INTERFACE umbrella, not the real archive).
    std::map<std::string, std::string> upstream_lib_kinds;
    bool has_autotools = false;   // configure / configure.ac / Makefile.am
    bool has_meson = false;       // meson.build
    std::vector<std::string> config_templates;      // *.in / *.cmake etc.

    // Include graph edges, filled by libclang and/or the textual scan.
    // inter_dep_includes[other_dep_name] = resolved include count.
    std::map<std::string, int> inter_dep_includes;
    std::map<std::string, int> system_includes;     // <foo/bar.h> -> hits
    int internal_includes = 0;
    int unresolved_includes = 0;

    // Preprocessor guards observed in dep code (see analyzer.cc).
    std::map<std::string, int> config_macros;    // e.g. HAVE_*, *_STATIC_DEFINE
    std::map<std::string, int> platform_macros;  // _WIN32, __linux__, ...

    bool libclang_used = false;
    std::string warning;
};

// include_dirs_of_deps: dep name -> include dirs that other deps could
// resolve this dep's headers through (absolute paths). Used to
// classify inclusions as inter-dep edges and to pass -I to libclang
// so cross-dep #includes actually resolve.
struct AnalyzeOptions {
    std::map<std::string, std::vector<fs::path>> include_dirs_of_deps;
    std::vector<std::string> extra_include_dirs;  // extra -I roots
    std::string sysroot_include;                  // e.g. clang64/include
    int max_libclang_headers = 48;   // probe cap; textual scan covers all
    bool use_libclang = true;
};

// Scans `dep_root` and fills `*out`. Never fails hard — individual
// problems land in out->warning; returns false only when root is not
// a directory at all.
bool AnalyzeDep(const Dependency& dep, const fs::path& dep_root,
                const AnalyzeOptions& opts, DepAnalysis* out);

}  // namespace xer_sync_dev
