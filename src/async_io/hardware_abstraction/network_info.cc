#include "network_info.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cctype>

#if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <iphlpapi.h>
    #pragma comment(lib, "iphlpapi.lib")
    #pragma comment(lib, "ws2_32.lib")
#elif defined(__linux__) || defined(__APPLE__)
    #include <ifaddrs.h>
    #include <arpa/inet.h>
    #include <net/if.h>
    #include <netinet/in.h>
    #include <sys/types.h>
    #include <sys/socket.h>
#endif

#if defined(__linux__)
    #include <fstream>
    #include <sstream>
    #include <dirent.h>
#endif

namespace AsyncIO::IO::Hardware {

namespace {

std::string FormatMac(const uint8_t* mac, size_t len) {
    char buf[24] = {};
    size_t pos = 0;
    for (size_t i = 0; i < len; ++i) {
        pos += std::snprintf(buf + pos, sizeof(buf) - pos, (i == 0) ? "%02X" : ":%02X", mac[i]);
    }
    return std::string(buf);
}

#if defined(_WIN32)

std::string InetToString(SOCKADDR* sa) {
    if (!sa) return {};
    char buf[INET6_ADDRSTRLEN] = {};
    if (sa->sa_family == AF_INET) {
        auto* sin = reinterpret_cast<sockaddr_in*>(sa);
        inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
    } else if (sa->sa_family == AF_INET6) {
        auto* sin6 = reinterpret_cast<sockaddr_in6*>(sa);
        inet_ntop(AF_INET6, &sin6->sin6_addr, buf, sizeof(buf));
    }
    return std::string(buf);
}

// IF_TYPE_IEEE80211 = 71
constexpr ULONG kIfTypeWireless = 71;
// IF_TYPE_SOFTWARE_LOOPBACK = 24
constexpr ULONG kIfTypeLoopback = 24;

#endif  // _WIN32

}  // namespace

NetworkInfoProvider::NetworkInfoProvider() = default;
NetworkInfoProvider::~NetworkInfoProvider() = default;

bool NetworkInfoProvider::Initialize() {
#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);  // best-effort; ignore double-init
#endif
    initialized_ = true;
    return true;
}

#if defined(_WIN32)

std::vector<NetworkAdapter> NetworkInfoProvider::GetAdapters() const {
    std::vector<NetworkAdapter> adapters;

    // GAA_FLAG_INCLUDE_STATISTICS is only available on Vista+; older SDKs
    // don't define it. We query counters via GetIfEntry2 instead below.
    ULONG flags = GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST;
#ifdef GAA_FLAG_INCLUDE_STATISTICS
    flags |= GAA_FLAG_INCLUDE_STATISTICS;
#endif

    ULONG buf_len = 0;
    if (GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, nullptr, &buf_len)
        != ERROR_BUFFER_OVERFLOW) {
        buf_len = 16 * 1024;
    }
    std::vector<uint8_t> buffer(buf_len);
    PIP_ADAPTER_ADDRESSES addrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
    ULONG rc = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, addrs, &buf_len);
    if (rc != NO_ERROR) {
        buf_len = (std::max)(buf_len * 2UL, 64UL * 1024UL);
        buffer.assign(buf_len, 0);
        addrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
        rc = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, addrs, &buf_len);
        if (rc != NO_ERROR) return adapters;
    }

    // Wide -> narrow (ASCII subset). FriendlyName/Description are wchar_t*;
    // AdapterName is already a narrow GUID string (char*).
    auto to_narrow_w = [](const wchar_t* w) -> std::string {
        if (!w) return {};
        std::string out;
        for (; *w; ++w) out.push_back(static_cast<char>(*w < 128 ? *w : L'?'));
        return out;
    };
    auto to_narrow_a = [](const char* s) -> std::string {
        return s ? std::string(s) : std::string{};
    };

    for (PIP_ADAPTER_ADDRESSES a = addrs; a; a = a->Next) {
        NetworkAdapter n;

        n.friendly_name = to_narrow_w(a->FriendlyName);
        n.description   = to_narrow_w(a->Description);
        // AdapterName is the GUID-like narrow string; use FriendlyName as the
        // user-facing name and fall back to the GUID if absent.
        n.name = n.friendly_name.empty() ? to_narrow_a(a->AdapterName) : n.friendly_name;

        if (a->PhysicalAddressLength == 6 || a->PhysicalAddressLength == 8) {
            n.mac_address = FormatMac(a->PhysicalAddress, a->PhysicalAddressLength);
        }

        n.is_loopback = (a->IfType == kIfTypeLoopback);
        n.is_wireless = (a->IfType == kIfTypeWireless);
        n.is_up = (a->OperStatus == IfOperStatusUp);
        switch (a->IfType) {
            case IF_TYPE_SOFTWARE_LOOPBACK:
            case IF_TYPE_TUNNEL:
            case IF_TYPE_PPP:
                n.is_virtual = true;
                break;
            default:
                break;
        }

        n.link_speed_bps = (a->ReceiveLinkSpeed > 0) ? a->ReceiveLinkSpeed
                                                     : a->TransmitLinkSpeed;

        // Counters via GetIfEntry2 on the interface LUID. This works regardless
        // of GAA_FLAG_INCLUDE_STATISTICS availability.
        MIB_IF_ROW2 row{};
        row.InterfaceLuid = a->Luid;
        if (GetIfEntry2(&row) == NO_ERROR) {
            n.bytes_received    = row.InOctets;
            n.bytes_sent        = row.OutOctets;
            // InNUcastPkts/OutNUcastPkts = multicast + broadcast combined.
            n.packets_received  = row.InUcastPkts + row.InNUcastPkts;
            n.packets_sent      = row.OutUcastPkts + row.OutNUcastPkts;
            if (n.link_speed_bps == 0) {
                n.link_speed_bps = (row.ReceiveLinkSpeed > 0) ? row.ReceiveLinkSpeed
                                                              : row.TransmitLinkSpeed;
            }
        }

        // IP addresses (unicast).
        for (PIP_ADAPTER_UNICAST_ADDRESS ua = a->FirstUnicastAddress; ua; ua = ua->Next) {
            if (!ua->Address.lpSockaddr) continue;
            const std::string ip = InetToString(ua->Address.lpSockaddr);
            if (ip.empty()) continue;
            if (ua->Address.lpSockaddr->sa_family == AF_INET) {
                n.ipv4_addresses.push_back(ip);
                if (n.ipv4_address.empty()) n.ipv4_address = ip;
            } else if (ua->Address.lpSockaddr->sa_family == AF_INET6) {
                n.ipv6_addresses.push_back(ip);
            }
        }

        adapters.push_back(std::move(n));
    }
    return adapters;
}

#elif defined(__linux__) || defined(__APPLE__)

std::vector<NetworkAdapter> NetworkInfoProvider::GetAdapters() const {
    std::vector<NetworkAdapter> adapters;

    struct ifaddrs* ifap = nullptr;
    if (getifaddrs(&ifap) != 0) return adapters;

    // First pass: collect interface names in insertion order.
    std::vector<std::string> names;
    for (struct ifaddrs* i = ifap; i; i = i->ifa_next) {
        if (!i->ifa_name) continue;
        if (std::find(names.begin(), names.end(), i->ifa_name) == names.end()) {
            names.emplace_back(i->ifa_name);
        }
    }

    for (const auto& name : names) {
        NetworkAdapter n;
        n.name = name;
        n.friendly_name = name;

        // Flags from a representative ifaddr
        for (struct ifaddrs* i = ifap; i; i = i->ifa_next) {
            if (!i->ifa_name || name != i->ifa_name) continue;
            if (i->ifa_flags & IFF_UP)        n.is_up = true;
            if (i->ifa_flags & IFF_LOOPBACK)   n.is_loopback = true;
            if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET) {
                char buf[INET_ADDRSTRLEN] = {};
                inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(i->ifa_addr)->sin_addr,
                          buf, sizeof(buf));
                n.ipv4_addresses.emplace_back(buf);
                if (n.ipv4_address.empty()) n.ipv4_address = buf;
            } else if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET6) {
                char buf[INET6_ADDRSTRLEN] = {};
                inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6*>(i->ifa_addr)->sin6_addr,
                          buf, sizeof(buf));
                n.ipv6_addresses.emplace_back(buf);
            }
        }

        if (n.is_loopback) n.is_virtual = true;

#if defined(__linux__)
        const std::string base = "/sys/class/net/" + name;
        // MAC
        {
            std::ifstream f(base + "/address");
            if (f.is_open()) {
                std::string mac;
                std::getline(f, mac);
                // Normalize to upper, trim newline
                if (!mac.empty() && mac.back() == '\n') mac.pop_back();
                std::transform(mac.begin(), mac.end(), mac.begin(),
                    [](unsigned char c){ return static_cast<char>(std::toupper(c)); });
                n.mac_address = mac;
            }
        }
        // Operstate
        {
            std::ifstream f(base + "/operstate");
            if (f.is_open()) {
                std::string s; f >> s;
                n.is_up = (s == "up");
            } else {
                n.is_up = n.is_up;  // keep ifaddrs-derived value
            }
        }
        // Wireless?
        {
            std::ifstream f(base + "/wireless");
            n.is_wireless = f.good();
        }
        // Statistics
        {
            auto read_uint = [&](const char* stat) -> uint64_t {
                std::ifstream f(base + "/statistics/" + stat);
                if (!f.is_open()) return 0;
                uint64_t v = 0; f >> v; return v;
            };
            n.bytes_received    = read_uint("rx_bytes");
            n.bytes_sent        = read_uint("tx_bytes");
            n.packets_received  = read_uint("rx_packets");
            n.packets_sent      = read_uint("tx_packets");
        }
        // Speed (Mbps -> bps)
        {
            std::ifstream f(base + "/speed");
            if (f.is_open()) {
                uint64_t mbps = 0; f >> mbps;
                n.link_speed_bps = mbps * 1'000'000ULL;
            }
        }
#elif defined(__APPLE__)
        // MAC address via getifaddrs doesn't expose it; leave empty (best-effort).
        // Stats require sysctl net.route; left as 0 for portability.
#endif
        adapters.push_back(std::move(n));
    }

    freeifaddrs(ifap);
    return adapters;
}

#else
std::vector<NetworkAdapter> NetworkInfoProvider::GetAdapters() const {
    return {};
}
#endif

uint64_t NetworkInfoProvider::GetTotalBytesReceived() const {
    uint64_t total = 0;
    for (const auto& a : GetAdapters()) {
        if (!a.is_loopback) total += a.bytes_received;
    }
    return total;
}

uint64_t NetworkInfoProvider::GetTotalBytesSent() const {
    uint64_t total = 0;
    for (const auto& a : GetAdapters()) {
        if (!a.is_loopback) total += a.bytes_sent;
    }
    return total;
}

}  // namespace AsyncIO::IO::Hardware
