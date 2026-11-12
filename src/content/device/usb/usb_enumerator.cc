/**
 * This file is part of XER, Device open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/device/usb/usb_enumerator.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#if ENGINE_HAS_LIBUSB
#include <libusb.h>
#endif

#if defined(__linux__)
#include <dirent.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <usbiodef.h>
#pragma comment(lib, "setupapi.lib")
#endif

namespace device::usb {

namespace {

#if ENGINE_HAS_LIBUSB
static UsbSpeed ParseLibusbSpeed(int speed) {
    switch (speed) {
        case LIBUSB_SPEED_LOW: return UsbSpeed::kLow;
        case LIBUSB_SPEED_FULL: return UsbSpeed::kFull;
        case LIBUSB_SPEED_HIGH: return UsbSpeed::kHigh;
        case LIBUSB_SPEED_SUPER: return UsbSpeed::kSuper;
        case LIBUSB_SPEED_SUPER_PLUS: return UsbSpeed::kSuperPlus;
        default: return UsbSpeed::kUnknown;
    }
}

static std::string ReadUsbString(libusb_device_handle* handle, uint8_t descriptor_index) {
    if (!handle || descriptor_index == 0) return {};
    unsigned char buffer[256]{};
    const int length = libusb_get_string_descriptor_ascii(handle, descriptor_index, buffer, sizeof(buffer));
    return length > 0 ? std::string(reinterpret_cast<const char*>(buffer), static_cast<size_t>(length)) : std::string{};
}

static bool ReadLibusbDevice(libusb_device* device, UsbDevice* output) {
    libusb_device_descriptor descriptor{};
    if (libusb_get_device_descriptor(device, &descriptor) != LIBUSB_SUCCESS) return false;

    output->vendor_id = descriptor.idVendor;
    output->product_id = descriptor.idProduct;
    output->device_class = descriptor.bDeviceClass;
    output->device_subclass = descriptor.bDeviceSubClass;
    output->device_protocol = descriptor.bDeviceProtocol;
    output->busnum_devnum = std::to_string(libusb_get_bus_number(device)) + ":" +
        std::to_string(libusb_get_device_address(device));
    output->speed = ParseLibusbSpeed(libusb_get_device_speed(device));

    libusb_device_handle* handle = nullptr;
    if (libusb_open(device, &handle) == LIBUSB_SUCCESS) {
        output->manufacturer = ReadUsbString(handle, descriptor.iManufacturer);
        output->product = ReadUsbString(handle, descriptor.iProduct);
        output->serial_number = ReadUsbString(handle, descriptor.iSerialNumber);
        libusb_close(handle);
    }
    return true;
}
#endif

// Read a single-line sysfs attribute; returns empty string on failure.
#if defined(__linux__)
static std::string ReadSysfsAttr(const std::string& dev_path,
                                  const std::string& attr) {
    std::ifstream f(dev_path + "/" + attr);
    if (!f.good()) return {};
    std::string val;
    std::getline(f, val);
    return val;
}

static uint16_t ParseHex16(const std::string& s) {
    if (s.empty()) return 0;
    unsigned long v = 0;
    try { v = std::stoul(s, nullptr, 16); } catch (...) {}
    return static_cast<uint16_t>(v);
}

static uint8_t ParseDec8(const std::string& s) {
    if (s.empty()) return 0;
    unsigned long v = 0;
    try { v = std::stoul(s); } catch (...) {}
    return static_cast<uint8_t>(v);
}

static UsbSpeed ParseSpeed(const std::string& s) {
    if (s == "1.5")   return UsbSpeed::kLow;
    if (s == "12")    return UsbSpeed::kFull;
    if (s == "480")   return UsbSpeed::kHigh;
    if (s == "5000")  return UsbSpeed::kSuper;
    if (s == "10000") return UsbSpeed::kSuperPlus;
    return UsbSpeed::kUnknown;
}

static UsbDevice ReadDevice(const std::string& dev_path) {
    UsbDevice dev;
    dev.sysfs_path   = dev_path;
    dev.vendor_id    = ParseHex16(ReadSysfsAttr(dev_path, "idVendor"));
    dev.product_id   = ParseHex16(ReadSysfsAttr(dev_path, "idProduct"));
    dev.device_class    = ParseDec8(ReadSysfsAttr(dev_path, "bDeviceClass"));
    dev.device_subclass = ParseDec8(ReadSysfsAttr(dev_path, "bDeviceSubClass"));
    dev.device_protocol = ParseDec8(ReadSysfsAttr(dev_path, "bDeviceProtocol"));
    dev.manufacturer = ReadSysfsAttr(dev_path, "manufacturer");
    dev.product      = ReadSysfsAttr(dev_path, "product");
    dev.serial_number = ReadSysfsAttr(dev_path, "serial");
    dev.speed = ParseSpeed(ReadSysfsAttr(dev_path, "speed"));

    const std::string bus  = ReadSysfsAttr(dev_path, "busnum");
    const std::string dnum = ReadSysfsAttr(dev_path, "devnum");
    if (!bus.empty() && !dnum.empty())
        dev.busnum_devnum = bus + ":" + dnum;

    return dev;
}
#endif  // __linux__

#if defined(_WIN32)
// ── Windows USB enumeration via SetupAPI ────────────────────────────────────

std::string WideToUtf8(const WCHAR* w, DWORD chars) {
    if (!w || chars == 0) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(chars),
                                      nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(chars), out.data(), n,
                        nullptr, nullptr);
    return out;
}

// Read a string registry property (SPDRP_*) from a device info set.
std::string ReadDeviceStringProp(HDEVINFO set, SP_DEVINFO_DATA* data,
                                 DWORD prop) {
    WCHAR buf[512]{};
    DWORD type = 0, size = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(set, data, prop, &type,
                                           reinterpret_cast<PBYTE>(buf),
                                           sizeof(buf), &size)) {
        return {};
    }
    return WideToUtf8(buf, size / sizeof(WCHAR));
}

// Parse "VID_XXXX" / "PID_XXXX" tokens out of a USB instance ID such as
// "USB\VID_046D&PID_C52B\6&2F4A&0&3".
void ParseVidPid(const std::string& instance_id, uint16_t* vid,
                 uint16_t* pid) {
    const auto parse_hex = [](const char* p) -> uint16_t {
        if (!p) return 0;
        return static_cast<uint16_t>(std::strtoul(p, nullptr, 16));
    };
    const char* v = std::strstr(instance_id.c_str(), "VID_");
    const char* p = std::strstr(instance_id.c_str(), "PID_");
    if (v) *vid = parse_hex(v + 4);
    if (p) *pid = parse_hex(p + 4);
}

// Instance path tail after the last '\\' — closest thing to sysfs "bus:dev".
std::string InstanceTail(const std::string& instance_id) {
    const size_t pos = instance_id.find_last_of('\\');
    return pos == std::string::npos ? instance_id
                                    : instance_id.substr(pos + 1);
}
#endif  // _WIN32

}  // namespace

// ──────────────────────────────────────────────────────────────────────────────

UsbEnumerator& UsbEnumerator::Instance() {
    static UsbEnumerator instance;
    return instance;
}

std::vector<UsbDevice> UsbEnumerator::EnumerateDevices() const {
    std::vector<UsbDevice> result;
#if ENGINE_HAS_LIBUSB
    libusb_context* context = nullptr;
    if (libusb_init(&context) == LIBUSB_SUCCESS) {
        libusb_device** devices = nullptr;
        const ssize_t count = libusb_get_device_list(context, &devices);
        if (count >= 0) {
            for (ssize_t index = 0; index < count; ++index) {
                UsbDevice device{};
                if (ReadLibusbDevice(devices[index], &device)) result.push_back(std::move(device));
            }
            libusb_free_device_list(devices, 1);
            libusb_exit(context);
            return result;
        }
        libusb_exit(context);
    }
#endif
#if defined(__linux__)
    const char* kSysfsBase = "/sys/bus/usb/devices";
    DIR* dir = opendir(kSysfsBase);
    if (!dir) return result;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string name = entry->d_name;
        // USB device directory names look like "1-1", "1-1.2", "2-0:1.0", etc.
        // Skip host controllers (usb1, usb2 …) and interface entries (containing ':').
        if (name.empty() || name[0] == '.') continue;
        if (name.find(':') != std::string::npos) continue; // interface descriptor
        const std::string path = std::string(kSysfsBase) + "/" + name;
        UsbDevice dev = ReadDevice(path);
        if (dev.vendor_id != 0 || dev.product_id != 0)
            result.push_back(std::move(dev));
    }
    closedir(dir);
#endif
#if defined(_WIN32)
    HDEVINFO set = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_USB_DEVICE, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return result;

    SP_DEVICE_INTERFACE_DATA if_data{};
    if_data.cbSize = sizeof(if_data);
    for (DWORD i = 0;
         SetupDiEnumDeviceInterfaces(set, nullptr, &GUID_DEVINTERFACE_USB_DEVICE,
                                     i, &if_data);
         ++i) {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &if_data, nullptr, 0, &needed,
                                         nullptr);
        if (needed == 0) continue;
        std::vector<BYTE> buf(needed);
        auto* detail =
            reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        SP_DEVINFO_DATA dev_data{};
        dev_data.cbSize = sizeof(dev_data);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &if_data, detail, needed,
                                              nullptr, &dev_data)) {
            continue;
        }

        UsbDevice dev;
        dev.sysfs_path = WideToUtf8(detail->DevicePath, MAX_PATH);

        char instance_id[MAX_DEVICE_ID_LEN]{};
        if (SetupDiGetDeviceInstanceIdA(set, &dev_data, instance_id,
                                        sizeof(instance_id), nullptr)) {
            const std::string id(instance_id);
            ParseVidPid(id, &dev.vendor_id, &dev.product_id);
            dev.busnum_devnum = InstanceTail(id);
        }

        dev.product = ReadDeviceStringProp(set, &dev_data, SPDRP_FRIENDLYNAME);
        if (dev.product.empty()) {
            dev.product = ReadDeviceStringProp(set, &dev_data, SPDRP_DEVICEDESC);
        }
        dev.manufacturer = ReadDeviceStringProp(set, &dev_data, SPDRP_MFG);

        // Class/subclass/protocol and speed are only available via device
        // descriptors (libusb/WinUSB); left at defaults on Windows.
        if (dev.IsValid()) result.push_back(std::move(dev));
    }
    SetupDiDestroyDeviceInfoList(set);
#endif
    return result;
}

void UsbEnumerator::SetHotplugCallback(HotplugCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    hotplug_cb_ = std::move(callback);
}

bool UsbEnumerator::StartMonitoring() {
    if (monitoring_.load()) return true;
    StopMonitoring();
#if ENGINE_HAS_LIBUSB
    if (!libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) return false;
    if (libusb_init(&monitor_context_) != LIBUSB_SUCCESS) {
        monitor_context_ = nullptr;
        return false;
    }

    const int registration = libusb_hotplug_register_callback(
        monitor_context_,
        static_cast<libusb_hotplug_event>(LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
                                          LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT),
        LIBUSB_HOTPLUG_ENUMERATE,
        LIBUSB_HOTPLUG_MATCH_ANY,
        LIBUSB_HOTPLUG_MATCH_ANY,
        LIBUSB_HOTPLUG_MATCH_ANY,
        &UsbEnumerator::HotplugThunk,
        this,
        &hotplug_callback_handle_);
    if (registration != LIBUSB_SUCCESS) {
        libusb_exit(monitor_context_);
        monitor_context_ = nullptr;
        hotplug_callback_handle_ = -1;
        return false;
    }

    monitoring_.store(true);
    try {
        monitor_thread_ = std::thread([this] {
            while (monitoring_.load()) {
                timeval timeout{0, 250000};
                const int result = libusb_handle_events_timeout(monitor_context_, &timeout);
                if (result < 0 && result != LIBUSB_ERROR_INTERRUPTED) break;
            }
            monitoring_.store(false);
        });
    } catch (...) {
        monitoring_.store(false);
        libusb_hotplug_deregister_callback(monitor_context_, hotplug_callback_handle_);
        libusb_exit(monitor_context_);
        monitor_context_ = nullptr;
        hotplug_callback_handle_ = -1;
        return false;
    }
    return true;
#else
    return false;
#endif
}

void UsbEnumerator::StopMonitoring() {
    monitoring_.store(false);
    if (monitor_thread_.joinable()) monitor_thread_.join();
#if ENGINE_HAS_LIBUSB
    if (monitor_context_) {
        if (hotplug_callback_handle_ >= 0)
            libusb_hotplug_deregister_callback(monitor_context_, hotplug_callback_handle_);
        libusb_exit(monitor_context_);
        monitor_context_ = nullptr;
        hotplug_callback_handle_ = -1;
    }
#endif
}

UsbEnumerator::~UsbEnumerator() {
    StopMonitoring();
}

int UsbEnumerator::HotplugThunk(libusb_context*, libusb_device* device, int event, void* user_data) {
#if ENGINE_HAS_LIBUSB
    auto* enumerator = static_cast<UsbEnumerator*>(user_data);
    UsbDevice info{};
    if (!ReadLibusbDevice(device, &info)) return 0;

    HotplugCallback callback;
    {
        std::lock_guard<std::mutex> lock(enumerator->callback_mutex_);
        callback = enumerator->hotplug_cb_;
    }
    if (callback) callback(info, event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED);
#else
    (void)device;
    (void)event;
    (void)user_data;
#endif
    return 0;
}

}  // namespace device::usb
