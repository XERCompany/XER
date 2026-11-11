/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/http/http_server.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <thread>

namespace XER::Network::Http {

namespace {

using Socket::SocketStream;
using Socket::TcpClientSocket;

bool IEquals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb + 32);
        if (ca != cb) return false;
    }
    return true;
}

std::string_view StripEol(std::string_view line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        line.remove_suffix(1);
    return line;
}

// ---- Request parsing ----------------------------------------------------

// Splits "METHOD SP TARGET SP VERSION". Returns false on malformed input.
bool ParseRequestLine(std::string_view line, HttpRequest* request) {
    const size_t sp1 = line.find(' ');
    if (sp1 == std::string::npos) return false;
    const size_t sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return false;

    request->method_string = line.substr(0, sp1);
    request->method = HttpMethodFromString(request->method_string);
    const std::string_view target =
        std::string_view(line).substr(sp1 + 1, sp2 - sp1 - 1);
    request->version = line.substr(sp2 + 1);

    // Absolute-form targets ("http://host/path") are reduced to origin-form.
    std::string_view path_part = target;
    if (path_part.substr(0, 7) == "http://" ||
        path_part.substr(0, 8) == "https://") {
        const size_t scheme_end = path_part.find("://");
        const size_t auth_end = path_part.find('/', scheme_end + 3);
        path_part = auth_end == std::string_view::npos
                        ? std::string_view("/")
                        : path_part.substr(auth_end);
    }
    if (path_part == "*") {
        request->path = "*";
        return true;
    }

    const size_t q = path_part.find('?');
    request->path = UrlDecode(path_part.substr(0, q));
    if (q != std::string_view::npos) {
        request->query = std::string(path_part.substr(q + 1));
    }
    return true;
}

// Reads the request head (request line + headers) line by line.
bool ReadHead(TcpClientSocket* client, size_t max_head_bytes,
              HttpRequest* request, std::string* error) {
    SocketStream* s = client->stream();
    if (s == nullptr) {
        *error = "socket stream unavailable";
        return false;
    }

    std::string line;
    if (!s->ReadLine(&line, error)) return false;
    if (!ParseRequestLine(StripEol(line), request)) {
        *error = "malformed request line";
        return false;
    }

    size_t head_bytes = line.size();
    for (;;) {
        if (!s->ReadLine(&line, error)) return false;
        head_bytes += line.size();
        if (head_bytes > max_head_bytes) {
            *error = "request headers too large";
            return false;
        }
        const std::string_view stripped = StripEol(line);
        if (stripped.empty()) break;  // end of headers

        const size_t colon = stripped.find(':');
        if (colon == std::string_view::npos) continue;  // tolerate junk lines
        const std::string name(stripped.substr(0, colon));
        std::string_view value = stripped.substr(colon + 1);
        const size_t vb = value.find_first_not_of(" \t");
        if (vb != std::string_view::npos) value.remove_prefix(vb);
        request->headers.Add(name, std::string(value));
    }
    return true;
}

// Reads exactly n bytes through the stream's read-ahead buffer.
bool ReadExactBody(TcpClientSocket* client, size_t n, std::string* out,
                   std::string* error) {
    SocketStream* s = client->stream();
    std::vector<uint8_t> buf(n);
    if (!s->ReadExact(&buf, n, error)) return false;
    out->assign(reinterpret_cast<const char*>(buf.data()), n);
    return true;
}

// Reads a chunked transfer-encoded body (RFC 9112 section 7.1).
bool ReadChunkedBody(TcpClientSocket* client, size_t max_bytes,
                     std::string* out, std::string* error) {
    SocketStream* s = client->stream();
    out->clear();
    for (;;) {
        std::string line;
        if (!s->ReadLine(&line, error)) return false;
        const std::string_view stripped = StripEol(line);
        const size_t semi = stripped.find(';');  // chunk extensions
        unsigned long long size = 0;
        try {
            size = std::stoull(std::string(stripped.substr(0, semi)),
                               nullptr, 16);
        } catch (...) {
            *error = "malformed chunk size";
            return false;
        }
        if (size == 0) {
            // Consume trailer lines until the blank line.
            for (;;) {
                if (!s->ReadLine(&line, error)) return false;
                if (StripEol(line).empty()) return true;
            }
        }
        if (out->size() + size > max_bytes) {
            *error = "request body too large";
            return false;
        }
        std::vector<uint8_t> chunk(size);
        if (!s->ReadExact(&chunk, size, error)) return false;
        out->append(reinterpret_cast<const char*>(chunk.data()), size);
        // Chunk data is followed by CRLF.
        std::vector<uint8_t> crlf(2);
        if (!s->ReadExact(&crlf, 2, error)) return false;
        if (crlf[0] != '\r' || crlf[1] != '\n') {
            *error = "malformed chunk terminator";
            return false;
        }
    }
}

// Reads the request body according to Content-Length / Transfer-Encoding.
bool ReadRequestBody(TcpClientSocket* client, HttpRequest& request,
                     size_t max_bytes, std::string* error) {
    const std::string te = request.headers.Get("transfer-encoding");
    std::string te_lower = te;
    for (char& c : te_lower) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (te_lower.find("chunked") != std::string::npos) {
        return ReadChunkedBody(client, max_bytes, &request.body, error);
    }
    const std::string cl = request.headers.Get("content-length");
    if (cl.empty()) return true;
    size_t len = 0;
    try {
        len = std::stoull(cl);
    } catch (...) {
        *error = "malformed Content-Length";
        return false;
    }
    if (len > max_bytes) {
        *error = "request body too large";
        return false;
    }
    return ReadExactBody(client, len, &request.body, error);
}

// ---- Response writing ---------------------------------------------------

void WriteResponse(SocketStream* stream, const HttpRequest& request,
                   const HttpResponse& response) {
    std::string head;
    head.reserve(256);
    head += "HTTP/1.1 ";
    head += std::to_string(response.status);
    head += ' ';
    head += response.StatusText();
    head += "\r\n";

    for (const auto& [name, value] : response.headers.MapView()) {
        // Content-Length and Connection are managed by the server.
        if (IEquals(name, "content-length") || IEquals(name, "connection"))
            continue;
        head += name;
        head += ": ";
        head += value;
        head += "\r\n";
    }

    head += "Content-Length: ";
    head += std::to_string(response.body.size());
    head += "\r\nConnection: ";
    head += request.IsKeepAlive() ? "keep-alive" : "close";
    head += "\r\n\r\n";

    std::string error;
    stream->WriteAll(head, &error);
    if (request.method != HttpMethod::kHead && !response.body.empty()) {
        stream->WriteAll(response.body, &error);
    }
}

// ---- Static files -------------------------------------------------------

std::string MimeTypeFor(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    const std::string ext = path.substr(dot + 1);
    static const std::pair<const char*, const char*> kTypes[] = {
        {"html", "text/html; charset=utf-8"}, {"htm", "text/html; charset=utf-8"},
        {"css", "text/css; charset=utf-8"},       {"js", "text/javascript; charset=utf-8"},
        {"mjs", "text/javascript; charset=utf-8"},{"json", "application/json"},
        {"svg", "image/svg+xml"},                  {"png", "image/png"},
        {"jpg", "image/jpeg"},                     {"jpeg", "image/jpeg"},
        {"gif", "image/gif"},                      {"webp", "image/webp"},
        {"ico", "image/x-icon"},                   {"txt", "text/plain; charset=utf-8"},
        {"md", "text/markdown; charset=utf-8"},    {"map", "application/json"},
        {"wasm", "application/wasm"},              {"woff", "font/woff"},
        {"woff2", "font/woff2"},                   {"ttf", "font/ttf"},
        {"mp3", "audio/mpeg"},                     {"wav", "audio/wav"},
        {"mp4", "video/mp4"},                      {"webm", "video/webm"},
        {"pdf", "application/pdf"},                {"zip", "application/zip"},
        {"apk", "application/vnd.android.package-archive"},
    };
    for (const auto& [e, t] : kTypes) {
        if (ext == e) return t;
    }
    return "application/octet-stream";
}

// Rejects paths that escape the static root ("..", drive letters, etc.).
bool IsSafeRelativePath(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.front() == '\\') return false;
    size_t pos = 0;
    while (pos <= path.size()) {
        const size_t sep = path.find_first_of("/\\", pos);
        const std::string_view seg = std::string_view(path).substr(
            pos, sep == std::string::npos ? std::string_view::npos : sep - pos);
        if (seg.empty() || seg == ".." ||
            seg.find(':') != std::string_view::npos) {
            return false;
        }
        if (sep == std::string::npos) break;
        pos = sep + 1;
    }
    return true;
}

// Appends each path segment of `rel_path` onto `root`.
std::filesystem::path ResolveUnder(const std::filesystem::path& root,
                                  const std::string& rel_path) {
    std::filesystem::path out = root;
    size_t pos = 0;
    while (pos <= rel_path.size()) {
        const size_t slash = rel_path.find('/', pos);
        const std::string seg = std::string(rel_path.substr(
            pos, slash == std::string::npos ? std::string::npos : slash - pos));
        if (!seg.empty()) out /= seg;
        if (slash == std::string::npos) break;
        pos = slash + 1;
    }
    return out;
}

}  // namespace

// ---- HttpServer ---------------------------------------------------------

HttpServer::~HttpServer() { Stop(); }

bool HttpServer::Listen(const std::string& bind_address, uint16_t port,
                        int backlog, std::string* error) {
    return server_.Listen(bind_address, port, backlog, error);
}

void HttpServer::SetRouter(std::shared_ptr<HttpRouter> router) {
    router_ = std::move(router);
}

void HttpServer::SetStaticRoot(std::string root) {
    static_root_ = std::move(root);
}

void HttpServer::Run() {
    running_ = true;
    std::string error;
    while (server_.IsListening()) {
        auto client = server_.Accept(&error);
        if (!client) {
            if (server_.IsListening()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            continue;
        }
        std::thread([this, c = std::move(client)]() mutable {
            ServeClient(std::move(c));
        }).detach();
    }
    running_ = false;
}

void HttpServer::Stop() { server_.Close(); }

void HttpServer::ServeClient(std::unique_ptr<TcpClientSocket> client) {
    SocketStream* stream = client->stream();
    if (stream == nullptr) return;

    while (client->IsConnected()) {
        HttpRequest request;
        std::string error;
        if (!ReadHead(client.get(), max_header_bytes_, &request, &error)) {
            // If we got far enough to know the method, send a 400.
            if (!request.method_string.empty()) {
                WriteResponse(stream, request,
                              HttpResponse::JsonError(400, error));
            }
            break;
        }

        if (request.method == HttpMethod::kUnknown &&
            !request.method_string.empty()) {
            // Unknown methods still get a response, then we close.
            WriteResponse(stream, request,
                          HttpResponse::JsonError(501, "unsupported method"));
            break;
        }

        if (!ReadRequestBody(client.get(), request, max_body_bytes_,
                             &error)) {
            WriteResponse(stream, request,
                          HttpResponse::JsonError(400, error));
            break;
        }

        HttpResponse response;
        bool have_response = false;
        if (router_ && router_->RouteCount() > 0) {
            response = router_->Dispatch(request);
            have_response = true;
        }
        if ((!have_response || response.status == 404) &&
            (request.method == HttpMethod::kGet ||
             request.method == HttpMethod::kHead) &&
            TryServeStatic(request, &response)) {
            have_response = true;
        }
        if (!have_response) {
            response = HttpResponse::JsonError(404, "not found");
        }

        WriteResponse(stream, request, response);
        if (!request.IsKeepAlive()) break;
    }
    client->Disconnect();
}

bool HttpServer::TryServeStatic(const HttpRequest& request,
                                HttpResponse* response) const {
    if (static_root_.empty() || request.path.empty() ||
        request.path == "*") {
        return false;
    }
    // HTTP paths are root-relative ("/x"); strip the leading slash before the
    // relative-path safety check.
    std::string rel_path = UrlDecode(request.path);
    if (!rel_path.empty() && rel_path.front() == '/') {
        rel_path.erase(0, 1);
    }
    if (rel_path.empty()) rel_path = ".";
    if (!IsSafeRelativePath(rel_path)) return false;

    std::filesystem::path rel = ResolveUnder(static_root_, rel_path);

    std::error_code ec;
    if (std::filesystem::is_directory(rel, ec)) {
        rel /= "index.html";
    }
    if (!std::filesystem::is_regular_file(rel, ec)) return false;

    const auto size = std::filesystem::file_size(rel, ec);
    if (ec || size > 256ull * 1024 * 1024) return false;

    std::ifstream in(rel, std::ios::binary);
    if (!in) return false;
    std::string data(static_cast<size_t>(size), '\0');
    in.read(data.data(), static_cast<std::streamsize>(size));
    if (in.gcount() != static_cast<std::streamsize>(size)) return false;

    *response =
        HttpResponse::Make(200, std::move(data), MimeTypeFor(rel.string()));
    return true;
}

}  // namespace XER::Network::Http
