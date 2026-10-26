#pragma once

#include "../winapi_common.h"
#include <string>
#include <vector>
#include <functional>

namespace Engine::Content::Platform::WinAPI::Network {

// Network interface information
struct NetworkInterface {
    std::string name;
    std::string description;
    std::string ipAddress;
    std::string subnetMask;
    std::string gateway;
    bool isUp;
};

// Network helper
class NetworkHelper {
public:
    static std::vector<NetworkInterface> EnumerateInterfaces();
    static NetworkInterface GetPrimaryInterface();
    
    static std::string GetLocalIPAddress();
    static std::string GetMacAddress(const std::string& interfaceName);
    
    static bool IsConnected();
    static bool HasInternetConnection();
    
    static std::string GetHostname();
    static std::vector<std::string> ResolveHostname(const std::string& hostname);
};

// WinHTTP helper (simplified)
class WinHTTPHelper {
public:
    using ResponseCallback = std::function<void(const std::string&, bool)>;
    
    static bool Initialize();
    static void Shutdown();
    
    static std::string Get(const std::string& url);
    static std::string Post(const std::string& url, const std::string& data);
    
    static void AsyncGet(const std::string& url, ResponseCallback callback);
    
    static void SetProxy(const std::string& proxy);
    static void SetTimeout(int milliseconds);
    
private:
    static void* hSession_;  // HINTERNET
    static bool initialized_;
};

// WinSock helper (simplified)
class WinSockHelper {
public:
    static bool Initialize();
    static void Shutdown();
    
    static std::string GetHostByName(const std::string& hostname);
    static std::string GetHostByAddr(const std::string& ipAddress);
    
    static bool IsInitialized() { return initialized_; }
    
private:
    static bool initialized_;
};

// Bluetooth helper (basic)
class BluetoothHelper {
public:
    static bool IsAvailable();
    static std::vector<std::string> EnumerateDevices();
    static bool IsDeviceEnabled();
};

// Windows Sockets 2 helper
class WinSock2Helper {
public:
    struct SocketInfo {
        std::string localAddress;
        int localPort;
        std::string remoteAddress;
        int remotePort;
        int state;
    };
    
    static std::vector<SocketInfo> EnumerateSockets();
    static bool IsPortOpen(int port);
    
    static bool GetTCPStatistics(int* connectionsOut, int* segmentsReceived, int* segmentsSent);
    static bool GetUDPStatistics(int* datagramsReceived, int* datagramsSent);
};

}  // namespace Engine::Content::Platform::WinAPI::Network
