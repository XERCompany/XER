/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace XER::Content::Archive {

// Metadata for a single entry inside an archive.
struct ArchiveEntryInfo {
    std::string path;      // pathname inside the archive ('/' separators)
    bool is_dir = false;
    bool is_symlink = false;
    int64_t size = 0;      // uncompressed size in bytes
    int64_t mtime = 0;     // unix timestamp, seconds
};

// High-level information about an archive file.
struct ArchiveInfo {
    std::string format;         // e.g. "zip", "7-zip", "rar"
    std::string compression;    // e.g. "gzip", "bzip2", ""
    bool encrypted = false;     // archive contains encrypted entries
    bool encryption_unknown = false;
    int64_t entry_count = 0;
    uint64_t file_size = 0;     // archive file size on disk
};

// A disk file to be added into an archive.
struct AddItem {
    std::string disk_path;  // path of the file on disk
    std::string arcname;    // path inside the archive; empty -> basename
};

// Called by libarchive when an encrypted archive needs a passphrase.
// `archive_path` identifies which archive is being read.
// Return the password, or std::nullopt to abort the operation.
using PasswordProvider =
    std::function<std::optional<std::string>(const std::string& archive_path)>;

// Normalizes an archive-internal path: backslashes -> '/', leading
// "./" and "/" stripped. Used for matching entries on input.
std::string NormalizeEntryPath(std::string_view path);

// Universal archive manager backed by libarchive.
// Supports reading: zip, rar, rar5, 7z, tar (+gz/bz2/xz/zstd/lz4 filters),
// cab, cpio, iso9660, xar, warc, lha, mtree and more.
// Supports writing: any format libarchive can produce, chosen by file
// extension (zip, 7z, tar, tar.gz, ...). RAR writing is NOT supported
// by libarchive.
//
// All methods are static and thread-safe; concurrent calls on different
// archives are fine. PasswordProvider may be invoked from the calling
// thread at the point libarchive actually needs a passphrase.
class ArchiveManager {
public:
    // Reads format/compression/encryption metadata and counts entries.
    // Does not extract data. For encrypted archives the provider may be
    // consulted so the entry count is accurate.
    static bool Probe(const std::string& path, ArchiveInfo* info,
                      std::string* error, PasswordProvider provider = {});

    // Lists all entries (headers only, no data extraction).
    static bool List(const std::string& path,
                     std::vector<ArchiveEntryInfo>* entries,
                     std::string* error, PasswordProvider provider = {});

    // Extracts `entry_paths` (normalized) into `dest_dir`.
    // An empty `entry_paths` extracts everything. Returns the number of
    // entries written. Path traversal is blocked by libarchive secure
    // extraction flags.
    static bool Extract(const std::string& archive_path,
                        const std::string& dest_dir,
                        const std::vector<std::string>& entry_paths,
                        int64_t* extracted, std::string* error,
                        PasswordProvider provider = {});

    // Reads a single entry's uncompressed data into `data`.
    // Fails if the entry is a directory or larger than internal limits.
    static bool ReadEntry(const std::string& archive_path,
                          const std::string& entry_path, std::string* data,
                          std::string* error, PasswordProvider provider = {});

    // Adds `items` to `archive_path`, preserving existing entries.
    // If the archive does not exist it is created (format by extension).
    // If it exists but is not writable (e.g. RAR), the call fails.
    // Rewriting is done via a temporary sibling file + atomic replace.
    static bool AddFiles(const std::string& archive_path,
                         const std::vector<AddItem>& items, int64_t* added,
                         std::string* error, PasswordProvider provider = {});

    // Removes `entry_paths` (normalized) from the archive by rewriting
    // it without those entries. Empty vector is a no-op success.
    static bool RemoveEntries(const std::string& archive_path,
                              const std::vector<std::string>& entry_paths,
                              int64_t* removed, std::string* error,
                              PasswordProvider provider = {});

    // Creates a new empty archive at `archive_path` (format by extension).
    static bool Create(const std::string& archive_path, std::string* error,
                       PasswordProvider provider = {});
};

}  // namespace XER::Content::Archive
