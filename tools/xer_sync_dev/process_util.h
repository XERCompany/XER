/**
 * This file is part of XER, Tools open source project.
 * Copyright (C) 2026 Yoshi A.
 *
 * xer_sync_dev — process spawning + HTTP download helpers.
 * Git is invoked via the CLI (honest, documented); HTTP downloads
 * stream through libcurl (CURL::libcurl, already a repo dependency)
 * rather than XER_NetClient because NetClient::DownloadFile buffers
 * the whole body in memory — wrong shape for ~300 MB CEF tarballs.
 */
#pragma once

#include <functional>
#include <string>

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

// Streams `url` to `dest_path` via libcurl (file write callback — no
// in-memory buffering). Follows redirects, fails on HTTP >= 400.
// `progress(down_total, down_now)` is optional throttling output.
bool HttpDownload(const std::string& url, const std::string& dest_path,
                  std::string* error,
                  const std::function<void(double, double)>& progress = {});

}  // namespace xer_sync_dev
