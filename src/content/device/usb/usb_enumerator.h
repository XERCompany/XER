/**
 * This file is part of XER, Device open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "content/device/usb/usb_device.h"

struct libusb_context;
struct libusb_device;

namespace device::usb {

using HotplugCallback = std::function<void(const UsbDevice& dev, bool attached)>;

// Enumerates USB devices through libusb, with Linux sysfs as a fallback.
class UsbEnumerator {
public:
    static UsbEnumerator& Instance();
    ~UsbEnumerator();

    // Returns all currently connected USB devices.
    std::vector<UsbDevice> EnumerateDevices() const;

    // Register a callback to be invoked when a device is attached or removed.
    // Returns false when the available backend cannot monitor hotplug events.
    void SetHotplugCallback(HotplugCallback callback);

    // Start background hotplug monitoring (non-blocking).
    bool StartMonitoring();
    void StopMonitoring();

private:
    UsbEnumerator() = default;
    static int HotplugThunk(libusb_context* context, libusb_device* device, int event, void* user_data);
    HotplugCallback hotplug_cb_;
    std::mutex callback_mutex_;
    std::atomic<bool> monitoring_{false};
    std::thread monitor_thread_;
    libusb_context* monitor_context_ = nullptr;
    int hotplug_callback_handle_ = -1;
};

}  // namespace device::usb
