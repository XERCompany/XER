/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/http/http_auth.h"
#include "content/network/http/http_util.h"
#include "helper/string_utils.h"

#include <vector>
#include <cstring>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

namespace XER::network::http {

static std::string Base64Encode(const std::string& in) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, mem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, in.data(), static_cast<int>(in.size()));
    BIO_flush(b64);
    BUF_MEM* buf;
    BIO_get_mem_ptr(b64, &buf);
    std::string out(buf->data, buf->length);
    BIO_free_all(b64);
    return out;
}

std::string BuildAuthorizationHeader(const HttpAuthCredentials& creds) {
    switch (creds.scheme) {
    case AuthScheme::kBasic:
        return "Basic " + Base64Encode(creds.username + ":" + creds.password);
    case AuthScheme::kBearer:
        return "Bearer " + creds.token;
    default:
        return {};
    }
}

AuthScheme ParseAuthChallenge(const std::string& challenge) {
    const std::string lc = XER::Engine::Helper::String::ToLowerAscii(challenge);
    if (lc.find("bearer") == 0)  return AuthScheme::kBearer;
    if (lc.find("digest") == 0)  return AuthScheme::kDigest;
    if (lc.find("basic")  == 0)  return AuthScheme::kBasic;
    return AuthScheme::kNone;
}

}  // namespace XER::network::http
