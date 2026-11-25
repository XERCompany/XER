/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace XER::Packaging {

// What Packager::Pack produces at the end of staging.
enum class PackageFormat {
    // Staging tree + manifest.json only — the layout a real installer
    // maker (NSIS/Inno/WiX) consumes as-is.
    Staging,
    // Staging + a .zip of the staging contents via XER_Archive
    // (libarchive). Only available when the archive module was linked.
    Zip,
};

// Everything needed to build a distributable bundle of one app.
struct PackageSpec {
    std::filesystem::path exe_path;      // required: the app entry point
    std::vector<std::filesystem::path> extra_files;  // copied next to the exe
    std::vector<std::filesystem::path> extra_dirs;   // copied recursively by name
    std::filesystem::path output_dir;    // parent dir of the staging tree

    std::string name;                    // empty -> exe stem
    std::string version = "0.0.0";
    std::filesystem::path icon_path;     // optional .ico copied into the tree

    // Directories searched for DLL dependencies after the exe's own dir.
    // Empty -> Packager::DefaultSearchDirs(exe_path) is used.
    std::vector<std::filesystem::path> search_dirs;

    PackageFormat format = PackageFormat::Staging;
};

// One staged file as recorded in manifest.json.
struct PackageFileEntry {
    std::string path;      // path inside the staging tree ('/' separators)
    std::string role;      // entry_point | dependency | asset | icon | manifest
    std::uint64_t size = 0;
    std::string crc32;     // 8 lowercase hex digits (zlib crc32)
};

// DLL-name origin in DependencyScanResult / staged file metadata.
enum class DepOrigin {
    ExeDir,     // resolved beside the scanned binary
    SearchDir,  // resolved inside one of the search dirs
};

struct ResolvedDependency {
    std::filesystem::path path;
    DepOrigin origin = DepOrigin::ExeDir;
    bool delay_loaded = false;   // came from the delay-import table only
};

struct DependencyScanResult {
    std::filesystem::path exe_dir;
    // Resolved DLLs, sorted by lowercase file name.
    std::vector<ResolvedDependency> dlls;
    // Import names that resolved nowhere — OS-provided (KERNEL32.dll,
    // api-ms-*, msvcrt.dll, ...) or genuinely missing. Kept separate so
    // callers can decide whether to warn.
    std::vector<std::string> unresolved;
    // Non-fatal problems seen mid-walk (unparseable sibling DLL, ...).
    std::vector<std::string> warnings;
};

struct PackageResult {
    std::filesystem::path staging_dir;
    std::filesystem::path manifest_path;
    std::filesystem::path archive_path;   // empty unless format == Zip
    std::vector<PackageFileEntry> files;
    std::vector<std::string> unresolved_imports;
    std::vector<std::string> warnings;
};

// Progress callback: fraction in [0,1] + a short step label. May run on a
// worker thread — UI callers must marshal.
using PackProgress = std::function<void(float fraction, const std::string& step)>;
using PackLog = std::function<void(const std::string& line)>;

}  // namespace XER::Packaging
