#include "content/s3/s3_signer.h"

#include "content/hex/hex.h"
#include "helper/string_utils.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <utility>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

namespace XER::Content::S3::Sign {
namespace {

bool IsUnreserved(unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

std::string HmacSha256Raw(std::string_view key, std::string_view data) {
    std::string out(EVP_MAX_MD_SIZE, '\0');
    unsigned int len = 0;
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const uint8_t*>(data.data()), data.size(),
         reinterpret_cast<uint8_t*>(out.data()), &len);
    out.resize(len);
    return out;
}

std::string ToLower(std::string s) {
    return XER::Engine::Helper::String::ToLowerAscii(s);
}

std::string Trim(std::string_view s) {
    return XER::Engine::Helper::String::TrimAsciiWhitespaceCopy(s);
}

} // namespace

std::string Sha256Hex(std::string_view data) {
    uint8_t digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const uint8_t*>(data.data()), data.size(), digest);
    return XER::Content::Hex::Encode(digest, sizeof(digest));
}

std::string UriEncode(std::string_view segment, bool keep_slash) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(segment.size() + 8);
    for (const unsigned char c : segment) {
        if (IsUnreserved(c) || (keep_slash && c == '/')) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0x0F];
        }
    }
    return out;
}

std::string CanonicalQuery(
    const std::vector<std::pair<std::string, std::string>>& pairs) {
    std::vector<std::pair<std::string, std::string>> enc;
    enc.reserve(pairs.size());
    for (const auto& [k, v] : pairs) {
        enc.emplace_back(UriEncode(k, false), UriEncode(v, false));
    }
    std::sort(enc.begin(), enc.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::string out;
    for (std::size_t i = 0; i < enc.size(); ++i) {
        if (i) out += '&';
        out += enc[i].first + "=" + enc[i].second;
    }
    return out;
}

V4Auth SignHeaders(const S3Config& cfg,
                   const std::string& method,
                   const std::string& canonical_uri,
                   const std::string& canonical_query,
                   const std::string& host_header,
                   const std::string& payload_hash,
                   const std::string& content_type,
                   const std::vector<std::string>& extra_signed) {
    std::string amz_date, date_stamp;
    AmzTimestamps(&amz_date, &date_stamp);

    // Collect headers to sign: lowercase name -> trimmed value.
    std::vector<std::pair<std::string, std::string>> signed_pairs;
    signed_pairs.emplace_back("host", Trim(host_header));
    signed_pairs.emplace_back("x-amz-content-sha256", payload_hash);
    signed_pairs.emplace_back("x-amz-date", amz_date);
    if (!cfg.session_token.empty()) {
        signed_pairs.emplace_back("x-amz-security-token", cfg.session_token);
    }
    if (!content_type.empty()) {
        signed_pairs.emplace_back("content-type", Trim(content_type));
    }
    for (const auto& h : extra_signed) {
        const std::size_t colon = h.find(':');
        if (colon == std::string::npos) continue;
        const std::string name = ToLower(Trim(std::string_view(h).substr(0, colon)));
        const std::string value = Trim(std::string_view(h).substr(colon + 1));
        // Skip headers already in the fixed set.
        if (name == "host" || name == "content-type" ||
            name == "x-amz-content-sha256" || name == "x-amz-date" ||
            name == "x-amz-security-token") {
            continue;
        }
        signed_pairs.emplace_back(name, value);
    }
    std::sort(signed_pairs.begin(), signed_pairs.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::string canonical_headers;
    std::string signed_names;
    for (std::size_t i = 0; i < signed_pairs.size(); ++i) {
        canonical_headers += signed_pairs[i].first + ":" +
                             signed_pairs[i].second + "\n";
        if (i) signed_names += ';';
        signed_names += signed_pairs[i].first;
    }

    const std::string canonical_request =
        method + "\n" + canonical_uri + "\n" + canonical_query + "\n" +
        canonical_headers + "\n" + signed_names + "\n" + payload_hash;
    const std::string scope =
        date_stamp + "/" + cfg.region + "/s3/aws4_request";
    const std::string string_to_sign =
        "AWS4-HMAC-SHA256\n" + amz_date + "\n" + scope + "\n" +
        Sha256Hex(canonical_request);

    const std::string k_date = "AWS4" + cfg.secret_key;
    const std::string k_region =
        HmacSha256Raw(k_date, date_stamp);
    const std::string k_service = HmacSha256Raw(k_region, cfg.region);
    const std::string k_signing =
        HmacSha256Raw(k_service, "aws4_request");
    const std::string sig_raw = HmacSha256Raw(k_signing, string_to_sign);
    const std::string signature =
        XER::Content::Hex::Encode(sig_raw.data(), sig_raw.size());

    V4Auth auth;
    auth.authorization =
        "AWS4-HMAC-SHA256 Credential=" + cfg.access_key + "/" + scope +
        ", SignedHeaders=" + signed_names + ", Signature=" + signature;
    auth.amz_date = amz_date;
    auth.payload_hash = payload_hash;
    return auth;
}

std::string PresignQueryGet(const S3Config& cfg,
                            const std::string& canonical_uri,
                            const std::string& host_header,
                            long expires_s) {
    std::string amz_date, date_stamp;
    AmzTimestamps(&amz_date, &date_stamp);
    expires_s = std::clamp(expires_s, 60L, 604800L); // 1 min .. 7 days

    const std::string scope =
        date_stamp + "/" + cfg.region + "/s3/aws4_request";
    const std::string credential = cfg.access_key + "/" + scope;
    const std::string signed_headers = "host";
    const std::string canonical_headers = "host:" + Trim(host_header) + "\n";
    const std::string payload_hash = "UNSIGNED-PAYLOAD";
    const std::string canonical_request =
        "GET\n" + canonical_uri + "\n\n" + canonical_headers + "\n" +
        signed_headers + "\n" + payload_hash;
    const std::string string_to_sign =
        "AWS4-HMAC-SHA256\n" + amz_date + "\n" + scope + "\n" +
        Sha256Hex(canonical_request);

    const std::string k_date = "AWS4" + cfg.secret_key;
    const std::string k_region = HmacSha256Raw(k_date, date_stamp);
    const std::string k_service = HmacSha256Raw(k_region, cfg.region);
    const std::string k_signing = HmacSha256Raw(k_service, "aws4_request");
    const std::string sig_raw = HmacSha256Raw(k_signing, string_to_sign);
    const std::string signature =
        XER::Content::Hex::Encode(sig_raw.data(), sig_raw.size());

    return CanonicalQuery({
        {"X-Amz-Algorithm", "AWS4-HMAC-SHA256"},
        {"X-Amz-Credential", credential},
        {"X-Amz-Date", amz_date},
        {"X-Amz-Expires", std::to_string(expires_s)},
        {"X-Amz-SignedHeaders", signed_headers},
        {"X-Amz-Signature", signature},
    });
}

void AmzTimestamps(std::string* amz_date, std::string* date_stamp) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char a[32], d[16];
    std::strftime(a, sizeof(a), "%Y%m%dT%H%M%SZ", &tm);
    std::strftime(d, sizeof(d), "%Y%m%d", &tm);
    *amz_date = a;
    *date_stamp = d;
}

} // namespace XER::Content::S3::Sign
