#include "url_import.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "content/network/url/url_request.h"
#include "content/network/url/url_request_context.h"
#include "content/network/url/url_request_job.h"
#include "helper/env_utils.h"
#include "helper/path_utils.h"
#include "helper/string_utils.h"
#include "media_catalog.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "helper/win_job.h"   // WinJobGroup::Assign — child dies with us
#endif

namespace XER::VideoEditor::UrlImport {
namespace {

namespace Net = XER::Network::Url;
namespace Path = XER::Helper::Path;
namespace Str = XER::Helper::String;

// Known video-platform hosts — a URL whose host IS one of these or a
// subdomain of one classifies as kPlatform (yt-dlp territory). The
// table drives both ClassifyUrl() and the offline smoke check.
constexpr const char* kPlatformHosts[] = {
    "youtube.com",   "youtu.be",   "vimeo.com",  "tiktok.com",
    "instagram.com", "twitch.tv",
};

bool HostMatches(const std::string& host, const char* domain) {
    if (host == domain) return true;
    const std::size_t n = std::strlen(domain);
    return host.size() > n &&
           host.compare(host.size() - n, n, domain) == 0 &&
           host[host.size() - n - 1] == '.';
}

// Case-insensitive response-header lookup — HttpResponseHeaders keeps
// the wire casing (same helper shape as url_request_job.cc's).
std::string HeaderValue(const Net::URLResponse& r, const char* name) {
    const std::size_t n = std::strlen(name);
    for (const auto& [k, v] : r.headers) {
        if (k.size() == n &&
            std::equal(k.begin(), k.end(), name, name + n,
                       [](char a, char b) {
                           return std::tolower(
                                      static_cast<unsigned char>(a)) ==
                                  std::tolower(
                                      static_cast<unsigned char>(b));
                       })) {
            return v;
        }
    }
    return {};
}

// RFC 5987 decode for filename*=UTF-8''<percent-encoded>.
std::string Decode5987(const std::string& value) {
    const auto tick = value.find_last_of('\'');
    if (tick == std::string::npos) return {};
    return PercentDecode(value.substr(tick + 1));
}

void Report(const ProgressFn& progress, float fraction,
            const std::string& status) {
    if (progress) progress(fraction, status);
}

// Unique file name inside `dir`: "<name>", "<base>-2<ext>", ... —
// checked against the file itself (a direct download picks its final
// name up front).
std::string UniqueFileName(const std::filesystem::path& dir,
                           const std::string& name) {
    const std::filesystem::path np = Path::FromUtf8(name);
    const std::string stem = np.stem().string();
    const std::string ext = np.extension().string();
    for (int i = 1; i < 1000; ++i) {
        const std::string candidate =
            i == 1 ? name
                   : stem + "-" + std::to_string(i) + ext;
        std::error_code ec;
        if (!std::filesystem::exists(dir / Path::FromUtf8(candidate),
                                     ec)) {
            return candidate;
        }
    }
    return name;
}

// Unique stem inside `dir`: "<base>", "<base>-2", ... — every file a
// yt-dlp run produces shares the stem, which makes the produced-file
// scan and cancel-cleanup exact.
std::string UniqueStem(const std::filesystem::path& dir,
                       const std::string& base) {
    for (int i = 1; i < 1000; ++i) {
        const std::string stem =
            i == 1 ? base : base + "-" + std::to_string(i);
        std::error_code ec;
        bool taken = false;
        for (const auto& de :
             std::filesystem::directory_iterator(dir, ec)) {
            if (de.path().stem().string() == stem) {
                taken = true;
                break;
            }
        }
        if (!taken) return stem;
    }
    return base + "-x";
}

// Files in `dir` whose stem equals `stem` (any extension) — yt-dlp's
// "<stem>.%(ext)s" output plus its .part/.ytdl intermediates.
std::vector<std::filesystem::path> StemFiles(
    const std::filesystem::path& dir, const std::string& stem) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (const auto& de : std::filesystem::directory_iterator(dir, ec)) {
        std::error_code rec;
        if (!de.is_regular_file(rec)) continue;
        if (de.path().stem().string() == stem) {
            out.push_back(de.path());
        }
    }
    return out;
}

// Deletes partial output left by a failed/cancelled yt-dlp run —
// the stem is unique per Download call so nothing pre-existing matches.
void CleanStem(const std::filesystem::path& dir,
               const std::string& stem) {
    for (const auto& f : StemFiles(dir, stem)) {
        std::error_code ec;
        std::filesystem::remove(f, ec);
    }
}

// Direct GET through the in-repo network stack. The job is fully
// buffered (URLRequestJob returns the whole body), so progress is
// indeterminate until the response lands — the status line carries
// the target host instead.
Result RunHttpGet(const std::string& url,
                  const std::filesystem::path& dir,
                  const ProgressFn& progress, const CancelFn& cancel) {
    Result result;
    Report(progress, -1.0f, "Downloading " + HostOf(url) + "…");

    Net::URLRequestContext ctx;
    ctx.user_agent = "XER-VideoEditor/1.0";
    ctx.socket_timeout_ms = 120000;

    Net::URLRequest req;
    req.method = "GET";
    req.url = url;
    req.follow_redirects = true;
    req.max_redirects = 5;
    req.timeout_ms = 120000;

    Net::URLRequestJob job(ctx);
    Net::URLResponse response;
    std::string error;
    std::string final_url;
    if (!job.Run(req, &response, &error, &final_url)) {
        result.error = "GET failed: " + error;
        return result;
    }
    if (cancel && cancel()) {
        result.cancelled = true;
        return result;
    }
    if (response.status_code >= 400) {
        result.error = "HTTP " + std::to_string(response.status_code) +
                       " " + response.status_text;
        return result;
    }
    if (response.body.empty()) {
        result.error = "empty response body";
        return result;
    }

    // Filename: Content-Disposition wins, then the basename of the
    // FINAL (post-redirect) URL — download links routinely 30x onto
    // the real file, and naming off the original URL would drop the
    // extension onto an extensionless alias like "dl" — then a fixed
    // fallback. Sanitized and deduped inside the cache dir.
    std::string name = FilenameFromContentDisposition(
        HeaderValue(response, "Content-Disposition"));
    if (name.empty()) {
        name = UrlBasename(final_url.empty() ? url : final_url);
    }
    name = SanitizeFilename(name);
    const std::filesystem::path dest =
        dir / Path::FromUtf8(UniqueFileName(dir, name));
    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        result.error = "cannot write " + Path::ToUtf8(dest);
        return result;
    }
    out.write(reinterpret_cast<const char*>(response.body.data()),
              static_cast<std::streamsize>(response.body.size()));
    if (!out.good()) {
        result.error = "write failed for " + Path::ToUtf8(dest);
        return result;
    }
    out.close();

    result.ok = true;
    result.path = Path::ToUtf8(dest);
    result.log = "HTTP " + std::to_string(response.status_code) + ", " +
                 std::to_string(response.body.size()) + " bytes";
    Report(progress, 1.0f, "Downloaded " + name);
    return result;
}

#if defined(_WIN32)
// Spawns yt-dlp with piped stdout/stderr, streams "[download] NN%"
// lines into progress, and resolves the produced file. The child joins
// the process-wide WinJobGroup so an editor death reaps it too.
Result RunYtDlp(const std::string& ytdlp, const std::string& url,
                const std::filesystem::path& dir,
                const std::string& stem, const ProgressFn& progress,
                const CancelFn& cancel) {
    Result result;
    result.used_ytdlp = true;
    Report(progress, 0.0f, "yt-dlp: resolving " + HostOf(url) + "…");

    const std::filesystem::path templ =
        dir / Path::FromUtf8(stem + ".%(ext)s");
    // bv*+ba/b: modern platforms (YouTube especially) no longer serve
    // pre-merged formats, so plain "best" has nothing to select —
    // merge best video+audio via the ffmpeg yt-dlp finds next to its
    // own exe (or on PATH), falling back to a combined file when one
    // exists. "*" and "+" need no shell quoting: CreateProcessW hands
    // the argv to the child unparsed.
    const std::wstring cmd =
        L"\"" + Path::FromUtf8(ytdlp).wstring() +
        L"\" --newline --no-playlist -f \"bv*+ba/b\""
        L" --print after_move:filepath -o \"" + templ.wstring() +
        L"\" \"" + Str::Utf8ToWide(url) + L"\"";

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE out_read = nullptr;
    HANDLE out_write = nullptr;
    if (!CreatePipe(&out_read, &out_write, &sa, 1 << 16)) {
        result.error = "CreatePipe failed";
        return result;
    }
    // The parent's read end must not be inherited or the child never
    // sees a clean EOF on the pipe.
    SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = out_write;
    si.hStdError = out_write;   // errors ride the same line stream
    PROCESS_INFORMATION pi{};
    std::wstring mutable_cmd = cmd;
    const BOOL spawned = CreateProcessW(
        nullptr, mutable_cmd.data(), nullptr, nullptr,
        /*inherit*/ TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(out_write);   // the child holds its own copy now
    if (!spawned) {
        CloseHandle(out_read);
        result.error = "cannot start yt-dlp (error " +
                       std::to_string(GetLastError()) + ")";
        return result;
    }
    XER::Helper::WinJobGroup::Shared().Assign(pi.hProcess);
    CloseHandle(pi.hThread);

    // Read the merged stdout/stderr stream line by line. Progress
    // arrives as "[download]  42.1% of ..." (--newline keeps each
    // update on its own line); "--print after_move:filepath" emits the
    // final file path which is collected as a fallback for the
    // stem-scan below.
    std::string carry;
    std::string printed_path;
    std::string tail;
    std::array<char, 8192> buf{};
    bool killed = false;
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(out_read, nullptr, 0, nullptr, &avail,
                           nullptr)) {
            break;   // pipe closed — child exited
        }
        if (avail == 0) {
            DWORD wait = WaitForSingleObject(pi.hProcess, 40);
            if (wait == WAIT_OBJECT_0) {
                // Drain whatever the exit flushed, then stop.
                DWORD n = 0;
                while (ReadFile(out_read, buf.data(),
                                static_cast<DWORD>(buf.size()), &n,
                                nullptr) &&
                       n > 0) {
                    carry.append(buf.data(), n);
                }
                break;
            }
            if (cancel && cancel()) {
                // The child is job-grouped and disposable (same
                // contract as the worker exes): terminate it and drop
                // the partial output.
                TerminateProcess(pi.hProcess, 2);
                killed = true;
                break;
            }
            continue;
        }
        DWORD n = 0;
        if (!ReadFile(out_read, buf.data(),
                      static_cast<DWORD>(buf.size()), &n, nullptr) ||
            n == 0) {
            break;
        }
        carry.append(buf.data(), n);
        for (;;) {
            const auto nl = carry.find('\n');
            if (nl == std::string::npos) break;
            std::string line = carry.substr(0, nl);
            carry.erase(0, nl + 1);
            while (!line.empty() &&
                   (line.back() == '\r' || line.back() == ' ')) {
                line.pop_back();
            }
            if (line.empty()) continue;
            tail = line;   // last line becomes the error context
            if (line.rfind("[download]", 0) == 0) {
                const auto pct = line.find('%');
                if (pct != std::string::npos) {
                    // "... NN.N% of ..." — the float right before '%'.
                    const auto begin =
                        line.find_last_of(" \t", pct - 1);
                    if (begin != std::string::npos) {
                        const float f = std::strtof(
                            line.c_str() + begin + 1, nullptr);
                        if (f > 0.0f) {
                            Report(progress,
                                   std::min(f / 100.0f, 1.0f),
                                   "yt-dlp: " + line);
                        }
                    }
                }
            } else if (line.rfind("ERROR:", 0) == 0) {
                tail = line;
            } else {
                // A bare absolute path = the --print output.
                if (line.size() > 2 && line[1] == ':' &&
                    (line[2] == '\\' || line[2] == '/')) {
                    printed_path = line;
                }
            }
        }
    }
    CloseHandle(out_read);
    DWORD exit_code = 1;
    WaitForSingleObject(pi.hProcess, 5000);
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    result.log = tail;

    if (killed) {
        CleanStem(dir, stem);
        result.cancelled = true;
        return result;
    }
    if (exit_code != 0) {
        CleanStem(dir, stem);
        result.error = tail.empty()
            ? "yt-dlp exited with code " + std::to_string(exit_code)
            : tail;
        return result;
    }

    // Resolve the produced file: the printed after_move path first,
    // then the newest file sharing our unique stem.
    std::filesystem::path produced;
    if (!printed_path.empty()) {
        const std::filesystem::path pp(
            Str::Utf8ToWide(printed_path));
        std::error_code ec;
        if (std::filesystem::is_regular_file(pp, ec)) {
            produced = pp;
        }
    }
    if (produced.empty()) {
        const auto files = StemFiles(dir, stem);
        const auto newest = std::max_element(
            files.begin(), files.end(),
            [](const std::filesystem::path& a,
               const std::filesystem::path& b) {
                std::error_code ea, eb;
                return std::filesystem::last_write_time(a, ea) <
                       std::filesystem::last_write_time(b, eb);
            });
        if (newest != files.end()) produced = *newest;
    }
    if (produced.empty()) {
        result.error = "yt-dlp finished but no file was produced";
        return result;
    }
    result.ok = true;
    result.path = Path::ToUtf8(produced);
    Report(progress, 1.0f, "Downloaded " + produced.filename().string());
    return result;
}
#endif  // _WIN32

}  // namespace

UrlKind ClassifyUrl(const std::string& url) {
    const std::string lower = Str::ToLowerAscii(url);
    if (lower.rfind("http://", 0) != 0 &&
        lower.rfind("https://", 0) != 0) {
        return UrlKind::kInvalid;
    }
    const std::string host = HostOf(url);
    if (host.empty()) return UrlKind::kInvalid;
    for (const char* domain : kPlatformHosts) {
        if (HostMatches(host, domain)) return UrlKind::kPlatform;
    }
    return UrlKind::kDirect;
}

bool IsPlatformUrl(const std::string& url) {
    return ClassifyUrl(url) == UrlKind::kPlatform;
}

std::string HostOf(const std::string& url) {
    const auto sep = url.find("://");
    if (sep == std::string::npos) return {};
    std::string rest = url.substr(sep + 3);
    const auto slash = rest.find_first_of("/?#");
    const std::string authority =
        slash == std::string::npos ? rest : rest.substr(0, slash);
    std::string host = authority;
    const auto at2 = host.find_last_of('@');
    if (at2 != std::string::npos) host = host.substr(at2 + 1);
    // Strip the port — IPv6 keeps its brackets, which never match a
    // platform domain anyway.
    if (!host.empty() && host.front() == '[') {
        const auto close = host.find(']');
        host = close == std::string::npos ? "" : host.substr(0, close + 1);
    } else {
        const auto colon = host.find(':');
        if (colon != std::string::npos) host = host.substr(0, colon);
    }
    return Str::ToLowerAscii(host);
}

std::string UrlBasename(const std::string& url) {
    std::string path = url;
    // Isolate the path: a bare "https://host" carries no filename —
    // the authority's slashes must not be mistaken for a basename.
    const auto scheme_end = path.find("://");
    if (scheme_end != std::string::npos) {
        const auto first_slash = path.find('/', scheme_end + 3);
        path = first_slash == std::string::npos
                   ? std::string()
                   : path.substr(first_slash);
    }
    const auto cut = path.find_first_of("?#");
    if (cut != std::string::npos) path.erase(cut);
    const auto slash = path.find_last_of('/');
    if (slash == std::string::npos || slash + 1 >= path.size()) {
        return {};
    }
    return PercentDecode(path.substr(slash + 1));
}

std::string FilenameFromContentDisposition(const std::string& header) {
    if (header.empty()) return {};
    // filename*=UTF-8''... wins over the plain filename= form (RFC
    // 6266: the extended notation is authoritative when both exist).
    const auto star = header.find("filename*=");
    if (star != std::string::npos) {
        std::string v = header.substr(star + 10);
        const auto semi = v.find(';');
        if (semi != std::string::npos) v.erase(semi);
        if (!v.empty() && v.front() == '"') v.erase(0, 1);
        if (!v.empty() && v.back() == '"') v.pop_back();
        const std::string decoded = Decode5987(v);
        if (!decoded.empty()) return decoded;
    }
    const auto plain = header.find("filename=");
    if (plain != std::string::npos) {
        std::string v = header.substr(plain + 9);
        const auto semi = v.find(';');
        if (semi != std::string::npos) v.erase(semi);
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) {
            v.erase(0, 1);
        }
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
            v = v.substr(1, v.size() - 2);
        }
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) {
            v.pop_back();
        }
        return v;
    }
    return {};
}

std::string PercentDecode(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const int hi = hex(text[i + 1]);
            const int lo = hex(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>(hi * 16 + lo));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

std::string SanitizeFilename(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (const unsigned char c : name) {
        const bool illegal =
            c < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' ||
            c == '/' || c == '\\' || c == '|' || c == '?' || c == '*';
        out.push_back(illegal ? '_' : static_cast<char>(c));
    }
    // Windows forbids trailing dots/spaces; trim them both ends.
    while (!out.empty() &&
           (out.back() == '.' || out.back() == ' ')) {
        out.pop_back();
    }
    const auto first = out.find_first_not_of(" .");
    if (first == std::string::npos) return "download";
    out.erase(0, first);
    if (out.size() > 120) {
        // Keep the extension readable after truncation.
        const auto dot = out.find_last_of('.');
        const std::string ext =
            (dot != std::string::npos && out.size() - dot <= 12)
                ? out.substr(dot)
                : "";
        out = out.substr(0, 120 - ext.size()) + ext;
    }
    return out;
}

std::string MediaCacheDir() {
    // <catalog_db_dir>/downloads — the catalog's DefaultDbPath() already
    // walks the LOCALAPPDATA/XDG/HOME/temp env chain.
    const std::filesystem::path dir =
        Path::FromUtf8(MediaCatalog::DefaultDbPath()).parent_path() /
        "downloads";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return Path::ToUtf8(dir);
}

std::string FindYtDlp() {
#ifdef _WIN32
    static const std::string cached = [] {
        const std::array<const char*, 2> names = {"yt-dlp.exe",
                                                  "yt-dlp.bat"};
        const auto file_exists = [](const std::filesystem::path& p) {
            std::error_code ec;
            return std::filesystem::is_regular_file(p, ec);
        };
        // 1) PATH dirs.
        const std::string path_env =
            Path::NormalizeUtf8Path(XER::Helper::GetEnv("PATH"));
        std::size_t pos = 0;
        while (pos <= path_env.size()) {
            const auto semi = path_env.find(';', pos);
            const std::string dir = path_env.substr(
                pos, semi == std::string::npos
                         ? std::string::npos
                         : semi - pos);
            if (!dir.empty()) {
                for (const char* name : names) {
                    const auto candidate =
                        Path::FromUtf8(dir) / name;
                    if (file_exists(candidate)) {
                        return Path::ToUtf8(candidate);
                    }
                }
            }
            if (semi == std::string::npos) break;
            pos = semi + 1;
        }
        // 2) Usual install spots — MSYS2 clang64/mingw64/usr-bin (the
        // pacman package), WinGet links, scoop, Program Files.
        const std::string local =
            Path::NormalizeUtf8Path(XER::Helper::GetEnv("LOCALAPPDATA"));
        std::vector<std::filesystem::path> spots = {
            "C:/tools/msys64/clang64/bin",
            "C:/tools/msys64/mingw64/bin",
            "C:/tools/msys64/usr/bin",
            "C:/msys64/clang64/bin",
            "C:/msys64/mingw64/bin",
            "C:/msys64/usr/bin",
            "C:/Program Files/yt-dlp",
        };
        if (!local.empty()) {
            spots.push_back(Path::FromUtf8(local) /
                            "Microsoft/WinGet/Links");
            spots.push_back(Path::FromUtf8(local) /
                            "Programs/yt-dlp");
        }
        const std::string home =
            Path::NormalizeUtf8Path(XER::Helper::GetEnv("USERPROFILE"));
        if (!home.empty()) {
            spots.push_back(Path::FromUtf8(home) /
                            "scoop/shims");
        }
        for (const auto& dir : spots) {
            for (const char* name : names) {
                const auto candidate = dir / name;
                if (file_exists(candidate)) {
                    return Path::ToUtf8(candidate);
                }
            }
        }
        return std::string();
    }();
    return cached;
#else
    // POSIX: a PATH probe is enough — yt-dlp is almost always
    // /usr/bin or a venv shim.
    const std::string path_env = XER::Helper::GetEnv("PATH");
    std::size_t pos = 0;
    while (pos <= path_env.size()) {
        const auto colon = path_env.find(':', pos);
        const std::string dir = path_env.substr(
            pos, colon == std::string::npos
                     ? std::string::npos
                     : colon - pos);
        if (!dir.empty()) {
            const auto candidate =
                std::filesystem::path(dir) / "yt-dlp";
            std::error_code ec;
            if (std::filesystem::is_regular_file(candidate, ec)) {
                return Path::ToUtf8(candidate);
            }
        }
        if (colon == std::string::npos) break;
        pos = colon + 1;
    }
    return {};
#endif
}

Result Download(const std::string& url, const std::string& dest_dir,
                const ProgressFn& progress, const CancelFn& cancel) {
    Result result;
    const UrlKind kind = ClassifyUrl(url);
    if (kind == UrlKind::kInvalid) {
        result.error = "not an http(s) URL";
        return result;
    }
    const std::filesystem::path dir = Path::FromUtf8(
        dest_dir.empty() ? MediaCacheDir() : Path::NormalizeUtf8Path(
                                               dest_dir));
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        result.error = "cannot create cache dir " + Path::ToUtf8(dir);
        return result;
    }

    if (kind == UrlKind::kPlatform) {
        const std::string ytdlp = FindYtDlp();
        if (!ytdlp.empty()) {
            // Stem seeds off the URL basename (video pages usually have
            // none → "video"); the unique suffix keeps concurrent and
            // repeat downloads from colliding.
            std::string base = UrlBasename(url);
            if (base.empty() || base.size() < 3) base = "video";
            // yt-dlp fills %(ext)s — drop any extension the URL had.
            const auto dot = base.find_last_of('.');
            if (dot != std::string::npos) base.erase(dot);
            base = SanitizeFilename(base);
            if (base.size() > 48) base.erase(48);
            const std::string stem = UniqueStem(dir, "dl-" + base);
#if defined(_WIN32)
            return RunYtDlp(ytdlp, url, dir, stem, progress, cancel);
#else
            Report(progress, -1.0f,
                   "Platform import needs yt-dlp (Windows spawn "
                   "path only) — trying direct URL");
#endif
        } else {
            Report(progress, -1.0f,
                   "Install yt-dlp for platform links — trying "
                   "direct URL");
        }
    }
    return RunHttpGet(url, dir, progress, cancel);
}

}  // namespace XER::VideoEditor::UrlImport
