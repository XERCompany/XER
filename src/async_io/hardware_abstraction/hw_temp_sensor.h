#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>

namespace XER::AsyncIO::IO::Hardware {

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

// Temperature monitoring with optional background refresh and rising-edge
// alert callbacks.
//
// Thread-safety contract:
//   - GetAllReadings()/GetReading()/Get*Temperature()/NeedsAlert() are const
//     and safe from any thread; they take an internal mutex.
//   - Initialize()/Refresh()/StartMonitoring()/StopMonitoring()/
//     SetTemperatureAlert()/SetAlertCallback() are safe from any thread.
//   - StartMonitoring() spawns a background thread that periodically calls
//     UpdateReadings(). The alert callback (if set) is invoked from that
//     thread; it must not call back into the TemperatureSensor
//     (reentrancy is undefined — e.g. calling StopMonitoring() from the
//     callback would deadlock joining the monitor thread).
//   - After StopMonitoring() (or destruction), no more callbacks fire.
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

    // Register a callback invoked from the monitor thread whenever any
    // supported reading crosses the alert threshold (rising edge only —
    // the callback fires once per crossing, not on every poll while above).
    // Pass nullptr to clear. The callback must not call back into the
    // sensor (it runs on the monitor thread; reentrancy is undefined).
    using AlertCallback = std::function<void(const TemperatureReading&)>;
    void SetAlertCallback(AlertCallback cb);

private:
    mutable std::mutex mutex_;
    std::vector<TemperatureReading> readings_;
    std::atomic<bool> initialized_;
    std::atomic<bool> is_monitoring_;
    std::atomic<double> alert_threshold_;
    std::thread monitor_thread_;
    int monitor_interval_ms_ = 2000;

    // Alert callback + per-sensor "already alerted" state (rising-edge).
    mutable std::mutex callback_mutex_;
    AlertCallback alert_callback_;
    std::vector<bool> alerted_per_sensor_;

    void UpdateReadings();
    void MonitorLoop(int interval_ms);
};

}  // namespace XER::AsyncIO::IO::Hardware
