#ifndef XER_CONTENT_S3_S3_SIGNER_H
#define XER_CONTENT_S3_S3_SIGNER_H

#include <string>
#include <string_view>
#include <vector>

#include "content/s3/s3_types.h"

namespace XER::Content::S3::Sign {

// AWS Signature Version 4 helpers (OpenSSL-backed).

// Hex-encoded SHA-256 of arbitrary bytes.
std::string Sha256Hex(std::string_view data);

// Percent-encode one URI path segment per SigV4 rules
// (unreserved: A-Z a-z 0-9 - _ . ~). '/' inside `segment` is preserved.
std::string UriEncode(std::string_view segment, bool keep_slash = true);

// Canonical query string: "a=1&b=2" pairs already encoded, sorted by name.
// Caller passes raw (unencoded) pairs; this encodes and sorts them.
std::string CanonicalQuery(
    const std::vector<std::pair<std::string, std::string>>& pairs);

// Headers that must accompany a header-authenticated request.
// `extra_signed` are additional "Name: value" headers to include in the
// signature (e.g. Content-Type); they are sent verbatim by the caller.
struct V4Auth {
    std::string authorization;   // Authorization header value
    std::string amz_date;        // x-amz-date value (ISO basic)
    std::string payload_hash;    // x-amz-content-sha256 value
};

// Produces the Authorization + x-amz-* values for a SigV4 header-signed
// request. `canonical_uri` and `canonical_query` must already be encoded;
// `host_header` is the Host header value that will actually be sent
// (host or host:port). Signed header set: host, x-amz-content-sha256,
// x-amz-date, plus content-type and every entry in `extra_signed`.
V4Auth SignHeaders(const S3Config& cfg,
                   const std::string& method,
                   const std::string& canonical_uri,
                   const std::string& canonical_query,
                   const std::string& host_header,
                   const std::string& payload_hash,
                   const std::string& content_type,
                   const std::vector<std::string>& extra_signed);

// Builds a presigned URL query string for a GET (UNSIGNED-PAYLOAD).
// `canonical_uri`/`host_header` as above; `expires_s` clamps to S3's 7 days.
std::string PresignQueryGet(const S3Config& cfg,
                            const std::string& canonical_uri,
                            const std::string& host_header,
                            long expires_s);

// Current UTC time formatted as YYYYMMDDTHHMMSSZ and YYYYMMDD.
void AmzTimestamps(std::string* amz_date, std::string* date_stamp);

} // namespace XER::Content::S3::Sign

#endif // XER_CONTENT_S3_S3_SIGNER_H
