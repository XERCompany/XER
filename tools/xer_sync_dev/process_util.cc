/**
 * This file is part of XER, Tools open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "process_util.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <thread>
#include <chrono>

#include "common.h"

#include <curl/curl.h>

// CURL_VERSION_NATIVE_CA is not in every shipped curl.h (the feature
// bit arrived in later 8.x releases); when it's absent the build
// simply can't report native-CA support.
#ifndef CURL_VERSION_NATIVE_CA
#define CURL_VERSION_NATIVE_CA 0
#endif

namespace xer_sync_dev {

std::string ShellQuote(const std::string& arg) {
    std::string q = "\"";
    for (char c : arg) {
        if (c == '"') q += "\\\"";
        else q += c;
    }
    q += "\"";
    return q;
}

ProcResult RunProcess(const std::string& command_line, bool merge_stderr,
                      long /*timeout_ms*/) {
    ProcResult r;
    std::string cmd = command_line;
    if (merge_stderr) cmd += " 2>&1";

#ifdef _WIN32
    FILE* pipe = _popen(cmd.c_str(), "rb");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) return r;
    r.ran = true;

    std::array<char, 8192> buf{};
    size_t n = 0;
    while ((n = std::fread(buf.data(), 1, buf.size(), pipe)) > 0) {
        r.output.append(buf.data(), n);
    }
#ifdef _WIN32
    r.exit_code = _pclose(pipe);
#else
    r.exit_code = pclose(pipe);
#endif
    return r;
}

bool CurlHasNativeCa() {
    const curl_version_info_data* vi =
        curl_version_info(CURLVERSION_NOW);
    return vi &&
           (vi->features & CURL_VERSION_NATIVE_CA) != 0;
}

namespace {

bool PathExists(const std::string& p) {
    if (p.empty()) return false;
    std::error_code ec;
    return fs::is_regular_file(fs::path(p), ec);
}

}  // namespace

std::string ResolveCaBundle(const std::string& explicit_path,
                            const std::vector<std::string>& candidates) {
    // 1) Explicit --cacert wins verbatim — even a missing file should
    //    surface curl's own "can't open CAfile" error, not a silent
    //    fallback that hides a user typo.
    if (!explicit_path.empty()) return explicit_path;

    // 2) curl's own env conventions.
    for (const char* var : {"CURL_CA_BUNDLE", "SSL_CERT_FILE"}) {
        const char* v = std::getenv(var);
        if (v && PathExists(v)) return v;
    }

    // 3) Caller-supplied candidates (platform paths, repo-local
    //    third_party/cacert.pem, ...), first hit wins.
    for (const std::string& c : candidates) {
        if (PathExists(c)) return c;
    }
    return {};
}

namespace {

// file:// URI or bare local path -> filesystem path ("" when `url`
// is a real remote URL). Percent-decoding is deliberately minimal —
// selftest fixtures use plain ASCII paths.
std::string LocalPathFor(const std::string& url) {
    if (StartsWith(url, "file://")) {
        std::string p = url.substr(7);
        // file:///C:/x -> C:/x (Windows); file:///x -> /x (POSIX).
        if (p.size() >= 3 && p[0] == '/' &&
            ((p[2] == ':' && std::isalpha(
                  static_cast<unsigned char>(p[1]))) ||
             (p.size() >= 4 && p[2] == '|' &&
              std::isalpha(static_cast<unsigned char>(p[1]))))) {
            p = p.substr(1);
            if (p.size() >= 2 && p[1] == '|') p[1] = ':';
        }
        std::string out;
        out.reserve(p.size());
        for (size_t i = 0; i < p.size(); ++i) {
            if (p[i] == '%' && i + 2 < p.size()) {
                const std::string hex = p.substr(i + 1, 2);
                out += static_cast<char>(
                    std::strtol(hex.c_str(), nullptr, 16));
                i += 2;
            } else {
                out += p[i];
            }
        }
        return out;
    }
    // No "://" at all -> treat as a plain filesystem path.
    if (url.find("://") == std::string::npos) return url;
    return {};
}

struct CurlGuard {
    CurlGuard() = default;
    CURL* h = curl_easy_init();
    ~CurlGuard() {
        if (h) curl_easy_cleanup(h);
    }
    CurlGuard(const CurlGuard&) = delete;
    CurlGuard& operator=(const CurlGuard&) = delete;
};

}  // namespace

bool HttpDownload(const std::string& url, const std::string& dest_path,
                  std::string* error, const DownloadOptions& opt) {
    auto fail = [error](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    if (opt.attempts) *opt.attempts = 0;

    // Local fixture support: file:// URIs and bare paths bypass curl.
    const std::string local = LocalPathFor(url);
    if (!local.empty()) {
        std::error_code ec;
        if (!fs::is_regular_file(local, ec)) {
            return fail("HttpDownload: local source not found: " + local);
        }
        fs::create_directories(fs::path(dest_path).parent_path(), ec);
        fs::copy_file(local, dest_path,
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
            return fail("HttpDownload: copy " + local + " -> " +
                        dest_path + " failed: " + ec.message());
        }
        if (opt.attempts) *opt.attempts = 1;
        return true;
    }

    // TLS strategy, resolved once per call: explicit --cacert >
    // CURLSSLOPT_NATIVE_CA > probed CURLOPT_CAINFO.
    std::string cainfo;
    const bool native_ca =
        opt.cacert.empty() && CurlHasNativeCa();
    if (!opt.cacert.empty()) {
        cainfo = opt.cacert;
    } else if (!native_ca) {
        cainfo = ResolveCaBundle({}, opt.ca_candidates);
    }

    const int max_attempts = std::max(1, opt.max_retries);
    std::string last_err;

    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        if (opt.attempts) *opt.attempts = attempt;
        if (attempt > 1 && opt.retry_backoff_ms > 0) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(opt.retry_backoff_ms));
        }

        // Resume a partial file when allowed.
        uint64_t have = 0;
        {
            std::error_code ec;
            if (fs::is_regular_file(dest_path, ec)) {
                have = fs::file_size(dest_path, ec);
                if (ec) have = 0;
            }
        }
        const bool try_resume = opt.resume && have > 0;

        std::ofstream out(dest_path,
                          std::ios::binary |
                              (try_resume ? std::ios::app
                                          : std::ios::trunc));
        if (!out.is_open()) {
            return fail("HttpDownload: cannot open " + dest_path +
                        " for writing");
        }

        CurlGuard cg;
        if (!cg.h) return fail("HttpDownload: curl_easy_init failed");
        CURL* curl = cg.h;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "xer_sync_dev/1.0");
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
        // Stall guard instead of an overall timeout: abort only when
        // the transfer trickles below 1 KB/s for 60 s straight — a
        // healthy ~500 MB tarball may take arbitrarily long.
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
        if (try_resume) {
            curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE,
                             static_cast<curl_off_t>(have));
        }
        if (native_ca) {
            curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS,
                             static_cast<long>(CURLSSLOPT_NATIVE_CA));
        }
        if (!cainfo.empty()) {
            curl_easy_setopt(curl, CURLOPT_CAINFO, cainfo.c_str());
        }

        auto write_cb = [](char* ptr, size_t size, size_t nmemb,
                           void* userdata) -> size_t {
            auto* stream = static_cast<std::ofstream*>(userdata);
            const size_t total = size * nmemb;
            stream->write(ptr, static_cast<std::streamsize>(total));
            return stream->good() ? total : 0;
        };
        curl_easy_setopt(
            curl, CURLOPT_WRITEFUNCTION,
            static_cast<size_t (*)(char*, size_t, size_t, void*)>(
                write_cb));
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);

        auto progress_cb = [](void* clientp, double dltotal,
                              double dlnow, double, double) -> int {
            auto* cb = static_cast<
                const std::function<void(double, double)>*>(clientp);
            if (cb && *cb) (*cb)(dltotal, dlnow);
            return 0;
        };
        if (opt.progress) {
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,
                             progress_cb);
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA,
                             &opt.progress);
        }

        const CURLcode rc = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        out.close();

        if (rc == CURLE_OK) return true;

        last_err = "HttpDownload: GET " + url +
                   " failed: " + curl_easy_strerror(rc) +
                   (status ? " (HTTP " + std::to_string(status) + ")"
                           : "") +
                   (attempt < max_attempts
                        ? " [attempt " + std::to_string(attempt) + "/" +
                              std::to_string(max_attempts) + "]"
                        : "");

        if (try_resume) {
            // A failed resume is not trustworthy (server may have
            // ignored Range and appended a full body) — drop the
            // partial so the next attempt starts clean.
            std::error_code ec;
            fs::remove(dest_path, ec);
        }
    }

    // All attempts spent — no partial left behind.
    std::error_code ec;
    fs::remove(dest_path, ec);
    return fail(last_err);
}

}  // namespace xer_sync_dev
