/**
 * This file is part of XER, Tools open source project.
 * Copyright (C) 2026 Yoshi A.
 *
 * xer_sync_dev — process spawning + HTTP download helpers.
 * Git is invoked via the CLI (honest, documented); HTTP downloads
 * stream through libcurl (CURL::libcurl, already a repo dependency)
 * rather than XER_NetClient because NetClient::DownloadFile buffers
 * the whole body in memory — wrong shape for ~500 MB CEF tarballs.
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace xer_sync_dev {

struct ProcResult {
    bool ran = false;      // process could be spawned at all
    int exit_code = -1;
    std::string output;    // stdout (+stderr when merge_stderr)
};

// Runs `command_line` through the platform shell (cmd /c on Windows,
// /bin/sh elsewhere) and captures output. Callers build the command
// line themselves — use ShellQuote() on every interpolated path.
// When merge_stderr is true, "2>&1" is appended so progress/errors
// land in `output` (git writes progress to stderr).
ProcResult RunProcess(const std::string& command_line,
                      bool merge_stderr = true,
                      long timeout_ms = 0);

// Wraps `arg` in double quotes, escaping embedded quotes/backslashes
// enough for cmd.exe / sh round-tripping.
std::string ShellQuote(const std::string& arg);

// --- downloads ---------------------------------------------------------

// True when the linked libcurl advertises CURL_VERSION_NATIVE_CA
// (curl 7.71+; OpenSSL builds can read the Windows trust store via
// CertOpenSystemStore, schannel builds get it for free). MSYS2's
// OpenSSL curl does NOT enable it, so the bundle-probe path matters.
bool CurlHasNativeCa();

// Picks the CA bundle CURLOPT_CAINFO should use.
//   1. `explicit_path` (--cacert) wins verbatim — user knows best.
//   2. $CURL_CA_BUNDLE, then $SSL_CERT_FILE (must exist on disk).
//   3. First existing path in `candidates`.
// Returns "" when nothing usable was found (curl falls back to its
// compiled-in default, which is broken for a native Windows exe that
// was linked against an MSYS2 OpenSSL curl — hence this probing).
std::string ResolveCaBundle(const std::string& explicit_path,
                            const std::vector<std::string>& candidates);

struct DownloadOptions {
    std::string cacert;                 // --cacert override for CAINFO
    std::vector<std::string> ca_candidates;  // extra CA probe paths
    int max_retries = 3;                // total attempts for this URL
    int retry_backoff_ms = 1500;        // sleep between attempts
    bool resume = true;                 // CURLOPT_RESUME_FROM_LARGE on
                                        // a pre-existing partial file
    std::function<void(double, double)> progress;
    int* attempts = nullptr;            // out: attempts actually run
                                        // (debug/selftest seam)
};

// Streams `url` to `dest_path` via libcurl (write callback — no
// in-memory buffering). Follows redirects, fails on HTTP >= 400.
//
// `url` may also be a `file://` URI or a plain local path (selftest
// builds zip fixtures on disk) — those are copied, not curled.
//
// TLS: CURLOPT_SSL_OPTIONS/CURLSSLOPT_NATIVE_CA when the libcurl build
// supports it, else ResolveCaBundle() probing -> CURLOPT_CAINFO.
// `opt.cacert` (--cacert) always wins.
//
// Timeout policy: CONNECTTIMEOUT 30s, no overall CURLOPT_TIMEOUT —
// the stall guard (LOW_SPEED_LIMIT 1 KB/s for LOW_SPEED_TIME 60s) is
// what aborts dead mirrors, so a healthy ~500 MB CEF tarball can take
// as long as it needs.
//
// Retry: up to `opt.max_retries` attempts; on a retry with a partial
// dest file and `resume` set, the transfer resumes via
// CURLOPT_RESUME_FROM_LARGE. A failed resume attempt deletes the
// partial (the server may have ignored Range and appended a full
// body) and the next attempt restarts cleanly.
bool HttpDownload(const std::string& url, const std::string& dest_path,
                  std::string* error, const DownloadOptions& opt = {});

}  // namespace xer_sync_dev
