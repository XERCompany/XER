// ArchiveManager — universal archive manager with a web UI.
//
// Serves a single-page web interface on 127.0.0.1 that can browse an
// archive (ZIP, RAR, 7z, TAR.*, APK, ...) using libarchive: list entries,
// extract (all or selected), delete entries, upload files into archives
// and create new ones. Encrypted archives trigger a native XER UI
// password dialog (the window exists only to host those prompts; the
// actual UI is the web page).
//
// Usage: ArchiveManager.exe [--bind <addr>] [--port <port>] [--root <dir>]
//                             [--headless]

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "json/json.h"

#include "content/archive/archive_manager.h"
#include "content/network/http/http_message.h"
#include "content/network/http/http_router.h"
#include "content/network/http/http_server.h"
#include "content/network/socket/platform/socket_platform.h"
#include "ui/core/application.h"
#include "ui/core/window.h"
#include "ui/widgets/dialog.h"
#include "ui/widgets/label.h"
#include "ui/widgets/text_input.h"
#include "ui/widgets/widget.h"

namespace fs = std::filesystem;
namespace Http = XER::Network::Http;
namespace Arch = XER::Content::Archive;
namespace UIW = XER::UI::Widgets;
namespace UIC = XER::UI::Core;

namespace {

// ==========================================================================
// Password broker: bridges archive worker threads -> UI-thread dialog.
// ==========================================================================

class PasswordBroker {
public:
    // Blocks the calling (HTTP worker) thread until the UI dialog is
    // answered. Returns the password, or nullopt on cancel/empty.
    std::optional<std::string> Request(const std::string& archive_path) {
        auto pending = std::make_shared<Pending>();
        pending->archive_path = archive_path;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(pending);
        }
        std::unique_lock<std::mutex> lk(pending->mutex);
        pending->cv.wait(lk, [&] { return pending->done; });
        return pending->password;
    }

    void Shutdown() {
        std::vector<std::shared_ptr<Pending>> pending;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            while (!queue_.empty()) {
                pending.push_back(queue_.front());
                queue_.pop_front();
            }
        }
        for (auto& p : pending) {
            std::lock_guard<std::mutex> lk(p->mutex);
            p->password = std::nullopt;
            p->done = true;
            p->cv.notify_one();
        }
    }

    // UI thread only (frame callback). Shows a dialog for the next
    // queued request, if none is active.
    void ServicePending(UIW::Widget* host) {
        std::shared_ptr<Pending> pending;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (dialog_active_ || queue_.empty()) return;
            pending = queue_.front();
            queue_.pop_front();
            dialog_active_ = true;
        }
        ShowDialog(host, pending);
    }

private:
    struct Pending {
        std::string archive_path;
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        std::optional<std::string> password;
    };

    void ShowDialog(UIW::Widget* host,
                    const std::shared_ptr<Pending>& pending) {
        auto input = std::make_shared<UIW::TextInput>();
        input->SetPasswordMode(true);
        const std::string name = BaseName(pending->archive_path);
        input->SetPlaceholder("Password for " + name);

        auto dialog = UIW::Dialog::Create(
            "Password required — " + name, input, "Unlock", "Cancel");
        dialog->SetOnClosed(
            [this, host, pending, input, dlg = dialog.get()] {
                std::string pw;
                if (dlg->WasConfirmed()) pw = input->GetValue();
                {
                    std::lock_guard<std::mutex> lk(pending->mutex);
                    pending->password =
                        pw.empty() ? std::nullopt
                                   : std::optional<std::string>(pw);
                    pending->done = true;
                }
                pending->cv.notify_one();
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    dialog_active_ = false;
                }
                // Dialog::Close() already hid the overlay itself.
            });
        host->ShowOverlay(dialog);
    }

    static std::string BaseName(const std::string& path) {
        const size_t slash = path.find_last_of("/\\");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    std::mutex mutex_;
    std::deque<std::shared_ptr<Pending>> queue_;
    bool dialog_active_ = false;
};

// ==========================================================================
// Password cache + retry-on-auth-failure wrapper.
// ==========================================================================

struct PasswordCache {
    PasswordBroker& broker;
    bool headless = false;  // no UI: prompts auto-cancel
    std::mutex mutex;
    std::unordered_map<std::string, std::string> passwords;

    Arch::PasswordProvider Provider() {
        return [this](const std::string& path)
                   -> std::optional<std::string> {
            if (headless) return std::nullopt;
            {
                std::lock_guard<std::mutex> lk(mutex);
                const auto it = passwords.find(path);
                if (it != passwords.end()) return it->second;
            }
            auto pw = broker.Request(path);
            if (pw) {
                std::lock_guard<std::mutex> lk(mutex);
                passwords[path] = *pw;
            }
            return pw;
        };
    }

    void Evict(const std::string& path) {
        std::lock_guard<std::mutex> lk(mutex);
        passwords.erase(path);
    }
};

bool LooksLikeAuthError(const std::string& error) {
    std::string lower = error;
    for (char& c : lower) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    }
    return lower.find("passphrase") != std::string::npos ||
           lower.find("password") != std::string::npos ||
           lower.find("encrypted") != std::string::npos ||
           lower.find("checksum") != std::string::npos ||
           lower.find("crc") != std::string::npos ||
           lower.find("damaged") != std::string::npos ||
           lower.find("truncated") != std::string::npos;
}

// Runs `fn(provider, error_out)`; on an auth-looking failure evicts the
// cached password for `path` and retries once (re-prompting the user).
template <typename Fn>
bool ArchiveCall(PasswordCache& cache, const std::string& path, Fn&& fn,
                 std::string* error) {
    auto provider = cache.Provider();
    if (fn(provider, error)) return true;
    if (LooksLikeAuthError(*error)) {
        cache.Evict(path);
        provider = cache.Provider();
        return fn(provider, error);
    }
    return false;
}

// ==========================================================================
// Small helpers.
// ==========================================================================

struct Context {
    PasswordBroker broker;
    PasswordCache cache{broker};
    fs::path root;          // FS browsing is confined to this directory
    bool headless = false;  // no UI window: password prompts auto-cancel
};

std::string FsToUtf8(const fs::path& p) {
#ifdef _WIN32
    const std::u8string u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
#else
    return p.string();
#endif
}

fs::path Utf8ToFs(const std::string& utf8) {
#ifdef _WIN32
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
#else
    return fs::path(utf8);
#endif
}

std::string BaseName(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string DirName(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return ".";
    if (slash == 0) return path.substr(0, 1);
    return path.substr(0, slash);
}

std::string ToLower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    }
    return s;
}

bool HasArchiveExtension(const std::string& name) {
    static const std::set<std::string> kExts = {
        "zip", "jar", "apk", "ipa", "xpi", "docx", "xlsx", "pptx", "odt",
        "rar", "r00", "7z", "tar", "gz", "tgz", "bz2", "tbz2", "xz", "txz",
        "zst", "cab", "iso", "arj", "lzh", "lha", "cpio", "war", "xar",
        "wim", "deb", "rpm", "apk", "z", "lz4", "lzo", "sz"
    };
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= name.size()) return false;
    return kExts.count(ToLower(name.substr(dot + 1))) != 0;
}

int64_t ToUnixSeconds(const fs::file_time_type& t) {
    return std::chrono::duration_cast<std::chrono::seconds>(
               t.time_since_epoch())
        .count();
}

std::string ToJson(const Json::Value& v) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, v);
}

Json::Value OkJson() {
    Json::Value v;
    v["ok"] = true;
    return v;
}

bool ParseJsonBody(const Http::HttpRequest& req, Json::Value* out,
                   std::string* error) {
    if (req.body.empty()) {
        *out = Json::Value(Json::objectValue);
        return true;
    }
    Json::CharReaderBuilder builder;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(req.body.data(), req.body.data() + req.body.size(),
                       out, &errs)) {
        *error = "invalid JSON body: " + errs;
        return false;
    }
    return true;
}

std::string BodyString(const Json::Value& v, const char* key) {
    return v.isMember(key) && v[key].isString() ? v[key].asString() : "";
}

// Resolves a client-supplied path and verifies it stays inside ctx.root.
// Returns empty string on violation.
std::string ResolveInsideRoot(Context& ctx, const std::string& input) {
    std::error_code ec;
    fs::path base = input.empty() ? ctx.root : Utf8ToFs(input);
    if (!base.is_absolute()) base = ctx.root / base;
    fs::path canon = fs::weakly_canonical(base, ec);
    if (ec) canon = base;
    fs::path root_can = fs::weakly_canonical(ctx.root, ec);
    if (ec) root_can = ctx.root;

#ifdef _WIN32
    auto norm = [](const fs::path& p) {
        std::wstring w = p.wstring();
        for (wchar_t& c : w) {
            if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + 32);
            if (c == L'/') c = L'\\';
        }
        return w;
    };
    std::wstring c = norm(canon), r = norm(root_can);
    // A root ending in a separator (e.g. "c:\") already contains the
    // separator, so any continuation is inside.
    while (r.size() > 3 && r.back() == L'\\') r.pop_back();
    const bool inside =
        c == r || (c.size() > r.size() && c.compare(0, r.size(), r) == 0 &&
                   (r.back() == L'\\' || c[r.size()] == L'\\'));
#else
    std::string c = canon.string(), r = root_can.string();
    while (r.size() > 1 && r.back() == '/') r.pop_back();
    const bool inside =
        c == r || (c.size() > r.size() && c.compare(0, r.size(), r) == 0 &&
                   (r.back() == '/' || c[r.size()] == '/'));
#endif
    return inside ? FsToUtf8(canon) : std::string();
}

// ==========================================================================
// Route handlers.
// ==========================================================================

using RouteParams = std::map<std::string, std::string>;
using HandlerFn = Http::HttpHandler;

Http::HttpResponse MethodNotAllowed() {
    Http::HttpResponse r = Http::HttpResponse::JsonError(405, "method not allowed");
    r.headers.Set("Allow", "GET, POST, PUT, DELETE");
    return r;
}

HandlerFn HandleFsList(Context& ctx) {
    return [&ctx](const Http::HttpRequest& req, const RouteParams&) {
        if (req.method != Http::HttpMethod::kGet &&
            req.method != Http::HttpMethod::kHead) {
            return MethodNotAllowed();
        }
        const std::string dir = ResolveInsideRoot(ctx, req.QueryParam("path"));
        if (dir.empty()) {
            return Http::HttpResponse::JsonError(403, "path outside root");
        }
        std::error_code ec;
        if (!fs::is_directory(Utf8ToFs(dir), ec)) {
            return Http::HttpResponse::JsonError(404, "not a directory");
        }

        Json::Value entries(Json::arrayValue);
        std::vector<fs::directory_entry> items;
        for (const auto& de : fs::directory_iterator(Utf8ToFs(dir), ec)) {
            items.push_back(de);
        }
        std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
            const bool ad = a.is_directory(), bd = b.is_directory();
            if (ad != bd) return ad > bd;
            return ToLower(a.path().filename().string()) <
                   ToLower(b.path().filename().string());
        });
        for (const auto& de : items) {
            Json::Value e(Json::objectValue);
            const std::string name = FsToUtf8(de.path().filename());
            const std::string full = FsToUtf8(de.path());
            e["name"] = name;
            e["path"] = full;
            const auto st = de.symlink_status(ec);
            if (ec) continue;
            if (fs::is_directory(st)) {
                e["type"] = "dir";
            } else if (fs::is_regular_file(st)) {
                e["type"] = "file";
                e["size"] = static_cast<Json::Int64>(de.file_size(ec));
            } else if (fs::is_symlink(st)) {
                e["type"] = "link";
                e["size"] = static_cast<Json::Int64>(de.file_size(ec));
            } else {
                e["type"] = "other";
            }
            e["mtime"] = ToUnixSeconds(de.last_write_time(ec));
            e["is_archive"] = HasArchiveExtension(name);
            entries.append(e);
        }

        Json::Value out = OkJson();
        out["path"] = dir;
        out["parent"] = DirName(dir) == dir ? "" : DirName(dir);
        out["entries"] = entries;
        return Http::HttpResponse::Json(ToJson(out));
    };
}

HandlerFn HandleFsMkdir(Context& ctx) {
    return [&ctx](const Http::HttpRequest& req, const RouteParams&) {
        if (req.method != Http::HttpMethod::kPost &&
            req.method != Http::HttpMethod::kPut) {
            return MethodNotAllowed();
        }
        Json::Value body;
        std::string err;
        if (!ParseJsonBody(req, &body, &err)) {
            return Http::HttpResponse::JsonError(400, err);
        }
        const std::string dir = ResolveInsideRoot(ctx, BodyString(body, "path"));
        if (dir.empty()) {
            return Http::HttpResponse::JsonError(403, "path outside root");
        }
        std::error_code ec;
        if (!fs::create_directories(Utf8ToFs(dir), ec) && ec) {
            return Http::HttpResponse::JsonError(500, ec.message());
        }
        Json::Value out = OkJson();
        out["path"] = dir;
        return Http::HttpResponse::Json(ToJson(out));
    };
}

HandlerFn HandleFsDownload(Context& ctx) {
    return [&ctx](const Http::HttpRequest& req, const RouteParams&) {
        if (req.method != Http::HttpMethod::kGet &&
            req.method != Http::HttpMethod::kHead) {
            return MethodNotAllowed();
        }
        const std::string path = ResolveInsideRoot(ctx, req.QueryParam("path"));
        if (path.empty()) {
            return Http::HttpResponse::JsonError(403, "path outside root");
        }
        const fs::path fp = Utf8ToFs(path);
        std::error_code ec;
        if (!fs::is_regular_file(fp, ec)) {
            return Http::HttpResponse::JsonError(404, "not a file");
        }
        const auto size = fs::file_size(fp, ec);
        if (ec || size > 512ull * 1024 * 1024) {
            return Http::HttpResponse::JsonError(413, "file too large");
        }
        std::ifstream in(fp, std::ios::binary);
        if (!in) {
            return Http::HttpResponse::JsonError(500, "cannot open file");
        }
        std::string data(static_cast<size_t>(size), '\0');
        in.read(data.data(), static_cast<std::streamsize>(size));
        if (in.gcount() != static_cast<std::streamsize>(size)) {
            return Http::HttpResponse::JsonError(500, "read failed");
        }
        Http::HttpResponse r;
        r.status = 200;
        r.body = std::move(data);
        r.headers.Set("Content-Type", "application/octet-stream");
        const std::string fname = BaseName(path);
        std::string dispo = "attachment; filename=\"";
        for (char c : fname) {
            dispo += (c >= 32 && c != '"' && c != '\\') ? c : '_';
        }
        dispo += "\"";
        r.headers.Set("Content-Disposition", dispo);
        return r;
    };
}

HandlerFn HandleFsDelete(Context& ctx) {
    return [&ctx](const Http::HttpRequest& req, const RouteParams&) {
        if (req.method != Http::HttpMethod::kPost &&
            req.method != Http::HttpMethod::kDelete) {
            return MethodNotAllowed();
        }
        Json::Value body;
        std::string err;
        if (!ParseJsonBody(req, &body, &err)) {
            return Http::HttpResponse::JsonError(400, err);
        }
        const std::string path = ResolveInsideRoot(ctx, BodyString(body, "path"));
        if (path.empty()) {
            return Http::HttpResponse::JsonError(403, "path outside root");
        }
        // Never allow deleting the root itself.
        if (ToLower(path) == ToLower(FsToUtf8(ctx.root))) {
            return Http::HttpResponse::JsonError(400, "cannot delete root");
        }
        std::error_code ec;
        const bool is_dir = fs::is_directory(Utf8ToFs(path), ec);
        const bool ok = is_dir ? fs::remove_all(Utf8ToFs(path), ec)
                               : fs::remove(Utf8ToFs(path), ec);
        if (ec || !ok) {
            return Http::HttpResponse::JsonError(
                500, ec ? ec.message() : "delete failed");
        }
        return Http::HttpResponse::Json(ToJson(OkJson()));
    };
}

HandlerFn HandleArchiveInfo(Context& ctx) {
    return [&ctx](const Http::HttpRequest& req, const RouteParams&) {
        if (req.method != Http::HttpMethod::kGet &&
            req.method != Http::HttpMethod::kHead) {
            return MethodNotAllowed();
        }
        const std::string path = req.QueryParam("path");
        if (path.empty()) {
            return Http::HttpResponse::JsonError(400, "missing path");
        }
        Arch::ArchiveInfo info;
        std::string err;
        if (!ArchiveCall(ctx.cache, path,
                         [&](Arch::PasswordProvider prov, std::string* e) {
                             return Arch::ArchiveManager::Probe(path, &info,
                                                                 e, prov);
                         },
                         &err)) {
            return Http::HttpResponse::JsonError(400, err);
        }
        Json::Value out = OkJson();
        out["format"] = info.format;
        out["compression"] = info.compression;
        out["encrypted"] = info.encrypted;
        out["encryption_unknown"] = info.encryption_unknown;
        out["entry_count"] = static_cast<Json::Int64>(info.entry_count);
        out["file_size"] = static_cast<Json::UInt64>(info.file_size);
        return Http::HttpResponse::Json(ToJson(out));
    };
}

HandlerFn HandleArchiveList(Context& ctx) {
    return [&ctx](const Http::HttpRequest& req, const RouteParams&) {
        if (req.method != Http::HttpMethod::kGet &&
            req.method != Http::HttpMethod::kHead) {
            return MethodNotAllowed();
        }
        const std::string path = req.QueryParam("path");
        if (path.empty()) {
            return Http::HttpResponse::JsonError(400, "missing path");
        }
        std::vector<Arch::ArchiveEntryInfo> entries;
        std::string err;
        if (!ArchiveCall(ctx.cache, path,
                         [&](Arch::PasswordProvider prov, std::string* e) {
                             return Arch::ArchiveManager::List(
                                 path, &entries, e, prov);
                         },
                         &err)) {
            return Http::HttpResponse::JsonError(400, err);
        }
        Json::Value arr(Json::arrayValue);
        for (const auto& e : entries) {
            Json::Value j(Json::objectValue);
            j["path"] = e.path;
            j["is_dir"] = e.is_dir;
            j["is_symlink"] = e.is_symlink;
            j["size"] = static_cast<Json::Int64>(e.size);
            j["mtime"] = static_cast<Json::Int64>(e.mtime);
            arr.append(j);
        }
        Json::Value out = OkJson();
        out["entries"] = arr;
        out["count"] = static_cast<Json::Int64>(entries.size());
        return Http::HttpResponse::Json(ToJson(out));
    };
}

HandlerFn HandleArchiveDownload(Context& ctx) {
    return [&ctx](const Http::HttpRequest& req, const RouteParams&) {
        if (req.method != Http::HttpMethod::kGet &&
            req.method != Http::HttpMethod::kHead) {
            return MethodNotAllowed();
        }
        const std::string path = req.QueryParam("path");
        const std::string entry = req.QueryParam("entry");
        if (path.empty() || entry.empty()) {
            return Http::HttpResponse::JsonError(400, "missing path/entry");
        }
        std::string data;
        std::string err;
        if (!ArchiveCall(ctx.cache, path,
                         [&](Arch::PasswordProvider prov, std::string* e) {
                             return Arch::ArchiveManager::ReadEntry(
                                 path, entry, &data, e, prov);
                         },
                         &err)) {
            return Http::HttpResponse::JsonError(400, err);
        }
        Http::HttpResponse r;
        r.status = 200;
        r.body = std::move(data);
        r.headers.Set("Content-Type", "application/octet-stream");
        // Content-Disposition with a safe ASCII fallback filename.
        std::string fname = BaseName(entry);
        std::string dispo = "attachment; filename=\"";
        for (char c : fname) {
            dispo += (c >= 32 && c != '"' && c != '\\') ? c : '_';
        }
        dispo += "\"";
        r.headers.Set("Content-Disposition", dispo);
        return r;
    };
}

std::vector<std::string> StringArray(const Json::Value& v) {
    std::vector<std::string> out;
    if (!v.isArray()) return out;
    for (const auto& item : v) {
        if (item.isString()) out.push_back(item.asString());
    }
    return out;
}

HandlerFn HandleArchiveExtract(Context& ctx) {
    return [&ctx](const Http::HttpRequest& req, const RouteParams&) {
        if (req.method != Http::HttpMethod::kPost &&
            req.method != Http::HttpMethod::kPut) {
            return MethodNotAllowed();
        }
        Json::Value body;
        std::string err;
        if (!ParseJsonBody(req, &body, &err)) {
            return Http::HttpResponse::JsonError(400, err);
        }
        const std::string path = BodyString(body, "path");
        if (path.empty()) {
            return Http::HttpResponse::JsonError(400, "missing path");
        }
        // Default destination: <parent>/<archive-stem>/
        std::string dest = BodyString(body, "dest");
        if (dest.empty()) {
            const std::string stem = BaseName(path);
            const size_t dot = stem.find_last_of('.');
            dest = DirName(path) + "/" +
                   (dot == std::string::npos ? stem : stem.substr(0, dot));
        }
        const auto entries = body.isMember("entries")
                                 ? StringArray(body["entries"])
                                 : std::vector<std::string>{};
        int64_t extracted = 0;
        if (!ArchiveCall(ctx.cache, path,
                         [&](Arch::PasswordProvider prov, std::string* e) {
                             return Arch::ArchiveManager::Extract(
                                 path, dest, entries, &extracted, e, prov);
                         },
                         &err)) {
            return Http::HttpResponse::JsonError(400, err);
        }
        Json::Value out = OkJson();
        out["extracted"] = static_cast<Json::Int64>(extracted);
        out["dest"] = dest;
        return Http::HttpResponse::Json(ToJson(out));
    };
}

HandlerFn HandleArchiveDeleteEntries(Context& ctx) {
    return [&ctx](const Http::HttpRequest& req, const RouteParams&) {
        if (req.method != Http::HttpMethod::kPost &&
            req.method != Http::HttpMethod::kPut &&
            req.method != Http::HttpMethod::kDelete) {
            return MethodNotAllowed();
        }
        Json::Value body;
        std::string err;
        if (!ParseJsonBody(req, &body, &err)) {
            return Http::HttpResponse::JsonError(400, err);
        }
        const std::string path = BodyString(body, "path");
        if (path.empty()) {
            return Http::HttpResponse::JsonError(400, "missing path");
        }
        const auto entries = body.isMember("entries")
                                 ? StringArray(body["entries"])
                                 : std::vector<std::string>{};
        if (entries.empty()) {
            return Http::HttpResponse::JsonError(400, "no entries selected");
        }
        int64_t removed = 0;
        if (!ArchiveCall(ctx.cache, path,
                         [&](Arch::PasswordProvider prov, std::string* e) {
                             return Arch::ArchiveManager::RemoveEntries(
                                 path, entries, &removed, e, prov);
                         },
                         &err)) {
            return Http::HttpResponse::JsonError(400, err);
        }
        Json::Value out = OkJson();
        out["removed"] = static_cast<Json::Int64>(removed);
        return Http::HttpResponse::Json(ToJson(out));
    };
}

HandlerFn HandleArchiveCreate(Context& ctx) {
    return [&ctx](const Http::HttpRequest& req, const RouteParams&) {
        if (req.method != Http::HttpMethod::kPost &&
            req.method != Http::HttpMethod::kPut) {
            return MethodNotAllowed();
        }
        Json::Value body;
        std::string err;
        if (!ParseJsonBody(req, &body, &err)) {
            return Http::HttpResponse::JsonError(400, err);
        }
        const std::string path = BodyString(body, "path");
        if (path.empty()) {
            return Http::HttpResponse::JsonError(400, "missing path");
        }
        if (!ArchiveCall(ctx.cache, path,
                         [&](Arch::PasswordProvider prov, std::string* e) {
                             return Arch::ArchiveManager::Create(path, e,
                                                                  prov);
                         },
                         &err)) {
            return Http::HttpResponse::JsonError(400, err);
        }
        return Http::HttpResponse::Json(ToJson(OkJson()));
    };
}

// Multipart upload: field "archive" = target archive path, every other
// part with a filename is added to the archive.
HandlerFn HandleArchiveUpload(Context& ctx) {
    return [&ctx](const Http::HttpRequest& req, const RouteParams&) {
        if (req.method != Http::HttpMethod::kPost &&
            req.method != Http::HttpMethod::kPut) {
            return MethodNotAllowed();
        }
        std::vector<Http::MultipartPart> parts;
        if (!Http::ParseMultipartFormData(
                req.headers.Get("content-type"), req.body, &parts)) {
            return Http::HttpResponse::JsonError(
                400, "expected multipart/form-data body");
        }

        std::string archive_path;
        std::vector<Http::MultipartPart> files;
        for (const auto& p : parts) {
            if (p.name == "archive") {
                archive_path = p.data;
                while (!archive_path.empty() &&
                       (archive_path.back() == ' ' ||
                        archive_path.back() == '\r' ||
                        archive_path.back() == '\n')) {
                    archive_path.pop_back();
                }
            } else if (!p.filename.empty()) {
                files.push_back(p);
            }
        }
        if (archive_path.empty()) {
            return Http::HttpResponse::JsonError(
                400, "missing 'archive' form field");
        }
        if (files.empty()) {
            return Http::HttpResponse::JsonError(400, "no files uploaded");
        }

        // Spill uploads to temp files next to the archive, then add.
        std::vector<Arch::AddItem> items;
        std::vector<std::string> temps;
        const auto cleanup_temps = [&temps] {
            for (const auto& t : temps) {
                std::error_code ec2;
                fs::remove(Utf8ToFs(t), ec2);
            }
        };
        for (size_t i = 0; i < files.size(); ++i) {
            std::string arcname = BaseName(files[i].filename);
            // Sanitize: no traversal, no separators inside arcname.
            for (char& c : arcname) {
                if (c == '/' || c == '\\') c = '_';
            }
            if (arcname.empty() || arcname == "." || arcname == "..") {
                arcname = "file-" + std::to_string(i);
            }
            const std::string tmp =
                archive_path + ".xer-up-" + std::to_string(i);
            temps.push_back(tmp);
            {
                std::ofstream out(Utf8ToFs(tmp), std::ios::binary);
                if (out && !files[i].data.empty()) {
                    out.write(files[i].data.data(),
                              static_cast<std::streamsize>(
                                  files[i].data.size()));
                }
                if (!out.good()) {
                    cleanup_temps();
                    return Http::HttpResponse::JsonError(
                        500, "cannot write temp file " + tmp);
                }
            }
            items.push_back(Arch::AddItem{tmp, arcname});
        }

        int64_t added = 0;
        std::string err;
        const bool ok = ArchiveCall(
            ctx.cache, archive_path,
            [&](Arch::PasswordProvider prov, std::string* e) {
                return Arch::ArchiveManager::AddFiles(archive_path, items,
                                                      &added, e, prov);
            },
            &err);

        cleanup_temps();
        if (!ok) {
            return Http::HttpResponse::JsonError(400, err);
        }
        Json::Value out = OkJson();
        out["added"] = static_cast<Json::Int64>(added);
        return Http::HttpResponse::Json(ToJson(out));
    };
}

// ==========================================================================
// main
// ==========================================================================

void PrintUsage(const char* argv0) {
    std::cout << "Usage: " << argv0
              << " [--bind <addr>] [--port <port>] [--root <dir>] [--headless]\n"
              << "  --bind   listen address (default 127.0.0.1)\n"
              << "  --port   listen port   (default 8787)\n"
              << "  --root   file browser root (default: current directory)\n"
              << "  --headless  no UI window; password prompts are disabled\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string bind = "127.0.0.1";
    uint16_t port = 8787;
    std::string root_arg;
    bool headless = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << name << " requires a value\n";
                PrintUsage(argv[0]);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--bind") bind = next("--bind");
        else if (a == "--port") port = static_cast<uint16_t>(std::stoi(next("--port")));
        else if (a == "--root") root_arg = next("--root");
        else if (a == "--headless") headless = true;
        else if (a == "--help" || a == "-h") {
            PrintUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "unknown argument: " << a << "\n";
            PrintUsage(argv[0]);
            return 2;
        }
    }

    Context ctx;
    ctx.headless = headless;
    ctx.cache.headless = headless;
    ctx.root = root_arg.empty()
                   ? fs::current_path()
                   : fs::weakly_canonical(Utf8ToFs(root_arg));
    std::error_code ec;
    if (!fs::is_directory(ctx.root, ec)) {
        std::cerr << "root is not a directory: " << FsToUtf8(ctx.root) << "\n";
        return 1;
    }

    // ---- HTTP server -----------------------------------------------------
    auto router = std::make_shared<Http::HttpRouter>();
    router->AddRoute(Http::HttpMethod::kUnknown, "/api/fs", HandleFsList(ctx));
    router->AddRoute(Http::HttpMethod::kUnknown, "/api/fs/mkdir", HandleFsMkdir(ctx));
    router->AddRoute(Http::HttpMethod::kUnknown, "/api/fs/delete", HandleFsDelete(ctx));
    router->AddRoute(Http::HttpMethod::kUnknown, "/api/fs/download", HandleFsDownload(ctx));
    router->AddRoute(Http::HttpMethod::kUnknown, "/api/archive/info", HandleArchiveInfo(ctx));
    router->AddRoute(Http::HttpMethod::kUnknown, "/api/archive/list", HandleArchiveList(ctx));
    router->AddRoute(Http::HttpMethod::kUnknown, "/api/archive/download", HandleArchiveDownload(ctx));
    router->AddRoute(Http::HttpMethod::kUnknown, "/api/archive/extract", HandleArchiveExtract(ctx));
    router->AddRoute(Http::HttpMethod::kUnknown, "/api/archive/delete", HandleArchiveDeleteEntries(ctx));
    router->AddRoute(Http::HttpMethod::kUnknown, "/api/archive/upload", HandleArchiveUpload(ctx));
    router->AddRoute(Http::HttpMethod::kUnknown, "/api/archive/create", HandleArchiveCreate(ctx));

    auto server = std::make_shared<Http::HttpServer>();
    server->SetRouter(router);

    // Static web UI: <exe_dir>/www (copied there by CMake).
#ifdef _WIN32
    wchar_t exe_path[MAX_PATH * 4]{};
    const DWORD n = GetModuleFileNameW(nullptr, exe_path,
                                       sizeof(exe_path) / sizeof(wchar_t));
    fs::path exe_dir =
        n > 0 ? fs::path(exe_path).parent_path() : fs::current_path();
#else
    fs::path exe_dir = fs::current_path();
#endif
    const fs::path www = exe_dir / "www";
    if (fs::is_directory(www, ec)) {
        server->SetStaticRoot(FsToUtf8(www));
    }
    server->SetMaxBodyBytes(1ull * 1024 * 1024 * 1024);  // 1 GiB uploads

    std::string socket_error;
    auto& socket_platform =
        XER::Network::Socket::Platform::SocketPlatform::GetInstance();
    if (!socket_platform.Initialize(&socket_error)) {
        std::cerr << "socket init failed: " << socket_error << "\n";
        return 1;
    }

    std::string listen_error;
    if (!server->Listen(bind, port, 16, &listen_error)) {
        std::cerr << "listen failed on " << bind << ":" << port << ": "
                  << listen_error << "\n";
        return 1;
    }

    std::thread http_thread([server] { server->Run(); });
    std::cout << "ArchiveManager listening on http://" << bind << ":" << port
              << "\n  root: " << FsToUtf8(ctx.root) << "\n";
    if (!fs::is_directory(www, ec)) {
        std::cout << "  warning: www/ not found next to exe; API only\n";
    }
    if (headless) {
        std::cout << "  headless mode: password prompts disabled\n";
    }

    // ---- UI (password prompts only) --------------------------------------
    int rc = 0;
    if (!headless) {
        auto& ui_app = UIC::Application::Instance();
        ui_app.SetAppName("ArchiveManager");
        if (!ui_app.Initialize()) {
            std::cerr << "UI initialization failed\n";
            rc = 1;
        } else {
            auto window = std::make_shared<UIC::Window>();
            if (!window->Create("ArchiveManager — password prompts", 480, 240)) {
                std::cerr << "window creation failed\n";
                rc = 1;
            } else {
                auto label = std::make_shared<UIW::Label>(
                    "ArchiveManager is running.\n"
                    "Open http://" + bind + ":" + std::to_string(port) +
                    " in your browser.\n\n"
                    "This window only appears when an archive asks for a "
                    "password.");
                window->SetContent(label);
                window->SetFrameCallback(
                    [label, &ctx] { ctx.broker.ServicePending(label.get()); },
                    100);
                window->Show();
                rc = ui_app.Run();
            }
        }
    } else {
        // Headless: serve until the process is killed.
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    // ---- Shutdown ----------------------------------------------------------
    ctx.broker.Shutdown();
    server->Stop();
    if (http_thread.joinable()) http_thread.join();
    socket_platform.Shutdown();
    return rc;
}
