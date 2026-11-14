/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/dns/dns_client.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstring>
#include <random>
#include <sstream>
#include <stdexcept>

namespace XER::Network::Dns {

DnsClient::DnsClient(const DnsConfig& config) : config_(config) {}

bool DnsClient::Query(const std::string& name,
                      DnsRecordType type,
                      std::vector<DnsRecord>* records,
                      std::string* error) {
    if (config_.nameservers.empty()) {
        if (error) *error = "no nameservers configured";
        return false;
    }

    std::mt19937 rng(std::random_device{}());
    const uint16_t txid = static_cast<uint16_t>(rng());
    const auto query = BuildQuery(name, type, txid);

    const std::string& ns = config_.nameservers[0];

    int sock = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, 0));
    if (sock < 0) {
        if (error) *error = "socket() failed";
        return false;
    }

#ifdef _WIN32
    DWORD tv_ms = static_cast<DWORD>(config_.timeout_ms);
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&tv_ms), sizeof(tv_ms));
#else
    struct timeval tv{};
    tv.tv_sec  = static_cast<time_t>(config_.timeout_ms / 1000);
    tv.tv_usec = static_cast<suseconds_t>((config_.timeout_ms % 1000) * 1000);
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(53);
    ::inet_pton(AF_INET, ns.c_str(), &addr.sin_addr);

    ::sendto(sock, reinterpret_cast<const char*>(query.data()),
             static_cast<int>(query.size()), 0,
             reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    std::vector<uint8_t> buf(512);
#ifdef _WIN32
    const int n = ::recv(sock, reinterpret_cast<char*>(buf.data()),
                         static_cast<int>(buf.size()), 0);
    ::closesocket(sock);
#else
    const ssize_t n = ::recv(sock, buf.data(), buf.size(), 0);
    ::close(sock);
#endif

    if (n <= 0) {
        if (error) *error = "DNS query timed out or failed";
        return false;
    }
    buf.resize(static_cast<size_t>(n));
    return ParseResponse(buf, records, error);
}

// static
std::vector<uint8_t> DnsClient::BuildQuery(const std::string& name,
                                            DnsRecordType type,
                                            uint16_t txid) {
    std::vector<uint8_t> pkt;
    // Header
    pkt.push_back(txid >> 8); pkt.push_back(txid & 0xff);
    pkt.push_back(0x01); pkt.push_back(0x00);  // flags: RD
    pkt.push_back(0x00); pkt.push_back(0x01);  // QDCOUNT=1
    pkt.push_back(0x00); pkt.push_back(0x00);  // ANCOUNT
    pkt.push_back(0x00); pkt.push_back(0x00);  // NSCOUNT
    pkt.push_back(0x00); pkt.push_back(0x00);  // ARCOUNT
    // QNAME
    std::istringstream ss(name);
    std::string label;
    while (std::getline(ss, label, '.')) {
        pkt.push_back(static_cast<uint8_t>(label.size()));
        for (char c : label) pkt.push_back(static_cast<uint8_t>(c));
    }
    pkt.push_back(0x00);  // root
    const uint16_t qtype = static_cast<uint16_t>(type);
    pkt.push_back(qtype >> 8); pkt.push_back(qtype & 0xff);
    pkt.push_back(0x00); pkt.push_back(0x01);  // QCLASS IN
    return pkt;
}

// static
bool DnsClient::ParseResponse(const std::vector<uint8_t>& buf,
                               std::vector<DnsRecord>* records,
                               std::string* error) {
    if (buf.size() < 12) {
        if (error) *error = "response too short";
        return false;
    }
    const uint16_t ancount = (buf[6] << 8) | buf[7];
    // Skip question section starting at offset 12
    size_t pos = 12;
    // Skip QNAME
    while (pos < buf.size() && buf[pos] != 0) {
        if ((buf[pos] & 0xc0) == 0xc0) { pos += 2; break; }
        pos += buf[pos] + 1;
    }
    if (pos < buf.size() && buf[pos] == 0) pos++;
    pos += 4;  // skip QTYPE + QCLASS

    for (int i = 0; i < ancount && pos + 10 < buf.size(); ++i) {
        // Skip NAME (may be pointer)
        if ((buf[pos] & 0xc0) == 0xc0) pos += 2;
        else { while (pos < buf.size() && buf[pos]) pos += buf[pos] + 1; pos++; }

        if (pos + 10 > buf.size()) break;
        const uint16_t rtype  = (buf[pos] << 8)   | buf[pos + 1];
        const uint32_t ttl    = (buf[pos + 4] << 24) | (buf[pos + 5] << 16) |
                                 (buf[pos + 6] << 8)  |  buf[pos + 7];
        const uint16_t rdlen  = (buf[pos + 8] << 8) | buf[pos + 9];
        pos += 10;

        DnsRecord rec{};
        rec.type    = static_cast<DnsRecordType>(rtype);
        rec.ttl_sec = ttl;

        if (rtype == 1 && rdlen == 4 && pos + 4 <= buf.size()) {
            char ip[INET_ADDRSTRLEN];
            ::inet_ntop(AF_INET, &buf[pos], ip, sizeof(ip));
            rec.value = ip;
        } else if (rtype == 28 && rdlen == 16 && pos + 16 <= buf.size()) {
            char ip[INET6_ADDRSTRLEN];
            ::inet_ntop(AF_INET6, &buf[pos], ip, sizeof(ip));
            rec.value = ip;
        }
        records->push_back(rec);
        pos += rdlen;
    }
    return true;
}

}  // namespace XER::Network::Dns
