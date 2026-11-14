/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "model_hub_http.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "helper/env_utils.h"

#include "content/network/socket/ssl_client_socket.h"
#include "content/network/socket/tcp_client_socket.h"
#include "content/network/transport/transport_client_socket.h"

namespace content::civitai::internal {

namespace {

constexpr size_t kReadChunk = 64 * 1024;
constexpr const char* kUserAgent = "XER-ModelHub/1.0";

bool IsUnreserved(uint8_t c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

std::string ToLower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(
        static_cast<unsigned char>(c)));
    return s;
}

// Case-insensitive header lookup on the stored (lowercase) map.
std::string HeaderValue(const std::map<std::string, std::string>& h,
                        const std::string& name) {
    auto it = h.find(name);
    return it == h.end() ? std::string{} : it->second;
}

void StripFragment(std::string* s) {
    auto pos = s->find('#');
    if (pos != std::string::npos) s->resize(pos);
}

}  // namespace

// ---------------------------------------------------------------------------
// URL helpers
// ---------------------------------------------------------------------------

bool ParseUrl(std::string_view url, ParsedUrl* out) {
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) return false;
    out->scheme = std::string(url.substr(0, scheme_end));
    if (out->scheme != "http" && out->scheme != "https") return false;

    std::string rest(url.substr(scheme_end + 3));
    StripFragment(&rest);

    const auto slash = rest.find('/');
    std::string authority = slash == std::string::npos
                                ? rest
                                : rest.substr(0, slash);
    std::string path_query = slash == std::string::npos
                                 ? "/"
                                 : rest.substr(slash);

    const auto q = path_query.find('?');
    out->path = q == std::string::npos ? path_query
                                       : path_query.substr(0, q);
    out->query = q == std::string::npos ? "" : path_query.substr(q + 1);
    if (out->path.empty()) out->path = "/";

    // userinfo@host[:port]
    const auto at = authority.rfind('@');
    if (at != std::string::npos) authority.erase(0, at + 1);
    const auto colon = authority.rfind(':');
    if (colon != std::string::npos &&
        authority.find(']') == std::string::npos) {  // not an IPv6 literal
        out->host = authority.substr(0, colon);
        try {
            out->port = static_cast<uint16_t>(
                std::stoul(authority.substr(colon + 1)));
        } catch (...) {
            return false;
        }
    } else {
        out->host = authority;
    }
    if (out->host.empty()) return false;
    if (out->port == 0) out->port = out->scheme == "https" ? 443 : 80;
    return true;
}

std::string UrlEncode(std::string_view value) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (const uint8_t c : value) {
        if (IsUnreserved(c)) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0xF]);
        }
    }
    return out;
}

std::string UrlDecode(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    auto hex_val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const int hi = hex_val(value[i + 1]);
            const int lo = hex_val(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i]);
    }
    return out;
}

// ---------------------------------------------------------------------------
// HttpStream
// ---------------------------------------------------------------------------

struct detail::HttpStreamImpl {
    std::unique_ptr<XER::Network::Transport::TransportClientSocket> sock;
    ParsedUrl url;
    std::map<std::string, std::string> headers;  // request headers (lowercase)
    std::string ca_bundle_path;
    bool skip_verify = false;

    // Raw bytes read past the header terminator; consumed by the decoder.
    std::vector<uint8_t> pending;
    size_t pending_pos = 0;

    enum class BodyMode { kUnset, kLength, kChunked, kClose } body_mode =
        BodyMode::kUnset;
    uint64_t body_remaining = 0;    // Content-Length mode
    uint64_t expected_total = 0;    // Content-Length or Content-Range total
    uint64_t resume_offset = 0;     // start offset for 206 responses
    bool body_eof = false;

    // Chunked decoder state.
    enum class ChunkState { kSize, kData, kDataCrlf, kTrailer, kDone }
        chunk_state = ChunkState::kSize;
    uint64_t chunk_remaining = 0;
    std::string chunk_line;  // partial line accumulator
};

HttpStream::HttpStream() : impl_(std::make_unique<detail::HttpStreamImpl>()) {}
HttpStream::~HttpStream() { Close(); }

void HttpStream::SetCaBundlePath(const std::string& path) {
    impl_->ca_bundle_path = path;
}
void HttpStream::SetSkipVerify(bool skip) { impl_->skip_verify = skip; }

void HttpStream::Close() {
    if (impl_ && impl_->sock) impl_->sock->Disconnect();
    if (impl_) impl_->sock.reset();
}

uint64_t HttpStream::ExpectedBodyBytes() const {
    return impl_ ? impl_->expected_total : 0;
}

uint64_t HttpStream::ResumeOffset() const {
    return impl_ ? impl_->resume_offset : 0;
}

namespace {

// Reads from the socket into |impl->pending| until the header terminator is
// found or EOF/error. Returns false on socket error.
bool FillUntilHeaders(detail::HttpStreamImpl* impl, std::string* error) {
    std::vector<uint8_t> buf(kReadChunk);
    for (;;) {
        const auto begin = impl->pending.begin() +
                           static_cast<std::ptrdiff_t>(impl->pending_pos);
        const auto end = impl->pending.end();
        if (std::search(begin, end, std::begin("\r\n\r\n"),
                        std::end("\r\n\r\n") - 1) != end) {
            return true;
        }
        const int n = impl->sock->Read(buf.data(), buf.size(), error);
        if (n < 0) return false;
        if (n == 0) return false;  // EOF before headers complete
        impl->pending.insert(impl->pending.end(), buf.begin(),
                             buf.begin() + n);
    }
}

// Extracts "key: value" header lines into |headers| (lowercase keys).
void ParseHeaderBlock(const std::string& block,
                      std::map<std::string, std::string>* headers) {
    size_t pos = 0;
    while (pos < block.size()) {
        const auto eol = block.find("\r\n", pos);
        if (eol == std::string::npos) break;
        const std::string line = block.substr(pos, eol - pos);
        pos = eol + 2;
        const auto colon = line.find(':');
        if (colon == std::string::npos || line.empty()) continue;
        std::string value = line.substr(colon + 1);
        // trim leading/trailing whitespace
        const auto first = value.find_first_not_of(" \t");
        const auto last = value.find_last_not_of(" \t");
        value = first == std::string::npos
                    ? ""
                    : value.substr(first, last - first + 1);
        (*headers)[ToLower(line.substr(0, colon))] = value;
    }
}

bool SendAll(XER::Network::Transport::TransportClientSocket* sock,
             const std::string& data, std::string* error) {
    size_t off = 0;
    while (off < data.size()) {
        const int n = sock->Write(
            reinterpret_cast<const uint8_t*>(data.data() + off),
            data.size() - off, error);
        if (n <= 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

// Pulls more raw bytes from the socket into pending. Returns false on error.
bool FillBody(detail::HttpStreamImpl* impl, std::string* error) {
    if (impl->body_eof) return true;
    std::vector<uint8_t> buf(kReadChunk);
    const int n = impl->sock->Read(buf.data(), buf.size(), error);
    if (n < 0) return false;
    if (n == 0) {
        impl->body_eof = true;
        return true;
    }
    impl->pending.insert(impl->pending.end(), buf.begin(),
                         buf.begin() + n);
    return true;
}

// Ensures at least one raw byte is available in pending (or EOF).
bool EnsureRaw(detail::HttpStreamImpl* impl, std::string* error) {
    if (impl->pending_pos < impl->pending.size()) return true;
    impl->pending.clear();
    impl->pending_pos = 0;
    return FillBody(impl, error);
}

// Reads one CRLF-terminated line from pending into |line| (without CRLF).
// Returns false on EOF before a complete line.
bool ReadLine(detail::HttpStreamImpl* impl, std::string* line, std::string* error) {
    for (;;) {
        const auto begin = impl->pending.begin() +
                           static_cast<std::ptrdiff_t>(impl->pending_pos);
        const auto it = std::search(begin, impl->pending.end(),
                                    std::begin("\r\n"),
                                    std::end("\r\n") - 1);
        if (it != impl->pending.end()) {
            const size_t line_len = static_cast<size_t>(
                it - (impl->pending.begin() +
                      static_cast<std::ptrdiff_t>(impl->pending_pos)));
            line->assign(
                reinterpret_cast<const char*>(
                    impl->pending.data() + impl->pending_pos),
                line_len);
            impl->pending_pos = static_cast<size_t>(
                (it - impl->pending.begin()) + 2);
            return true;
        }
        if (!FillBody(impl, error)) return false;
        if (impl->body_eof) return false;
    }
}

uint64_t ParseHex(const std::string& s) {
    uint64_t v = 0;
    for (char c : s) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        v = v * 16 + static_cast<uint64_t>(d);
    }
    return v;
}

// Consumes decoded body bytes into |dst|. Returns count produced, 0 at clean
// EOF, or -1 on error.
int64_t DecodeBody(detail::HttpStreamImpl* impl, uint8_t* dst, size_t dst_len,
                   std::string* error) {
    size_t produced = 0;
    while (produced < dst_len) {
        switch (impl->body_mode) {
            case detail::HttpStreamImpl::BodyMode::kLength: {
                if (impl->body_remaining == 0) {
                    impl->body_mode = detail::HttpStreamImpl::BodyMode::kUnset;
                    return produced ? static_cast<int64_t>(produced) : 0;
                }
                if (!EnsureRaw(impl, error)) {
                    return produced ? static_cast<int64_t>(produced) : -1;
                }
                const size_t avail = impl->pending.size() - impl->pending_pos;
                if (avail == 0) {
                    if (impl->body_eof) {
                        impl->body_mode = detail::HttpStreamImpl::BodyMode::kUnset;
                        return produced ? static_cast<int64_t>(produced) : -1;
                    }
                    continue;
                }
                const size_t take = std::min<size_t>(
                    avail, std::min<size_t>(dst_len - produced,
                                            impl->body_remaining));
                std::memcpy(dst + produced,
                            impl->pending.data() + impl->pending_pos, take);
                impl->pending_pos += take;
                impl->body_remaining -= take;
                produced += take;
                break;
            }
            case detail::HttpStreamImpl::BodyMode::kClose: {
                if (!EnsureRaw(impl, error)) {
                    return produced ? static_cast<int64_t>(produced) : -1;
                }
                const size_t avail = impl->pending.size() - impl->pending_pos;
                if (avail == 0) {
                    impl->body_mode = detail::HttpStreamImpl::BodyMode::kUnset;
                    return static_cast<int64_t>(produced);
                }
                const size_t take = std::min<size_t>(avail, dst_len - produced);
                std::memcpy(dst + produced,
                            impl->pending.data() + impl->pending_pos, take);
                impl->pending_pos += take;
                produced += take;
                break;
            }
            case detail::HttpStreamImpl::BodyMode::kChunked: {
                for (;;) {
                    if (impl->chunk_state == detail::HttpStreamImpl::ChunkState::kDone) {
                        impl->body_mode = detail::HttpStreamImpl::BodyMode::kUnset;
                        return produced ? static_cast<int64_t>(produced) : 0;
                    }
                    if (impl->chunk_state == detail::HttpStreamImpl::ChunkState::kSize) {
                        std::string line;
                        if (!ReadLine(impl, &line, error)) {
                            if (impl->body_eof) {
                                impl->body_mode = detail::HttpStreamImpl::BodyMode::kUnset;
                                return produced
                                           ? static_cast<int64_t>(produced)
                                           : -1;
                            }
                            return produced ? static_cast<int64_t>(produced)
                                            : -1;
                        }
                        // Strip chunk extensions after ';'.
                        const auto semi = line.find(';');
                        if (semi != std::string::npos) line.resize(semi);
                        impl->chunk_remaining = ParseHex(line);
                        impl->chunk_state = impl->chunk_remaining == 0
                                                ? detail::HttpStreamImpl::ChunkState::kTrailer
                                                : detail::HttpStreamImpl::ChunkState::kData;
                    } else if (impl->chunk_state ==
                               detail::HttpStreamImpl::ChunkState::kData) {
                        if (!EnsureRaw(impl, error)) {
                            return produced
                                       ? static_cast<int64_t>(produced)
                                       : -1;
                        }
                        const size_t avail =
                            impl->pending.size() - impl->pending_pos;
                        if (avail == 0) {
                            if (impl->body_eof) {
                                impl->body_mode = detail::HttpStreamImpl::BodyMode::kUnset;
                                return produced
                                           ? static_cast<int64_t>(produced)
                                           : -1;
                            }
                            continue;
                        }
                        const size_t take = std::min<size_t>(
                            avail,
                            std::min<size_t>(dst_len - produced,
                                             impl->chunk_remaining));
                        if (take == 0) break;  // dst full
                        std::memcpy(dst + produced,
                                    impl->pending.data() + impl->pending_pos,
                                    take);
                        impl->pending_pos += take;
                        impl->chunk_remaining -= take;
                        produced += take;
                        if (impl->chunk_remaining == 0)
                            impl->chunk_state =
                                detail::HttpStreamImpl::ChunkState::kDataCrlf;
                        break;
                    } else if (impl->chunk_state ==
                               detail::HttpStreamImpl::ChunkState::kDataCrlf) {
                        std::string crlf;
                        if (!ReadLine(impl, &crlf, error)) {
                            impl->body_mode = detail::HttpStreamImpl::BodyMode::kUnset;
                            return produced ? static_cast<int64_t>(produced)
                                            : -1;
                        }
                        impl->chunk_state = detail::HttpStreamImpl::ChunkState::kSize;
                    } else {  // kTrailer: consume trailer lines until empty
                        std::string line;
                        if (!ReadLine(impl, &line, error)) {
                            impl->body_mode = detail::HttpStreamImpl::BodyMode::kUnset;
                            return produced ? static_cast<int64_t>(produced)
                                            : -1;
                        }
                        if (line.empty()) impl->chunk_state = detail::HttpStreamImpl::ChunkState::kDone;
                    }
                    if (produced >= dst_len) break;
                }
                if (produced >= dst_len) return static_cast<int64_t>(produced);
                break;
            }
            case detail::HttpStreamImpl::BodyMode::kUnset:
            default:
                return static_cast<int64_t>(produced);
        }
        if (produced >= dst_len) break;
    }
    return static_cast<int64_t>(produced);
}

}  // namespace

bool HttpStream::Open(std::string_view url,
                      const std::map<std::string, std::string>& extra_headers,
                      std::string_view range, int max_redirects,
                      std::string* error) {
    std::string current(url);
    std::map<std::string, std::string> headers = extra_headers;
    // Normalize header names to lowercase for consistent lookup.
    std::map<std::string, std::string> norm_headers;
    for (const auto& [k, v] : headers) norm_headers[ToLower(k)] = v;

    for (int redirect = 0; redirect <= max_redirects; ++redirect) {
        ParsedUrl pu;
        if (!ParseUrl(current, &pu)) {
            if (error) *error = "malformed URL: " + current;
            return false;
        }

        impl_->url = pu;
        impl_->headers = norm_headers;
        impl_->pending.clear();
        impl_->pending_pos = 0;
        impl_->body_mode = detail::HttpStreamImpl::BodyMode::kUnset;
        impl_->body_remaining = 0;
        impl_->expected_total = 0;
        impl_->resume_offset = 0;
        impl_->body_eof = false;
        impl_->chunk_state = detail::HttpStreamImpl::ChunkState::kSize;
        impl_->chunk_remaining = 0;
        impl_->chunk_line.clear();
        info_ = ResponseInfo{};

        impl_->sock = pu.scheme == "https"
                          ? std::unique_ptr<XER::Network::Transport::
                                                TransportClientSocket>(
                                std::make_unique<XER::Network::Socket::
                                                     SslClientSocket>())
                          : std::unique_ptr<XER::Network::Transport::
                                                TransportClientSocket>(
                                std::make_unique<XER::Network::Socket::
                                                     TcpClientSocket>());

        if (auto* ssl = dynamic_cast<XER::Network::Socket::SslClientSocket*>(
                impl_->sock.get())) {
            const std::string ca = impl_->ca_bundle_path.empty()
                                       ? DefaultCaBundlePath()
                                       : impl_->ca_bundle_path;
            ssl->SetCaBundlePath(ca);
            ssl->SetSkipVerify(impl_->skip_verify);
            ssl->SetSniHostname(pu.host);
        }

        std::string conn_error;
        if (!impl_->sock->Connect(pu.host, pu.port, &conn_error)) {
            if (error) *error = "connect " + pu.host + ":" +
                                std::to_string(pu.port) + " failed: " +
                                conn_error;
            return false;
        }

        // Build the request.
        std::string req = "GET " + pu.path;
        if (!pu.query.empty()) req += "?" + pu.query;
        req += " HTTP/1.1\r\n";
        req += "Host: " + pu.host;
        if (pu.port != (pu.scheme == "https" ? 443 : 80))
            req += ":" + std::to_string(pu.port);
        req += "\r\n";
        req += "User-Agent: " + std::string(kUserAgent) + "\r\n";
        req += "Accept: */*\r\n";
        req += "Connection: close\r\n";
        if (!range.empty()) req += "Range: " + std::string(range) + "\r\n";
        for (const auto& [k, v] : impl_->headers) {
            // Header names were normalized to lowercase; emit canonical form
            // for common ones, otherwise the lowercase name (HTTP/1.1 header
            // names are case-insensitive).
            req += k + ": " + v + "\r\n";
        }
        req += "\r\n";

        if (!SendAll(impl_->sock.get(), req, &conn_error)) {
            if (error) *error = "send request failed: " + conn_error;
            return false;
        }

        // Read until end of headers.
        if (!FillUntilHeaders(impl_.get(), &conn_error)) {
            if (error) *error = "read response headers failed: " +
                                conn_error;
            return false;
        }
        // Locate the header terminator in pending.
        const auto& pend = impl_->pending;
        const auto term = std::search(
            pend.begin() + static_cast<std::ptrdiff_t>(impl_->pending_pos),
            pend.end(), std::begin("\r\n\r\n"), std::end("\r\n\r\n") - 1);
        if (term == pend.end()) {
            if (error) *error = "incomplete response headers";
            return false;
        }
        const auto header_bytes = static_cast<size_t>(
            term - (pend.begin() +
                    static_cast<std::ptrdiff_t>(impl_->pending_pos)));
        const std::string header_block(reinterpret_cast<const char*>(
                                           pend.data() + impl_->pending_pos),
                                       header_bytes);
        impl_->pending_pos += header_bytes + 4;
        // Compact pending if the consumed prefix is large.
        if (impl_->pending_pos > 4096) {
            impl_->pending.erase(
                impl_->pending.begin(),
                impl_->pending.begin() +
                    static_cast<std::ptrdiff_t>(impl_->pending_pos));
            impl_->pending_pos = 0;
        }

        // Status line.
        const auto first_eol = header_block.find("\r\n");
        const std::string status_line = header_block.substr(
            0, first_eol == std::string::npos ? header_block.size()
                                              : first_eol);
        {
            std::sscanf(status_line.c_str(), "HTTP/%*s %d %*[^\r\n]",
                        &info_.status);
        }
        const auto sp1 = status_line.find(' ');
        const auto sp2 = sp1 == std::string::npos
                             ? std::string::npos
                             : status_line.find(' ', sp1 + 1);
        info_.status_text = sp2 == std::string::npos
                                ? ""
                                : status_line.substr(sp2 + 1);

        ParseHeaderBlock(header_block.substr(
                             first_eol == std::string::npos
                                 ? header_block.size()
                                 : first_eol + 2),
                         &info_.headers);
        info_.final_url = current;

        // Redirect?
        if (info_.status >= 300 && info_.status < 400) {
            const std::string loc =
                HeaderValue(info_.headers, "location");
            if (loc.empty() || redirect == max_redirects) {
                if (error) *error = "redirect without Location or too many";
                return false;
            }
            // Resolve relative Location against current URL.
            std::string next;
            if (loc.rfind("http://", 0) == 0 ||
                loc.rfind("https://", 0) == 0) {
                next = loc;
            } else if (!loc.empty() && loc[0] == '/') {
                next = pu.scheme + "://" + pu.host;
                if (pu.port != (pu.scheme == "https" ? 443 : 80))
                    next += ":" + std::to_string(pu.port);
                next += loc;
            } else {
                // Relative path — resolve against current path directory.
                std::string base = pu.path;
                const auto slash = base.rfind('/');
                base = slash == std::string::npos ? "/" : base.substr(0, slash + 1);
                next = pu.scheme + "://" + pu.host;
                if (pu.port != (pu.scheme == "https" ? 443 : 80))
                    next += ":" + std::to_string(pu.port);
                next += base + loc;
            }
            // Drop Authorization when crossing hosts.
            ParsedUrl next_url;
            if (ParseUrl(next, &next_url) && next_url.host != pu.host) {
                norm_headers.erase("authorization");
            }
            current = next;
            Close();
            continue;
        }

        if (info_.status < 200 || info_.status >= 300) {
            if (error) *error = "HTTP " + std::to_string(info_.status) +
                                " " + info_.status_text;
            return true;  // headers are valid; caller inspects status
        }

        // Determine body framing.
        const std::string te = HeaderValue(info_.headers, "transfer-encoding");
        const std::string cl = HeaderValue(info_.headers, "content-length");
        const std::string cr = HeaderValue(info_.headers, "content-range");

        impl_->body_mode = detail::HttpStreamImpl::BodyMode::kClose;
        impl_->expected_total = 0;
        impl_->resume_offset = 0;

        if (te.find("chunked") != std::string::npos) {
            impl_->body_mode = detail::HttpStreamImpl::BodyMode::kChunked;
            impl_->chunk_state = detail::HttpStreamImpl::ChunkState::kSize;
        } else if (!cl.empty()) {
            impl_->body_mode = detail::HttpStreamImpl::BodyMode::kLength;
            impl_->body_remaining = std::strtoull(cl.c_str(), nullptr, 10);
            impl_->expected_total = impl_->body_remaining;
        } else if (!cr.empty()) {
            // Content-Range: bytes <start>-<end>/<total|*>
            const auto sp = cr.find(' ');
            const auto dash = cr.find('-', sp == std::string::npos ? 0 : sp);
            const auto slash = cr.find('/', dash == std::string::npos
                                                 ? 0
                                                 : dash);
            if (dash != std::string::npos && slash != std::string::npos) {
                impl_->resume_offset = std::strtoull(
                    cr.c_str() + (sp == std::string::npos ? 0 : sp + 1),
                    nullptr, 10);
                const std::string total = cr.substr(slash + 1);
                if (total != "*")
                    impl_->expected_total =
                        std::strtoull(total.c_str(), nullptr, 10);
                impl_->body_mode = detail::HttpStreamImpl::BodyMode::kLength;
                impl_->body_remaining = impl_->expected_total >
                                                impl_->resume_offset
                                            ? impl_->expected_total -
                                                  impl_->resume_offset
                                            : 0;
            }
        }
        return true;
    }
    if (error) *error = "too many redirects";
    return false;
}

int HttpStream::ReadBody(uint8_t* buf, size_t len) {
    if (!impl_ || !impl_->sock || impl_->body_mode == detail::HttpStreamImpl::BodyMode::kUnset)
        return 0;
    const int64_t n = DecodeBody(impl_.get(), buf, len, &error_);
    if (n < 0) {
        error_ = error_.empty() ? "body read failed" : error_;
        return -1;
    }
    return static_cast<int>(n);
}

// ---------------------------------------------------------------------------
// HttpGet
// ---------------------------------------------------------------------------

bool HttpGet(std::string_view url,
             const std::map<std::string, std::string>& headers,
             std::string* body_out, int* status_out, size_t max_bytes,
             std::string* error_out) {
    if (max_bytes == 0) max_bytes = 64u * 1024u * 1024u;
    HttpStream stream;
    if (!stream.Open(url, headers, "", 5, error_out)) return false;
    if (status_out) *status_out = stream.Info().status;
    if (stream.Info().status < 200 || stream.Info().status >= 300)
        return false;

    body_out->clear();
    std::vector<uint8_t> buf(kReadChunk);
    for (;;) {
        const int n = stream.ReadBody(buf.data(), buf.size());
        if (n < 0) {
            if (error_out) *error_out = stream.Error();
            return false;
        }
        if (n == 0) break;
        if (body_out->size() + static_cast<size_t>(n) > max_bytes) {
            if (error_out) *error_out = "response exceeds size cap";
            return false;
        }
        body_out->append(reinterpret_cast<const char*>(buf.data()),
                         static_cast<size_t>(n));
    }
    return true;
}

std::string DefaultCaBundlePath() {
    // 1. Explicit env overrides.
    for (const char* var : {"XER_CA_BUNDLE", "SSL_CERT_FILE",
                            "CURL_CA_BUNDLE"}) {
        const std::string v = XER::Helper::GetEnv(var);
        if (!v.empty() && std::filesystem::exists(v)) {
            return v;
        }
    }

    // 2. Well-known install locations (Windows-first, then POSIX).
    static const char* kCandidates[] = {
#if defined(_WIN32)
        "C:/vcpkg/installed/x64-windows-release/share/curl/cacert.pem",
        "C:/vcpkg/installed/x64-windows-release/share/openssl/cacert.pem",
        "C:/Program Files/Git/mingw64/ssl/certs/ca-bundle.crt",
        "C:/Program Files/Git/mingw64/etc/ssl/certs/ca-bundle.crt",
        "C:/Windows/System32/curl-ca-bundle.crt",
#else
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/ssl/cert.pem",
        "/etc/pki/tls/certs/ca-bundle.crt",
#endif
    };
    for (const char* c : kCandidates) {
        std::error_code ec;
        if (std::filesystem::exists(c, ec)) {
            return c;
        }
    }
    return {};
}

}  // namespace content::civitai::internal
