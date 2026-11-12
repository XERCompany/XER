/**
 * This file is part of XER, Device open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/device/device_manager.h"

namespace device {

DeviceManager& DeviceManager::Instance() {
    static DeviceManager inst;
    return inst;
}

void DeviceManager::Initialize() {
    if (initialized_) return;

    perf_.Enable();

    // ── USB ────────────────────────────────────────────────────────────────
    perf_.StartCounter("usb_init");
    Usb().StartMonitoring();
    perf_.StopCounter("usb_init");

    // ── Wi-Fi (background) ────────────────────────────────────────────────
    // Initial scan on the thread pool; result discarded here (callers poll).
    Wifi().ScanAsync("wlan0", [](auto) {});

    // ── Memory monitor ────────────────────────────────────────────────────
    mem_monitor_.StartMonitoring();

    initialized_ = true;
}

void DeviceManager::Shutdown() {
    if (!initialized_) return;
    Usb().StopMonitoring();
    mem_monitor_.StopMonitoring();
    if (Vr().IsInitialized())       Vr().Shutdown();
    if (VrInput().IsInitialized())  VrInput().Shutdown();
    AuraCast().Disconnect();
    MiraCast().Disconnect();
    initialized_ = false;
}

XER::AsyncIO::IO::Hardware::MemoryStats DeviceManager::GetMemoryStats() const {
    return mem_monitor_.GetMemoryStats();
}

std::string DeviceManager::GetPerfReport() const {
    return perf_.GetReport();
}

void DeviceManager::PostTask(std::function<void()> task) {
    XER::AsyncIO::IO::Async::IOThreadPool::GetSharedInstance().Enqueue(std::move(task));
}

}  // namespace device
