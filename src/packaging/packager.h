/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include "packaging/package_spec.h"

#include <filesystem>
#include <string>
#include <vector>

namespace XER::Packaging {

// Packager — builds a distributable bundle of a Windows app:
//
//   <output_dir>/<name>-<version>-windows-x64/
//       <app>.exe               (entry point)
//       *.dll                   (resolved PE import closure)
//       <extra files/dirs>      (assets copied verbatim)
//       <name>.ico              (optional icon)
//       manifest.json           (xer-package/1: files, sizes, crc32,
//                              name/version/entry point, unresolved
//                              OS-level imports)
//
// With PackageFormat::Zip the staging contents are additionally packed
// into <staging>.zip via XER_Archive (libarchive) when that module was
// linked into xer_packaging.
//
// All methods are static; no global state. Progress/log callbacks may
// run on the caller's thread only — Pack() itself is synchronous, so
// async hosts drive it from a worker and marshal updates themselves.
class Packager {
public:
    // Runs the full pack. `progress` gets coarse fractions (0..1);
    // `log` receives human-readable lines as work proceeds.
    // Returns false with `error` set on failure; `result` is filled on
    // success.
    static bool Pack(const PackageSpec& spec, PackageResult* result,
                     std::string* error, PackProgress progress = {},
                     PackLog log = {});

    // exe dir + every `out/build/<preset>/bin` found under the repo root
    // that contains `exe` (walked up from the exe location). Mirrors the
    // build-side staging lookup (xer_stage_runtime_dlls / xer_pe_deps)
    // so a packaged exe finds the same DLLs it ran against.
    static std::vector<std::filesystem::path> DefaultSearchDirs(
        const std::filesystem::path& exe);

    // "App Name 1.0!" -> "App-Name-1.0-" style sanitization applied to
    // the staging dir / archive base name.
    static std::string SanitizeName(const std::string& name);

    // Streaming zlib CRC32 of a file; false when unreadable.
    static bool FileCrc32(const std::filesystem::path& path,
                          std::string* hex_out, std::string* error);
};

}  // namespace XER::Packaging
