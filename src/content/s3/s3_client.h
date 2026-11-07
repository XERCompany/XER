#ifndef XER_CONTENT_S3_S3_CLIENT_H
#define XER_CONTENT_S3_S3_CLIENT_H

#include <string>
#include <string_view>
#include <vector>

#include "content/s3/s3_types.h"

// wingdi.h (windows.h) #defines GetObject → GetObjectA; keep the S3 name.
#pragma push_macro("GetObject")
#ifdef GetObject
#undef GetObject
#endif

namespace XER::Content::S3 {

// Synchronous S3 client over libcurl with AWS Signature V4 auth.
// Thread-safe for concurrent calls on the same instance (stateless per call).
class S3Client {
public:
    explicit S3Client(S3Config cfg = {});

    const S3Config& config() const { return cfg_; }
    void SetConfig(S3Config cfg) { cfg_ = std::move(cfg); }

    // Object operations -------------------------------------------------
    S3Result PutObject(const std::string& bucket, const std::string& key,
                       std::string_view data,
                       const std::string& content_type = {});
    S3Result PutObjectFile(const std::string& bucket, const std::string& key,
                           const std::string& file_path,
                           const std::string& content_type = {});
    // Downloads into `out` (binary-safe). Fails on non-2xx.
    S3Result GetObject(const std::string& bucket, const std::string& key,
                       std::string* out_data);
    S3Result GetObjectToFile(const std::string& bucket, const std::string& key,
                             const std::string& file_path);
    S3Result DeleteObject(const std::string& bucket, const std::string& key);
    // 200 = exists, 404 = missing, other = error in result.error.
    S3Result HeadObject(const std::string& bucket, const std::string& key);
    // HeadBucket: ok when the bucket exists and credentials work.
    S3Result Ping(const std::string& bucket);

    // Listing -----------------------------------------------------------
    // Fills `out` with up to max_keys entries (S3 caps at 1000 per call;
    // continuation tokens are followed automatically when `follow` is true).
    S3Result ListObjectsV2(const std::string& bucket,
                           const std::string& prefix,
                           int max_keys,
                           std::vector<S3ObjectMeta>* out,
                           bool* truncated_out = nullptr);

    // Presigned GET URL (query-string auth, no headers needed).
    std::string PresignGetUrl(const std::string& bucket,
                              const std::string& key,
                              long expires_s = 3600) const;

private:
    struct HttpResponse {
        long status = 0;
        std::string body;
        std::map<std::string, std::string> headers; // lowercase names
        std::string error;
        bool transport_ok = false;
    };

    // URL/path helpers ---------------------------------------------------
    std::string Scheme() const;
    std::string HostHeader(const std::string& bucket) const;
    // Canonical (encoded) URI path for signing, e.g. /bucket/a%20b.txt
    std::string CanonicalUri(const std::string& bucket,
                             const std::string& key) const;
    // Request path actually appended to the endpoint host.
    std::string RequestPath(const std::string& bucket,
                            const std::string& key) const;

    // Performs a signed request. `canonical_query` is the already-encoded
    // sorted query (may be empty); `query_for_url` is what goes on the wire.
    HttpResponse Perform(const std::string& method,
                         const std::string& bucket,
                         const std::string& key,
                         const std::string& canonical_query,
                         const std::string& query_for_url,
                         const std::string& body,
                         const std::string& content_type,
                         const std::vector<std::string>& extra_headers);

    HttpResponse PerformRaw(const std::string& method,
                            const std::string& url,
                            const std::vector<std::string>& headers,
                            const std::string& body,
                            bool head_only);

    S3Config cfg_;
};

} // namespace XER::Content::S3

#pragma pop_macro("GetObject")

#endif // XER_CONTENT_S3_S3_CLIENT_H
