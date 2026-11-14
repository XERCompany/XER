#include "winapi_network.h"
#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "winhttp.lib")

namespace XER::Content::Platform::WinAPI::Network {

namespace {

// Converts an IPv4 address to dotted-quad string (replaces deprecated inet_ntoa).
std::string Ipv4ToString(const in_addr& addr) {
    char buf[INET_ADDRSTRLEN] = {};
    if (inet_ntop(AF_INET, &addr, buf, sizeof(buf)) == nullptr) {
        return {};
    }
    return buf;
}

}  // namespace

// ---------------------------------------------------------------------------
// Network Helper
// ---------------------------------------------------------------------------

std::vector<NetworkInterface> NetworkHelper::EnumerateInterfaces() {
    std::vector<NetworkInterface> interfaces;
    
    PMIB_IPADDRTABLE pIPAddrTable;
    DWORD dwSize = 0;
    
    // Get the size of the table
    if (GetIpAddrTable(NULL, &dwSize, 0) == ERROR_INSUFFICIENT_BUFFER) {
        pIPAddrTable = (PMIB_IPADDRTABLE)malloc(dwSize);
        if (pIPAddrTable) {
            if (GetIpAddrTable(pIPAddrTable, &dwSize, 0) == NO_ERROR) {
                for (DWORD i = 0; i < pIPAddrTable->dwNumEntries; ++i) {
                    NetworkInterface iface;
                    
                    // Convert IP address to string
                    struct in_addr ipAddr;
                    ipAddr.S_un.S_addr = (u_long)pIPAddrTable->table[i].dwAddr;
                    iface.ipAddress = Ipv4ToString(ipAddr);
                    
                    iface.name = "Interface " + std::to_string(i);
                    iface.description = "Ethernet Adapter";
                    iface.isUp = true;
                    
                    interfaces.push_back(iface);
                }
            }
            free(pIPAddrTable);
        }
    }
    
    return interfaces;
}

NetworkInterface NetworkHelper::GetPrimaryInterface() {
    auto interfaces = EnumerateInterfaces();
    if (!interfaces.empty()) {
        return interfaces[0];
    }
    return NetworkInterface{};
}

std::string NetworkHelper::GetLocalIPAddress() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* result = nullptr;
        if (getaddrinfo(hostname, nullptr, &hints, &result) == 0 && result != nullptr) {
            const auto* sa = reinterpret_cast<const sockaddr_in*>(result->ai_addr);
            const std::string ip = Ipv4ToString(sa->sin_addr);
            freeaddrinfo(result);
            if (!ip.empty()) {
                return ip;
            }
        }
    }
    return "127.0.0.1";
}

std::string NetworkHelper::GetMacAddress(const std::string& interfaceName) {
    (void)interfaceName;
    // Note: This requires GetAdaptersInfo or GetAdaptersAddresses
    // Simplified placeholder
    return "00:00:00:00:00:00";
}

bool NetworkHelper::IsConnected() {
    return !GetLocalIPAddress().empty();
}

bool NetworkHelper::HasInternetConnection() {
    // Note: This would typically ping a known server or use InternetGetConnectedState
    // Simplified check
    return IsConnected();
}

std::string NetworkHelper::GetHostname() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return std::string(hostname);
    }
    return "localhost";
}

std::vector<std::string> NetworkHelper::ResolveHostname(const std::string& hostname) {
    std::vector<std::string> addresses;
    
    struct addrinfo hints, *result;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    if (getaddrinfo(hostname.c_str(), NULL, &hints, &result) == 0) {
        for (struct addrinfo* ptr = result; ptr != NULL; ptr = ptr->ai_next) {
            struct sockaddr_in* sockaddr = (struct sockaddr_in*)ptr->ai_addr;
            addresses.push_back(Ipv4ToString(sockaddr->sin_addr));
        }
        freeaddrinfo(result);
    }
    
    return addresses;
}

// ---------------------------------------------------------------------------
// WinHTTP Helper
// ---------------------------------------------------------------------------

void* WinHTTPHelper::hSession_ = nullptr;
bool WinHTTPHelper::initialized_ = false;

bool WinHTTPHelper::Initialize() {
    if (initialized_) return true;
    
    // Note: This requires actual WinHTTP initialization
    // HINTERNET hSession = WinHttpOpen(...);
    // hSession_ = hSession;
    // initialized_ = (hSession != NULL);
    
    initialized_ = false;  // Placeholder
    return initialized_;
}

void WinHTTPHelper::Shutdown() {
    if (hSession_) {
        // WinHttpCloseHandle(hSession_);
        hSession_ = nullptr;
    }
    initialized_ = false;
}

std::string WinHTTPHelper::Get(const std::string& url) {
    (void)url;
    // Note: Simplified placeholder
    // Full implementation would use WinHttpOpenRequest, WinHttpSendRequest, etc.
    return "";
}

std::string WinHTTPHelper::Post(const std::string& url, const std::string& data) {
    (void)url;
    (void)data;
    // Note: Simplified placeholder
    return "";
}

void WinHTTPHelper::AsyncGet(const std::string& url, ResponseCallback callback) {
    (void)url;
    // Note: Simplified placeholder
    // Full implementation would use async WinHTTP operations
    callback("", false);
}

void WinHTTPHelper::SetProxy(const std::string& proxy) {
    (void)proxy;
    // Note: Simplified placeholder
}

void WinHTTPHelper::SetTimeout(int milliseconds) {
    (void)milliseconds;
    // Note: Simplified placeholder
}

// ---------------------------------------------------------------------------
// WinSock Helper
// ---------------------------------------------------------------------------

bool WinSockHelper::initialized_ = false;

bool WinSockHelper::Initialize() {
    if (initialized_) return true;
    
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    initialized_ = (result == 0);
    return initialized_;
}

void WinSockHelper::Shutdown() {
    if (initialized_) {
        WSACleanup();
        initialized_ = false;
    }
}

std::string WinSockHelper::GetHostByName(const std::string& hostname) {
    if (!Initialize()) return "";

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(hostname.c_str(), nullptr, &hints, &result) == 0 && result != nullptr) {
        const auto* sa = reinterpret_cast<const sockaddr_in*>(result->ai_addr);
        const std::string ip = Ipv4ToString(sa->sin_addr);
        freeaddrinfo(result);
        return ip;
    }

    return "";
}

std::string WinSockHelper::GetHostByAddr(const std::string& ipAddress) {
    if (!Initialize()) return "";
    
    in_addr addr{};
    if (inet_pton(AF_INET, ipAddress.c_str(), &addr) != 1) {
        return "";
    }

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr = addr;

    char name[NI_MAXHOST] = {};
    if (getnameinfo(reinterpret_cast<const sockaddr*>(&sa), sizeof(sa),
                    name, sizeof(name), nullptr, 0, NI_NAMEREQD) == 0) {
        return std::string(name);
    }

    return "";
}

// ---------------------------------------------------------------------------
// Bluetooth Helper
// ---------------------------------------------------------------------------

bool BluetoothHelper::IsAvailable() {
    // Note: This requires Bluetooth API (ws2bth.h, bluetoothapis.h)
    // Simplified placeholder
    return false;
}

std::vector<std::string> BluetoothHelper::EnumerateDevices() {
    // Note: Simplified placeholder
    return std::vector<std::string>();
}

bool BluetoothHelper::IsDeviceEnabled() {
    // Note: Simplified placeholder
    return false;
}

// ---------------------------------------------------------------------------
// WinSock2 Helper
// ---------------------------------------------------------------------------

std::vector<WinSock2Helper::SocketInfo> WinSock2Helper::EnumerateSockets() {
    std::vector<SocketInfo> sockets;
    
    // Note: This requires more complex implementation with socket enumeration
    // Simplified placeholder
    return sockets;
}

bool WinSock2Helper::IsPortOpen(int port) {
    (void)port;
    // Note: Simplified placeholder
    // Full implementation would attempt to bind to the port
    return false;
}

bool WinSock2Helper::GetTCPStatistics(int* connectionsOut, int* segmentsReceived, int* segmentsSent) {
    // Note: This requires GetTcpStatisticsEx
    // Simplified placeholder
    if (connectionsOut) *connectionsOut = 0;
    if (segmentsReceived) *segmentsReceived = 0;
    if (segmentsSent) *segmentsSent = 0;
    return false;
}

bool WinSock2Helper::GetUDPStatistics(int* datagramsReceived, int* datagramsSent) {
    // Note: This requires GetUdpStatisticsEx
    // Simplified placeholder
    if (datagramsReceived) *datagramsReceived = 0;
    if (datagramsSent) *datagramsSent = 0;
    return false;
}

}  // namespace XER::Content::Platform::WinAPI::Network
