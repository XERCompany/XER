/**
 * This file is part of XER, Device open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/device/bluetooth/bluetooth_gatt.h"

#include <cstdio>
#include <sstream>

// Project async thread pool — GATT discovery runs in the background.
#include "async_io/io_thread_pool.h"

namespace device::bluetooth {

BluetoothGattClient& BluetoothGattClient::Instance() {
    static BluetoothGattClient inst;
    return inst;
}

// ──────────────────────────────────────────────────────────────────────────────
// Internal helpers

std::string BluetoothGattClient::RunGatttool(const std::string& address,
                                               const std::string& args) const {
#if !defined(__linux__)
    // gatttool/bluetoothctl are BlueZ (Linux) tools; no GATT backend on
    // this platform yet.
    (void)address;
    (void)args;
    return {};
#else
    // gatttool is part of bluez-utils; fall back to bluetoothctl if missing.
    const std::string cmd =
        "gatttool -b " + address + " " + args + " 2>/dev/null";
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return {};
    std::string out;
    char buf[512];
    while (fgets(buf, sizeof(buf), fp)) out += buf;
    pclose(fp);
    return out;
#endif
}

namespace {
// Parse "gatttool --characteristics" output lines like:
// handle = 0x0025, char properties = 0x02, char value handle = 0x0026, uuid = 0000180a-...
static GattCharacteristic ParseCharLine(const std::string& line) {
    GattCharacteristic ch;
    auto extract = [&](const std::string& key) -> std::string {
        const auto p = line.find(key);
        if (p == std::string::npos) return {};
        const auto start = line.find('=', p) + 2;
        const auto end   = line.find(',', start);
        return line.substr(start, end == std::string::npos ? std::string::npos : end - start);
    };
    ch.handle_hex = extract("handle");
    const std::string props_str = extract("char properties");
    uint8_t props = 0;
    if (!props_str.empty()) {
        try { props = static_cast<uint8_t>(std::stoul(props_str, nullptr, 16)); } catch (...) {}
    }
    ch.readable   = (props & 0x02) != 0;
    ch.writable   = (props & 0x08) != 0 || (props & 0x04) != 0;
    ch.notifiable = (props & 0x10) != 0;
    ch.uuid = extract("uuid");
    return ch;
}
}  // namespace

// ──────────────────────────────────────────────────────────────────────────────

void BluetoothGattClient::DiscoverServicesAsync(const std::string& address,
                                                  GattDiscoveryCallback callback) {
    // Offload to the shared IO thread pool so discovery does not block.
    XER::AsyncIO::IO::Async::IOThreadPool::GetSharedInstance().Enqueue(
        [this, address, cb = std::move(callback)]() mutable {
            std::vector<GattService> services;
            std::string err;

            // First get primary services.
            const std::string svc_out = RunGatttool(address, "--primary");
            if (svc_out.empty()) {
                err = "gatttool primary services query failed";
                if (cb) cb(address, services, err);
                return;
            }

            // Parse each service line: "attr handle = 0x0001, end grp handle = 0x0009 uuid: 0000180a-..."
            std::istringstream sstream(svc_out);
            std::string sline;
            while (std::getline(sstream, sline)) {
                if (sline.find("attr handle") == std::string::npos) continue;
                GattService svc;
                const auto upos = sline.find("uuid:");
                if (upos != std::string::npos) svc.uuid = sline.substr(upos + 6);
                // Trim trailing whitespace.
                while (!svc.uuid.empty() && (svc.uuid.back() == ' ' || svc.uuid.back() == '\n'))
                    svc.uuid.pop_back();

                // Query characteristics for this service.
                const std::string char_out = RunGatttool(
                    address, "--characteristics --uuid=" + svc.uuid);
                std::istringstream cstream(char_out);
                std::string cline;
                while (std::getline(cstream, cline)) {
                    if (cline.find("handle") == std::string::npos) continue;
                    svc.characteristics.push_back(ParseCharLine(cline));
                }
                services.push_back(std::move(svc));
            }

            if (cb) cb(address, services, {});
        });
}

bool BluetoothGattClient::ReadCharacteristic(const std::string& address,
                                               const std::string& char_uuid,
                                               std::vector<uint8_t>* value_out,
                                               std::string* error) {
    const std::string out = RunGatttool(
        address, "--char-read --uuid=" + char_uuid);
    // Expected format: "Characteristic value/descriptor: de ad be ef"
    const auto p = out.find(':');
    if (p == std::string::npos) {
        if (error) *error = "Read failed: " + out;
        return false;
    }
    std::istringstream ss(out.substr(p + 2));
    std::string byte_str;
    while (ss >> byte_str) {
        try {
            value_out->push_back(
                static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16)));
        } catch (...) {}
    }
    return true;
}

bool BluetoothGattClient::WriteCharacteristic(const std::string& address,
                                                const std::string& char_uuid,
                                                const std::vector<uint8_t>& data,
                                                std::string* error) {
    // Build hex string for gatttool --char-write-req.
    std::ostringstream hex;
    for (uint8_t b : data) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02x", b);
        hex << buf;
    }
    const std::string out = RunGatttool(
        address,
        "--char-write-req --uuid=" + char_uuid + " --value=" + hex.str());
    const bool ok = out.find("successfully") != std::string::npos;
    if (!ok && error) *error = "Write failed: " + out;
    return ok;
}

bool BluetoothGattClient::Subscribe(const std::string& address,
                                     const std::string& char_uuid,
                                     GattNotifyCallback callback) {
#if !defined(__linux__)
    // No GATT notification backend on this platform yet.
    (void)address;
    (void)char_uuid;
    (void)callback;
    return false;
#else
    // Start a background notification listener via gatttool --listen.
    XER::AsyncIO::IO::Async::IOThreadPool::GetSharedInstance().Enqueue(
        [this, address, char_uuid, cb = std::move(callback)]() mutable {
            const std::string cmd =
                "gatttool -b " + address +
                " --char-read --listen --uuid=" + char_uuid + " 2>/dev/null";
            FILE* fp = popen(cmd.c_str(), "r");
            if (!fp) return;
            char buf[512];
            while (fgets(buf, sizeof(buf), fp)) {
                const std::string line(buf);
                const auto p = line.find(':');
                if (p == std::string::npos) continue;
                std::istringstream ss(line.substr(p + 2));
                std::vector<uint8_t> val;
                std::string byte_str;
                while (ss >> byte_str) {
                    try {
                        val.push_back(
                            static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16)));
                    } catch (...) {}
                }
                if (cb && !val.empty()) cb(char_uuid, val);
            }
            pclose(fp);
        });
    return true;
#endif
}

void BluetoothGattClient::Unsubscribe(const std::string& /*address*/,
                                       const std::string& /*char_uuid*/) {
    // Killing the background popen process would require tracking the PID.
    // In a full implementation this would store the FILE* and pclose() it here.
}

}  // namespace device::bluetooth
