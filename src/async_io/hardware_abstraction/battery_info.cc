#include "battery_info.h"

#include <algorithm>
#include <cmath>

#if defined(_WIN32)
    #include <windows.h>
#elif defined(__linux__)
    #include <fstream>
    #include <sstream>
    #include <dirent.h>
#endif

namespace XER::AsyncIO::IO::Hardware {

namespace {

const char* StateName(BatteryState s) {
    switch (s) {
        case BatteryState::Discharging:  return "Discharging";
        case BatteryState::Charging:     return "Charging";
        case BatteryState::Full:         return "Full";
        case BatteryState::NotPresent:   return "NotPresent";
        case BatteryState::Critical:    return "Critical";
        case BatteryState::Unknown:
        default:                          return "Unknown";
    }
}

#if defined(__linux__)
double ReadSysfsDouble(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return 0.0;
    double v = 0.0;
    f >> v;
    return v;
}
std::string ReadSysfsString(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::string s;
    std::getline(f, s);
    return s;
}
#endif

}  // namespace

BatteryInfoProvider::BatteryInfoProvider() = default;
BatteryInfoProvider::~BatteryInfoProvider() = default;

bool BatteryInfoProvider::Initialize() {
    initialized_ = true;
    return true;
}

#if defined(_WIN32)

std::vector<BatteryInfo> BatteryInfoProvider::GetBatteries() const {
    std::vector<BatteryInfo> out;

    // GetSystemPowerStatus gives a single aggregate battery view on Windows.
    // Per-battery detail (each slot, wear, design capacity) requires opening
    // \\.\Device\BatteryX via IOCTL_BATTERY_QUERY_INFORMATION; that path is
    // significantly more code and privileges for limited extra value here.
    SYSTEM_POWER_STATUS sps{};
    if (!GetSystemPowerStatus(&sps)) return out;

    BatteryInfo b{};
    b.id = 0;
    b.name = "Battery";
    b.present = (sps.BatteryFlag != 128);  // 128 = no battery
    b.on_ac_power = (sps.ACLineStatus == 1);
    b.percent = (sps.BatteryLifePercent <= 100) ? static_cast<double>(sps.BatteryLifePercent) : 0.0;

    if (!b.present) {
        b.state = BatteryState::NotPresent;
    } else if (sps.BatteryFlag & 8) {        // charging
        b.state = BatteryState::Charging;
    } else if (!b.on_ac_power) {
        b.state = (sps.BatteryFlag & 4) ? BatteryState::Critical
                                         : BatteryState::Discharging;
    } else {
        b.state = (sps.BatteryFlag & 5) ? BatteryState::Full : BatteryState::Unknown;
    }
    b.state_name = StateName(b.state);

    if (sps.BatteryLifeTime != static_cast<DWORD>(-1) && !b.on_ac_power) {
        b.estimated_seconds_remaining = static_cast<int64_t>(sps.BatteryLifeTime);
    } else {
        b.estimated_seconds_remaining = -1;
    }

    out.push_back(std::move(b));
    return out;
}

bool BatteryInfoProvider::IsOnACPower() const {
    SYSTEM_POWER_STATUS sps{};
    if (!GetSystemPowerStatus(&sps)) return true;
    return sps.ACLineStatus == 1;
}

#elif defined(__linux__)

std::vector<BatteryInfo> BatteryInfoProvider::GetBatteries() const {
    std::vector<BatteryInfo> out;

    // Enumerate /sys/class/power_supply/BAT*
    DIR* dir = opendir("/sys/class/power_supply");
    if (!dir) {
        // No power supply info available — synthesize an "unknown" entry.
        BatteryInfo b;
        b.name = "Battery";
        b.state = BatteryState::Unknown;
        b.state_name = StateName(b.state);
        out.push_back(b);
        return out;
    }

    int idx = 0;
    for (struct dirent* e = readdir(dir); e; e = readdir(dir)) {
        if (!e->d_name || e->d_name[0] == '.') continue;
        const std::string base = std::string("/sys/class/power_supply/") + e->d_name;
        const std::string type = ReadSysfsString(base + "/type");
        if (type != "Battery" && type.find("Battery") == std::string::npos) continue;

        BatteryInfo b;
        b.id = idx++;
        b.name = e->d_name;

        const std::string present = ReadSysfsString(base + "/present");
        b.present = (present == "1");

        if (b.present) {
            double energy_now   = ReadSysfsDouble(base + "/energy_now");   // µWh
            double energy_full  = ReadSysfsDouble(base + "/energy_full");  // µWh
            double energy_design= ReadSysfsDouble(base + "/energy_full_design");
            double charge_now   = ReadSysfsDouble(base + "/charge_now");   // µAh
            double charge_full  = ReadSysfsDouble(base + "/charge_full");
            double charge_design= ReadSysfsDouble(base + "/charge_full_design");
            double voltage_uv   = ReadSysfsDouble(base + "/voltage_now");  // µV

            // Standardize to Wh. If energy_* present, use them directly (µWh -> Wh).
            // Else convert charge_* (µAh * V / 1000 = Wh).
            bool have_energy = (energy_now > 0 || energy_full > 0);
            bool have_charge = (charge_now > 0 || charge_full > 0);
            if (have_energy) {
                b.current_capacity_wh     = energy_now / 1e6;
                b.full_charged_capacity_wh = energy_full / 1e6;
                b.design_capacity_wh      = (energy_design > 0) ? energy_design / 1e6
                                                                 : b.full_charged_capacity_wh;
            } else if (have_charge && voltage_uv > 0) {
                const double volts = voltage_uv / 1e6;
                b.current_capacity_wh      = (charge_now  / 1e6) * volts;     // µAh->Ah * V
                b.full_charged_capacity_wh = (charge_full / 1e6) * volts;
                b.design_capacity_wh       = (charge_design > 0) ? (charge_design / 1e6) * volts
                                                                   : b.full_charged_capacity_wh;
            }
            if (voltage_uv > 0) b.voltage_volts = voltage_uv / 1e6;

            if (b.design_capacity_wh > 0 && b.full_charged_capacity_wh > 0) {
                b.health_fraction = b.full_charged_capacity_wh / b.design_capacity_wh;
            } else {
                b.health_fraction = 1.0;
            }

            if (b.full_charged_capacity_wh > 0) {
                b.percent = std::clamp(
                    100.0 * (b.current_capacity_wh / b.full_charged_capacity_wh), 0.0, 100.0);
            }

            const std::string status = ReadSysfsString(base + "/status");
            if (status == "Charging")        b.state = BatteryState::Charging;
            else if (status == "Discharging") b.state = BatteryState::Discharging;
            else if (status == "Full")        b.state = BatteryState::Full;
            else                              b.state = BatteryState::Unknown;
            b.state_name = StateName(b.state);

            // Online AC: read AC adapter status (any online = on AC).
            // Done in IsOnACPower to avoid duplicate scanning.
            b.on_ac_power = false;  // filled by caller via IsOnACPower if needed
        } else {
            b.state = BatteryState::NotPresent;
            b.state_name = StateName(b.state);
        }

        out.push_back(std::move(b));
    }
    closedir(dir);
    return out;
}

bool BatteryInfoProvider::IsOnACPower() const {
    // Any /sys/class/power_supply/AC*/online == 1 means on AC.
    DIR* dir = opendir("/sys/class/power_supply");
    if (!dir) return true;  // assume on AC if unknown
    bool any_online = false;
    for (struct dirent* e = readdir(dir); e; e = readdir(dir)) {
        if (!e->d_name || e->d_name[0] == '.') continue;
        const std::string name = e->d_name;
        if (name.rfind("AC", 0) != 0 && name.rfind("ADP", 0) != 0) continue;
        std::ifstream f(std::string("/sys/class/power_supply/") + name + "/online");
        if (!f.is_open()) continue;
        int v = 0; f >> v;
        if (v == 1) { any_online = true; break; }
    }
    closedir(dir);
    return any_online;
}

#else

std::vector<BatteryInfo> BatteryInfoProvider::GetBatteries() const {
    BatteryInfo b;
    b.name = "Battery";
    b.state = BatteryState::Unknown;
    b.state_name = StateName(b.state);
    return {b};
}

bool BatteryInfoProvider::IsOnACPower() const {
    return true;
}

#endif

}  // namespace XER::AsyncIO::IO::Hardware
