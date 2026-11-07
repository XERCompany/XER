#include "hw_temp_sensor.h"

#include "nvml_loader.h"

#include <chrono>
#include <algorithm>

#if defined(_WIN32)
    #include <windows.h>
    // WMI for ACPI thermal zones (best-effort CPU temperature on Windows).
    #include <objbase.h>
    #include <wbemidl.h>
#elif defined(__linux__)
    #include <fstream>
#endif

namespace XER::AsyncIO::IO::Hardware {

namespace {

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

#if defined(_WIN32)

// Query MSAcpi_ThermalZoneTemperature via WMI (root\WMI namespace).
// Values are in tenths of Kelvin; convert to Celsius: C = (val/10) - 273.15.
// This is best-effort: the class may be absent or report a single generic
// zone rather than per-core temps. Unsupported zones are skipped.
std::vector<TemperatureReading> QueryWmiThermalZonesWin() {
    std::vector<TemperatureReading> out;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool must_uninit = (hr == S_OK || hr == S_FALSE);
    if (hr == RPC_E_CHANGED_MODE) {
        // Thread already has a different apartment; we cannot re-init, but
        // we can still use COM that's already up. Proceed without uninit.
    } else if (FAILED(hr)) {
        return out;
    }

    IWbemLocator* locator = nullptr;
    hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                         IID_IWbemLocator, reinterpret_cast<void**>(&locator));
    if (FAILED(hr) || !locator) {
        if (must_uninit) CoUninitialize();
        return out;
    }

    IWbemServices* services = nullptr;
    hr = locator->ConnectServer(SysAllocString(L"ROOT\\WMI"), nullptr, nullptr, 0,
                                0, 0, nullptr, &services);
    locator->Release();
    if (FAILED(hr) || !services) {
        if (must_uninit) CoUninitialize();
        return out;
    }

    CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                      RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                      nullptr, EOAC_NONE);

    IEnumWbemClassObject* enumerator = nullptr;
    hr = services->ExecQuery(
        SysAllocString(L"WQL"),
        SysAllocString(L"SELECT * FROM MSAcpi_ThermalZoneTemperature"),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &enumerator);
    if (FAILED(hr) || !enumerator) {
        services->Release();
        if (must_uninit) CoUninitialize();
        return out;
    }

    const int64_t now = NowMs();
    for (;;) {
        IWbemClassObject* obj = nullptr;
        ULONG returned = 0;
        hr = enumerator->Next(WBEM_INFINITE, 1, &obj, &returned);
        if (hr != S_OK || returned == 0) {
            if (obj) obj->Release();
            break;
        }

        VARIANT vt_name, vt_temp, vt_crit, vt_active;
        VariantInit(&vt_name);  VariantInit(&vt_temp);
        VariantInit(&vt_crit);  VariantInit(&vt_active);

        TemperatureReading r{};
        r.source = "WMI/ACPI";
        r.timestamp_ms = now;
        r.temperature_critical = 105.0;  // conservative default
        r.temperature_slowdown = 95.0;

        if (SUCCEEDED(obj->Get(L"_InstanceName", 0, &vt_name, nullptr, nullptr)) &&
            vt_name.vt == VT_BSTR && vt_name.bstrVal) {
            // Convert BSTR to narrow string (best-effort, ASCII subset).
            // We narrow each wchar_t individually rather than assign(begin,end)
            // to avoid silent narrowing via std::string's template ctor.
            std::wstring w(vt_name.bstrVal);
            std::string narrow;
            narrow.reserve(w.size());
            for (wchar_t wc : w) narrow.push_back(static_cast<char>(wc < 128 ? wc : L'?'));
            r.sensor_name = std::move(narrow);
        }
        if (SUCCEEDED(obj->Get(L"_Temperature", 0, &vt_temp, nullptr, nullptr)) &&
            (vt_temp.vt == VT_UI4 || vt_temp.vt == VT_I4)) {
            const uint32_t tenths_k = (vt_temp.vt == VT_UI4) ? vt_temp.ulVal
                                                            : static_cast<uint32_t>(vt_temp.lVal);
            if (tenths_k > 0) {
                r.temperature_celsius = (static_cast<double>(tenths_k) / 10.0) - 273.15;
                r.is_supported = true;
            }
        }
        if (SUCCEEDED(obj->Get(L"_CriticalThreshold", 0, &vt_crit, nullptr, nullptr)) &&
            (vt_crit.vt == VT_UI4 || vt_crit.vt == VT_I4)) {
            const uint32_t tenths_k = (vt_crit.vt == VT_UI4) ? vt_crit.ulVal
                                                            : static_cast<uint32_t>(vt_crit.lVal);
            if (tenths_k > 0) {
                r.temperature_critical = (static_cast<double>(tenths_k) / 10.0) - 273.15;
            }
        }
        if (SUCCEEDED(obj->Get(L"_ActiveThreshold", 0, &vt_active, nullptr, nullptr)) &&
            (vt_active.vt == VT_UI4 || vt_active.vt == VT_I4)) {
            const uint32_t tenths_k = (vt_active.vt == VT_UI4) ? vt_active.ulVal
                                                              : static_cast<uint32_t>(vt_active.lVal);
            if (tenths_k > 0) {
                r.temperature_slowdown = (static_cast<double>(tenths_k) / 10.0) - 273.15;
            }
        }

        VariantClear(&vt_name); VariantClear(&vt_temp);
        VariantClear(&vt_crit); VariantClear(&vt_active);

        // Sanity: ACPI zones occasionally report implausible values (e.g. <0 or
        // >200). Skip obvious garbage.
        if (r.is_supported &&
            r.temperature_celsius >= -40.0 && r.temperature_celsius <= 200.0) {
            r.is_critical = r.temperature_celsius >= r.temperature_critical;
            if (r.sensor_name.empty()) r.sensor_name = "ACPI_TZ";
            out.push_back(std::move(r));
        }

        obj->Release();
    }

    enumerator->Release();
    services->Release();
    if (must_uninit) CoUninitialize();
    return out;
}

#endif  // _WIN32

// GPU temperature readings from NVML (Windows + Linux).
std::vector<TemperatureReading> GpuReadingsNvml() {
    std::vector<TemperatureReading> out;
    auto& nvml = NvmlLibrary::Instance();
    if (!nvml.IsAvailable()) return out;

    const auto snapshots = nvml.GetSnapshots();
    const int64_t now = NowMs();
    for (const auto& s : snapshots) {
        TemperatureReading r{};
        r.sensor_name = "GPU" + std::to_string(s.index);
        r.source = "NVML";
        r.timestamp_ms = now;
        r.temperature_celsius = s.temp_celsius;
        r.is_supported = s.temp_supported;
        // Prefer shutdown as the "critical" threshold; fall back to slowdown.
        r.temperature_critical = (s.temp_shutdown_c > 0.0) ? s.temp_shutdown_c
                                                            : s.temp_slowdown_c;
        r.temperature_slowdown = s.temp_slowdown_c;
        r.is_critical = r.is_supported && r.temperature_celsius >= r.temperature_critical;
        out.push_back(std::move(r));
    }
    return out;
}

}  // namespace

TemperatureSensor::TemperatureSensor()
    : initialized_(false), is_monitoring_(false), alert_threshold_(90.0) {}

TemperatureSensor::~TemperatureSensor() {
    StopMonitoring();
}

bool TemperatureSensor::Initialize() {
    UpdateReadings();
    initialized_.store(true, std::memory_order_release);
    return true;  // Always "initialized" — callers should check per-reading is_supported.
}

bool TemperatureSensor::IsInitialized() const {
    return initialized_.load(std::memory_order_acquire);
}

int TemperatureSensor::GetSensorCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(readings_.size());
}

std::vector<TemperatureReading> TemperatureSensor::GetAllReadings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return readings_;
}

TemperatureReading TemperatureSensor::GetReading(int sensor_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sensor_id >= 0 && sensor_id < static_cast<int>(readings_.size())) {
        return readings_[sensor_id];
    }
    return TemperatureReading{};
}

double TemperatureSensor::GetMaxTemperature() const {
    std::lock_guard<std::mutex> lock(mutex_);
    double max_temp = 0.0;
    bool any = false;
    for (const auto& r : readings_) {
        if (r.is_supported && r.temperature_celsius > max_temp) {
            max_temp = r.temperature_celsius;
            any = true;
        }
    }
    return any ? max_temp : 0.0;
}

double TemperatureSensor::GetAverageTemperature() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (readings_.empty()) return 0.0;

    double sum = 0.0;
    int count = 0;
    for (const auto& r : readings_) {
        if (r.is_supported) {
            sum += r.temperature_celsius;
            ++count;
        }
    }
    return count > 0 ? (sum / count) : 0.0;
}

double TemperatureSensor::GetCPUTemperature() const {
    std::lock_guard<std::mutex> lock(mutex_);
    // CPU readings come from sysfs (linux) or WMI/ACPI (windows) and are
    // labelled with "CPU"/"Core"/ACPI instance names.
    for (const auto& r : readings_) {
        if (!r.is_supported) continue;
        if (r.sensor_name.find("CPU") != std::string::npos ||
            r.sensor_name.find("core") != std::string::npos ||
            r.sensor_name.find("Core") != std::string::npos ||
            r.source == "WMI/ACPI") {
            return r.temperature_celsius;
        }
    }
    return 0.0;
}

double TemperatureSensor::GetCPUCriticalTemperature() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& r : readings_) {
        if (r.sensor_name.find("CPU") != std::string::npos ||
            r.sensor_name.find("core") != std::string::npos ||
            r.sensor_name.find("Core") != std::string::npos ||
            r.source == "WMI/ACPI") {
            return r.temperature_critical;
        }
    }
    return 100.0;
}

double TemperatureSensor::GetGPUTemperature(int device_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string pattern = "GPU" + std::to_string(device_id);
    for (const auto& r : readings_) {
        if (r.sensor_name == pattern) {
            return r.temperature_celsius;
        }
    }
    return 0.0;
}

double TemperatureSensor::GetGPUCriticalTemperature(int device_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string pattern = "GPU" + std::to_string(device_id);
    for (const auto& r : readings_) {
        if (r.sensor_name == pattern) {
            return r.temperature_critical;
        }
    }
    return 100.0;
}

bool TemperatureSensor::StartMonitoring(int interval_ms) {
    bool expected = false;
    if (!is_monitoring_.compare_exchange_strong(expected, true)) {
        return false;  // Already running
    }
    monitor_interval_ms_ = (interval_ms > 0) ? interval_ms : 2000;
    if (monitor_thread_.joinable()) monitor_thread_.join();
    monitor_thread_ = std::thread(&TemperatureSensor::MonitorLoop, this, monitor_interval_ms_);
    return true;
}

bool TemperatureSensor::StopMonitoring() {
    if (!is_monitoring_.exchange(false)) {
        return false;
    }
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    return true;
}

void TemperatureSensor::Refresh() {
    UpdateReadings();
}

void TemperatureSensor::SetTemperatureAlert(double threshold_celsius) {
    alert_threshold_.store(threshold_celsius, std::memory_order_relaxed);
}

bool TemperatureSensor::NeedsAlert() const {
    return GetMaxTemperature() >= alert_threshold_.load(std::memory_order_relaxed);
}

void TemperatureSensor::SetAlertCallback(AlertCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    alert_callback_ = std::move(cb);
}

void TemperatureSensor::MonitorLoop(int interval_ms) {
    while (is_monitoring_.load(std::memory_order_relaxed)) {
        UpdateReadings();

        // Rising-edge alert detection. We fire the callback once when a
        // sensor crosses the threshold (low->high), and re-arm it only when
        // it drops back below. This avoids calling the callback on every
        // poll while above the threshold.
        {
            std::vector<TemperatureReading> snapshot;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                snapshot = readings_;
            }
            const double threshold = alert_threshold_.load(std::memory_order_relaxed);

            // Resize the per-sensor "already alerted" vector to match the
            // current sensor count (sensors can appear/disappear between polls).
            std::vector<bool> now_alerted;
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                now_alerted = alerted_per_sensor_;
            }
            now_alerted.resize(snapshot.size(), false);

            AlertCallback cb;
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                cb = alert_callback_;
            }

            bool state_changed = false;
            for (size_t i = 0; i < snapshot.size(); ++i) {
                if (!snapshot[i].is_supported) {
                    now_alerted[i] = false;
                    continue;
                }
                const bool above = snapshot[i].temperature_celsius >= threshold;
                if (above && !now_alerted[i]) {
                    now_alerted[i] = true;
                    state_changed = true;
                    if (cb) cb(snapshot[i]);
                } else if (!above && now_alerted[i]) {
                    now_alerted[i] = false;
                    state_changed = true;
                }
            }
            if (state_changed) {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                alerted_per_sensor_ = std::move(now_alerted);
            }
        }

        // Sleep in small increments so StopMonitoring responds promptly.
        const int step = 100;
        int remaining = interval_ms;
        while (remaining > 0 && is_monitoring_.load(std::memory_order_relaxed)) {
            const int s = (remaining < step) ? remaining : step;
            std::this_thread::sleep_for(std::chrono::milliseconds(s));
            remaining -= s;
        }
    }
}

void TemperatureSensor::UpdateReadings() {
    std::vector<TemperatureReading> next;
    const int64_t now = NowMs();

#if defined(__linux__)
    // Enumerate /sys/class/thermal/thermal_zoneN
    for (int zone = 0; zone < 32; ++zone) {
        const std::string base = "/sys/class/thermal/thermal_zone" + std::to_string(zone);

        std::ifstream type_f(base + "/type");
        if (!type_f.is_open()) break;  // no more zones

        std::string sensor_type;
        std::getline(type_f, sensor_type);

        std::ifstream temp_f(base + "/temp");
        if (!temp_f.is_open()) continue;
        int64_t raw_millideg = 0;
        temp_f >> raw_millideg;
        const double celsius = static_cast<double>(raw_millideg) / 1000.0;

        double critical = 105.0, slowdown = 95.0;
        for (int tp = 0; tp < 8; ++tp) {
            std::ifstream tt(base + "/trip_point_" + std::to_string(tp) + "_type");
            if (!tt.is_open()) break;
            std::string ttype;
            std::getline(tt, ttype);
            std::ifstream tv(base + "/trip_point_" + std::to_string(tp) + "_temp");
            if (!tv.is_open()) continue;
            int64_t crit_raw = 0;
            tv >> crit_raw;
            const double c = static_cast<double>(crit_raw) / 1000.0;
            if (ttype == "critical") critical = c;
            else if (ttype == "hot" || ttype == "passive") slowdown = c;
        }

        TemperatureReading r;
        r.sensor_name = sensor_type;
        r.source = "sysfs";
        r.temperature_celsius = celsius;
        r.temperature_critical = critical;
        r.temperature_slowdown = slowdown;
        r.is_critical = celsius >= critical;
        r.is_supported = true;
        r.timestamp_ms = now;
        next.push_back(r);
    }

    // Append NVIDIA GPU readings (NVML).
    {
        auto gpu = GpuReadingsNvml();
        for (auto& r : gpu) next.push_back(std::move(r));
    }

#elif defined(_WIN32)
    // WMI/ACPI thermal zones (CPU, best-effort).
    {
        auto zones = QueryWmiThermalZonesWin();
        for (auto& r : zones) next.push_back(std::move(r));
    }
    // NVIDIA GPU temperatures via NVML.
    {
        auto gpu = GpuReadingsNvml();
        for (auto& r : gpu) next.push_back(std::move(r));
    }
    // If nothing was found at all, emit a single clearly-unsupported CPU
    // placeholder so callers see an honest "unsupported" rather than silence.
    if (next.empty()) {
        TemperatureReading r;
        r.sensor_name = "CPU";
        r.source = "unsupported";
        r.temperature_celsius = 0.0;
        r.temperature_critical = 100.0;
        r.is_critical = false;
        r.is_supported = false;
        r.timestamp_ms = now;
        next.push_back(r);
    }
#else
    TemperatureReading r;
    r.sensor_name = "CPU";
    r.source = "unsupported";
    r.temperature_celsius = 0.0;
    r.temperature_critical = 100.0;
    r.is_critical = false;
    r.is_supported = false;
    r.timestamp_ms = now;
    next.push_back(r);
#endif

    std::lock_guard<std::mutex> lock(mutex_);
    readings_ = std::move(next);
}

}  // namespace XER::AsyncIO::IO::Hardware
