/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/civitaimodels/civitai_model_downloader.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <system_error>
#include <thread>
#include <vector>

#include <openssl/evp.h>

#include "async_io/io_thread_pool.h"
#include "cache/cache_manager.h"
#include "model_hub_http.h"

namespace content::civitai {

namespace {

constexpr size_t kIoBufSize = 256 * 1024;
constexpr int kMaxRedirects = 5;

// Streams |path|'s existing bytes through the digest context so a resumed
// download produces a hash over the complete file.
void HashFileInto(EVP_MD_CTX* ctx, const std::filesystem::path& path,
                  bool* ok) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        *ok = false;
        return;
    }
    std::vector<uint8_t> buf(kIoBufSize);
    while (in) {
        in.read(reinterpret_cast<char*>(buf.data()),
                static_cast<std::streamsize>(buf.size()));
        const auto n = in.gcount();
        if (n > 0) EVP_DigestUpdate(ctx, buf.data(), static_cast<size_t>(n));
    }
    *ok = !in.bad();
}

std::string HexDigest(EVP_MD_CTX* ctx) {
    uint8_t md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_DigestFinal_ex(ctx, md, &len);
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (unsigned int i = 0; i < len; ++i) {
        out.push_back(kHex[md[i] >> 4]);
        out.push_back(kHex[md[i] & 0xF]);
    }
    return out;
}

std::string Lower(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct DigestGuard {
    EVP_MD_CTX* ctx = nullptr;
    DigestGuard() { ctx = EVP_MD_CTX_new(); }
    ~DigestGuard() {
        if (ctx) EVP_MD_CTX_free(ctx);
    }
    DigestGuard(const DigestGuard&) = delete;
    DigestGuard& operator=(const DigestGuard&) = delete;
    bool Begin() {
        return ctx && EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1;
    }
    void Update(const void* data, size_t len) {
        EVP_DigestUpdate(ctx, data, len);
    }
};

// Resolves cache paths for an Options set. Returns false on IO error.
bool ResolvePaths(const ModelFileDownloader::Options& opt,
                  std::filesystem::path* dir_out,
                  std::filesystem::path* final_out,
                  std::filesystem::path* part_out,
                  std::string* error) {
    namespace fs = std::filesystem;
    fs::path dir;
    if (opt.scope.empty() || opt.key.empty()) {
        if (error) *error = "empty cache scope/key";
        return false;
    }
    dir = XER::Cache::CacheManager::Instance().DirectoryFor(opt.scope,
                                                            opt.key);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        if (error) *error = "create cache dir: " + ec.message();
        return false;
    }
    *dir_out = dir;
    *final_out = dir / opt.file_name;
    *part_out = dir / (opt.file_name + ".part");
    // file_name may carry repo-relative subdirectories (HF layouts).
    std::error_code ec2;
    fs::create_directories(final_out->parent_path(), ec2);
    if (ec2) {
        if (error) *error = "create file parent dir: " + ec2.message();
        return false;
    }
    return true;
}

uint64_t FileSize(const std::filesystem::path& p) {
    std::error_code ec;
    const auto sz = std::filesystem::file_size(p, ec);
    return ec ? 0 : static_cast<uint64_t>(sz);
}

// Verifies an existing file against an optional sha256; size check first.
bool VerifyExisting(const std::filesystem::path& path, uint64_t expected_size,
                    const std::string& sha256, bool verify_checksum,
                    std::string* error) {
    if (expected_size != 0 && FileSize(path) != expected_size) {
        if (error) *error = "cached file size mismatch";
        return false;
    }
    if (!verify_checksum || sha256.empty()) return true;
    // Hash the file and compare.
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open cached file";
        return false;
    }
    DigestGuard dg;
    if (!dg.Begin()) {
        if (error) *error = "digest init failed";
        return false;
    }
    std::vector<uint8_t> buf(kIoBufSize);
    while (in) {
        in.read(reinterpret_cast<char*>(buf.data()),
                static_cast<std::streamsize>(buf.size()));
        const auto n = in.gcount();
        if (n > 0) dg.Update(buf.data(), static_cast<size_t>(n));
    }
    if (in.bad()) {
        if (error) *error = "read cached file failed";
        return false;
    }
    const std::string got = HexDigest(dg.ctx);
    if (Lower(got) != Lower(sha256)) {
        if (error) *error = "sha256 mismatch";
        return false;
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// ModelFileDownloader
// ---------------------------------------------------------------------------

bool ModelFileDownloader::IsCached(const Options& options) {
    std::filesystem::path dir, final_path, part_path;
    std::string err;
    if (!ResolvePaths(options, &dir, &final_path, &part_path, &err))
        return false;
    if (!std::filesystem::exists(final_path)) return false;
    return VerifyExisting(final_path, options.expected_size,
                          options.sha256, options.verify_checksum, &err);
}

bool ModelFileDownloader::WriteManifest(const Options& options,
                                        const std::string& json,
                                        std::string* error_out) {
    namespace fs = std::filesystem;
    fs::path dir;
    if (XER::Cache::CacheManager::Instance()
            .WritePersistentText(options.scope, options.key, "manifest.json",
                                 json, error_out)) {
        return true;
    }
    // Fallback: write next to the file when CacheManager rejects the key.
    fs::path dir2, fin, part;
    std::string err;
    if (ResolvePaths(options, &dir2, &fin, &part, &err)) {
        std::ofstream out(dir2 / "manifest.json",
                          std::ios::binary | std::ios::trunc);
        if (out) {
            out << json;
            return static_cast<bool>(out);
        }
    }
    return false;
}

bool ModelFileDownloader::Download(const std::string& url,
                                   const Options& options,
                                   ProgressCallback on_progress,
                                   DoneCallback on_done) {
    if (url.empty() || options.key.empty() || options.file_name.empty()) {
        if (on_done) on_done(false, "empty url/key/file_name", {});
        return false;
    }
    struct State {
        std::string url;
        Options options;
        ProgressCallback progress;
        DoneCallback done;
    };
    auto state = std::make_shared<State>(State{url, options, on_progress,
                                               on_done});
    XER::AsyncIO::IO::Async::IOThreadPool::GetSharedInstance().Enqueue(
        [state]() {
            std::string err;
            std::filesystem::path out;
            const bool ok = ModelFileDownloader().DownloadSync(
                state->url, state->options, state->progress, &err, &out);
            if (state->done) {
                state->done(ok, err, ok ? out : std::filesystem::path{});
            }
        });
    return true;
}

bool ModelFileDownloader::DownloadSync(const std::string& url,
                                       const Options& options,
                                       ProgressCallback on_progress,
                                       std::string* error_out,
                                       std::filesystem::path* out_path) {
    namespace fs = std::filesystem;

    fs::path dir, final_path, part_path;
    if (!ResolvePaths(options, &dir, &final_path, &part_path, error_out))
        return false;

    // Fast path: a verified file already exists.
    if (fs::exists(final_path)) {
        std::string verr;
        if (VerifyExisting(final_path, options.expected_size,
                           options.sha256, options.verify_checksum, &verr)) {
            if (out_path) *out_path = final_path;
            if (on_progress)
                on_progress(options.expected_size, options.expected_size);
            return true;
        }
        // Corrupt cache entry — remove and re-download.
        std::error_code ec;
        fs::remove(final_path, ec);
    }

    const int max_retries = options.max_retries > 0 ? options.max_retries : 1;
    std::string last_error;

    for (int attempt = 0; attempt < max_retries; ++attempt) {
        if (attempt > 0) {
            // Brief backoff before reconnecting.
            std::this_thread::sleep_for(
                std::chrono::milliseconds(200 * attempt));
        }

        // Resume offset = current .part size.
        uint64_t part_size = FileSize(part_path);
        const bool want_range = part_size > 0;

        internal::HttpStream stream;
        const std::string ca = options.ca_bundle_path.empty()
                                   ? internal::DefaultCaBundlePath()
                                   : options.ca_bundle_path;
        stream.SetCaBundlePath(ca);
        stream.SetSkipVerify(options.skip_verify);

        const std::string range =
            want_range ? "bytes=" + std::to_string(part_size) + "-" : "";
        std::string open_err;
        if (!stream.Open(url, options.headers, range, kMaxRedirects,
                         &open_err)) {
            last_error = open_err;
            continue;  // retry
        }

        const int status = stream.Info().status;
        if (status == 416) {
            // Range not satisfiable — stale .part; drop and retry once.
            std::error_code ec;
            fs::remove(part_path, ec);
            last_error = "server rejected resume range (416)";
            continue;
        }
        if (status != 200 && status != 206) {
            last_error = "HTTP " + std::to_string(status) + " " +
                         stream.Info().status_text;
            return false;  // auth/404/etc — retrying won't help
        }

        // Decide append vs restart.
        uint64_t offset = 0;
        if (status == 206) {
            offset = stream.ResumeOffset();
            if (offset != part_size) {
                // Server resumed from a different offset — restart cleanly.
                std::error_code ec;
                fs::remove(part_path, ec);
                offset = 0;
            }
        } else if (want_range) {
            // Server ignored Range — restart from byte 0.
            std::error_code ec;
            fs::remove(part_path, ec);
            offset = 0;
        }

        // Total expected: prefer server's Content-Range total, then
        // Content-Length, then caller-supplied size.
        uint64_t total = stream.ExpectedBodyBytes();
        if (total == 0) total = options.expected_size;

        // Open the part file for appending at |offset|.
        std::ofstream out;
        out.open(part_path, std::ios::binary | std::ios::app);
        if (!out) {
            last_error = "cannot open part file for writing";
            return false;
        }
        if (offset == 0) {
            out.close();
            out.open(part_path,
                     std::ios::binary | std::ios::out | std::ios::trunc);
            if (!out) {
                last_error = "cannot truncate part file";
                return false;
            }
        }

        // Hashing: only when verification requested. Existing bytes are
        // pre-hashed so the digest covers the complete file.
        DigestGuard dg;
        const bool do_hash = options.verify_checksum &&
                             !options.sha256.empty();
        if (do_hash) {
            if (!dg.Begin() ||
                (offset > 0 && !([&] {
                     bool ok = true;
                     HashFileInto(dg.ctx, part_path, &ok);
                     return ok;
                 }()))) {
                last_error = "sha256 init failed";
                return false;
            }
        }

        std::vector<uint8_t> buf(kIoBufSize);
        bool io_error = false;
        uint64_t written = offset;
        for (;;) {
            const int n = stream.ReadBody(buf.data(), buf.size());
            if (n < 0) {
                last_error = stream.Error().empty()
                                 ? "body read failed"
                                 : stream.Error();
                io_error = true;
                break;
            }
            if (n == 0) break;  // clean EOF
            out.write(reinterpret_cast<const char*>(buf.data()), n);
            if (!out) {
                last_error = "write to part file failed";
                io_error = true;
                break;
            }
            if (do_hash) dg.Update(buf.data(), static_cast<size_t>(n));
            written += static_cast<uint64_t>(n);
            if (on_progress) on_progress(written, total);
        }
        out.close();

        if (io_error) continue;  // resume on next attempt

        // Completion checks.
        const uint64_t final_size = FileSize(part_path);
        if (total != 0 && final_size != total) {
            // Server closed early — treat as retryable unless attempts out.
            last_error = "incomplete body: got " +
                         std::to_string(final_size) + " of " +
                         std::to_string(total) + " bytes";
            continue;
        }
        if (options.expected_size != 0 &&
            final_size != options.expected_size) {
            last_error = "size mismatch: got " +
                         std::to_string(final_size) + ", expected " +
                         std::to_string(options.expected_size);
            std::error_code ec;
            fs::remove(part_path, ec);
            return false;
        }

        if (do_hash) {
            const std::string got = HexDigest(dg.ctx);
            if (Lower(got) != Lower(options.sha256)) {
                last_error = "sha256 mismatch";
                std::error_code ec;
                fs::remove(part_path, ec);
                return false;
            }
        }

        // Publish: rename .part → final.
        std::error_code ec;
        fs::rename(part_path, final_path, ec);
        if (ec) {
            // Windows rename fails if destination exists — remove first.
            fs::remove(final_path, ec);
            fs::rename(part_path, final_path, ec);
            if (ec) {
                last_error = "rename part file: " + ec.message();
                return false;
            }
        }
        if (out_path) *out_path = final_path;
        if (on_progress && total != 0)
            on_progress(total, total);
        return true;
    }

    if (error_out) *error_out = last_error.empty() ? "download failed"
                                                    : last_error;
    return false;
}

}  // namespace content::civitai
