/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <string>

namespace XER::network::http {

enum class AuthScheme { kNone, kBasic, kDigest, kBearer };

struct HttpAuthCredentials {
    AuthScheme  scheme   = AuthScheme::kNone;
    std::string username;
    std::string password;
    std::string token;   // for Bearer
};

// Build an "Authorization" header value from credentials.
std::string BuildAuthorizationHeader(const HttpAuthCredentials& creds);

// Parse a "WWW-Authenticate" challenge.
AuthScheme ParseAuthChallenge(const std::string& challenge);

}  // namespace XER::network::http
