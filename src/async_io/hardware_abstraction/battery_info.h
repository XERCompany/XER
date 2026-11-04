#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace AsyncIO::IO::Hardware {

enum class BatteryState {
    Unknown,
    Discharging,    // On battery, draining
    Charging,        // Plugged in, charging
    Full,            // Plugged in, fully charged
    NotPresent,      // No battery installed
    Critical,        // Discharging, below critical threshold
};

struct BatteryInfo {
    int id = 0;                       // Device index (0-based)
    std::string name;                 // "Battery 0", "BAT0", etc.
    bool present = false;

    double percent = 0.0;             // 0..100
    BatteryState state = BatteryState::Unknown;
    std::string state_name;           // Human-readable

    int64_t estimated_seconds_remaining = -1;  // -1 if unknown / on AC
    double voltage_volts = 0.0;               // 0 if unknown
    double design_capacity_wh = 0.0;          // 0 if unknown
    double full_charged_capacity_wh = 0.0;    // 0 if unknown (may be < design = wear)
    double current_capacity_wh = 0.0;

    // Health: full_charged / design (0..1). <0.8 means significant wear.
    double health_fraction = 0.0;

    bool on_ac_power = false;          // System on external power (any adapter)
};

class BatteryInfoProvider {
public:
    BatteryInfoProvider();
    ~BatteryInfoProvider();

    bool Initialize();
    bool IsInitialized() const { return initialized_; }

    std::vector<BatteryInfo> GetBatteries() const;
    bool IsOnACPower() const;

private:
    bool initialized_ = false;
};

}  // namespace AsyncIO::IO::Hardware
