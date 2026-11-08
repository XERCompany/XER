/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/quic/quic_protocol.h"

namespace XER::Network::Quic {

std::string QuicErrorCodeToString(QuicErrorCode code) {
    switch (code) {
        case QuicErrorCode::kNoError:            return "NO_ERROR";
        case QuicErrorCode::kInternalError:      return "INTERNAL_ERROR";
        case QuicErrorCode::kStreamDataAfterFin: return "STREAM_DATA_AFTER_FIN";
        case QuicErrorCode::kConnectionTimeout:  return "CONNECTION_TIMEOUT";
        case QuicErrorCode::kCryptoError:        return "CRYPTO_ERROR";
        default: return "UNKNOWN";
    }
}

}  // namespace XER::Network::Quic
