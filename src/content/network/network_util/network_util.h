/**
 * This file is part of XER, Network open source project.
 *
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <string>

namespace XER::Network {

// ── Error codes ─────────────────────────────────────────────────────────────
// Negative = error, 0 = OK, -1 = async pending.
enum class NetError : int {
    kOk                       =    0,
    kPending                  =   -1,
    kFailed                   =   -2,
    kAborted                  =   -3,
    kInvalidArgument          =   -4,
    kTimedOut                 =   -7,
    kConnectionClosed         =  -100,
    kConnectionReset          =  -101,
    kConnectionRefused        =  -102,
    kConnectionAborted        =  -103,
    kConnectionFailed         =  -104,
    kNameNotResolved          =  -105,
    kAddressUnreachable       =  -109,
    kSslProtocolError         =  -107,
    kCertCommonNameInvalid    =  -200,
    kCertDateInvalid          =  -201,
    kCertAuthorityInvalid     =  -202,
    kCertRevoked              =  -203,
    kCertInvalid              =  -204,
    kProxyConnectionFailed    =  -130,
    kSocksConnectionFailed    =  -133,
    kDnsServerNotFound        =  -109,
    kDnsMalformedResponse     =  -113,
};

// Returns human-readable string for a NetError code.
std::string NetErrorToString(NetError error);

// Returns true when |port| is in the valid range [1, 65535].
bool IsValidPort(int port);

// Returns true if |host| is an IPv4 or IPv6 address literal.
bool IsIPLiteral(const std::string& host);

// Returns true if |host| is a bracketed IPv6 literal, e.g. "[::1]".
bool IsIPv6Literal(const std::string& host);

// Strips surrounding brackets from IPv6 literals.  No-op otherwise.
std::string UnwrapIPv6(const std::string& host);

// Canonicalises |host| to lowercase ASCII, strips trailing dot.
std::string CanonicaliseHost(const std::string& host);

// Builds a host:port string, wrapping IPv6 literals in brackets.
std::string MakeHostPortPair(const std::string& host, std::uint16_t port);

// Splits a "host:port" string into separate components.
// Returns false if port is not a valid number.
// Handles IPv6 literals by unwrapping brackets (e.g., "[::1]:8080").
bool SplitHostPort(const std::string& host_port_str,
                   std::string* host, uint16_t* port);

// Canonicalizes a URL scheme (lowercase, standard forms).
// @param scheme   Input scheme (e.g., "HTTP", "HTTPS")
// @return         Canonical scheme ("http", "https", etc.)
std::string CanonicalizeScheme(const std::string& scheme);

// Checks if a scheme requires secure connection (TLS/SSL).
// Secure schemes: https, wss (WebSocket Secure), ftps, etc.
// @param scheme   Canonicalized scheme
// @return         True if scheme is secure
bool IsSecureScheme(const std::string& scheme);

// Gets the default port for a URL scheme.
// @param scheme   Canonicalized scheme (e.g., "http", "https")
// @return         Default port number, or 0 if scheme is unknown
uint16_t DefaultPortForScheme(const std::string& scheme);

}  // namespace XER::Network