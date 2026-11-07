/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/websocket/websocket_handshake_stream.h"

#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <openssl/rand.h>
#include <string>
#include <vector>
#include <cstring>

namespace XER::network::websocket {

static std::string Base64Encode(const uint8_t* data, size_t len) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, mem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, data, static_cast<int>(len));
    BIO_flush(b64);
    BUF_MEM* buf;
    BIO_get_mem_ptr(b64, &buf);
    std::string out(buf->data, buf->length);
    BIO_free_all(b64);
    return out;
}

std::string WebSocketHandshakeStream::GenerateSecKey() {
    uint8_t raw[16];
    RAND_bytes(raw, 16);
    return Base64Encode(raw, 16);
}

std::string WebSocketHandshakeStream::ExpectedAcceptKey(
    const std::string& sec_key) {
    static const char kGUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    const std::string cat = sec_key + kGUID;
    uint8_t digest[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const uint8_t*>(cat.data()), cat.size(), digest);
    return Base64Encode(digest, SHA_DIGEST_LENGTH);
}

bool WebSocketHandshakeStream::DoHandshake(
    transport::TransportClientSocket* sock,
    const std::string& host,
    const std::string& path,
    const std::string& origin,
    std::string* error) {
    const std::string sec_key = GenerateSecKey();
    const std::string req =
        "GET " + path + " HTTP/1.1\r\n"
        "Host: " + host + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: " + sec_key + "\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Origin: " + origin + "\r\n"
        "\r\n";

    if (!sock->Write(reinterpret_cast<const uint8_t*>(req.data()),
                     req.size(), error))
        return false;

    // Read response headers
    std::string resp;
    uint8_t buf[1];
    std::string rd_err;
    while (resp.size() < 65536) {
        const int n = sock->Read(buf, 1, &rd_err);
        if (n <= 0) break;
        resp.push_back(static_cast<char>(buf[0]));
        if (resp.size() >= 4 &&
            resp.substr(resp.size()-4) == "\r\n\r\n") break;
    }

    if (resp.find("101") == std::string::npos) {
        if (error) *error = "WebSocket upgrade rejected: " + resp.substr(0, 64);
        return false;
    }
    const std::string expected = ExpectedAcceptKey(sec_key);
    if (resp.find(expected) == std::string::npos) {
        if (error) *error = "Sec-WebSocket-Accept mismatch";
        return false;
    }
    return true;
}

}  // namespace XER::network::websocket
