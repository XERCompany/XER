#include "content/s3/s3_client.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <utility>

#include <pugixml.hpp>

#include "content/network/http/http_sync_request.h"
#include "content/s3/s3_signer.h"

// wingdi.h (pulled in via curl → windows.h) #defines GetObject → GetObjectA,
// which would rename this class's method. Undo it here.
#ifdef GetObject
#undef GetObject
#endif

namespace XER::Content::S3 {
namespace {

std::string ReadFileBytes(const std::string& path, bool* ok) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        *ok = false;
        return {};
    }
    std::string data((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    *ok = true;
    return data;
}

} // namespace

S3Client::S3Client(S3Config cfg) : cfg_(std::move(cfg)) {}

std::string S3Client::Scheme() const { return cfg_.use_http ? "http" : "https"; }

std::string S3Client::HostHeader(const std::string& bucket) const {
    if (cfg_.path_style) return cfg_.endpoint;
    return bucket + "." + cfg_.endpoint;
}

std::string S3Client::CanonicalUri(const std::string& bucket,
                                   const std::string& key) const {
    std::string path;
    if (cfg_.path_style) {
        path = "/" + bucket;
        if (!key.empty()) path += "/" + key;
    } else {
        path = "/";
        if (!key.empty()) path += key;
    }
    return Sign::UriEncode(path, true);
}

std::string S3Client::RequestPath(const std::string& bucket,
                                  const std::string& key) const {
    // Same as CanonicalUri — the wire path IS the canonical path for S3.
    return CanonicalUri(bucket, key);
}

S3Client::HttpResponse S3Client::PerformRaw(
    const std::string& method, const std::string& url,
    const std::vector<std::string>& headers, const std::string& body,
    bool head_only) {
    (void)head_only;  // SyncRequest suppresses the body for HEAD itself.
    const auto r = XER::network::http::SyncRequest(method, url, body, headers,
                                              cfg_.timeout_ms, "XER-S3/1.0");
    HttpResponse out;
    out.status = r.status;
    out.body = std::move(r.body);
    out.headers = std::move(r.headers);
    out.error = std::move(r.error);
    out.transport_ok = r.transport_ok;
    return out;
}

S3Client::HttpResponse S3Client::Perform(
    const std::string& method, const std::string& bucket,
    const std::string& key, const std::string& canonical_query,
    const std::string& query_for_url, const std::string& body,
    const std::string& content_type,
    const std::vector<std::string>& extra_headers) {
    const std::string payload_hash = Sign::Sha256Hex(body);
    const std::string host = HostHeader(bucket);
    const std::string canonical_uri = CanonicalUri(bucket, key);
    const Sign::V4Auth auth = Sign::SignHeaders(
        cfg_, method, canonical_uri, canonical_query, host, payload_hash,
        content_type, extra_headers);

    std::vector<std::string> headers;
    headers.push_back("Authorization: " + auth.authorization);
    headers.push_back("x-amz-date: " + auth.amz_date);
    headers.push_back("x-amz-content-sha256: " + auth.payload_hash);
    if (!cfg_.session_token.empty()) {
        headers.push_back("x-amz-security-token: " + cfg_.session_token);
    }
    if (!content_type.empty()) {
        headers.push_back("Content-Type: " + content_type);
    }
    for (const auto& h : extra_headers) headers.push_back(h);

    std::string url = Scheme() + "://" + host + RequestPath(bucket, key);
    if (!query_for_url.empty()) url += "?" + query_for_url;
    return PerformRaw(method, url, headers, body, method == "HEAD");
}

S3Result S3Client::PutObject(const std::string& bucket,
                             const std::string& key,
                             std::string_view data,
                             const std::string& content_type) {
    const std::string body(data);
    const auto r = Perform("PUT", bucket, key, "", "", body, content_type, {});
    S3Result res;
    res.status = r.status;
    res.ok = r.transport_ok && r.status >= 200 && r.status < 300;
    if (!r.transport_ok) res.error = r.error;
    else if (!res.ok)
        res.error = "HTTP " + std::to_string(r.status);
    for (const auto& [k, v] : r.headers) res.headers[k] = v;
    return res;
}

S3Result S3Client::PutObjectFile(const std::string& bucket,
                                 const std::string& key,
                                 const std::string& file_path,
                                 const std::string& content_type) {
    bool ok = false;
    const std::string body = ReadFileBytes(file_path, &ok);
    if (!ok) {
        S3Result res;
        res.error = "cannot read " + file_path;
        return res;
    }
    return PutObject(bucket, key, body, content_type);
}

S3Result S3Client::GetObject(const std::string& bucket,
                             const std::string& key,
                             std::string* out_data) {
    const auto r = Perform("GET", bucket, key, "", "", "", "", {});
    S3Result res;
    res.status = r.status;
    res.ok = r.transport_ok && r.status >= 200 && r.status < 300;
    if (res.ok && out_data != nullptr) *out_data = r.body;
    if (!r.transport_ok) res.error = r.error;
    else if (!res.ok)
        res.error = "HTTP " + std::to_string(r.status);
    for (const auto& [k, v] : r.headers) res.headers[k] = v;
    return res;
}

S3Result S3Client::GetObjectToFile(const std::string& bucket,
                                   const std::string& key,
                                   const std::string& file_path) {
    std::string data;
    const auto res = GetObject(bucket, key, &data);
    if (!res.ok) return res;
    std::ofstream f(file_path, std::ios::binary | std::ios::trunc);
    if (!f) {
        S3Result out;
        out.error = "cannot write " + file_path;
        return out;
    }
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    return res;
}

S3Result S3Client::DeleteObject(const std::string& bucket,
                                const std::string& key) {
    const auto r = Perform("DELETE", bucket, key, "", "", "", "", {});
    S3Result res;
    res.status = r.status;
    res.ok = r.transport_ok && (r.status == 204 || r.status == 200);
    if (!r.transport_ok) res.error = r.error;
    else if (!res.ok)
        res.error = "HTTP " + std::to_string(r.status);
    return res;
}

S3Result S3Client::HeadObject(const std::string& bucket,
                              const std::string& key) {
    // HEAD has an empty body; sha256 of "" is the payload hash.
    const auto r = Perform("HEAD", bucket, key, "", "", "", "", {});
    S3Result res;
    res.status = r.status;
    res.ok = r.transport_ok && r.status == 200;
    if (!r.transport_ok) res.error = r.error;
    for (const auto& [k, v] : r.headers) res.headers[k] = v;
    return res;
}

S3Result S3Client::Ping(const std::string& bucket) {
    // HeadBucket: HEAD on the bucket root (no key).
    const auto r = Perform("HEAD", bucket, "", "", "", "", "", {});
    S3Result res;
    res.status = r.status;
    res.ok = r.transport_ok && r.status == 200;
    if (!r.transport_ok) res.error = r.error;
    else if (!res.ok)
        res.error = "HTTP " + std::to_string(r.status);
    return res;
}

S3Result S3Client::ListObjectsV2(const std::string& bucket,
                                 const std::string& prefix,
                                 int max_keys,
                                 std::vector<S3ObjectMeta>* out,
                                 bool* truncated_out) {
    if (out == nullptr) {
        S3Result res;
        res.error = "null output vector";
        return res;
    }
    out->clear();
    bool truncated = false;
    std::string continuation;
    S3Result last;
    const int cap = std::max(1, max_keys);

    while (static_cast<int>(out->size()) < cap) {
        const int remaining = cap - static_cast<int>(out->size());
        const int page_size = std::min(1000, remaining);
        std::vector<std::pair<std::string, std::string>> q = {
            {"list-type", "2"},
            {"max-keys", std::to_string(page_size)},
        };
        if (!prefix.empty()) q.emplace_back("prefix", prefix);
        if (!continuation.empty()) {
            q.emplace_back("continuation-token", continuation);
        }
        const std::string canonical_query = Sign::CanonicalQuery(q);

        // ListObjectsV2 targets the bucket root — key is empty and the
        // query carries everything.
        const auto r = Perform("GET", bucket, "", canonical_query,
                               canonical_query, "", "", {});
        last.status = r.status;
        last.ok = r.transport_ok && r.status >= 200 && r.status < 300;
        if (!r.transport_ok) {
            last.error = r.error;
            break;
        }
        if (r.status < 200 || r.status >= 300) {
            last.error = "HTTP " + std::to_string(r.status) + ": " + r.body;
            break;
        }

        pugi::xml_document doc;
        const pugi::xml_parse_result pr = doc.load_buffer(
            r.body.data(), r.body.size());
        if (!pr) {
            last.error = std::string("XML parse: ") + pr.description();
            last.ok = false;
            break;
        }
        bool page_truncated = false;
        continuation.clear();
        for (const auto& node : doc.child("ListBucketResult").children("Contents")) {
            S3ObjectMeta meta;
            if (const auto k = node.child("Key").text(); k) meta.key = k.as_string();
            if (const auto s = node.child("Size").text(); s)
                meta.size = std::strtoull(s.as_string(), nullptr, 10);
            if (const auto e = node.child("ETag").text(); e)
                meta.etag = e.as_string();
            if (const auto m = node.child("LastModified").text(); m)
                meta.last_modified = m.as_string();
            out->push_back(std::move(meta));
        }
        if (const auto t = doc.child("ListBucketResult").child("IsTruncated").text();
            t && std::string(t.as_string()) == "true") {
            page_truncated = true;
            if (const auto ct =
                    doc.child("ListBucketResult").child("NextContinuationToken").text();
                ct) {
                continuation = ct.as_string();
            } else {
                page_truncated = false; // truncated but no token — stop anyway
            }
        }
        truncated = page_truncated;
        if (!page_truncated || continuation.empty()) break;
    }

    if (truncated_out) *truncated_out = truncated;
    return last;
}

std::string S3Client::PresignGetUrl(const std::string& bucket,
                                   const std::string& key,
                                   long expires_s) const {
    const std::string host = HostHeader(bucket);
    const std::string path = RequestPath(bucket, key);
    const std::string query = Sign::PresignQueryGet(cfg_, path, host, expires_s);
    return Scheme() + "://" + host + path + "?" + query;
}

} // namespace XER::Content::S3
