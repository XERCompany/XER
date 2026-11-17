// See vst3_params.h — inspector parameter metadata + preset strings.
#include "vst/vst3_params.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <string_view>

#include "vst/vst3_factory.h"   // Vst3PluginInstance::PrimaryHost

namespace XER::VideoEditor::Vst {
namespace {

std::string_view Trim(std::string_view s) {
    auto is_space = [](char c) {
        return std::isspace(static_cast<unsigned char>(c)) != 0;
    };
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
    return s;
}

bool IEquals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// The synthesized-name convention shared by DescribeStoredParams and
// SetParamByName: "Param <id>" — resolvable on ANY IPluginInstance,
// including ones that cannot enumerate.
std::string SyntheticName(std::uint32_t id) {
    return "Param " + std::to_string(id);
}

// "Param 7" / "param7" / "Param_7" / bare "7" -> param id. Case-
// insensitive; strict all-digits tail so "Param EQ" never resolves.
bool ParseSyntheticId(std::string_view name, std::uint32_t* id_out) {
    std::string_view s = Trim(name);
    if (s.size() >= 5 && IEquals(s.substr(0, 5), "param")) {
        s.remove_prefix(5);
        while (!s.empty() &&
               (s.front() == ' ' || s.front() == '_' ||
                s.front() == '\t')) {
            s.remove_prefix(1);
        }
    }
    if (s.empty() || s.size() > 10) return false;
    std::uint32_t id = 0;
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!std::isdigit(u)) return false;
        const std::uint32_t d = static_cast<std::uint32_t>(u - '0');
        if (id > (UINT32_MAX - d) / 10u) return false;   // overflow
        id = id * 10u + d;
    }
    *id_out = id;
    return true;
}

std::string FormatFloat(float v) {
    char buf[32];
    const auto res = std::to_chars(buf, buf + sizeof(buf), v);
    return std::string(buf, res.ptr);
}

bool ParseFloat(std::string_view text, float* out) {
    const std::string_view s = Trim(text);
    if (s.empty()) return false;
    float v = 0.0f;
    const auto* first = s.data();
    const auto* last = first + s.size();
    const auto res = std::from_chars(first, last, v);
    if (res.ec != std::errc{} || res.ptr != last) return false;
    *out = v;
    return true;
}

// Key written into preset strings: real name when present, synthetic
// otherwise; ';' and '=' flattened so Format always re-parses.
std::string ParamKey(const Vst3ParamInfo& p) {
    std::string key = p.name;
    for (char& c : key) {
        if (c == ';' || c == '=') c = '_';
    }
    key = std::string(Trim(key));
    if (key.empty()) {
        key = SyntheticName(static_cast<std::uint32_t>(std::max(0, p.id)));
    }
    return key;
}

}  // namespace

std::vector<Vst3ParamInfo> DescribeParams(const PluginInfo& /*info*/) {
    // Vst3PluginInfo (scanner output) has no parameter list — only
    // path/module/name/vendor/category/version. Real enumeration
    // requires a loaded instance: DescribeParams(IPluginInstance*).
    return {};
}

std::vector<Vst3ParamInfo> DescribeParams(IPluginInstance* instance) {
    std::vector<Vst3ParamInfo> out;
    auto* vst3 = dynamic_cast<Vst3PluginInstance*>(instance);
    if (vst3 == nullptr || vst3->PrimaryHost() == nullptr) {
        return out;   // interface exposes no enumeration — see header
    }
    auto* host = vst3->PrimaryHost();
    for (const auto& p : host->GetParameters()) {
        Vst3ParamInfo row;
        row.id = static_cast<int>(p.id);
        row.name = p.name;
        if (row.name.empty()) row.name = SyntheticName(p.id);
        float live = 0.0f;
        // Normalized value: GetParameter is the live source of truth;
        // PluginParameterInfo.current_value is the registered
        // fallback (external hosts leave it at default).
        row.norm_value =
            host->GetParameter(p.id, &live)
                ? std::clamp(live, 0.0f, 1.0f)
                : std::clamp(p.current_value, 0.0f, 1.0f);
        out.push_back(std::move(row));
    }
    return out;
}

std::vector<Vst3ParamInfo> DescribeStoredParams(const ClipAudioFx& fx) {
    std::vector<Vst3ParamInfo> out;
    out.reserve(fx.params.size());
    for (const auto& [id, value] : fx.params) {
        Vst3ParamInfo row;
        row.id = static_cast<int>(id);
        row.name = SyntheticName(id);
        row.norm_value = std::clamp(value, 0.0f, 1.0f);
        out.push_back(std::move(row));
    }
    return out;
}

bool SetParamByName(IPluginInstance* instance, const std::string& name,
                    float norm_value) {
    if (instance == nullptr) return false;
    const float value = std::clamp(norm_value, 0.0f, 1.0f);

    // 1) Real-name resolution when the instance can enumerate.
    const std::vector<Vst3ParamInfo> described = DescribeParams(instance);
    for (const auto& p : described) {
        if (p.name == name || IEquals(p.name, name)) {
            return instance->SetParameter(
                static_cast<std::uint32_t>(p.id), value);
        }
    }
    // 2) Synthetic "Param <id>" / bare-id resolution — works on any
    //    instance, enumerated or not.
    std::uint32_t id = 0;
    if (ParseSyntheticId(name, &id)) {
        return instance->SetParameter(id, value);
    }
    return false;
}

std::string FormatParamsString(
    const std::vector<Vst3ParamInfo>& params) {
    std::string out;
    for (const Vst3ParamInfo& p : params) {
        if (!out.empty()) out.push_back(';');
        out += ParamKey(p);
        out.push_back('=');
        out += FormatFloat(p.norm_value);
    }
    return out;
}

std::vector<std::pair<std::string, float>> ParseParamsString(
    const std::string& text) {
    std::vector<std::pair<std::string, float>> out;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t semi = text.find(';', pos);
        const std::string_view token = Trim(
            std::string_view(text).substr(
                pos, semi == std::string::npos
                        ? std::string_view::npos
                        : semi - pos));
        if (!token.empty()) {
            const std::size_t eq = token.find('=');
            if (eq != std::string_view::npos) {
                const std::string_view key = Trim(token.substr(0, eq));
                float value = 0.0f;
                if (!key.empty() &&
                    ParseFloat(token.substr(eq + 1), &value)) {
                    out.emplace_back(std::string(key), value);
                }
            }
        }
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }
    return out;
}

std::size_t ApplyParamsString(IPluginInstance* instance,
                              const std::string& text) {
    std::size_t applied = 0;
    for (const auto& [name, value] : ParseParamsString(text)) {
        if (SetParamByName(instance, name, value)) ++applied;
    }
    return applied;
}

}  // namespace XER::VideoEditor::Vst
