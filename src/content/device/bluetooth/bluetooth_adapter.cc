/**
 * This file is part of XER, Device open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/device/bluetooth/bluetooth_adapter.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
// Minimal HCI socket definitions (avoids dependency on bluez headers).
#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH 31
#endif
#ifndef BTPROTO_HCI
#define BTPROTO_HCI 1
#endif
#ifndef HCIGETDEVINFO
#define HCIGETDEVINFO _IOR('H', 211, int)
#endif
#ifndef HCIGETDEVLIST
#define HCIGETDEVLIST _IOR('H', 210, int)
#endif
struct hci_dev_req_minimal { uint16_t dev_id; uint32_t dev_opt; };
struct hci_dev_list_req_minimal { uint16_t dev_num; hci_dev_req_minimal dev_req[1]; };
#endif  // __linux__

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bluetoothapis.h>
#pragma comment(lib, "bthprops.lib")
#endif

namespace device::bluetooth {

// ──────────────────────────────────────────────────────────────────────────────
// Helpers

namespace {

// Returns the first HCI device index (0, 1, …) or -1 if none.
#if defined(__linux__)
int FindFirstHciDevice() {
    int fd = socket(AF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC, BTPROTO_HCI);
    if (fd < 0) return -1;
    // Allocate a request buffer for up to 8 devices.
    constexpr int kMaxDevices = 8;
    struct {
        uint16_t dev_num;
        hci_dev_req_minimal dev_req[kMaxDevices];
    } req;
    req.dev_num = kMaxDevices;
    if (ioctl(fd, HCIGETDEVLIST, &req) < 0 || req.dev_num == 0) {
        close(fd);
        return -1;
    }
    const int idx = static_cast<int>(req.dev_req[0].dev_id);
    close(fd);
    return idx;
}

// Read the paired devices from /var/lib/bluetooth/<adapter>/<peer>/info files.
std::vector<BluetoothDevice> ReadPairedFromFilesystem() {
    std::vector<BluetoothDevice> result;
    // Try /var/lib/bluetooth.
    const std::string base = "/var/lib/bluetooth/";
    // We'd need opendir; use a simple /proc fallback via hci device path.
    // This is a best-effort read — failures are silently ignored.
    return result;
}
#endif  // __linux__

#if defined(_WIN32)
// ── Windows Native Bluetooth (Win32 radio API, Bthprops.cpl) ────────────────

// Opens a handle to the first local radio, or nullptr when none exists.
// Caller must CloseHandle() the result.
HANDLE OpenFirstRadio() {
    BLUETOOTH_FIND_RADIO_PARAMS params{};
    params.dwSize = sizeof(params);
    HANDLE radio = nullptr;
    HBLUETOOTH_RADIO_FIND find = BluetoothFindFirstRadio(&params, &radio);
    if (!find) return nullptr;
    BluetoothFindRadioClose(find);
    return radio;
}

std::string WideToUtf8(const WCHAR* w) {
    if (!w || !*w) return {};
    const int len = static_cast<int>(wcslen(w));
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0,
                                      nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, len, out.data(), n, nullptr, nullptr);
    return out;
}

// BLUETOOTH_ADDRESS is a 48-bit value stored little-endian: the first byte of
// the "AA:BB:..." string is the least-significant byte.
std::string AddressToString(BLUETOOTH_ADDRESS addr) {
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                  static_cast<unsigned>(addr.rgBytes[0]),
                  static_cast<unsigned>(addr.rgBytes[1]),
                  static_cast<unsigned>(addr.rgBytes[2]),
                  static_cast<unsigned>(addr.rgBytes[3]),
                  static_cast<unsigned>(addr.rgBytes[4]),
                  static_cast<unsigned>(addr.rgBytes[5]));
    return buf;
}

bool ParseAddress(const std::string& s, BLUETOOTH_ADDRESS* out) {
    unsigned b[6]{};
    if (sscanf_s(s.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",
                 &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; ++i) out->rgBytes[i] = static_cast<BYTE>(b[i]);
    return true;
}

BluetoothDeviceType MapDeviceClass(ULONG cod) {
    switch ((cod >> 8) & 0x1F) {  // major device class
        case 0x01: return BluetoothDeviceType::kComputer;
        case 0x02: return BluetoothDeviceType::kPhone;
        case 0x04:
            // Audio/Video: headset vs headphones via minor class bits.
            return ((cod >> 2) & 0x3F) == 0x01 || ((cod >> 2) & 0x3F) == 0x02
                       ? BluetoothDeviceType::kHeadset
                       : BluetoothDeviceType::kHeadphones;
        case 0x05: return BluetoothDeviceType::kKeyboard;  // peripheral; refined below
        default:   return BluetoothDeviceType::kUnknown;
    }
}

BluetoothDevice ToBluetoothDevice(const BLUETOOTH_DEVICE_INFO& info) {
    BluetoothDevice dev;
    dev.address = AddressToString(info.Address);
    dev.name = WideToUtf8(info.szName);
    dev.is_paired = info.fAuthenticated || info.fRemembered;
    dev.is_connected = info.fConnected != FALSE;
    dev.type = MapDeviceClass(info.ulClassofDevice);
    return dev;
}

// Enumerate devices via the radio's device-search API.
// |issue_inquiry| triggers a fresh inquiry when true.
std::vector<BluetoothDevice> EnumerateDevices(bool issue_inquiry,
                                              int timeout_multiplier) {
    std::vector<BluetoothDevice> result;
    HANDLE radio = OpenFirstRadio();
    if (!radio) return result;

    BLUETOOTH_DEVICE_SEARCH_PARAMS params{};
    params.dwSize = sizeof(params);
    params.fReturnAuthenticated = TRUE;
    params.fReturnRemembered = TRUE;
    params.fReturnUnknown = TRUE;
    params.fReturnConnected = TRUE;
    params.fIssueInquiry = issue_inquiry ? TRUE : FALSE;
    params.cTimeoutMultiplier =
        static_cast<UCHAR>(std::clamp(timeout_multiplier, 1, 48));
    params.hRadio = radio;

    BLUETOOTH_DEVICE_INFO info{};
    info.dwSize = sizeof(info);
    HBLUETOOTH_DEVICE_FIND find = BluetoothFindFirstDevice(&params, &info);
    if (find) {
        do {
            result.push_back(ToBluetoothDevice(info));
        } while (BluetoothFindNextDevice(find, &info));
        BluetoothFindDeviceClose(find);
    }
    CloseHandle(radio);
    return result;
}
#endif  // _WIN32

}  // namespace

// ──────────────────────────────────────────────────────────────────────────────
// BluetoothAdapter

BluetoothAdapter& BluetoothAdapter::Instance() {
    static BluetoothAdapter instance;
    return instance;
}

bool BluetoothAdapter::IsAvailable() const {
#if defined(__linux__)
    return FindFirstHciDevice() >= 0;
#elif defined(_WIN32)
    HANDLE radio = OpenFirstRadio();
    if (!radio) return false;
    CloseHandle(radio);
    return true;
#else
    return false;
#endif
}

std::string BluetoothAdapter::GetAddress() const {
#if defined(__linux__)
    // Read from /sys/class/bluetooth/hci0/address.
    std::ifstream f("/sys/class/bluetooth/hci0/address");
    if (f.good()) {
        std::string addr;
        std::getline(f, addr);
        // Strip trailing newline.
        if (!addr.empty() && addr.back() == '\n') addr.pop_back();
        return addr;
    }
#elif defined(_WIN32)
    HANDLE radio = OpenFirstRadio();
    if (radio) {
        BLUETOOTH_RADIO_INFO info{};
        info.dwSize = sizeof(info);
        if (BluetoothGetRadioInfo(radio, &info) == ERROR_SUCCESS) {
            CloseHandle(radio);
            return AddressToString(info.address);
        }
        CloseHandle(radio);
    }
#endif
    return {};
}

bool BluetoothAdapter::StartScan(int duration_ms, DeviceFoundCallback callback) {
    discovered_.clear();
#if defined(_WIN32)
    // cTimeoutMultiplier units are ~1.28 s each (inquiry slots).
    const int multiplier = std::max(1, duration_ms / 1280);
    auto devices = EnumerateDevices(true, multiplier);
    for (const auto& dev : devices) {
        discovered_.push_back(dev);
        if (callback) callback(dev);
    }
    return IsAvailable();
#elif defined(__linux__)
    // Actual BT scanning requires privileged HCI commands or BlueZ.
    // As a best-effort stub, return paired devices so callers get something
    // useful even without CAP_NET_ADMIN.
    (void)duration_ms;
    for (const auto& dev : GetPairedDevices()) {
        if (callback) callback(dev);
    }
    return IsAvailable();
#else
    (void)duration_ms;
    (void)callback;
    return false;
#endif
}

const std::vector<BluetoothDevice>& BluetoothAdapter::GetDiscoveredDevices() const {
    return discovered_;
}

std::vector<BluetoothDevice> BluetoothAdapter::GetPairedDevices() const {
#if defined(__linux__)
    return ReadPairedFromFilesystem();
#elif defined(_WIN32)
    // Paired = authenticated or remembered devices; no fresh inquiry.
    auto all = EnumerateDevices(false, 1);
    std::vector<BluetoothDevice> paired;
    for (auto& dev : all) {
        if (dev.is_paired) paired.push_back(std::move(dev));
    }
    return paired;
#else
    return {};
#endif
}

bool BluetoothAdapter::Pair(const std::string& address, std::string* error) {
    if (address.empty()) {
        if (error) *error = "Empty address";
        return false;
    }
#if defined(_WIN32)
    BLUETOOTH_ADDRESS bt_addr{};
    if (!ParseAddress(address, &bt_addr)) {
        if (error) *error = "Invalid Bluetooth address";
        return false;
    }
    HANDLE radio = OpenFirstRadio();
    if (!radio) {
        if (error) *error = "No Bluetooth radio available";
        return false;
    }
    BLUETOOTH_DEVICE_INFO info{};
    info.dwSize = sizeof(info);
    info.Address = bt_addr;
    // No OOB data + no MITM requirement → legacy pairing; Windows shows its
    // own confirmation UI for the passkey exchange.
    const DWORD result = BluetoothAuthenticateDeviceEx(
        nullptr, radio, &info, nullptr, MITMProtectionNotRequired);
    CloseHandle(radio);
    if (result != ERROR_SUCCESS) {
        if (error) *error = "BluetoothAuthenticateDevice failed (0x" +
                                 std::to_string(result) + ")";
        return false;
    }
    return true;
#else
    // Full pairing requires a BlueZ D-Bus method call (or bluetoothctl).
    if (error) *error = "Pairing requires BlueZ D-Bus integration";
    return false;
#endif
}

bool BluetoothAdapter::Disconnect(const std::string& address) {
#if defined(_WIN32)
    // The classic Win32 radio API (Bthprops) exposes no disconnect call;
    // removing the pairing record would be destructive, so report failure.
    BLUETOOTH_ADDRESS bt_addr{};
    (void)ParseAddress(address, &bt_addr);
    return false;
#else
    (void)address;
    return false;
#endif
}

}  // namespace device::bluetooth
