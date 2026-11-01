/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/ftp/ftp_ftp_stream.h"

#include "content/network/async/async_operation_manager.h"
#include "content/network/logging/network_logger.h"
#include "content/network/socket/platform/socket_platform.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif

#include <sstream>

namespace platform = network::socket::platform;

namespace network::ftp {

FtpStream::~FtpStream() {
    if (ctrl_fd_ >= 0) {
#ifdef _WIN32
        closesocket(ctrl_fd_);
#else
        close(ctrl_fd_);
#endif
    }
}

bool FtpStream::Connect(const std::string& host, uint16_t port, std::string* error) {
    logging::NetworkLogger::Instance().LogInfo("ftp",
        "Connecting to " + host + ":" + std::to_string(port));

    auto& platform = platform::SocketPlatform::GetInstance();

    // Resolve address
    std::vector<platform::AddressInfo> addresses;
    if (!platform.ResolveAddress(host, port, &addresses, error)) {
        return false;
    }

    // Try each address
    for (const auto& addr : addresses) {
        platform::SocketHandle socket;
        if (addr.is_ipv6) {
            socket = platform.CreateTcpSocket(true, error);
        } else {
            socket = platform.CreateTcpSocket(false, error);
        }

        if (!socket.IsValid()) {
            continue;
        }

        if (platform.Connect(socket, addr, error)) {
            ctrl_fd_ = static_cast<int>(socket.Get());
            socket.Detach();
            break;
        }
    }

    if (ctrl_fd_ < 0) {
        if (error && error->empty()) *error = "FTP: connect failed";
        return false;
    }

    // Read welcome banner
    int code; std::string text;
    return ReadReply(&code, &text, error) && code == 220;
}

bool FtpStream::Login(const std::string& user, const std::string& pass, std::string* error) {
    logging::NetworkLogger::Instance().LogInfo("ftp", "Logging in as " + user);

    int code; std::string text;
    if (!SendCommand("USER " + user, error)) return false;
    if (!ReadReply(&code, &text, error)) return false;
    if (code == 230) return true; // no password needed
    if (code != 331) { if (error) *error = "FTP: unexpected reply to USER"; return false; }
    if (!SendCommand("PASS " + pass, error)) return false;
    if (!ReadReply(&code, &text, error)) return false;
    if (code != 230) { if (error) *error = "FTP: login failed: " + text; return false; }
    return true;
}

bool FtpStream::SetBinaryMode(std::string* error) {
    int code; std::string text;
    return SendCommand("TYPE I", error) && ReadReply(&code, &text, error) && code == 200;
}

bool FtpStream::EnterPassive(std::string* data_host, uint16_t* data_port, std::string* error) {
    int code; std::string text;
    if (!SendCommand("PASV", error)) return false;
    if (!ReadReply(&code, &text, error) || code != 227) {
        if (error) *error = "FTP: PASV failed: " + text;
        return false;
    }
    // Parse (h1,h2,h3,h4,p1,p2)
    const size_t lp = text.find('('), rp = text.find(')');
    if (lp == std::string::npos || rp == std::string::npos) {
        if (error) *error = "FTP: bad PASV response";
        return false;
    }
    std::string nums = text.substr(lp+1, rp-lp-1);
    int h1,h2,h3,h4,p1,p2;
    if (std::sscanf(nums.c_str(), "%d,%d,%d,%d,%d,%d",
                    &h1,&h2,&h3,&h4,&p1,&p2) != 6) {
        if (error) *error = "FTP: PASV parse error";
        return false;
    }
    *data_host = std::to_string(h1)+"."+std::to_string(h2)+"."+
                 std::to_string(h3)+"."+std::to_string(h4);
    *data_port  = static_cast<uint16_t>(p1 * 256 + p2);
    return true;
}

bool FtpStream::Retrieve(const std::string& path,
                          std::vector<uint8_t>* out,
                          std::string* error) {
    logging::NetworkLogger::Instance().LogInfo("ftp", "Retrieving " + path);

    std::string dhost; uint16_t dport = 0;
    if (!SetBinaryMode(error) || !EnterPassive(&dhost, &dport, error)) return false;

    // Open data connection using platform abstraction
    auto& platform = platform::SocketPlatform::GetInstance();
    platform::SocketHandle data_socket = platform.CreateTcpSocket(false, error);
    if (!data_socket.IsValid()) {
        return false;
    }

    platform::AddressInfo addr;
    addr.ip_address = dhost;
    addr.port = dport;
    addr.is_ipv6 = false;

    if (!platform.Connect(data_socket, addr, error)) {
        if (error) *error = "FTP: data connection refused";
        return false;
    }

    int dfd = static_cast<int>(data_socket.Get());
    data_socket.Detach();

    if (!SendCommand("RETR " + path, error)) {
#ifdef _WIN32
        closesocket(dfd);
#else
        close(dfd);
#endif
        return false;
    }
    int code; std::string text;
    if (!ReadReply(&code, &text, error) || (code != 125 && code != 150)) {
#ifdef _WIN32
        closesocket(dfd);
#else
        close(dfd);
#endif
        if (error) *error = "FTP: RETR rejected";
        return false;
    }

    uint8_t buf[8192];
    int n;
    platform::SocketHandle data_recv_handle(static_cast<uintptr_t>(dfd));
    while ((n = platform.Recv(data_recv_handle,
                             buf, sizeof(buf), error)) > 0) {
        out->insert(out->end(), buf, buf + n);
    }

#ifdef _WIN32
    closesocket(dfd);
#else
    close(dfd);
#endif
    ReadReply(&code, &text, error); // 226 Transfer complete
    return true;
}

bool FtpStream::List(const std::string& path,
                      std::vector<std::string>* out,
                      std::string* error) {
    logging::NetworkLogger::Instance().LogInfo("ftp", "Listing " + path);

    std::string dhost; uint16_t dport = 0;
    if (!EnterPassive(&dhost, &dport, error)) return false;

    auto& platform = platform::SocketPlatform::GetInstance();
    platform::SocketHandle data_socket = platform.CreateTcpSocket(false, error);
    if (!data_socket.IsValid()) {
        return false;
    }

    platform::AddressInfo addr;
    addr.ip_address = dhost;
    addr.port = dport;
    addr.is_ipv6 = false;

    if (!platform.Connect(data_socket, addr, error)) {
        return false;
    }

    int dfd = static_cast<int>(data_socket.Get());
    data_socket.Detach();

    if (!SendCommand("LIST " + path, error)) {
#ifdef _WIN32
        closesocket(dfd);
#else
        close(dfd);
#endif
        return false;
    }
    int code; std::string text;
    ReadReply(&code, &text, error);

    std::string raw;
    uint8_t buf[4096];
    int n;
    platform::SocketHandle list_recv_handle(static_cast<uintptr_t>(dfd));
    while ((n = platform.Recv(list_recv_handle,
                             buf, sizeof(buf), error)) > 0) {
        raw.append(reinterpret_cast<char*>(buf), n);
    }

#ifdef _WIN32
    closesocket(dfd);
#else
    close(dfd);
#endif
    ReadReply(&code, &text, error);

    std::istringstream ss(raw);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) out->push_back(line);
    }
    return true;
}

void FtpStream::RetrieveAsync(const std::string& path, FtpTransferCallback cb) {
    async::AsyncOperationManager::Instance().Enqueue(
        [this, path, cb = std::move(cb)]() {
            std::vector<uint8_t> data;
            std::string error;
            bool ok = Retrieve(path, &data, &error);
            cb(ok, data, error);
        });
}

void FtpStream::ListAsync(const std::string& path, FtpListCallback cb) {
    async::AsyncOperationManager::Instance().Enqueue(
        [this, path, cb = std::move(cb)]() {
            std::vector<std::string> items;
            std::string error;
            bool ok = List(path, &items, &error);
            cb(ok, items, error);
        });
}


bool FtpStream::Quit() {
    std::string err;
    SendCommand("QUIT", &err);
    if (ctrl_fd_ >= 0) {
#ifdef _WIN32
        closesocket(ctrl_fd_);
#else
        close(ctrl_fd_);
#endif
        ctrl_fd_ = -1;
    }
    return true;
}

bool FtpStream::SendCommand(const std::string& cmd, std::string* error) {
    auto& platform = platform::SocketPlatform::GetInstance();
    platform::SocketHandle socket(static_cast<uintptr_t>(ctrl_fd_));

    const std::string line = cmd + "\r\n";
    int n = platform.Send(socket,
                         reinterpret_cast<const uint8_t*>(line.data()),
                         line.size(),
                         error);
    if (n < 0) { if (error) *error = "FTP: write error"; return false; }
    return true;
}

bool FtpStream::ReadReply(int* code, std::string* text, std::string* error) {
    auto& platform = platform::SocketPlatform::GetInstance();
    platform::SocketHandle socket(static_cast<uintptr_t>(ctrl_fd_));

    std::string line;
    uint8_t c;
    while (platform.Recv(socket, &c, 1, error) == 1) {
        if (c == '\n') break;
        if (c != '\r') line.push_back(c);
    }
    if (line.size() < 3) { if (error) *error = "FTP: short reply"; return false; }
    try { *code = std::stoi(line.substr(0, 3)); }
    catch (...) { if (error) *error = "FTP: bad code"; return false; }
    if (text) *text = line.substr(3);
    return true;
}

FtpResponse ExecuteFtpRequest(const FtpRequest& req) {
    FtpResponse resp;
    FtpStream stream;
    if (!stream.Connect(req.host, req.port, &resp.error)) return resp;
    if (!stream.Login(req.username, req.password, &resp.error)) return resp;
    if (!stream.Retrieve(req.remote_path, &resp.data, &resp.error)) return resp;
    stream.Quit();
    resp.ok = true;
    return resp;
}

}  // namespace network::ftp
