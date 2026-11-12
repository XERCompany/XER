/**
 * This file is part of XER, Device open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/device/cast/mira/mira_cast_session.h"

#include <sstream>
#include <string>

#include "content/network/socket/tcp_client_socket.h"

namespace device::cast::mira {

namespace {
constexpr char kRtspUri[] = "rtsp://localhost/wfd1.0";

// Build an RTSP request string.
static std::string BuildRtsp(const std::string& method, const std::string& uri,
                               int cseq, const std::string& extra) {
    std::ostringstream ss;
    ss << method << " " << uri << " RTSP/1.0\r\n"
       << "CSeq: " << cseq << "\r\n"
       << "Require: org.wfa.wfd1.0\r\n"
       << extra
       << "\r\n";
    return ss.str();
}

// Parse the RTSP status code from a response like "RTSP/1.0 200 OK\r\n...".
static int ParseRtspStatus(const std::string& resp) {
    const auto pos = resp.find(' ');
    if (pos == std::string::npos) return -1;
    try { return std::stoi(resp.substr(pos + 1)); } catch (...) { return -1; }
}
}  // namespace

// ──────────────────────────────────────────────────────────────────────────────

MiraCastSession& MiraCastSession::Instance() {
    static MiraCastSession inst;
    return inst;
}

std::vector<MiraCastDevice> MiraCastSession::DiscoverSinks(int timeout_ms) const {
    // Wi-Fi Direct P2P discovery requires wpa_supplicant D-Bus/CLI integration.
    // Return empty; caller fills sinks via platform layer.
    (void)timeout_ms;
    return {};
}

int MiraCastSession::SendRtspRequest(const std::string& method,
                                      const std::string& uri,
                                      const std::string& extra_headers) {
    if (!tcp_socket_ || !tcp_socket_->IsConnected()) return -1;
    const std::string req = BuildRtsp(method, uri, cseq_++, extra_headers);
    std::string err;
    if (tcp_socket_->Write(
            reinterpret_cast<const uint8_t*>(req.data()), req.size(), &err) < 0)
        return -1;

    // Read response (up to 4 KB).
    std::string resp;
    uint8_t buf[4096];
    const int n = tcp_socket_->Read(buf, sizeof(buf), &err);
    if (n <= 0) return -1;
    resp.assign(reinterpret_cast<char*>(buf), static_cast<size_t>(n));
    return ParseRtspStatus(resp);
}

bool MiraCastSession::Connect(const MiraCastDevice& sink, StateCallback cb) {
    if (state_ == MiraCastState::kStreaming) return false;
    callback_ = std::move(cb);

    // Use the project TcpClientSocket for the WFD RTSP control channel.
    tcp_socket_ = std::make_unique<XER::Network::Socket::TcpClientSocket>();

    state_ = MiraCastState::kConnecting;
    if (callback_) callback_(state_, {});

    std::string err;
    if (!tcp_socket_->Connect(sink.device_address, sink.rtsp_port, &err)) {
        state_ = MiraCastState::kError;
        if (callback_) callback_(state_, err);
        return false;
    }

    // WFD capability negotiation: OPTIONS → GET_PARAMETER → SET_PARAMETER.
    state_ = MiraCastState::kNegotiating;
    if (callback_) callback_(state_, {});

    const int options_status = SendRtspRequest("OPTIONS", "*");
    if (options_status < 0 || options_status >= 400) {
        state_ = MiraCastState::kError;
        if (callback_) callback_(state_, "RTSP OPTIONS failed");
        return false;
    }

    // GET_PARAMETER: query WFD presentation URL and audio/video formats.
    const std::string gp_body =
        "Content-Type: text/parameters\r\n"
        "Content-Length: 26\r\n\r\n"
        "wfd_video_formats\r\n"
        "wfd_audio_codecs\r\n";
    SendRtspRequest("GET_PARAMETER", kRtspUri, gp_body);

    state_ = MiraCastState::kIdle;
    if (callback_) callback_(state_, {});
    return true;
}

bool MiraCastSession::StartStream() {
    if (!tcp_socket_ || !tcp_socket_->IsConnected()) return false;

    // SET_PARAMETER with wfd_presentation_URL triggers streaming start.
    const std::string sp_body =
        "Content-Type: text/parameters\r\n"
        "Content-Length: 38\r\n\r\n"
        "wfd_presentation_URL: rtsp://0.0.0.0/wfd1.0 none\r\n";
    const int status = SendRtspRequest("SET_PARAMETER", kRtspUri, sp_body);
    if (status < 0 || status >= 400) return false;

    state_ = MiraCastState::kStreaming;
    if (callback_) callback_(state_, {});
    return true;
}

bool MiraCastSession::PauseStream() {
    if (!tcp_socket_ || !tcp_socket_->IsConnected()) return false;
    const int status = SendRtspRequest("PAUSE", kRtspUri);
    if (status < 0 || status >= 400) return false;
    state_ = MiraCastState::kPaused;
    if (callback_) callback_(state_, {});
    return true;
}

bool MiraCastSession::StopStream() {
    Disconnect();
    return true;
}

void MiraCastSession::Disconnect() {
    if (tcp_socket_) {
        tcp_socket_->Disconnect();
        tcp_socket_.reset();
    }
    state_ = MiraCastState::kDisconnected;
    if (callback_) callback_(state_, {});
}

}  // namespace device::cast::mira

