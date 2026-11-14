/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/archive/archive_manager.h"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace XER::Content::Archive {

namespace {

constexpr size_t kMaxEntryBytes = 256ull * 1024 * 1024;  // 256 MiB per entry
constexpr size_t kIoBlockSize = 1024 * 1024;            // 1 MiB copy buffer

// ---- RAII wrappers --------------------------------------------------------

struct ReadDeleter {
    void operator()(archive* a) const {
        if (a) archive_read_free(a);
    }
};
struct WriteDeleter {
    void operator()(archive* a) const {
        if (a) archive_write_free(a);
    }
};
struct EntryDeleter {
    void operator()(archive_entry* e) const {
        if (e) archive_entry_free(e);
    }
};

using ReadPtr = std::unique_ptr<archive, ReadDeleter>;
using WritePtr = std::unique_ptr<archive, WriteDeleter>;
using EntryPtr = std::unique_ptr<archive_entry, EntryDeleter>;

// ---- Platform helpers -----------------------------------------------------

#ifdef _WIN32
std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                      static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                        static_cast<int>(utf8.size()), out.data(), n);
    return out;
}
#endif

// Interprets `utf8` as a native path (UTF-8 aware on Windows).
std::filesystem::path FsPath(const std::string& utf8) {
#ifdef _WIN32
    return std::filesystem::path(
        std::u8string(utf8.begin(), utf8.end()));
#else
    return std::filesystem::path(utf8);
#endif
}

std::string FsPathToUtf8(const std::filesystem::path& p) {
#ifdef _WIN32
    const std::u8string u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
#else
    return p.string();
#endif
}

std::string LastError(archive* a, std::string_view prefix) {
    std::string out(prefix);
    const char* e = archive_error_string(a);
    if (e != nullptr && *e != '\0') {
        out += ": ";
        out += e;
    }
    return out;
}

// ---- Passphrase callback ---------------------------------------------------

struct PassContext {
    PasswordProvider provider;
    std::string archive_path;
    std::string password;  // keeps the callback buffer alive
};

const char* PassphraseCallback(struct archive*, void* cd) {
    auto* ctx = static_cast<PassContext*>(cd);
    if (!ctx->provider) return nullptr;
    auto pw = ctx->provider(ctx->archive_path);
    if (!pw || pw->empty()) return nullptr;
    ctx->password = std::move(*pw);
    return ctx->password.c_str();
}

// ---- Open helpers -----------------------------------------------------------

struct Reader {
    ReadPtr ar;
    std::unique_ptr<PassContext> pass;
};

Reader OpenReader(const std::string& path, const PasswordProvider& provider,
                  std::string* error) {
    Reader r;
    archive* a = archive_read_new();
    if (a == nullptr) {
        *error = "archive_read_new failed";
        return r;
    }
    r.ar.reset(a);
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);

    if (provider) {
        r.pass = std::make_unique<PassContext>(
            PassContext{provider, path, {}});
        archive_read_set_passphrase_callback(a, r.pass.get(),
                                             PassphraseCallback);
    }

#ifdef _WIN32
    const std::wstring w = Utf8ToWide(path);
    const int rc = archive_read_open_filename_w(a, w.c_str(), 10240);
#else
    const int rc = archive_read_open_filename(a, path.c_str(), 10240);
#endif
    if (rc != ARCHIVE_OK) {
        *error = LastError(a, "cannot open archive");
        r.ar.reset();
        r.pass.reset();
    }
    return r;
}

// Picks format+compression filters from the file extension.
// Falls back to plain ZIP when the extension is unknown.
int ConfigureWriterFormat(archive* a, const std::string& path) {
    if (archive_write_set_format_filter_by_ext(a, path.c_str()) ==
        ARCHIVE_OK) {
        return ARCHIVE_OK;
    }
    return archive_write_set_format_zip(a);
}

WritePtr OpenWriter(const std::string& path, std::string* error) {
    archive* a = archive_write_new();
    if (a == nullptr) {
        *error = "archive_write_new failed";
        return {};
    }
    WritePtr w(a, WriteDeleter{});
    if (ConfigureWriterFormat(a, path) != ARCHIVE_OK) {
        *error = "cannot determine archive format for " + path;
        return {};
    }
#ifdef _WIN32
    const std::wstring wp = Utf8ToWide(path);
    const int rc = archive_write_open_filename_w(a, wp.c_str());
#else
    const int rc = archive_write_open_filename(a, path.c_str());
#endif
    if (rc != ARCHIVE_OK) {
        *error = LastError(a, "cannot create archive");
        return {};
    }
    return w;
}

// ---- Entry helpers ------------------------------------------------------------

const char* EntryName(archive_entry* e) {
    const char* name = archive_entry_pathname_utf8(e);
    if (name == nullptr || *name == '\0') {
        name = archive_entry_pathname(e);
    }
    return name != nullptr ? name : "";
}

// ---- Data copy ----------------------------------------------------------------

// Copies the current entry's data from reader to writer (zero-copy blocks).
bool CopyEntryData(archive* reader, archive* writer, std::string* error) {
    const void* buff = nullptr;
    size_t size = 0;
    int64_t offset = 0;
    int rc;
    while ((rc = archive_read_data_block(reader, &buff, &size, &offset)) ==
           ARCHIVE_OK) {
        if (archive_write_data_block(writer, buff, size, offset) !=
            ARCHIVE_OK) {
            *error = LastError(writer, "failed writing archive data");
            return false;
        }
    }
    if (rc != ARCHIVE_EOF) {
        *error = LastError(reader, "failed reading archive data");
        return false;
    }
    return true;
}

// ---- Rewrite engine (AddFiles / RemoveEntries) --------------------------------

// Rewrites `archive_path` through a temp sibling file:
//  - copies every existing entry except those in `remove`
//  - appends every file in `add`
// Appends one regular disk file to an open archive writer.
bool AppendDiskFile(archive* writer, const AddItem& item,
                    std::string* error);

// `remove`/`add` may be null to skip that phase.
// `changed` receives the number of entries removed + added.
bool RewriteArchive(const std::string& archive_path,
                    const std::set<std::string>* remove,
                    const std::vector<AddItem>* add,
                    const PasswordProvider& provider, std::string* error,
                    int64_t* changed) {
    *changed = 0;
    const std::filesystem::path target_fs = FsPath(archive_path);
    std::error_code ec;
    const bool source_exists = std::filesystem::exists(target_fs, ec);

    Reader reader;
    if (source_exists) {
        reader = OpenReader(archive_path, provider, error);
        if (!reader.ar) return false;
    }

    const std::string tmp_path = archive_path + ".xer-tmp";
    WritePtr writer = OpenWriter(tmp_path, error);
    if (!writer) return false;

    // If we are modifying an existing archive, the output format must
    // match the input format (libarchive cannot write RAR, CAB, etc.).
    if (reader.ar) {
        const std::string src_fmt =
            archive_format_name(reader.ar.get()) != nullptr
                ? archive_format_name(reader.ar.get())
                : "";
        const std::string dst_fmt =
            archive_format_name(writer.get()) != nullptr
                ? archive_format_name(writer.get())
                : "";
        if (!src_fmt.empty() && !dst_fmt.empty() && src_fmt != dst_fmt) {
            *error = "cannot modify '" + src_fmt +
                     "' archives (read-only format); output would be '" +
                     dst_fmt + "'";
            std::error_code ec_remove;
            std::filesystem::remove(FsPath(tmp_path), ec_remove);
            return false;
        }
    }

    int64_t touched = 0;

    // Phase 1: copy surviving entries.
    if (reader.ar) {
        archive_entry* e = nullptr;
        for (;;) {
            const int rc =
                archive_read_next_header(reader.ar.get(), &e);
            if (rc == ARCHIVE_EOF) break;
            if (rc != ARCHIVE_OK && rc != ARCHIVE_WARN) {
                *error = LastError(reader.ar.get(), "cannot read archive");
                goto fail;
            }
            if (e == nullptr) continue;

            const std::string name = NormalizeEntryPath(EntryName(e));
            if (remove != nullptr && remove->count(name) != 0) {
                archive_read_data_skip(reader.ar.get());
                ++touched;
                continue;
            }

            if (archive_write_header(writer.get(), e) != ARCHIVE_OK) {
                *error = LastError(writer.get(), "cannot write archive entry");
                goto fail;
            }
            if (!CopyEntryData(reader.ar.get(), writer.get(), error)) {
                goto fail;
            }
            if (archive_write_finish_entry(writer.get()) != ARCHIVE_OK) {
                *error = LastError(writer.get(), "cannot finish entry");
                goto fail;
            }
        }
    }

    // Phase 2: append new files.
    if (add != nullptr) {
        for (const AddItem& item : *add) {
            if (!AppendDiskFile(writer.get(), item, error)) goto fail;
            ++touched;
        }
    }

    if (archive_write_close(writer.get()) != ARCHIVE_OK) {
        *error = LastError(writer.get(), "cannot finalize archive");
        goto fail;
    }
    writer.reset();

    // Atomic-ish replace.
#ifdef _WIN32
    {
        const std::wstring wtmp = Utf8ToWide(tmp_path);
        const std::wstring wd = target_fs.wstring();
        if (!MoveFileExW(wtmp.c_str(), wd.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            *error = "cannot replace archive file (error " +
                     std::to_string(GetLastError()) + ")";
            std::error_code ec_remove;
            std::filesystem::remove(FsPath(tmp_path), ec_remove);
            return false;
        }
    }
#else
    if (std::rename(tmp_path.c_str(), archive_path.c_str()) != 0) {
        *error = std::string("cannot replace archive file: ") +
                 std::strerror(errno);
        std::error_code ec_remove;
        std::filesystem::remove(FsPath(tmp_path), ec_remove);
        return false;
    }
#endif

    *changed = touched;
    return true;

fail:
    std::filesystem::remove(FsPath(tmp_path), ec);
    return false;
}

}  // namespace

// ---- Public API ---------------------------------------------------------------

std::string NormalizeEntryPath(std::string_view path) {
    // Normalize separators, then resolve "." and ".." segments lexically.
    // ".." never escapes above the archive root (zip-slip safe).
    std::string normalized(path);
    for (char& c : normalized) {
        if (c == '\\') c = '/';
    }
    const std::string_view nview(normalized);

    std::vector<std::string_view> parts;
    size_t pos = 0;
    while (pos <= nview.size()) {
        const size_t slash = nview.find('/', pos);
        const std::string_view seg = nview.substr(
            pos, slash == std::string_view::npos ? std::string_view::npos
                                                 : slash - pos);
        pos = (slash == std::string::npos) ? normalized.size() + 1
                                           : slash + 1;
        if (seg.empty() || seg == ".") continue;
        if (seg == "..") {
            if (!parts.empty()) parts.pop_back();
            continue;
        }
        parts.push_back(seg);
    }

    std::string out;
    for (const auto& p : parts) {
        if (!out.empty()) out.push_back('/');
        out.append(p);
    }
    return out;
}

bool ArchiveManager::Probe(const std::string& path, ArchiveInfo* info,
                           std::string* error, PasswordProvider provider) {
    info->entry_count = 0;
    info->encrypted = false;
    info->encryption_unknown = false;

    Reader r = OpenReader(path, provider, error);
    if (!r.ar) return false;

    archive_entry* e = nullptr;
    for (;;) {
        const int rc = archive_read_next_header(r.ar.get(), &e);
        if (rc == ARCHIVE_EOF) break;
        if (rc != ARCHIVE_OK && rc != ARCHIVE_WARN) break;  // encrypted headers
        ++info->entry_count;
        archive_read_data_skip(r.ar.get());
    }

    // Format/encryption metadata is only populated once the reader has
    // iterated at least one header (the format bidder runs then).
    if (const char* f = archive_format_name(r.ar.get())) info->format = f;
    if (const char* c = archive_filter_name(r.ar.get(), 0)) {
        info->compression = c;
    }
    const int enc = archive_read_has_encrypted_entries(r.ar.get());
    info->encrypted = (enc == 1);
    info->encryption_unknown = (enc < 0);

    std::error_code ec;
    const auto sz = std::filesystem::file_size(FsPath(path), ec);
    info->file_size = ec ? 0 : static_cast<uint64_t>(sz);
    return true;
}

bool ArchiveManager::List(const std::string& path,
                          std::vector<ArchiveEntryInfo>* entries,
                          std::string* error, PasswordProvider provider) {
    entries->clear();
    Reader r = OpenReader(path, provider, error);
    if (!r.ar) return false;

    archive_entry* e = nullptr;
    for (;;) {
        const int rc = archive_read_next_header(r.ar.get(), &e);
        if (rc == ARCHIVE_EOF) break;
        if (rc != ARCHIVE_OK && rc != ARCHIVE_WARN) {
            *error = LastError(r.ar.get(), "cannot list archive");
            return false;
        }
        if (e == nullptr) continue;

        ArchiveEntryInfo info;
        info.path = NormalizeEntryPath(EntryName(e));
        const __LA_MODE_T ft = archive_entry_filetype(e);
        info.is_dir = (ft == AE_IFDIR);
        info.is_symlink = (ft == AE_IFLNK);
        info.size = archive_entry_size_is_set(e)
                        ? archive_entry_size(e)
                        : 0;
        info.mtime = archive_entry_mtime(e);
        entries->push_back(std::move(info));
        archive_read_data_skip(r.ar.get());
    }
    return true;
}

bool ArchiveManager::Extract(const std::string& archive_path,
                             const std::string& dest_dir,
                             const std::vector<std::string>& entry_paths,
                             int64_t* extracted, std::string* error,
                             PasswordProvider provider) {
    *extracted = 0;
    if (dest_dir.empty()) {
        *error = "destination directory is empty";
        return false;
    }
    std::set<std::string> wanted;
    for (const std::string& p : entry_paths) {
        wanted.insert(NormalizeEntryPath(p));
    }
    const bool all = wanted.empty();

    Reader r = OpenReader(archive_path, provider, error);
    if (!r.ar) return false;

    archive* dw = archive_write_disk_new();
    if (dw == nullptr) {
        *error = "archive_write_disk_new failed";
        return false;
    }
    archive_write_disk_set_options(
        dw, ARCHIVE_EXTRACT_SECURE_SYMLINKS |
                ARCHIVE_EXTRACT_SECURE_NODOTDOT |
                ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS |
                ARCHIVE_EXTRACT_NO_OVERWRITE);
    archive_write_disk_set_standard_lookup(dw);

    std::error_code ec;
    std::filesystem::create_directories(FsPath(dest_dir), ec);

    archive_entry* e = nullptr;
    bool ok = true;
    for (;;) {
        const int rc = archive_read_next_header(r.ar.get(), &e);
        if (rc == ARCHIVE_EOF) break;
        if (rc != ARCHIVE_OK && rc != ARCHIVE_WARN) {
            *error = LastError(r.ar.get(), "cannot read archive");
            ok = false;
            break;
        }
        if (e == nullptr) continue;

        const std::string name = NormalizeEntryPath(EntryName(e));
        if (!all && wanted.count(name) == 0) {
            archive_read_data_skip(r.ar.get());
            continue;
        }

        // Redirect the entry to dest_dir + internal path (UTF-8).
        std::string target = dest_dir;
        if (!target.empty() && target.back() != '/' && target.back() != '\\') {
            target += '/';
        }
        target += name;
        archive_entry_update_pathname_utf8(e, target.c_str());

        const int wrc = archive_write_header(dw, e);
        if (wrc == ARCHIVE_FAILED) {
            // Entry exists and NO_OVERWRITE is set: skip silently.
            archive_read_data_skip(r.ar.get());
            continue;
        }
        if (wrc != ARCHIVE_OK && wrc != ARCHIVE_WARN) {
            *error = LastError(r.ar.get(), "cannot extract entry");
            ok = false;
            break;
        }

        if (!CopyEntryData(r.ar.get(), dw, error)) {
            ok = false;
            break;
        }
        if (archive_write_finish_entry(dw) != ARCHIVE_OK) {
            *error = LastError(r.ar.get(), "cannot finish extracted entry");
            ok = false;
            break;
        }
        ++(*extracted);
    }

    archive_write_free(dw);
    return ok;
}

bool ArchiveManager::ReadEntry(const std::string& archive_path,
                               const std::string& entry_path,
                               std::string* data, std::string* error,
                               PasswordProvider provider) {
    const std::string target = NormalizeEntryPath(entry_path);
    data->clear();

    Reader r = OpenReader(archive_path, provider, error);
    if (!r.ar) return false;

    archive_entry* e = nullptr;
    for (;;) {
        const int rc = archive_read_next_header(r.ar.get(), &e);
        if (rc == ARCHIVE_EOF) break;
        if (rc != ARCHIVE_OK && rc != ARCHIVE_WARN) {
            *error = LastError(r.ar.get(), "cannot read archive");
            return false;
        }
        if (e == nullptr) continue;

        if (NormalizeEntryPath(EntryName(e)) != target) {
            archive_read_data_skip(r.ar.get());
            continue;
        }
        if (archive_entry_filetype(e) == AE_IFDIR) {
            *error = "entry is a directory: " + target;
            return false;
        }
        if (archive_entry_size_is_set(e) &&
            archive_entry_size(e) > static_cast<la_int64_t>(kMaxEntryBytes)) {
            *error = "entry too large to preview: " + target;
            return false;
        }

        std::vector<char> buf(kIoBlockSize);
        for (;;) {
            const la_ssize_t n =
                archive_read_data(r.ar.get(), buf.data(), buf.size());
            if (n == 0) break;  // EOF
            if (n == ARCHIVE_WARN || n == ARCHIVE_RETRY) continue;
            if (n < 0) {
                *error = LastError(r.ar.get(), "cannot read entry data");
                return false;
            }
            if (data->size() + static_cast<size_t>(n) > kMaxEntryBytes) {
                *error = "entry too large to preview: " + target;
                return false;
            }
            data->append(buf.data(), static_cast<size_t>(n));
        }
        return true;
    }

    *error = "entry not found: " + target;
    return false;
}

bool ArchiveManager::AddFiles(const std::string& archive_path,
                              const std::vector<AddItem>& items,
                              int64_t* added, std::string* error,
                              PasswordProvider provider) {
    std::vector<AddItem> normalized = items;
    for (AddItem& item : normalized) {
        if (item.arcname.empty()) {
            // Default: file name on disk.
            const size_t slash = item.disk_path.find_last_of("/\\");
            item.arcname = slash == std::string::npos
                               ? item.disk_path
                               : item.disk_path.substr(slash + 1);
        }
        item.arcname = NormalizeEntryPath(item.arcname);
    }
    return RewriteArchive(archive_path, nullptr, &normalized, provider,
                          error, added);
}

bool ArchiveManager::RemoveEntries(const std::string& archive_path,
                                   const std::vector<std::string>& entry_paths,
                                   int64_t* removed, std::string* error,
                                   PasswordProvider provider) {
    std::set<std::string> remove;
    for (const std::string& p : entry_paths) {
        remove.insert(NormalizeEntryPath(p));
    }
    if (remove.empty()) {
        *removed = 0;
        return true;
    }
    return RewriteArchive(archive_path, &remove, nullptr, provider, error,
                          removed);
}

bool ArchiveManager::Create(const std::string& archive_path,
                            std::string* error,
                            PasswordProvider provider) {
    std::error_code ec;
    if (std::filesystem::exists(FsPath(archive_path), ec)) {
        *error = "archive already exists: " + archive_path;
        return false;
    }
    int64_t changed = 0;
    return RewriteArchive(archive_path, nullptr, nullptr, provider, error,
                          &changed);
}

namespace {

// Appends one regular disk file to an open archive writer.
bool AppendDiskFile(archive* writer, const AddItem& item,
                    std::string* error) {
    const std::filesystem::path disk = FsPath(item.disk_path);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(disk, ec)) {
        *error = "not a regular file: " + item.disk_path;
        return false;
    }
    const auto fsize = std::filesystem::file_size(disk, ec);
    if (ec) {
        *error = "cannot stat file: " + item.disk_path;
        return false;
    }
    if (fsize > static_cast<uintmax_t>(INT64_MAX)) {
        *error = "file too large: " + item.disk_path;
        return false;
    }

    EntryPtr entry(archive_entry_new());
    if (!entry) {
        *error = "archive_entry_new failed";
        return false;
    }
    archive_entry_set_pathname_utf8(entry.get(), item.arcname.c_str());
    archive_entry_set_size(entry.get(), static_cast<la_int64_t>(fsize));
    archive_entry_set_filetype(entry.get(), AE_IFREG);
    archive_entry_set_perm(entry.get(), 0644);
    archive_entry_set_mtime(entry.get(), std::time(nullptr), 0);

    if (archive_write_header(writer, entry.get()) != ARCHIVE_OK) {
        *error = LastError(writer, "cannot write entry header for " +
                                    item.arcname);
        return false;
    }

    std::ifstream in(disk, std::ios::binary);
    if (!in) {
        *error = "cannot open file: " + item.disk_path;
        return false;
    }
    std::vector<char> buf(kIoBlockSize);
    la_int64_t remaining = static_cast<la_int64_t>(fsize);
    while (remaining > 0) {
        const size_t want = static_cast<size_t>(
            std::min<la_int64_t>(remaining,
                                 static_cast<la_int64_t>(buf.size())));
        in.read(buf.data(), static_cast<std::streamsize>(want));
        const std::streamsize got = in.gcount();
        if (got <= 0) {
            *error = "unexpected EOF reading " + item.disk_path;
            return false;
        }
        if (archive_write_data(writer, buf.data(),
                               static_cast<size_t>(got)) != got) {
            *error = LastError(writer, "cannot write entry data for " +
                                        item.arcname);
            return false;
        }
        remaining -= got;
    }

    if (archive_write_finish_entry(writer) != ARCHIVE_OK) {
        *error = LastError(writer, "cannot finish entry " + item.arcname);
        return false;
    }
    return true;
}

}  // namespace

}  // namespace XER::Content::Archive
