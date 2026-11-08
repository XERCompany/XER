/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/spdy/spdy_stream.h"

namespace XER::Network::Spdy {

void SpdyStream::OnDataReceived(const uint8_t* data, size_t len, bool fin) {
    fin_ = fin;
    if (on_data_) on_data_(data, len, fin);
}

void SpdyStream::Reset(SpdyRstStreamStatus /*status*/) {
    fin_ = true;
}

}  // namespace XER::Network::Spdy
