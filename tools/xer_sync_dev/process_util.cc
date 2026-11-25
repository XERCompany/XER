/**
 * This file is part of XER, Tools open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "process_util.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "common.h"

#include <curl/curl.h>

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

bool HttpDownload(const std::string& url, const std::string& dest_path,
                  std::string* error,
                  const std::function<void(double, double)>& progress) {
    auto fail = [error](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };

    std::ofstream out(dest_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return fail("HttpDownload: cannot open " + dest_path +
                    " for writing");
    }

    CURL* curl = curl_easy_init();
    if (!curl) return fail("HttpDownload: curl_easy_init failed");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "xer_sync_dev/1.0");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    // Stall guard: abort when the transfer trickles below 1 KB/s for
    // 60 s straight — keeps `sync` from hanging on a dead mirror.
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);

    auto write_cb = [](char* ptr, size_t size, size_t nmemb,
                       void* userdata) -> size_t {
        auto* stream = static_cast<std::ofstream*>(userdata);
        const size_t total = size * nmemb;
        stream->write(ptr, static_cast<std::streamsize>(total));
        return stream->good() ? total : 0;
    };
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                     static_cast<size_t (*)(char*, size_t, size_t, void*)>(
                         write_cb));
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);

    auto progress_cb = [](void* clientp, double dltotal, double dlnow,
                          double, double) -> int {
        auto* cb = static_cast<const std::function<void(double, double)>*>(
            clientp);
        if (cb && *cb) (*cb)(dltotal, dlnow);
        return 0;
    };
    if (progress) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);
    }

    const CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    out.close();

    if (rc != CURLE_OK) {
        std::error_code ec;
        fs::remove(dest_path, ec);
        return fail("HttpDownload: GET " + url + " failed: " +
                    curl_easy_strerror(rc) +
                    (status ? " (HTTP " + std::to_string(status) + ")" : ""));
    }
    return true;
}

}  // namespace xer_sync_dev
