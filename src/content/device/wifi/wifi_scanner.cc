/**
 * This file is part of XER, Device open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/device/wifi/wifi_scanner.h"

#include <cstring>
#include <fstream>
#include <sstream>

#if defined(__linux__)
#include <dirent.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/wireless.h>
#include <unistd.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <wlanapi.h>
#pragma comment(lib, "wlanapi.lib")
// objbase.h turns `interface` into a macro for `struct`.
#undef interface
#endif
#include <cmath>

namespace device::wifi {

namespace {

#if defined(__linux__)
// Parse /proc/net/wireless — returns signal for each interface.
// Format (after 2-line header):
//   wlan0: 0000  -50.  -80.   0.       0      0      0      0     0     0
static std::vector<std::pair<std::string, int>> ReadProcNetWireless() {
    std::vector<std::pair<std::string, int>> result;
    std::ifstream f("/proc/net/wireless");
    if (!f.good()) return result;

    std::string line;
    // Skip two header lines.
    std::getline(f, line);
    std::getline(f, line);

    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string iface;
        std::string status, link, level;
        ss >> iface >> status >> link >> level;
        if (iface.empty()) continue;
        // Remove trailing colon.
        if (!iface.empty() && iface.back() == ':') iface.pop_back();
        // Remove trailing dot from level.
        if (!level.empty() && level.back() == '.') level.pop_back();
        int sig = 0;
        try { sig = std::stoi(level); } catch (...) {}
        result.push_back({iface, sig});
    }
    return result;
}

// Enumerate wireless interfaces from /sys/class/net/<iface>/wireless.
static std::vector<std::string> ListWirelessInterfaces() {
    std::vector<std::string> result;
    DIR* dir = opendir("/sys/class/net");
    if (!dir) return result;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string name = entry->d_name;
        if (name.empty() || name[0] == '.') continue;
        // Check for the "wireless" subdirectory.
        const std::string wpath = "/sys/class/net/" + name + "/wireless";
        DIR* wd = opendir(wpath.c_str());
        if (wd) {
            result.push_back(name);
            closedir(wd);
        }
    }
    closedir(dir);
    return result;
}

// Read the current SSID for |iface| using SIOCGIWESSID ioctl.
static std::string ReadCurrentSsid(const std::string& iface) {
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return {};
    struct iwreq req{};
    char essid[IW_ESSID_MAX_SIZE + 1]{};
    strncpy(req.ifr_name, iface.c_str(), IFNAMSIZ - 1);
    req.u.essid.pointer = essid;
    req.u.essid.length  = IW_ESSID_MAX_SIZE;
    req.u.essid.flags   = 0;
    const bool ok = (ioctl(fd, SIOCGIWESSID, &req) == 0);
    close(fd);
    if (!ok) return {};
    essid[IW_ESSID_MAX_SIZE] = '\0';
    return essid;
}

// Read frequency (Hz) for |iface|.
static uint32_t ReadFrequencyMhz(const std::string& iface) {
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return 0;
    struct iwreq req{};
    strncpy(req.ifr_name, iface.c_str(), IFNAMSIZ - 1);
    uint32_t mhz = 0;
    if (ioctl(fd, SIOCGIWFREQ, &req) == 0) {
        double hz = req.u.freq.m * std::pow(10.0, req.u.freq.e);
        mhz = static_cast<uint32_t>(hz / 1'000'000.0);
    }
    close(fd);
    return mhz;
}
#endif  // __linux__

#if defined(_WIN32)
// ── Windows Native Wifi (WlanAPI) ──────────────────────────────────────────

struct WlanClient {
    HANDLE handle = nullptr;
    WlanClient() = default;
    ~WlanClient() { if (handle) WlanCloseHandle(handle, nullptr); }
    WlanClient(const WlanClient&) = delete;
    WlanClient& operator=(const WlanClient&) = delete;
    bool Open() {
        DWORD negotiated = 0;
        return WlanOpenHandle(2, nullptr, &negotiated, &handle) == ERROR_SUCCESS;
    }
    explicit operator bool() const { return handle != nullptr; }
};

// Interface names on Windows are the interface GUID rendered as "{...}".
std::string GuidToString(const GUID& g) {
    char buf[64];
    std::snprintf(buf, sizeof(buf),
        "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        static_cast<unsigned long>(g.Data1),
        static_cast<unsigned>(g.Data2), static_cast<unsigned>(g.Data3),
        g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
        g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return buf;
}

bool StringToGuid(const std::string& s, GUID* out) {
    return CLSIDFromString(std::wstring(s.begin(), s.end()).c_str(),
                           out) == S_OK;
}

int QualityToDbm(WLAN_SIGNAL_QUALITY q) {
    // 0-100 quality maps roughly to -100..0 dBm.
    return static_cast<int>(q) / 2 - 100;
}

WifiSecurityType MapSecurity(const WLAN_AVAILABLE_NETWORK& net) {
    if (!net.bSecurityEnabled) return WifiSecurityType::kOpen;
    switch (net.dot11DefaultAuthAlgorithm) {
        case DOT11_AUTH_ALGO_80211_SHARED_KEY:
            return WifiSecurityType::kWEP;
        case DOT11_AUTH_ALGO_WPA:
        case DOT11_AUTH_ALGO_WPA_PSK:
            return WifiSecurityType::kWPA;
        case DOT11_AUTH_ALGO_RSNA:
        case DOT11_AUTH_ALGO_RSNA_PSK:
            return WifiSecurityType::kWPA2;
        case DOT11_AUTH_ALGO_WPA3:
        case DOT11_AUTH_ALGO_WPA3_SAE:
        case DOT11_AUTH_ALGO_WPA3_ENT:
        case DOT11_AUTH_ALGO_OWE:
            return WifiSecurityType::kWPA3;
        default:
            return net.dot11DefaultAuthAlgorithm >= DOT11_AUTH_ALGO_IHV_START
                       ? WifiSecurityType::kUnknown
                       : WifiSecurityType::kWPA2;
    }
}

// Best (strongest-signal) BSS entry for |ssid| on |guid|; fills channel,
// frequency and bssid. Returns false when no BSS data is available.
bool QueryBestBss(HANDLE h, const GUID& guid, const DOT11_SSID& ssid,
                  uint32_t* freq_mhz, uint16_t* channel,
                  std::string* bssid_out) {
    PWLAN_BSS_LIST bss = nullptr;
    if (WlanGetNetworkBssList(h, &guid, const_cast<PDOT11_SSID>(&ssid),
                              dot11_BSS_type_any, TRUE, nullptr, &bss) != ERROR_SUCCESS ||
        !bss || bss->dwNumberOfItems == 0) {
        if (bss) WlanFreeMemory(bss);
        return false;
    }
    const WLAN_BSS_ENTRY* best = &bss->wlanBssEntries[0];
    for (DWORD i = 1; i < bss->dwNumberOfItems; ++i) {
        if (bss->wlanBssEntries[i].lRssi > best->lRssi) {
            best = &bss->wlanBssEntries[i];
        }
    }
    *freq_mhz = best->ulChCenterFrequency;
    if (*freq_mhz >= 2412 && *freq_mhz <= 2484) {
        *channel = static_cast<uint16_t>((*freq_mhz - 2407) / 5);
    } else if (*freq_mhz >= 5000 && *freq_mhz <= 5900) {
        *channel = static_cast<uint16_t>((*freq_mhz - 5000) / 5);
    }
    char mac[18];
    std::snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                  best->dot11Bssid[0], best->dot11Bssid[1],
                  best->dot11Bssid[2], best->dot11Bssid[3],
                  best->dot11Bssid[4], best->dot11Bssid[5]);
    *bssid_out = mac;
    WlanFreeMemory(bss);
    return true;
}
#endif  // _WIN32

#if defined(_WIN32)
// SSID bytes → std::string (WLAN SSIDs are raw bytes, not necessarily UTF-8).
std::string Dot11SsidToString(const DOT11_SSID& ssid) {
    if (ssid.uSSIDLength == 0 || ssid.uSSIDLength > DOT11_SSID_MAX_LENGTH) {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(ssid.ucSSID),
                       ssid.uSSIDLength);
}

// Poll WlanGetAvailableNetworkList until results arrive or we time out.
PWLAN_AVAILABLE_NETWORK_LIST WaitForNetworks(HANDLE h, const GUID& guid) {
    for (int i = 0; i < 12; ++i) {  // up to ~6 s
        Sleep(500);
        PWLAN_AVAILABLE_NETWORK_LIST list = nullptr;
        if (WlanGetAvailableNetworkList(h, &guid, 0, nullptr, &list) ==
                ERROR_SUCCESS &&
            list && list->dwNumberOfItems > 0) {
            return list;
        }
        if (list) WlanFreeMemory(list);
    }
    return nullptr;
}
#endif  // _WIN32

}  // namespace

// ──────────────────────────────────────────────────────────────────────────────

WifiScanner& WifiScanner::Instance() {
    static WifiScanner instance;
    return instance;
}

std::vector<WifiNetwork> WifiScanner::Scan() {
    std::vector<WifiNetwork> result;
#if defined(__linux__)
    const auto ifaces = GetInterfaces();
    for (const auto& iface : ifaces) {
        const std::string ssid = ReadCurrentSsid(iface);
        if (ssid.empty()) continue;
        WifiNetwork net;
        net.interface_name = iface;
        net.ssid = ssid;
        net.frequency_mhz = ReadFrequencyMhz(iface);
        if (net.frequency_mhz > 0 && net.frequency_mhz <= 2484)
            net.channel = static_cast<uint16_t>((net.frequency_mhz - 2412) / 5 + 1);
        else if (net.frequency_mhz >= 5180)
            net.channel = static_cast<uint16_t>((net.frequency_mhz - 5000) / 5);
        net.is_connected = true;
        // Get signal from /proc/net/wireless.
        for (const auto& [name, sig] : ReadProcNetWireless()) {
            if (name == iface) { net.signal_dbm = static_cast<int8_t>(sig); break; }
        }
        result.push_back(std::move(net));
    }
#elif defined(_WIN32)
    WlanClient client;
    if (!client.Open()) return result;

    PWLAN_INTERFACE_INFO_LIST ifaces = nullptr;
    if (WlanEnumInterfaces(client.handle, nullptr, &ifaces) != ERROR_SUCCESS ||
        !ifaces) {
        return result;
    }

    for (DWORD i = 0; i < ifaces->dwNumberOfItems; ++i) {
        const GUID& guid = ifaces->InterfaceInfo[i].InterfaceGuid;
        const std::string iface_name = GuidToString(guid);

        // Trigger an active scan, then collect results.
        WlanScan(client.handle, &guid, nullptr, nullptr, nullptr);
        PWLAN_AVAILABLE_NETWORK_LIST nets = WaitForNetworks(client.handle, guid);
        if (!nets) continue;

        // Connected SSID for this interface (used to flag is_connected).
        std::string connected_ssid;
        {
            PWLAN_CONNECTION_ATTRIBUTES conn = nullptr;
            DWORD size = 0;
            if (WlanQueryInterface(client.handle, &guid,
                                   wlan_intf_opcode_current_connection, nullptr,
                                   &size, reinterpret_cast<PVOID*>(&conn),
                                   nullptr) == ERROR_SUCCESS &&
                conn && conn->isState == wlan_interface_state_connected) {
                connected_ssid =
                    Dot11SsidToString(conn->wlanAssociationAttributes.dot11Ssid);
                if (connected_ssid.empty()) {
                    const int len =
                        static_cast<int>(wcslen(conn->strProfileName));
                    connected_ssid.reserve(len);
                    for (int c = 0; c < len; ++c) {
                        connected_ssid.push_back(
                            static_cast<char>(conn->strProfileName[c] & 0xFF));
                    }
                }
            }
            if (conn) WlanFreeMemory(conn);
        }

        for (DWORD n = 0; n < nets->dwNumberOfItems; ++n) {
            const WLAN_AVAILABLE_NETWORK& net = nets->Network[n];
            WifiNetwork out;
            out.interface_name = iface_name;
            out.ssid = Dot11SsidToString(net.dot11Ssid);
            if (out.ssid.empty()) out.ssid = "<hidden>";
            out.signal_dbm = static_cast<int8_t>(QualityToDbm(net.wlanSignalQuality));
            out.security = MapSecurity(net);
            QueryBestBss(client.handle, guid, net.dot11Ssid, &out.frequency_mhz,
                         &out.channel, &out.bssid);
            out.is_connected = (!connected_ssid.empty() &&
                                out.ssid == connected_ssid);
            result.push_back(std::move(out));
        }
        WlanFreeMemory(nets);
    }
    WlanFreeMemory(ifaces);
#endif
    return result;
}

WifiNetwork WifiScanner::GetConnectedNetwork(const std::string& interface_name) const {
#if defined(__linux__)
    const std::string ssid = ReadCurrentSsid(interface_name);
    if (ssid.empty()) return {};
    WifiNetwork net;
    net.interface_name = interface_name;
    net.ssid = ssid;
    net.frequency_mhz = ReadFrequencyMhz(interface_name);
    net.is_connected = true;
    for (const auto& [name, sig] : ReadProcNetWireless()) {
        if (name == interface_name) { net.signal_dbm = static_cast<int8_t>(sig); break; }
    }
    return net;
#elif defined(_WIN32)
    WlanClient client;
    if (!client.Open()) return {};
    GUID guid{};
    if (!StringToGuid(interface_name, &guid)) return {};

    PWLAN_CONNECTION_ATTRIBUTES conn = nullptr;
    DWORD size = 0;
    if (WlanQueryInterface(client.handle, &guid,
                           wlan_intf_opcode_current_connection, nullptr, &size,
                           reinterpret_cast<PVOID*>(&conn), nullptr) != ERROR_SUCCESS ||
        !conn || conn->isState != wlan_interface_state_connected) {
        if (conn) WlanFreeMemory(conn);
        return {};
    }

    WifiNetwork net;
    net.interface_name = interface_name;
    net.ssid = Dot11SsidToString(conn->wlanAssociationAttributes.dot11Ssid);
    if (net.ssid.empty()) {
        // Profile name is a UTF-16 SSID; convert best-effort.
        const int len = static_cast<int>(wcslen(conn->strProfileName));
        net.ssid.reserve(len);
        for (int i = 0; i < len; ++i) {
            net.ssid.push_back(static_cast<char>(conn->strProfileName[i] & 0xFF));
        }
    }
    net.signal_dbm = static_cast<int8_t>(
        QualityToDbm(conn->wlanAssociationAttributes.wlanSignalQuality));
    net.is_connected = true;
    const DOT11_MAC_ADDRESS& mac = conn->wlanAssociationAttributes.dot11Bssid;
    char macbuf[18];
    std::snprintf(macbuf, sizeof(macbuf),
                  "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
                  mac[3], mac[4], mac[5]);
    net.bssid = macbuf;
    net.security = conn->wlanSecurityAttributes.bSecurityEnabled
                      ? WifiSecurityType::kWPA2
                      : WifiSecurityType::kOpen;
    WlanFreeMemory(conn);
    return net;
#else
    return {};
#endif
}

std::vector<std::string> WifiScanner::GetInterfaces() const {
#if defined(__linux__)
    return ListWirelessInterfaces();
#elif defined(_WIN32)
    std::vector<std::string> result;
    WlanClient client;
    if (!client.Open()) return result;
    PWLAN_INTERFACE_INFO_LIST ifaces = nullptr;
    if (WlanEnumInterfaces(client.handle, nullptr, &ifaces) == ERROR_SUCCESS &&
        ifaces) {
        for (DWORD i = 0; i < ifaces->dwNumberOfItems; ++i) {
            result.push_back(GuidToString(ifaces->InterfaceInfo[i].InterfaceGuid));
        }
        WlanFreeMemory(ifaces);
    }
    return result;
#else
    return {};
#endif
}

}  // namespace device::wifi
