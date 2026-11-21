// URL / platform media import: downloads an http(s) asset into the
// media cache dir and hands the local file to the caller, which feeds
// it through the normal catalog import path (MediaCatalog::Import +
// media-prep poster request — same as a file-dialog import).
//
// Two transports:
//   * Direct URLs   — the network stack's synchronous
//                     Url::URLRequestJob (src/content/network) GETs the
//                     body; the filename comes from Content-Disposition
//                     or the URL basename.
//   * Platform URLs — youtube/vimeo/tiktok/instagram/twitch need
//                     yt-dlp: FindYtDlp() detects it on PATH and in the
//                     usual install spots (incl. the MSYS2 clang64
//                     package), then Download() spawns it with pipes,
//                     parses "[download] NN%" progress lines, and
//                     locates the produced file. The child is job-grouped
//                     via WinJobGroup::Assign so it dies with the editor.
//
// UI-free core module: classification/sanitizing are pure functions the
// smoke checks exercise offline; Download() is synchronous — callers
// run it on a worker (the app uses a Lab::AsyncOp and marshals the
// result back in Tick()).
#pragma once

#include <functional>
#include <string>

namespace XER::VideoEditor::UrlImport {

enum class UrlKind {
    kInvalid,    // not a usable http(s) URL
    kDirect,     // plain http(s) — fetch the body as the media file
    kPlatform,   // known video-platform host — route through yt-dlp
};

// Parses scheme + host and classifies the URL. Only http:// and
// https:// (any case) qualify; everything else is kInvalid — file://,
// ftp://, bare paths and empty strings are all rejected here.
UrlKind ClassifyUrl(const std::string& url);
// Convenience: kPlatform.
bool IsPlatformUrl(const std::string& url);
// Lowercased host component, "" when the URL has no usable host:port
// authority (no "://", empty host).
std::string HostOf(const std::string& url);
// Last '/'-separated path segment with query/fragment stripped and
// %-escapes decoded ("" when the path ends in '/').
std::string UrlBasename(const std::string& url);
// RFC 6266 filename extraction: prefers filename*=UTF-8''…, falls back
// to filename="…" / filename=token. "" when absent.
std::string FilenameFromContentDisposition(const std::string& header);
// Makes `name` safe for a Windows filename: illegal characters
// (<>:"/\|?* + controls) become '_', trailing dots/spaces are trimmed,
// empty collapses to "download", and the result is capped at ~120
// chars so the cache-dir path stays under MAX_PATH.
std::string SanitizeFilename(const std::string& name);
// Percent-decodes a URL component ('+' kept literal — path semantics,
// not form-encoding).
std::string PercentDecode(const std::string& text);

// The media download cache — <catalog_db_dir>/downloads, created on
// demand. Shares the DefaultDbPath env chain (LOCALAPPDATA/
// XER/VideoEditor on Windows) so downloads sit next to the catalog,
// never inside the project.
std::string MediaCacheDir();

// Absolute path to a runnable yt-dlp executable, "" when none is
// found. Searches PATH, then the common install spots:
// MSYS2 clang64/mingw64/usr-bin, WinGet links and Program Files.
// Result is cached — a yt-dlp install mid-session requires a restart
// (acceptable: installs are one-off).
std::string FindYtDlp();

struct Result {
    bool ok = false;
    bool cancelled = false;
    bool used_ytdlp = false;
    std::string path;    // UTF-8 absolute path of the downloaded file
    std::string error;   // failure detail for the status line
    std::string log;     // tail of yt-dlp output / transfer notes
};

// (fraction 0..1, status text). fraction < 0 = indeterminate.
using ProgressFn = std::function<void(float, const std::string&)>;
// True aborts the transfer — the yt-dlp child is terminated and the
// partial file cleaned up; an in-flight GET finishes and is discarded.
using CancelFn = std::function<bool()>;

// Synchronous download into `dest_dir` (created when missing; empty
// picks MediaCacheDir()). Platform URLs go through yt-dlp when found —
// otherwise the status reports "Install yt-dlp for platform links" and
// the URL is attempted as a direct GET anyway (works for hosts that
// still serve raw media URLs).
Result Download(const std::string& url, const std::string& dest_dir,
                const ProgressFn& progress, const CancelFn& cancel);

}  // namespace XER::VideoEditor::UrlImport
