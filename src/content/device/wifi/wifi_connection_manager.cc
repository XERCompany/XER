/**
 * This file is part of XER, Device open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/device/wifi/wifi_connection_manager.h"
#include "content/device/wifi/wifi_scanner.h"

#include <fstream>
#include <sstream>
#include <cstdio>

// Project network stack — DNS resolver for connectivity check.
#include "content/network/dns/host_resolver_impl.h"
// Project async thread pool — background scans.
#include "async_io/io_thread_pool.h"

#if defined(__linux__)
#include <unistd.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <objbase.h>
#include <wlanapi.h>
#include <iphlpapi.h>
#pragma comment(lib, "wlanapi.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ole32.lib")
// objbase.h turns `interface` into a macro for `struct`; the project API uses
// `interface` as a parameter name, so undo it here.
#undef interface
#endif

namespace device::wifi {

namespace {

#if defined(_WIN32)
// Convert UTF-8 → UTF-16 for WlanAPI profile APIs.
std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                      static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        w.data(), n);
    return w;
}

bool InterfaceNameToGuid(const std::string& name, GUID* out) {
    // Accept either "{XXXXXXXX-...}" strings or legacy aliases like "wlan0".
    if (CLSIDFromString(Utf8ToWide(name).c_str(), out) == S_OK) return true;
    NET_LUID luid{};
    const std::wstring wname = Utf8ToWide(name);
    if (ConvertInterfaceNameToLuidW(wname.c_str(), &luid) == NO_ERROR ||
        ConvertInterfaceAliasToLuid(wname.c_str(), &luid) == NO_ERROR) {
        return ConvertInterfaceLuidToGuid(&luid, out) == NO_ERROR;
    }
    return false;
}

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

// XML-escape a string for WLAN profile documents.
std::string XmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;       break;
        }
    }
    return out;
}

// Build a WPA2PSK (or open) WLAN profile XML. Returns UTF-16 profile XML.
std::wstring BuildWpaProfileXml(const std::string& ssid,
                                const std::string& passphrase) {
    // Hex-encode SSID for <hex>SSIDConfig</hex>.
    static const char kHex[] = "0123456789ABCDEF";
    std::string hex;
    hex.reserve(ssid.size() * 2);
    for (unsigned char c : ssid) {
        hex += kHex[c >> 4];
        hex += kHex[c & 0xF];
    }
    const bool open = passphrase.empty();
    const std::wstring wssid = Utf8ToWide(ssid);
    const std::wstring wpass = Utf8ToWide(passphrase);

    // Assemble with narrow chars then convert once.
    std::string xml;
    xml += "<?xml version=\"1.0\"?>";
    xml += "<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">";
    xml += "<name>" + XmlEscape(ssid) + "</name>";
    xml += "<SSIDConfig><SSID><hex>" + hex + "</hex><name>" +
           XmlEscape(ssid) + "</name></SSID><nonBroadcast>false</nonBroadcast>";
    xml += "</SSIDConfig>";
    xml += "<connectionType>ESS</connectionType>";
    xml += "<connectionMode>auto</connectionMode>";
    xml += "<MSM><security><authEncryption>";
    xml += open ? "<authentication>open</authentication><encryption>none</encryption>"
                : "<authentication>WPA2PSK</authentication><encryption>AES</encryption>";
    xml += "<useOneX>false</useOneX></authEncryption>";
    if (!open) {
        xml += "<sharedKey><keyType>passPhrase</keyType>"
               "<protected>false</protected><keyMaterial>" +
               XmlEscape(passphrase) + "</keyMaterial></sharedKey>";
    }
    xml += "</security></MSM></WLANProfile>";
    (void)wssid; (void)wpass;
    return Utf8ToWide(xml);
}
#endif  // _WIN32

// Run |cmd| via popen, return stdout as string.
static std::string RunWpaCli(const std::string& args) {
#if !defined(__linux__)
    // wpa_cli drives wpa_supplicant (Linux). Windows Wi-Fi control goes
    // through WlanAPI and is not wired here yet.
    (void)args;
    return {};
#else
    const std::string cmd = "wpa_cli " + args + " 2>/dev/null";
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return {};
    std::string out;
    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) out += buf;
    pclose(fp);
    return out;
#endif
}

#if defined(__linux__)
// Parse /proc/net/dev line for |iface|.
static WifiConnectionManager::NetStats ReadNetStats(const std::string& iface) {
    WifiConnectionManager::NetStats s{};
    std::ifstream f("/proc/net/dev");
    if (!f.good()) return s;
    std::string line;
    std::getline(f, line); std::getline(f, line); // skip headers
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string name;
        ss >> name;
        if (name.empty()) continue;
        if (name.back() == ':') name.pop_back();
        if (name != iface) continue;
        ss >> s.rx_bytes >> s.rx_packets >> s.rx_errors;
        // skip: drop fifo frame compressed multicast
        uint64_t skip;
        for (int i = 0; i < 5; ++i) ss >> skip;
        ss >> s.tx_bytes >> s.tx_packets >> s.tx_errors;
        break;
    }
    return s;
}
#endif  // __linux__

}  // namespace

// ──────────────────────────────────────────────────────────────────────────────

WifiConnectionManager& WifiConnectionManager::Instance() {
    static WifiConnectionManager inst;
    return inst;
}

bool WifiConnectionManager::Connect(const std::string& ssid,
                                     const std::string& passphrase,
                                     const std::string& interface,
                                     ConnectionStateCallback callback) {
    if (callback) callback(WifiConnectionState::kAuthenticating, {});

#if defined(_WIN32)
    // Windows: create a WLAN profile (WPA2PSK/AES or open) and connect to it
    // via WlanAPI. The profile name is the SSID so WlanConnect can resolve it.
    WlanClient client;
    GUID guid{};
    if (!client.Open() || !InterfaceNameToGuid(interface, &guid)) {
        if (callback) callback(WifiConnectionState::kFailed,
                               "WlanAPI unavailable or bad interface");
        return false;
    }

    const std::wstring xml = BuildWpaProfileXml(ssid, passphrase);
    DWORD reason = 0;
    if (WlanSetProfile(client.handle, &guid, 0 /* all users */, xml.c_str(),
                       nullptr, TRUE /* overwrite */, nullptr, &reason) !=
        ERROR_SUCCESS) {
        if (callback) callback(WifiConnectionState::kFailed,
                               "WlanSetProfile failed");
        return false;
    }

    if (callback) callback(WifiConnectionState::kAssociating, {});

    // Profile name = SSID (UTF-16).
    const std::wstring profile_name = Utf8ToWide(ssid);
    WLAN_CONNECTION_PARAMETERS params{};
    params.wlanConnectionMode = wlan_connection_mode_profile;
    params.strProfile = profile_name.c_str();
    params.dot11BssType = dot11_BSS_type_infrastructure;
    params.dwFlags = 0;

    if (WlanConnect(client.handle, &guid, &params, nullptr) != ERROR_SUCCESS) {
        if (callback) callback(WifiConnectionState::kFailed, "WlanConnect failed");
        return false;
    }
    if (callback) callback(WifiConnectionState::kConnected, {});
    return true;
#else
    // Add network via wpa_cli.
    const std::string netid_str = RunWpaCli("-i " + interface + " add_network");
    if (netid_str.empty() || netid_str[0] < '0' || netid_str[0] > '9') {
        if (callback) callback(WifiConnectionState::kFailed, "add_network failed");
        return false;
    }
    const std::string nid(1, netid_str[0]);
    RunWpaCli("-i " + interface + " set_network " + nid + " ssid '\"" + ssid + "\"'");
    RunWpaCli("-i " + interface + " set_network " + nid + " psk '\"" + passphrase + "\"'");
    RunWpaCli("-i " + interface + " enable_network " + nid);

    if (callback) callback(WifiConnectionState::kAssociating, {});

    const std::string status = RunWpaCli("-i " + interface + " select_network " + nid);
    const bool ok = (status.find("OK") != std::string::npos);
    if (!ok) {
        if (callback) callback(WifiConnectionState::kFailed, "select_network failed");
        return false;
    }
    if (callback) callback(WifiConnectionState::kConnected, {});
    return true;
#endif
}

bool WifiConnectionManager::Disconnect(const std::string& interface) {
#if defined(_WIN32)
    WlanClient client;
    GUID guid{};
    if (!client.Open() || !InterfaceNameToGuid(interface, &guid)) return false;
    return WlanDisconnect(client.handle, &guid, nullptr) == ERROR_SUCCESS;
#else
    const std::string out = RunWpaCli("-i " + interface + " disconnect");
    return out.find("OK") != std::string::npos;
#endif
}

WifiConnectionState WifiConnectionManager::GetState(
    const std::string& interface) const {
#if defined(_WIN32)
    WlanClient client;
    GUID guid{};
    if (!client.Open() || !InterfaceNameToGuid(interface, &guid)) {
        return WifiConnectionState::kDisconnected;
    }
    PWLAN_CONNECTION_ATTRIBUTES conn = nullptr;
    DWORD size = 0;
    if (WlanQueryInterface(client.handle, &guid,
                           wlan_intf_opcode_current_connection, nullptr, &size,
                           reinterpret_cast<PVOID*>(&conn), nullptr) ==
            ERROR_SUCCESS &&
        conn) {
        const auto state = conn->isState;
        if (conn->strProfileName[0] != L'\0' &&
            state == wlan_interface_state_connected) {
            WlanFreeMemory(conn);
            return WifiConnectionState::kConnected;
        }
        WlanFreeMemory(conn);
        return state == wlan_interface_state_associating ||
                       state == wlan_interface_state_discovering
                   ? WifiConnectionState::kAssociating
                   : WifiConnectionState::kDisconnected;
    }
    return WifiConnectionState::kDisconnected;
#else
    const std::string out = RunWpaCli("-i " + interface + " status");
    if (out.find("wpa_state=COMPLETED") != std::string::npos)
        return WifiConnectionState::kConnected;
    if (out.find("wpa_state=ASSOCIATING") != std::string::npos)
        return WifiConnectionState::kAssociating;
    if (out.find("wpa_state=4WAY_HANDSHAKE") != std::string::npos ||
        out.find("wpa_state=GROUP_HANDSHAKE") != std::string::npos)
        return WifiConnectionState::kAuthenticating;
    if (out.find("wpa_state=DISCONNECTED") != std::string::npos ||
        out.find("wpa_state=INACTIVE") != std::string::npos)
        return WifiConnectionState::kDisconnected;
    return WifiConnectionState::kDisconnected;
#endif
}

void WifiConnectionManager::ScanAsync(
    const std::string& interface,
    std::function<void(std::vector<WifiNetwork>)> cb) {
    // Run scan on the shared IO thread pool so the caller is not blocked.
    XER::AsyncIO::IO::Async::IOThreadPool::GetSharedInstance().Enqueue(
        [interface, cb = std::move(cb)]() mutable {
            RunWpaCli("-i " + interface + " scan");
            // Small wait for kernel scan results to arrive.
#if defined(__linux__)
            usleep(3'000'000);  // 3 s
#endif
            auto results = WifiScanner::Instance().Scan();
            if (cb) cb(std::move(results));
        });
}

bool WifiConnectionManager::CheckConnectivity(const std::string& host) const {
    // Use the project DNS resolver to verify we can reach the internet.
    XER::Network::Dns::HostResolverImpl resolver;
    std::vector<XER::Network::Dns::ResolvedAddress> addrs;
    std::string err;
    return resolver.Resolve(host, 80, &addrs, &err) && !addrs.empty();
}

WifiConnectionManager::NetStats
WifiConnectionManager::GetNetStats(const std::string& interface) const {
#if defined(_WIN32)
    NetStats s{};
    GUID guid{};
    if (!InterfaceNameToGuid(interface, &guid)) return s;
    NET_LUID luid{};
    if (ConvertInterfaceGuidToLuid(&guid, &luid) != NO_ERROR) return s;
    MIB_IF_ROW2 row{};
    row.InterfaceLuid = luid;
    if (GetIfEntry2(&row) != NO_ERROR) return s;
    s.rx_bytes   = row.InOctets;
    s.tx_bytes   = row.OutOctets;
    s.rx_packets = row.InUcastPkts + row.InNUcastPkts;
    s.tx_packets = row.OutUcastPkts + row.OutNUcastPkts;
    s.rx_errors  = row.InErrors;
    s.tx_errors  = row.OutErrors;
    return s;
#else
    return ReadNetStats(interface);
#endif
}

}  // namespace device::wifi
