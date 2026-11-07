#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace XER::AsyncIO::IO::Hardware {

struct NetworkAdapter {
    std::string name;            // OS interface name ("Ethernet", "eth0", "Wi-Fi")
    std::string friendly_name;   // Human-readable name (Windows friendly name)
    std::string description;     // Adapter description (Windows: adapter desc)
    std::string mac_address;     // "AA:BB:CC:DD:EE:FF", "" if none
    std::string ipv4_address;    // Primary IPv4 ("" if none)
    std::vector<std::string> ipv4_addresses;
    std::vector<std::string> ipv6_addresses;
    bool is_up        = false;
    bool is_loopback  = false;
    bool is_wireless  = false;
    bool is_virtual   = false;    // VPN/virtual adapters
    uint64_t bytes_received  = 0;
    uint64_t bytes_sent      = 0;
    uint64_t packets_received = 0;
    uint64_t packets_sent     = 0;
    uint64_t link_speed_bps   = 0;  // 0 if unknown
};

class NetworkInfoProvider {
public:
    NetworkInfoProvider();
    ~NetworkInfoProvider();

    bool Initialize();
    bool IsInitialized() const { return initialized_; }

    std::vector<NetworkAdapter> GetAdapters() const;

    // Aggregate counters across real (non-loopback) adapters.
    uint64_t GetTotalBytesReceived() const;
    uint64_t GetTotalBytesSent() const;

private:
    bool initialized_ = false;
};

}  // namespace XER::AsyncIO::IO::Hardware
