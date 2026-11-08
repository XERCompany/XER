/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/dns/dns_record_type.h"

namespace XER::Network::Dns {

std::string DnsRecordTypeToString(DnsRecordType type) {
    switch (type) {
        case DnsRecordType::kA:     return "A";
        case DnsRecordType::kNS:    return "NS";
        case DnsRecordType::kCNAME: return "CNAME";
        case DnsRecordType::kSOA:   return "SOA";
        case DnsRecordType::kPTR:   return "PTR";
        case DnsRecordType::kMX:    return "MX";
        case DnsRecordType::kTXT:   return "TXT";
        case DnsRecordType::kAAAA:  return "AAAA";
        case DnsRecordType::kSRV:   return "SRV";
        case DnsRecordType::kHTTPS: return "HTTPS";
        case DnsRecordType::kAny:   return "ANY";
    }
    return "UNKNOWN";
}

bool DnsRecordTypeFromString(const std::string& name, DnsRecordType* out) {
    if (name == "A")     { *out = DnsRecordType::kA;     return true; }
    if (name == "NS")    { *out = DnsRecordType::kNS;    return true; }
    if (name == "CNAME") { *out = DnsRecordType::kCNAME; return true; }
    if (name == "SOA")   { *out = DnsRecordType::kSOA;   return true; }
    if (name == "PTR")   { *out = DnsRecordType::kPTR;   return true; }
    if (name == "MX")    { *out = DnsRecordType::kMX;    return true; }
    if (name == "TXT")   { *out = DnsRecordType::kTXT;   return true; }
    if (name == "AAAA")  { *out = DnsRecordType::kAAAA;  return true; }
    if (name == "SRV")   { *out = DnsRecordType::kSRV;   return true; }
    if (name == "HTTPS") { *out = DnsRecordType::kHTTPS; return true; }
    if (name == "ANY")   { *out = DnsRecordType::kAny;   return true; }
    return false;
}

}  // namespace XER::Network::Dns
