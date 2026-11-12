/**
 * This file is part of XER, Device open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <memory>
#include <string>

// Sub-system includes — all device categories accessible through one facade.
#include "content/device/bluetooth/bluetooth_adapter.h"
#include "content/device/bluetooth/bluetooth_gatt.h"
#include "content/device/usb/usb_enumerator.h"
#include "content/device/wifi/wifi_scanner.h"
#include "content/device/wifi/wifi_connection_manager.h"
#include "content/device/vr/vr_session.h"
#include "content/device/vr/vr_input.h"
#include "content/device/cast/aura/aura_cast_session.h"
#include "content/device/cast/mira/mira_cast_session.h"

// Project infrastructure used for telemetry.
#include "async_io/hardware_abstraction/memory_monitor.h"
#include "async_io/log_and_debug/io_perf_counter.h"
#include "async_io/io_thread_pool.h"

namespace device {

// DeviceManager is the top-level singleton that exposes every device subsystem
// and integrates with the XER async/hardware infrastructure.
//
// Typical usage:
//   DeviceManager& dm = DeviceManager::Instance();
//   dm.Initialize();
//
//   // Bluetooth
//   auto& bt = dm.Bluetooth();
//   bt.StartScan(3000, [](const bluetooth::BluetoothDevice& d){ … });
//
//   // USB
//   auto devices = dm.Usb().EnumerateDevices();
//
//   // Wi-Fi
//   dm.Wifi().ScanAsync("wlan0", [](auto nets){ … });
//
//   // VR
//   if (dm.Vr().Initialize()) { … }
//
//   // Cast
//   auto sinks = dm.AuraCast().DiscoverDevices(3000);
class DeviceManager {
public:
    static DeviceManager& Instance();

    // Initialize all subsystems.  Safe to call multiple times.
    void Initialize();
    void Shutdown();
    bool IsInitialized() const { return initialized_; }

    // ── Accessors ────────────────────────────────────────────────────────────
    bluetooth::BluetoothAdapter&    Bluetooth()    { return bluetooth::BluetoothAdapter::Instance(); }
    bluetooth::BluetoothGattClient& Gatt()         { return bluetooth::BluetoothGattClient::Instance(); }
    usb::UsbEnumerator&             Usb()          { return usb::UsbEnumerator::Instance(); }
    wifi::WifiScanner&              WifiScan()     { return wifi::WifiScanner::Instance(); }
    wifi::WifiConnectionManager&    Wifi()         { return wifi::WifiConnectionManager::Instance(); }
    vr::VrSession&                  Vr()           { return vr::VrSession::Instance(); }
    vr::VrInput&                    VrInput()      { return vr::VrInput::Instance(); }
    cast::aura::AuraCastSession&    AuraCast()     { return cast::aura::AuraCastSession::Instance(); }
    cast::mira::MiraCastSession&    MiraCast()     { return cast::mira::MiraCastSession::Instance(); }

    // ── Infrastructure ───────────────────────────────────────────────────────
    // Returns a snapshot of the system memory used by device subsystems.
    XER::AsyncIO::IO::Hardware::MemoryStats  GetMemoryStats() const;

    // Returns a perf report string (Bluetooth scan timing, USB enum timing…).
    std::string GetPerfReport() const;

    // Async helper: posts a task on the shared IO thread pool.
    void PostTask(std::function<void()> task);

private:
    DeviceManager() = default;
    bool initialized_ = false;
    XER::AsyncIO::IO::Hardware::MemoryMonitor  mem_monitor_;
    XER::AsyncIO::IO::LogDebug::PerformanceCounter perf_;
};

}  // namespace device
