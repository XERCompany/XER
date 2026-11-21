// cef_ext_scheme.cc — see cef_ext_scheme.h.
#include "cef_ext_scheme.h"

#include "tz/tz.h"

#if defined(XER_HAS_CEF)
#include "cef_scheme.h"
#include "cef_request.h"
#include "cef_response.h"
#include "cef_resource_handler.h"
#include "cef_browser.h"
#include "cef_callback.h"
#include "cef_frame.h"
#endif

namespace XER::Cef {
namespace {

std::mutex g_mutex;
std::unordered_map<std::string, std::shared_ptr<Tz::TzArchive>>
    g_registry;
// Optional module-bridge fallback (see ExtSchemeSetProvider in the
// header). Guarded by g_mutex like the map — set once at startup.
ExtSchemeProvider g_provider = nullptr;

std::string ExtMimeType(const std::string& name) {
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    const std::string ext = name.substr(dot + 1);
    static const std::unordered_map<std::string, std::string> kMime = {
        {"html", "text/html"},        {"htm", "text/html"},
        {"css", "text/css"},          {"js", "text/javascript"},
        {"mjs", "text/javascript"},   {"json", "application/json"},
        {"toml", "text/plain"},       {"txt", "text/plain"},
        {"svg", "image/svg+xml"},     {"png", "image/png"},
        {"jpg", "image/jpeg"},        {"jpeg", "image/jpeg"},
        {"gif", "image/gif"},         {"webp", "image/webp"},
        {"ico", "image/x-icon"},      {"wasm", "application/wasm"},
        {"woff", "font/woff"},        {"woff2", "font/woff2"},
        {"lua", "text/plain"},        {"py", "text/plain"},
    };
    const auto it = kMime.find(ext);
    return it == kMime.end() ? "application/octet-stream" : it->second;
}

}  // namespace

void ExtSchemeRegister(const std::string& ext_id,
                       std::shared_ptr<Tz::TzArchive> archive) {
    std::lock_guard lock(g_mutex);
    if (archive) {
        g_registry[ext_id] = std::move(archive);
    } else {
        g_registry.erase(ext_id);
    }
}

void ExtSchemeUnregister(const std::string& ext_id) {
    std::lock_guard lock(g_mutex);
    g_registry.erase(ext_id);
}

std::shared_ptr<Tz::TzArchive> ExtSchemeLookup(
    const std::string& ext_id) {
    std::lock_guard lock(g_mutex);
    const auto it = g_registry.find(ext_id);
    return it == g_registry.end() ? nullptr : it->second;
}

void ExtSchemeSetProvider(ExtSchemeProvider provider) {
    std::lock_guard lock(g_mutex);
    g_provider = provider;
}

// Serve one member for `request_url`'s ext id — registered archive
// first, installed provider as the cross-module fallback. Shared by
// the CEF handler and diagnostics.
bool ExtSchemeServeMember(const std::string& ext_id,
                          const std::string& member,
                          std::vector<uint8_t>* bytes_out) {
    if (const auto archive = ExtSchemeLookup(ext_id);
        archive && archive->Read(member, bytes_out)) {
        return true;
    }
    ExtSchemeProvider provider;
    {
        std::lock_guard lock(g_mutex);
        provider = g_provider;
    }
    return provider != nullptr &&
           provider(ext_id, member, bytes_out);
}

std::string ExtSchemeUrl(const std::string& ext_id,
                         const std::string& member) {
    std::string m = member;
    while (!m.empty() && m.front() == '/') m.erase(0, 1);
    return "xer-ext://" + ext_id + "/" + m;
}

#if defined(XER_HAS_CEF)
namespace {

// Streams one archive member (already copied out of the TZ blob — the
// archive object may be unregistered while a response is in flight).
class ExtResourceHandler final : public CefResourceHandler {
public:
    ExtResourceHandler(std::vector<uint8_t> bytes, std::string mime,
                       bool found)
        : bytes_(std::move(bytes)),
          mime_(std::move(mime)),
          found_(found) {}

    bool Open(CefRefPtr<CefRequest> /*request*/,
              bool& handle_request,
              CefRefPtr<CefCallback> /*callback*/) override {
        handle_request = true;
        return true;
    }

    void GetResponseHeaders(CefRefPtr<CefResponse> response,
                            int64_t& response_length,
                            CefString& /*redirect_url*/) override {
        if (!found_) {
            response->SetStatus(404);
            response->SetMimeType("text/plain");
            static const char kMsg[] = "xer-ext: member not found";
            bytes_.assign(kMsg, kMsg + sizeof(kMsg) - 1);
        } else {
            response->SetStatus(200);
            response->SetMimeType(mime_);
        }
        // Extensions pages are app-internal; keep them off the network
        // namespace and away from other origins' data.
        response->SetHeaderByName("Access-Control-Allow-Origin", "*", true);
        response->SetHeaderByName("X-Content-Type-Options", "nosniff", true);
        response_length = static_cast<int64_t>(bytes_.size());
    }

    bool Read(void* data_out, int bytes_to_read, int& bytes_read,
              CefRefPtr<CefResourceReadCallback> callback) override {
        const size_t remaining = bytes_.size() - offset_;
        if (remaining == 0) {
            bytes_read = 0;
            return false;
        }
        const size_t n =
            std::min(remaining, static_cast<size_t>(bytes_to_read));
        std::memcpy(data_out, bytes_.data() + offset_, n);
        offset_ += n;
        bytes_read = static_cast<int>(n);
        return true;
    }

    void Cancel() override {}

private:
    std::vector<uint8_t> bytes_;
    std::string mime_;
    bool found_;
    size_t offset_ = 0;
    IMPLEMENT_REFCOUNTING(ExtResourceHandler);
};

class ExtSchemeHandlerFactory final : public CefSchemeHandlerFactory {
public:
    CefRefPtr<CefResourceHandler> Create(
        CefRefPtr<CefBrowser> /*browser*/,
        CefRefPtr<CefFrame> /*frame*/,
        const CefString& scheme_name,
        CefRefPtr<CefRequest> request) override {
        if (scheme_name != "xer-ext") return nullptr;
        // xer-ext://<ext-id>/<member-path>
        const std::string url = request->GetURL();
        const std::string prefix = "xer-ext://";
        if (url.rfind(prefix, 0) != 0) return nullptr;
        const auto slash = url.find('/', prefix.size());
        const std::string ext_id =
            slash == std::string::npos
                ? url.substr(prefix.size())
                : url.substr(prefix.size(), slash - prefix.size());
        std::string member =
            slash == std::string::npos ? "" : url.substr(slash + 1);
        const auto frag = member.find('#');
        if (frag != std::string::npos) member.resize(frag);
        const auto query = member.find('?');
        if (query != std::string::npos) member.resize(query);
        if (member.empty()) member = "static/index.html";

        std::vector<uint8_t> bytes;
        const bool found =
            ExtSchemeServeMember(ext_id, member, &bytes);
        return new ExtResourceHandler(std::move(bytes),
                                      ExtMimeType(member), found);
    }

private:
    IMPLEMENT_REFCOUNTING(ExtSchemeHandlerFactory);
};

}  // namespace

void RegisterExtSchemeHandler() {
    // Empty domain -> the factory receives every xer-ext:// URL.
    CefRegisterSchemeHandlerFactory("xer-ext", "",
                                    new ExtSchemeHandlerFactory());
}

#else

void RegisterExtSchemeHandler() {}

#endif  // XER_HAS_CEF

}  // namespace XER::Cef
