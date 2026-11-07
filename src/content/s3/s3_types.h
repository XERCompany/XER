#ifndef XER_CONTENT_S3_S3_TYPES_H
#define XER_CONTENT_S3_S3_TYPES_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace XER::Content::S3 {

// Connection + credentials for one S3(-compatible) endpoint.
// Works with AWS S3, MinIO, Ceph, Wasabi, R2 — anything speaking the S3 API.
struct S3Config {
    std::string endpoint = "s3.amazonaws.com"; // host[:port], no scheme
    std::string region = "us-east-1";
    std::string access_key;
    std::string secret_key;
    // Optional session token for temporary (STS) credentials.
    std::string session_token;
    // Path-style URLs: https://endpoint/bucket/key — required by MinIO and
    // most S3-compatible stores. Virtual-hosted (bucket.endpoint) is AWS default.
    bool path_style = false;
    bool use_http = false;    // plain http instead of https
    long timeout_ms = 60000;  // per-request timeout
};

// Outcome of a single S3 call.
struct S3Result {
    bool ok = false;
    long status = 0;                    // HTTP status (0 = transport error)
    std::string error;                  // human-readable failure reason
    std::map<std::string, std::string> headers; // response headers, lowercase keys
};

// One entry from ListObjectsV2.
struct S3ObjectMeta {
    std::string key;
    std::string etag;
    std::string last_modified;          // RFC-3339 as returned by S3
    std::uint64_t size = 0;
};

} // namespace XER::Content::S3

#endif // XER_CONTENT_S3_S3_TYPES_H
