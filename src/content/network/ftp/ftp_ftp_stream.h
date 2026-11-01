/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "content/network/ftp/ftp_ftp_request.h"
#include "content/network/socket/socket_stream.h"

namespace network::ftp {

// FTP callback types
using FtpTransferCallback = std::function<void(bool, const std::vector<uint8_t>&, const std::string&)>;
using FtpListCallback = std::function<void(bool, const std::vector<std::string>&, const std::string&)>;

// Minimal FTP client stream (control channel + passive data channel).
class FtpStream {
public:
    FtpStream() = default;
    ~FtpStream();

    // Synchronous operations
    bool Connect(const std::string& host, uint16_t port, std::string* error);
    bool Login(const std::string& user, const std::string& pass, std::string* error);
    bool SetBinaryMode(std::string* error);
    bool EnterPassive(std::string* data_host, uint16_t* data_port, std::string* error);
    bool Retrieve(const std::string& path, std::vector<uint8_t>* out, std::string* error);
    bool List(const std::string& path, std::vector<std::string>* out, std::string* error);
    bool Quit();

    // Asynchronous operations
    void RetrieveAsync(const std::string& path, FtpTransferCallback cb);
    void ListAsync(const std::string& path, FtpListCallback cb);

private:
    bool SendCommand(const std::string& cmd, std::string* error);
    bool ReadReply(int* code, std::string* text, std::string* error);

    int ctrl_fd_ = -1;
};

// Execute a full FTP download using FtpStream.
FtpResponse ExecuteFtpRequest(const FtpRequest& req);

}  // namespace network::ftp
