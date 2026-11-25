/**
 * This file is part of XER, Tools open source project.
 * Copyright (C) 2026 Yoshi A.
 *
 * xer_sync_dev — per-dep cmake patch generation.
 * Emits xer-<name>.cmake INSIDE the dep directory (a patch file that
 * lives alongside the upstream tree; upstream files are never
 * modified). Consumers include() it; it declares target XER-<name>.
 *
 * Shapes:
 *  - upstream ships CMakeLists.txt        -> thin add_subdirectory
 *    wrapper + XER-<name> ALIAS of the detected upstream target.
 *  - header-only dep (fp16, fxdiv, ...)   -> INTERFACE target.
 *  - sources but no build files           -> STATIC target with the
 *    platform-filtered file list + include dirs + inter-dep links.
 *  - binary tarball dep (cef, ...)        -> IMPORTED/interface shim
 *    with XER_<NAME>_ROOT / _LIB_DIR hints.
 *  - autotools-only dep (ffmpeg, libxml2) -> STATIC skeleton + a
 *    prominent "needs configure" warning; clang analysis cannot
 *    produce generated headers (config.h) — documented gap.
 */
#pragma once

#include <string>

#include "analyzer.h"
#include "common.h"
#include "manifest.h"

namespace xer_sync_dev {

struct GenContext {
    const Manifest* manifest = nullptr;
    std::string upstream_url;
    std::string upstream_rev;      // git describe / sha / "binary"
    std::string applied_version;   // e.g. "XER-4dfe081c"
    std::string generated_at;      // ISO date
    bool libclang_used = false;
};

// Produces the patch text. `analysis` may be a default-constructed
// DepAnalysis for binary deps (they still get a shim).
std::string GeneratePatch(const Dependency& dep,
                          const DepAnalysis& analysis,
                          const GenContext& ctx);

// Writes <dep_root>/xer-<name>.cmake; returns the path written.
bool WritePatch(const fs::path& dep_root, const std::string& name,
                const std::string& content, fs::path* written,
                std::string* error);

// Static verification of a generated patch: parenthesis balance,
// command whitelist, existence of referenced paths and of an
// add_subdirectory target's CMakeLists.txt. Fills `problems`.
bool VerifyPatch(const fs::path& patch_file,
                 std::vector<std::string>* problems);

}  // namespace xer_sync_dev
