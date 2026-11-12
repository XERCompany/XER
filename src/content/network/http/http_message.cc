/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/http/http_message.h"

#include <cctype>

namespace XER::Network::Http {

namespace {

char ToLowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool IEquals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (ToLowerAscii(a[i]) != ToLowerAscii(b[i])) return false;
    }
    return true;
}

std::string_view Trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e-1] == ' ' || s[e-1] == '\t' || s[e-1] == '\r' || s[e-1] == '\n')) --e;
    return s.substr(b, e - b);
}

int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

HttpMethod HttpMethodFromString(std::string_view method) {
    if (IEquals(method, "GET"))     return HttpMethod::kGet;
    if (IEquals(method, "POST"))    return HttpMethod::kPost;
    if (IEquals(method, "PUT"))     return HttpMethod::kPut;
    if (IEquals(method, "DELETE"))  return HttpMethod::kDelete;
    if (IEquals(method, "PATCH"))   return HttpMethod::kPatch;
    if (IEquals(method, "HEAD"))    return HttpMethod::kHead;
    if (IEquals(method, "OPTIONS")) return HttpMethod::kOptions;
    if (IEquals(method, "CONNECT")) return HttpMethod::kConnect;
    if (IEquals(method, "TRACE"))   return HttpMethod::kTrace;
    return HttpMethod::kUnknown;
}

std::string_view HttpMethodToString(HttpMethod method) {
    switch (method) {
        case HttpMethod::kGet:     return "GET";
        case HttpMethod::kPost:    return "POST";
        case HttpMethod::kPut:     return "PUT";
        case HttpMethod::kDelete:  return "DELETE";
        case HttpMethod::kPatch:   return "PATCH";
        case HttpMethod::kHead:    return "HEAD";
        case HttpMethod::kOptions: return "OPTIONS";
        case HttpMethod::kConnect: return "CONNECT";
        case HttpMethod::kTrace:   return "TRACE";
        default:                   return "UNKNOWN";
    }
}

// ---- HttpHeaders --------------------------------------------------------

std::string HttpHeaders::Normalize(std::string_view name) {
    std::string out(name);
    for (char& c : out) c = ToLowerAscii(c);
    return out;
}

void HttpHeaders::Set(const std::string& name, const std::string& value) {
    const std::string key = Normalize(name);
    auto range = map_.equal_range(key);
    map_.erase(range.first, range.second);
    map_.emplace(std::move(key), value);
}

void HttpHeaders::Add(const std::string& name, const std::string& value) {
    map_.emplace(Normalize(name), value);
}

std::string HttpHeaders::Get(std::string_view name) const {
    const auto it = map_.find(Normalize(name));
    return it == map_.end() ? std::string() : it->second;
}

std::vector<std::string> HttpHeaders::GetAll(std::string_view name) const {
    std::vector<std::string> out;
    const auto range = map_.equal_range(Normalize(name));
    for (auto it = range.first; it != range.second; ++it) out.push_back(it->second);
    return out;
}

bool HttpHeaders::Contains(std::string_view name) const {
    return map_.find(Normalize(name)) != map_.end();
}

void HttpHeaders::Remove(std::string_view name) {
    const auto range = map_.equal_range(Normalize(name));
    map_.erase(range.first, range.second);
}

// ---- HttpRequest --------------------------------------------------------

bool HttpRequest::IsKeepAlive() const {
    const std::string conn = headers.Get("connection");
    if (IEquals(conn, "close")) return false;
    if (version == "HTTP/1.0") return IEquals(conn, "keep-alive");
    return true;  // HTTP/1.1 defaults to keep-alive
}

std::vector<std::pair<std::string, std::string>> HttpRequest::QueryParams() const {
    return ParseQuery(query);
}

std::string HttpRequest::QueryParam(std::string_view name) const {
    for (const auto& [k, v] : ParseQuery(query)) {
        if (k == name) return v;
    }
    return {};
}

// ---- HttpResponse -------------------------------------------------------

HttpResponse HttpResponse::Make(int status, std::string body,
                                std::string content_type) {
    HttpResponse r;
    r.status = status;
    r.body = std::move(body);
    r.headers.Set("Content-Type", std::move(content_type));
    return r;
}

HttpResponse HttpResponse::Text(std::string body, int status) {
    return Make(status, std::move(body), "text/plain; charset=utf-8");
}

HttpResponse HttpResponse::Html(std::string body, int status) {
    return Make(status, std::move(body), "text/html; charset=utf-8");
}

HttpResponse HttpResponse::Json(std::string json_body, int status) {
    return Make(status, std::move(json_body), "application/json; charset=utf-8");
}

HttpResponse HttpResponse::JsonError(int status, const std::string& message) {
    return Json("{\"error\":\"" + message + "\"}", status);
}

HttpResponse HttpResponse::Redirect(const std::string& location, int status) {
    HttpResponse r;
    r.status = status;
    r.headers.Set("Location", location);
    return r;
}

HttpResponse HttpResponse::NotFound(const std::string& what) {
    return JsonError(404, what);
}

std::string HttpResponse::StatusText() const {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        default:  return "Status";
    }
}

// ---- URL helpers --------------------------------------------------------

std::string UrlDecode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '%' && i + 2 < text.size()) {
            const int hi = HexVal(text[i+1]), lo = HexVal(text[i+2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(c == '+' ? ' ' : c);
    }
    return out;
}

std::string UrlEncode(std::string_view text) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) {
        const bool unreserved =
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~';
        if (unreserved) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> ParseQuery(
    std::string_view query) {
    std::vector<std::pair<std::string, std::string>> out;
    size_t pos = 0;
    while (pos <= query.size()) {
        const size_t amp = query.find('&', pos);
        const std::string_view pair =
            query.substr(pos, amp == std::string_view::npos ? std::string_view::npos : amp - pos);
        if (!pair.empty()) {
            const size_t eq = pair.find('=');
            if (eq == std::string_view::npos) {
                out.emplace_back(UrlDecode(pair), std::string());
            } else {
                out.emplace_back(UrlDecode(pair.substr(0, eq)),
                                 UrlDecode(pair.substr(eq + 1)));
            }
        }
        if (amp == std::string_view::npos) break;
        pos = amp + 1;
    }
    return out;
}

// ---- Multipart ----------------------------------------------------------

namespace {

std::string ExtractBoundary(const std::string& content_type) {
    const std::string needle = "boundary=";
    const size_t pos = content_type.find(needle);
    if (pos == std::string::npos) return {};
    std::string_view b = std::string_view(content_type).substr(pos + needle.size());
    const size_t semi = b.find(';');
    if (semi != std::string_view::npos) b = b.substr(0, semi);
    b = Trim(b);
    if (b.size() >= 2 && b.front() == '"' && b.back() == '"') {
        b = b.substr(1, b.size() - 2);
    }
    return std::string(b);
}

// Parses one part header block ("Name: value" lines) into name/filename/content_type.
void ParsePartHeaders(const std::string& headers, MultipartPart* part) {
    size_t pos = 0;
    while (pos < headers.size()) {
        const size_t eol = headers.find("\r\n", pos);
        const std::string_view line = std::string_view(headers).substr(
            pos, eol == std::string::npos ? std::string_view::npos : eol - pos);
        pos = eol == std::string::npos ? headers.size() : eol + 2;
        const size_t colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        const std::string hname = UrlDecode("");  // placeholder, replaced below
        (void)hname;
        std::string name(line.substr(0, colon));
        for (char& c : name) c = ToLowerAscii(c);
        const std::string value = std::string(Trim(line.substr(colon + 1)));
        if (name == "content-disposition") {
            // name="..." ; filename="..." (filename may be quoted, may contain ';')
            auto extract_param = [&](std::string_view key) -> std::string {
                const std::string needle = std::string(key) + "=";
                size_t p = value.find(needle);
                // ensure it's a real param start (preceded by ';' or start)
                while (p != std::string::npos && p > 0 && value[p-1] != ';' &&
                       value[p-1] != ' ' && value[p-1] != '\t') {
                    p = value.find(needle, p + 1);
                }
                if (p == std::string::npos) return {};
                p += needle.size();
                if (p < value.size() && value[p] == '"') {
                    const size_t end = value.find('"', p + 1);
                    return end == std::string::npos ? std::string() : value.substr(p + 1, end - p - 1);
                }
                const size_t end = value.find(';', p);
                return std::string(Trim(value.substr(p, end == std::string::npos ? std::string::npos : end - p)));
            };
            part->name = extract_param("name");
            part->filename = extract_param("filename");
            // filename* (RFC 5987) preferred when present
            const std::string fstar = extract_param("filename*");
            if (!fstar.empty()) {
                const size_t apos = fstar.find('\'');
                if (apos != std::string::npos && apos + 1 < fstar.size()) {
                    part->filename = UrlDecode(fstar.substr(apos + 1));
                }
            }
        } else if (name == "content-type") {
            part->content_type = value;
        }
    }
}

}  // namespace

bool ParseMultipartFormData(const std::string& content_type,
                            const std::string& body,
                            std::vector<MultipartPart>* parts) {
    const std::string boundary = ExtractBoundary(content_type);
    if (boundary.empty() || parts == nullptr) return false;
    const std::string delim = "--" + boundary;

    size_t pos = body.find(delim);
    if (pos == std::string::npos) return false;
    pos += delim.size();

    while (pos < body.size()) {
        // After delimiter: "\r\n" = part follows, "--" = end.
        if (pos + 1 < body.size() && body[pos] == '-' && body[pos+1] == '-') break;
        if (body.compare(pos, 2, "\r\n") == 0) pos += 2;
        else if (body[pos] == '\n') ++pos;
        else break;

        const size_t head_end = body.find("\r\n\r\n", pos);
        if (head_end == std::string::npos) return false;
        MultipartPart part;
        ParsePartHeaders(body.substr(pos, head_end - pos), &part);
        pos = head_end + 4;

        // Data runs until "\r\n--boundary"
        const std::string close = "\r\n" + delim;
        const size_t data_end = body.find(close, pos);
        if (data_end == std::string::npos) return false;
        part.data = body.substr(pos, data_end - pos);
        parts->push_back(std::move(part));
        pos = data_end + close.size();
    }
    return true;
}

}  // namespace XER::Network::Http
