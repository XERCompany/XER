#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <atomic>
#include <thread>
#include <mutex>

namespace AsyncIO::IO::Hardware {

// Temperature sensor reading.
struct TemperatureReading {
    std::string sensor_name;          // Human-friendly label (e.g. "GPU0", "CPU", "acpitz")
    std::string source;              // Backend that produced the value: "NVML", "sysfs", "WMI", "ACPI"
    double temperature_celsius;      // Current temperature (0.0 if unsupported)
    double temperature_critical;     // Critical/shutdown threshold (°C)
    double temperature_slowdown;     // Throttle/slowdown threshold (°C), 0 if unknown
    bool is_critical;                // temperature_celsius >= temperature_critical
    bool is_supported;               // False when no real reading is available for this sensor
    int64_t timestamp_ms;            // Reading time (steady_clock epoch), 0 if N/A
};

class TemperatureSensor {
public:
    TemperatureSensor();
    ~TemperatureSensor();

    // Discovery
    bool Initialize();
    bool IsInitialized() const;
    int GetSensorCount() const;

    // Readings (snapshot of the last UpdateReadings() pass)
    std::vector<TemperatureReading> GetAllReadings() const;
    TemperatureReading GetReading(int sensor_id) const;
    double GetMaxTemperature() const;       // Max of supported readings
    double GetAverageTemperature() const;   // Avg of supported readings

    // CPU temperature
    double GetCPUTemperature() const;
    double GetCPUCriticalTemperature() const;

    // GPU temperature (NVIDIA via NVML on Windows/Linux; 0 if unsupported)
    double GetGPUTemperature(int device_id = 0) const;
    double GetGPUCriticalTemperature(int device_id = 0) const;

    // Monitoring. StartMonitoring spawns a background thread that refreshes
    // readings every interval_ms (default 2000ms). StopMonitoring joins it.
    bool StartMonitoring(int interval_ms = 2000);
    bool StopMonitoring();
    bool IsMonitoring() const { return is_monitoring_.load(std::memory_order_relaxed); }

    // Force a refresh now (also called by the monitor thread).
    void Refresh();

    // Alerts
    void SetTemperatureAlert(double threshold_celsius);
    bool NeedsAlert() const;

private:
    mutable std::mutex mutex_;
    std::vector<TemperatureReading> readings_;
    std::atomic<bool> initialized_;
    std::atomic<bool> is_monitoring_;
    std::atomic<double> alert_threshold_;
    std::thread monitor_thread_;
    int monitor_interval_ms_ = 2000;

    void UpdateReadings();
    void MonitorLoop(int interval_ms);
};

}  // namespace AsyncIO::IO::Hardware
