/**
 * This file is part of XER, Device open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "content/device/wifi/wifi_network.h"

// Uses project's async thread pool and UDP socket.
namespace IO::AsyncIO  { class IOThreadPool; }
namespace XER::Network::Dns { class HostResolver; }

namespace device::wifi {

enum class WifiConnectionState {
    kDisconnected,
    kAuthenticating,
    kAssociating,
    kConnected,
    kFailed,
};

using ConnectionStateCallback =
    std::function<void(WifiConnectionState, const std::string& error)>;

// WifiConnectionManager wraps wpa_supplicant (via wpa_cli / ctrl socket) to
// connect/disconnect Wi-Fi networks and monitors association state.
// Background scans run on XER::AsyncIO::IO::Async::IOThreadPool.
class WifiConnectionManager {
public:
    static WifiConnectionManager& Instance();

    // Connect to a WPA2/WPA3 network.
    // Spawns wpa_cli in the background (no bluez/netlink dependency).
    bool Connect(const std::string& ssid,
                 const std::string& passphrase,
                 const std::string& interface = "wlan0",
                 ConnectionStateCallback callback = {});

    // Disconnect the current association on |interface|.
    bool Disconnect(const std::string& interface = "wlan0");

    // Returns current state for |interface|.
    WifiConnectionState GetState(const std::string& interface = "wlan0") const;

    // Trigger a background scan and invoke |cb| when done.
    // Uses IOThreadPool to avoid blocking the caller.
    void ScanAsync(const std::string& interface,
                   std::function<void(std::vector<WifiNetwork>)> cb);

    // Resolve a hostname using the project HostResolver; used to verify
    // connectivity after association.
    bool CheckConnectivity(const std::string& host = "connectivitycheck.gstatic.com") const;

    // Returns network stats from /proc/net/dev for |interface|.
    struct NetStats {
        uint64_t rx_bytes = 0, tx_bytes = 0;
        uint64_t rx_packets = 0, tx_packets = 0;
        uint64_t rx_errors = 0, tx_errors = 0;
    };
    NetStats GetNetStats(const std::string& interface) const;

private:
    WifiConnectionManager() = default;
};

}  // namespace device::wifi
