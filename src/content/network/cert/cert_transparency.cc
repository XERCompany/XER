/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/cert/cert_transparency.h"

#include "content/hex/hex.h"

#include <cstring>
#include <iomanip>
#include <sstream>

namespace XER::network::cert {

namespace {

// ---------------------------------------------------------------------------
// RFC 6962 §3.3 — SignedCertificateTimestamp serialisation
// TLS extension SignedCertificateTimestampList (§6.1.2):
//   uint16  sct_list_length
//   For each SCT:
//     uint16  sct_length
//     uint8   version          (0x00)
//     uint8[32] log_id
//     uint64  timestamp        (milliseconds since Unix epoch, big-endian)
//     uint16  extensions_length
//     uint8[] extensions
//     uint8   sig_hash_alg
//     uint8   sig_sign_alg
//     uint16  signature_length
//     uint8[] signature
// ---------------------------------------------------------------------------

static inline uint16_t Read16BE(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
static inline uint64_t Read64BE(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
    return v;
}

}  // namespace

// static
bool CertTransparency::ParseSCTs(const std::vector<uint8_t>& extension_data,
                                  std::vector<SignedCertTimestamp>* scts) {
    scts->clear();
    const uint8_t* p   = extension_data.data();
    const size_t   len = extension_data.size();

    // Outer list length.
    if (len < 2) return false;
    const uint16_t list_len = Read16BE(p);
    if (static_cast<size_t>(list_len) + 2 != len) return false;
    size_t pos = 2;
    const size_t list_end = 2 + list_len;

    while (pos + 2 <= list_end) {
        const uint16_t sct_len = Read16BE(p + pos); pos += 2;
        const size_t sct_start = pos;
        const size_t sct_end   = pos + sct_len;
        if (sct_end > list_end) break;

        // version (1 byte) + log_id (32 bytes) + timestamp (8 bytes) = 41 bytes min
        if (sct_len < 41) { pos = sct_end; continue; }

        const uint8_t version = p[pos]; ++pos;
        if (version != 0x00) { pos = sct_end; continue; }  // only v1

        SignedCertTimestamp sct;
        sct.log_id       = XER::Content::Hex::Encode(p + pos, 32); pos += 32;
        sct.timestamp_ms = Read64BE(p + pos);       pos += 8;

        // Skip extensions.
        if (pos + 2 > sct_end) { pos = sct_end; continue; }
        const uint16_t ext_len = Read16BE(p + pos); pos += 2;
        if (pos + ext_len > sct_end) { pos = sct_end; continue; }
        pos += ext_len;

        // Signature: alg (2) + length (2) + bytes.
        if (pos + 4 > sct_end) { pos = sct_end; continue; }
        pos += 2;  // hash + sign alg
        const uint16_t sig_len = Read16BE(p + pos); pos += 2;
        if (pos + sig_len > sct_end) { pos = sct_end; continue; }
        sct.signature = XER::Content::Hex::Encode(p + pos, sig_len); pos += sig_len;

        scts->push_back(std::move(sct));
        pos = sct_end;
    }

    return !scts->empty();
}

// static
bool CertTransparency::PolicySatisfied(
    const std::vector<SignedCertTimestamp>& scts,
    int min_scts) {
    return static_cast<int>(scts.size()) >= min_scts;
}

}  // namespace XER::network::cert
