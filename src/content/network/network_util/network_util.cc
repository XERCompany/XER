/**
 * This file is part of XER, Network open source project.
 *
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/network_util/network_util.h"
#include "helper/string_utils.h"

#include <algorithm>
#include <regex>
#include <sstream>

namespace XER::Network {

std::string NetErrorToString(NetError error) {
    switch (error) {
        case NetError::kOk:                    return "OK";
        case NetError::kPending:               return "ERR_IO_PENDING";
        case NetError::kFailed:                return "ERR_FAILED";
        case NetError::kAborted:               return "ERR_ABORTED";
        case NetError::kInvalidArgument:       return "ERR_INVALID_ARGUMENT";
        case NetError::kTimedOut:              return "ERR_TIMED_OUT";
        case NetError::kConnectionClosed:      return "ERR_CONNECTION_CLOSED";
        case NetError::kConnectionReset:       return "ERR_CONNECTION_RESET";
        case NetError::kConnectionRefused:     return "ERR_CONNECTION_REFUSED";
        case NetError::kConnectionAborted:     return "ERR_CONNECTION_ABORTED";
        case NetError::kConnectionFailed:      return "ERR_CONNECTION_FAILED";
        case NetError::kNameNotResolved:       return "ERR_NAME_NOT_RESOLVED";
        case NetError::kAddressUnreachable:    return "ERR_ADDRESS_UNREACHABLE";
        case NetError::kSslProtocolError:      return "ERR_SSL_PROTOCOL_ERROR";
        case NetError::kCertCommonNameInvalid: return "ERR_CERT_COMMON_NAME_INVALID";
        case NetError::kCertDateInvalid:       return "ERR_CERT_DATE_INVALID";
        case NetError::kCertAuthorityInvalid:  return "ERR_CERT_AUTHORITY_INVALID";
        case NetError::kCertRevoked:           return "ERR_CERT_REVOKED";
        case NetError::kCertInvalid:           return "ERR_CERT_INVALID";
        case NetError::kProxyConnectionFailed: return "ERR_PROXY_CONNECTION_FAILED";
        case NetError::kSocksConnectionFailed: return "ERR_SOCKS_CONNECTION_FAILED";
        case NetError::kDnsMalformedResponse:  return "ERR_DNS_MALFORMED_RESPONSE";
        default:                               return "ERR_UNKNOWN";
    }
}

bool IsValidPort(int port) {
    return port >= 1 && port <= 65535;
}

bool IsIPv6Literal(const std::string& host) {
    return host.size() >= 2 && host.front() == '[' && host.back() == ']';
}

bool IsIPLiteral(const std::string& host) {
    if (IsIPv6Literal(host)) return true;
    static const std::regex kIPv4(R"(^(\d{1,3}\.){3}\d{1,3}$)");
    return std::regex_match(host, kIPv4);
}

std::string UnwrapIPv6(const std::string& host) {
    if (IsIPv6Literal(host))
        return host.substr(1, host.size() - 2);
    return host;
}

std::string CanonicaliseHost(const std::string& host) {
    std::string out = XER::Helper::String::ToLowerAscii(host);
    // Strip trailing dot.
    if (!out.empty() && out.back() == '.')
        out.pop_back();
    return out;
}

std::string MakeHostPortPair(const std::string& host, std::uint16_t port) {
    std::ostringstream oss;
    if (host.find(':') != std::string::npos && !IsIPv6Literal(host))
        oss << '[' << host << ']';
    else
        oss << host;
    oss << ':' << port;
    return oss.str();
}

bool SplitHostPort(const std::string& host_port_str,
                   std::string* host, uint16_t* port) {
    if (!host || !port) return false;
    
    std::string str = host_port_str;
    
    // Handle IPv6 literals: "[::1]:8080"
    if (str.front() == '[') {
        size_t close_pos = str.find(']');
        if (close_pos == std::string::npos) return false;
        
        *host = str.substr(1, close_pos - 1);  // Extract IPv6 address without brackets
        
        // Check for ":port" after ]
        if (close_pos + 1 >= str.size()) {
            *port = 0;  // No port specified
            return true;
        }
        if (str[close_pos + 1] != ':') return false;
        
        std::string port_str = str.substr(close_pos + 2);
        try {
            int p = std::stoi(port_str);
            if (!IsValidPort(p)) return false;
            *port = static_cast<uint16_t>(p);
        } catch (...) {
            return false;
        }
        return true;
    }
    
    // Handle regular "host:port"
    size_t colon_pos = str.rfind(':');
    if (colon_pos == std::string::npos) {
        *host = str;
        *port = 0;
        return true;
    }
    
    *host = str.substr(0, colon_pos);
    std::string port_str = str.substr(colon_pos + 1);
    
    try {
        int p = std::stoi(port_str);
        if (!IsValidPort(p)) return false;
        *port = static_cast<uint16_t>(p);
    } catch (...) {
        return false;
    }
    return true;
}

std::string CanonicalizeScheme(const std::string& scheme) {
    std::string out = XER::Helper::String::ToLowerAscii(scheme);
    return out;
}

bool IsSecureScheme(const std::string& scheme) {
    std::string canonical = CanonicalizeScheme(scheme);
    return canonical == "https" || canonical == "wss" || canonical == "ftps";
}

uint16_t DefaultPortForScheme(const std::string& scheme) {
    std::string canonical = CanonicalizeScheme(scheme);
    if (canonical == "http")      return 80;
    if (canonical == "https")     return 443;
    if (canonical == "ftp")       return 21;
    if (canonical == "ftps")      return 990;
    if (canonical == "ws")        return 80;
    if (canonical == "wss")       return 443;
    if (canonical == "quic")      return 443;
    if (canonical == "spdy")      return 443;
    return 0;  // Unknown scheme
}

}  // namespace XER::Network