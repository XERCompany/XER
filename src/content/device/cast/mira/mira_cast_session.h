/**
 * This file is part of XER, Device open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "content/device/cast/mira/mira_cast_device.h"

// Uses the project TCP socket for the WFD RTSP control channel.
namespace XER::Network::Socket { class TcpClientSocket; }

namespace device::cast::mira {

using StateCallback = std::function<void(MiraCastState, const std::string& error)>;

// MiraCastSession implements the Wi-Fi Display (Miracast) sink/source protocol.
// WFD uses Wi-Fi Direct P2P for transport and RTSP on port 7236 for control.
// Uses XER::Network::Socket::TcpClientSocket from the project network stack.
class MiraCastSession {
public:
    static MiraCastSession& Instance();

    // Discover nearby Miracast sinks (Wi-Fi Direct P2P devices with WFD IE).
    std::vector<MiraCastDevice> DiscoverSinks(int timeout_ms = 5000) const;

    // Connect to the given sink and negotiate WFD capabilities via RTSP.
    bool Connect(const MiraCastDevice& sink, StateCallback callback);

    bool StartStream();
    bool PauseStream();
    bool StopStream();

    MiraCastState GetState() const { return state_; }
    void Disconnect();

private:
    MiraCastSession() = default;
    std::unique_ptr<XER::Network::Socket::TcpClientSocket> tcp_socket_;
    int cseq_     = 1;    // RTSP CSeq counter
    MiraCastState state_    = MiraCastState::kDisconnected;
    StateCallback callback_;

    // Send an RTSP request and return the response status code, or -1 on error.
    int SendRtspRequest(const std::string& method,
                        const std::string& uri,
                        const std::string& extra_headers = {});
};

}  // namespace device::cast::mira
