/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "packaging/packager.h"

#include "packaging/dependency_scanner.h"

#include <json/json.h>
#include <zlib.h>

#ifdef XER_PACKAGING_HAS_ARCHIVE
#include "content/archive/archive_manager.h"
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <set>
#include <thread>

namespace XER::Packaging {

namespace fs = std::filesystem;

namespace {

void Emit(const PackLog& log, const std::string& line) {
    if (log) log(line);
}
void Step(const PackProgress& progress, float f, const std::string& s) {
    if (progress) progress(f, s);
}

bool IsSafeChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '.' ||
           c == '_' || c == '-';
}

// fs::copy_file with the same brief retry as the build-side stager —
// destination DLLs are often held open for a few ms by an exiting
// process or a sibling writer.
bool CopyWithRetry(const fs::path& src, const fs::path& dst,
                   std::string* error) {
    std::error_code ec;
    for (int attempt = 0; attempt < 5; ++attempt) {
        ec.clear();
        if (fs::copy_file(src, dst, fs::copy_options::overwrite_existing,
                          ec)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (error != nullptr)
        *error = src.string() + " -> " + dst.string() + ": " +
                 ec.message();
    return false;
}

bool CopyDirRecursive(const fs::path& src, const fs::path& dst,
                      std::string* error) {
    std::error_code ec;
    fs::create_directories(dst, ec);
    if (ec) {
        if (error != nullptr)
            *error = "mkdir " + dst.string() + ": " + ec.message();
        return false;
    }
    for (const auto& it : fs::recursive_directory_iterator(src, ec)) {
        const fs::path rel = it.path().lexically_relative(src);
        const fs::path target = dst / rel;
        if (it.is_directory(ec)) {
            fs::create_directories(target, ec);
        } else if (it.is_regular_file(ec)) {
            fs::create_directories(target.parent_path(), ec);
            if (!CopyWithRetry(it.path(), target, error)) return false;
        }
    }
    if (ec) {
        if (error != nullptr)
            *error = "walk " + src.string() + ": " + ec.message();
        return false;
    }
    return true;
}

std::string IsoUtcNow() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[24];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// Forward-slashed staging-relative path for manifest entries.
std::string RelEntry(const fs::path& staging_root, const fs::path& p) {
    return p.lexically_relative(staging_root).generic_string();
}

// Collect every regular file under `root` (including root itself when
// it is a file) for manifest accounting, sorted for stable output.
void CollectTreeFiles(const fs::path& root,
                      std::vector<fs::path>* files) {
    std::error_code ec;
    if (fs::is_regular_file(root, ec)) {
        files->push_back(root);
        return;
    }
    for (const auto& it : fs::recursive_directory_iterator(root, ec)) {
        if (it.is_regular_file(ec)) files->push_back(it.path());
    }
    std::sort(files->begin(), files->end());
}

}  // namespace

std::string Packager::SanitizeName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) out.push_back(IsSafeChar(c) ? c : '-');
    while (!out.empty() && out.front() == '-') out.erase(out.begin());
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? "package" : out;
}

bool Packager::FileCrc32(const fs::path& path, std::string* hex_out,
                         std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error != nullptr)
            *error = "cannot open " + path.string();
        return false;
    }
    uLong crc = crc32(0L, Z_NULL, 0);
    char buf[64 * 1024];
    while (in) {
        in.read(buf, sizeof(buf));
        const std::streamsize n = in.gcount();
        if (n > 0) {
            crc = crc32(crc, reinterpret_cast<const Bytef*>(buf),
                        static_cast<uInt>(n));
        }
    }
    char hex[16];
    std::snprintf(hex, sizeof(hex), "%08lx",
                  static_cast<unsigned long>(crc));
    *hex_out = hex;
    return true;
}

std::vector<fs::path> Packager::DefaultSearchDirs(const fs::path& exe) {
    std::vector<fs::path> dirs;
    std::error_code ec;
    const fs::path abs = fs::absolute(exe, ec).lexically_normal();
    const fs::path exe_dir = abs.parent_path();
    if (!exe_dir.empty()) dirs.push_back(exe_dir);

    // Walk ancestors looking for a repo root (one that has out/build);
    // every preset's bin/ under it is a candidate DLL source. Dedup on
    // the exe dir itself (the packaged exe usually already lives in
    // one of those bin dirs).
    std::set<std::string> seen;
    if (!exe_dir.empty()) seen.insert(exe_dir.generic_string());
    for (fs::path a = exe_dir; !a.empty() && a != a.parent_path();
         a = a.parent_path()) {
        const fs::path builds = a / "out" / "build";
        if (!fs::is_directory(builds, ec)) continue;
        for (const auto& preset : fs::directory_iterator(builds, ec)) {
            const fs::path bin = preset.path() / "bin";
            if (!fs::is_directory(bin, ec)) continue;
            const std::string key = bin.generic_string();
            if (seen.insert(key).second) dirs.push_back(bin);
        }
        break;  // first repo root wins
    }
    return dirs;
}

bool Packager::Pack(const PackageSpec& spec, PackageResult* result,
                    std::string* error, PackProgress progress,
                    PackLog log) {
    if (result == nullptr) {
        if (error != nullptr) *error = "result parameter is null";
        return false;
    }
    *result = PackageResult{};

    std::error_code ec;
    if (spec.exe_path.empty() || !fs::exists(spec.exe_path, ec) ||
        fs::is_directory(spec.exe_path, ec)) {
        if (error != nullptr)
            *error = "target exe not found: " + spec.exe_path.string();
        return false;
    }
    if (spec.output_dir.empty()) {
        if (error != nullptr) *error = "output_dir is required";
        return false;
    }

    const std::string name =
        spec.name.empty()
            ? spec.exe_path.stem().string()
            : spec.name;
    const std::string base =
        SanitizeName(name) + "-" + SanitizeName(spec.version) +
        "-windows-x64";
    const fs::path staging = spec.output_dir / base;
    result->staging_dir = staging;

    Emit(log, "pack: " + name + " " + spec.version + " -> " +
                  staging.string());
    Step(progress, 0.02f, "validating inputs");

    // ---- dependency scan ----
    std::vector<fs::path> search = spec.search_dirs;
    if (search.empty()) search = DefaultSearchDirs(spec.exe_path);
    {
        std::string dirs_line = "scan dirs:";
        for (const auto& d : search) dirs_line += " " + d.string();
        Emit(log, dirs_line);
    }
    Step(progress, 0.06f, "scanning PE dependencies");

    DependencyScanResult scan;
    if (!ScanDependencies(spec.exe_path, search, &scan, error))
        return false;
    result->warnings = scan.warnings;
    result->unresolved_imports = scan.unresolved;
    Emit(log, "scan: " + std::to_string(scan.dlls.size()) +
                  " resolved DLL(s), " +
                  std::to_string(scan.unresolved.size()) +
                  " OS/unresolved import(s)");
    for (const auto& w : scan.warnings) Emit(log, "warn: " + w);

    // ---- staging ----
    Step(progress, 0.15f, "creating staging tree");
    fs::create_directories(staging, ec);
    if (ec) {
        if (error != nullptr)
            *error = "mkdir " + staging.string() + ": " + ec.message();
        return false;
    }

    // (path-in-staging, role) list built as files are copied, then
    // hashed + sized into manifest entries.
    std::vector<std::pair<fs::path, std::string>> staged;

    const fs::path exe_dst = staging / spec.exe_path.filename();
    if (!CopyWithRetry(spec.exe_path, exe_dst, error)) {
        if (error != nullptr) *error = "copy exe: " + *error;
        return false;
    }
    staged.emplace_back(exe_dst, "entry_point");
    Emit(log, "  + " + spec.exe_path.filename().string() +
                  " (entry point)");

    // Resolved DLLs — ExeDir hits are siblings of the app (shipped
    // with it), SearchDir hits come from the toolchain/preset bin.
    int copied_deps = 0;
    for (const auto& dep : scan.dlls) {
        const fs::path dst = staging / dep.path.filename();
        if (!CopyWithRetry(dep.path, dst, error)) {
            if (error != nullptr)
                *error = "copy dep " + dep.path.filename().string() +
                         ": " + *error;
            return false;
        }
        staged.emplace_back(dst, "dependency");
        ++copied_deps;
        Emit(log, "  + " + dep.path.filename().string() +
                      (dep.delay_loaded ? " (delay-loaded)" : ""));
    }
    Step(progress, 0.45f, "copying dependencies");

    // ---- extras ----
    for (const auto& f : spec.extra_files) {
        if (!fs::exists(f, ec)) {
            if (error != nullptr)
                *error = "extra file not found: " + f.string();
            return false;
        }
        if (fs::is_directory(f, ec)) {
            const fs::path dst = staging / f.filename();
            if (!CopyDirRecursive(f, dst, error)) return false;
            const std::string rel = RelEntry(staging, dst);
            for (const auto& sub : fs::recursive_directory_iterator(dst,
                                                                    ec)) {
                if (sub.is_regular_file(ec))
                    staged.emplace_back(sub.path(), "asset");
            }
            Emit(log, "  + " + rel + "/ (dir)");
        } else {
            const fs::path dst = staging / f.filename();
            if (!CopyWithRetry(f, dst, error)) {
                if (error != nullptr)
                    *error = "copy extra " + f.string() + ": " + *error;
                return false;
            }
            staged.emplace_back(dst, "asset");
            Emit(log, "  + " + f.filename().string());
        }
    }
    for (const auto& d : spec.extra_dirs) {
        if (!fs::is_directory(d, ec)) {
            if (error != nullptr)
                *error = "extra dir not found: " + d.string();
            return false;
        }
        const fs::path dst = staging / d.filename();
        if (!CopyDirRecursive(d, dst, error)) return false;
        for (const auto& sub : fs::recursive_directory_iterator(dst, ec)) {
            if (sub.is_regular_file(ec))
                staged.emplace_back(sub.path(), "asset");
        }
        Emit(log, "  + " + d.filename().string() + "/ (dir)");
    }
    Step(progress, 0.6f, "copying assets");

    // ---- icon ----
    if (!spec.icon_path.empty()) {
        if (!fs::exists(spec.icon_path, ec)) {
            if (error != nullptr)
                *error = "icon not found: " + spec.icon_path.string();
            return false;
        }
        const fs::path dst = staging / spec.icon_path.filename();
        if (!CopyWithRetry(spec.icon_path, dst, error)) {
            if (error != nullptr) *error = "copy icon: " + *error;
            return false;
        }
        staged.emplace_back(dst, "icon");
        Emit(log, "  + " + spec.icon_path.filename().string() +
                      " (icon)");
    }

    // ---- manifest ----
    Step(progress, 0.75f, "hashing staged files");
    result->files.reserve(staged.size());
    {
        std::size_t i = 0;
        for (const auto& [p, role] : staged) {
            PackageFileEntry e;
            e.path = RelEntry(staging, p);
            e.role = role;
            e.size = fs::file_size(p, ec);
            if (!FileCrc32(p, &e.crc32, error)) {
                if (error != nullptr)
                    *error = "crc32 " + e.path + ": " + *error;
                return false;
            }
            result->files.push_back(std::move(e));
            if (progress && staged.size() > 1) {
                Step(progress,
                     0.75f + 0.15f *
                                 (static_cast<float>(++i) /
                                  static_cast<float>(staged.size())),
                     "hashing " + e.path);
            }
        }
    }

    Json::Value root(Json::objectValue);
    root["format"] = "xer-package/1";
    root["name"] = name;
    root["version"] = spec.version;
    root["entry_point"] = spec.exe_path.filename().string();
    root["platform"] = "windows-x64";
    root["created_utc"] = IsoUtcNow();
    if (!spec.icon_path.empty())
        root["icon"] = spec.icon_path.filename().string();
    Json::Value files(Json::arrayValue);
    for (const auto& e : result->files) {
        Json::Value f(Json::objectValue);
        f["path"] = e.path;
        f["role"] = e.role;
        f["size"] = static_cast<Json::UInt64>(e.size);
        f["crc32"] = e.crc32;
        files.append(f);
    }
    root["files"] = files;
    Json::Value unresolved(Json::arrayValue);
    for (const auto& u : result->unresolved_imports)
        unresolved.append(u);
    root["unresolved_imports"] = unresolved;

    const fs::path manifest_path = staging / "manifest.json";
    {
        std::ofstream out(manifest_path,
                          std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error != nullptr)
                *error = "cannot write " + manifest_path.string();
            return false;
        }
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "  ";
        out << Json::writeString(wb, root);
    }
    result->manifest_path = manifest_path;
    PackageFileEntry me;
    me.path = "manifest.json";
    me.role = "manifest";
    me.size = fs::file_size(manifest_path, ec);
    FileCrc32(manifest_path, &me.crc32, nullptr);
    result->files.push_back(std::move(me));
    Emit(log, "manifest: " + manifest_path.string() + " (" +
                  std::to_string(result->files.size()) + " entries)");
    Step(progress, 0.9f, "manifest written");

    // ---- optional archive ----
    if (spec.format == PackageFormat::Zip) {
#ifdef XER_PACKAGING_HAS_ARCHIVE
        const fs::path zip_path = spec.output_dir / (base + ".zip");
        std::vector<XER::Content::Archive::AddItem> items;
        std::vector<fs::path> tree_files;
        CollectTreeFiles(staging, &tree_files);
        items.reserve(tree_files.size());
        for (const auto& p : tree_files) {
            items.push_back({p.string(), RelEntry(staging, p)});
        }
        std::string arch_err;
        std::int64_t added = 0;
        if (!XER::Content::Archive::ArchiveManager::AddFiles(
                zip_path.string(), items, &added, &arch_err)) {
            if (error != nullptr)
                *error = "zip " + zip_path.string() + ": " + arch_err;
            return false;
        }
        result->archive_path = zip_path;
        Emit(log, "zip: " + zip_path.string() + " (" +
                      std::to_string(added) + " entries)");
#else
        if (error != nullptr)
            *error =
                "PackageFormat::Zip requested but xer_packaging was "
                "built without XER_Archive";
        return false;
#endif
    }

    Step(progress, 1.0f, "done");
    Emit(log, "done: " + std::to_string(copied_deps) +
                  " dependenc(ies) staged");
    return true;
}

}  // namespace XER::Packaging
