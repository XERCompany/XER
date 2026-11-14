#include "fx_rack_state.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#if defined(_MSC_VER)
#define xer_sscanf sscanf_s
#else
#define xer_sscanf sscanf
#endif
#include <utility>

#include "audio/effects_rack/custom_effect_eq_engine.h"
#include "audio/effects_rack/custom_effect_fxdata.h"
#include "cache/cache_manager.h"

namespace XER::UI::Widgets {
namespace {

using A = XER::Audio::FX::CustomEffectAlgorithm;

XER::Audio::FX::CustomEffectNode MakeEqNode(float freq, float q, float gain) {
    return XER::Audio::FX::CreateCustomEffectParametricEqNode(freq, q, gain, 1.0f);
}

} // namespace

FxRackState::FxRackState() {
    static const float kFreqs[5] = {60.0f, 250.0f, 1000.0f, 4000.0f, 12000.0f};
    for (std::size_t i = 0; i < eq_bands.size(); ++i) {
        eq_bands[i].frequency_hz = kFreqs[i];
        eq_bands[i].q = 1.0f;
        eq_bands[i].gain_db = 0.0f;
    }
}

XER::Audio::FX::CustomEffectPackage FxRackState::BuildPackage() const {
    XER::Audio::FX::CustomEffectPackage package;
    package.label = "fx_rack";
    // Live playback: keep processing single-threaded and glitch-free.
    package.render_config.enable_multicore_render = false;
    if (!enabled) {
        return package;
    }

    // Nodes sharing a stage_index are processed as parallel branches and
    // summed — for a serial FX chain every node needs its own stage.
    uint32_t stage = 0;
    auto push_node = [&package, &stage](XER::Audio::FX::CustomEffectNode node) {
        node.stage_index = stage++;
        package.nodes.push_back(std::move(node));
    };

    for (const auto& band : eq_bands) {
        if (band.gain_db > 0.01f || band.gain_db < -0.01f) {
            push_node(MakeEqNode(band.frequency_hz, band.q, band.gain_db));
        }
    }
    if (bass_boost_db > 0.01f) {
        push_node(MakeEqNode(110.0f, 0.7f, bass_boost_db));
    }
    if (reverb_mix > 0.01f) {
        XER::Audio::FX::CustomEffectNode reverb;
        reverb.label = "reverb";
        reverb.backend = XER::Audio::FX::CustomEffectBackend::kBuiltinAlgorithm;
        reverb.algorithm = A::kReverbAlgorithmic;
        XER::Audio::FX::SetCustomEffectParameterValue(&reverb.parameters, "room_size", reverb_room_size);
        XER::Audio::FX::SetCustomEffectParameterValue(&reverb.parameters, "damping", reverb_damping);
        reverb.mix_curve.push_back(XER::Audio::FX::CustomEffectCurvePoint{0u, reverb_mix});
        push_node(std::move(reverb));
    }
    for (const auto& node : nodes) {
        if (!node.enabled) {
            continue;
        }
        XER::Audio::FX::CustomEffectNode built;
        built.label = AlgorithmDisplayName(node.algorithm);
        built.backend = XER::Audio::FX::CustomEffectBackend::kBuiltinAlgorithm;
        built.algorithm = node.algorithm;
        built.parameters = node.params;
        if (SupportsWetMix(node.algorithm)) {
            built.mix_curve.push_back(XER::Audio::FX::CustomEffectCurvePoint{0u, node.wet_mix});
        }
        push_node(std::move(built));
    }
    return package;
}

std::vector<FxParamSpec> FxRackState::ParamSpecsFor(A algorithm) {
    using namespace XER::Audio::FX;
    switch (algorithm) {
        case A::kCompressor:
            return {
                {"threshold_db", "Threshold", -60.f, 0.f, -16.f},
                {"ratio", "Ratio", 1.f, 20.f, 3.f},
            };
        case A::kExpander:
            return {
                {"threshold_db", "Threshold", -60.f, 0.f, -36.f},
                {"ratio", "Ratio", 1.f, 10.f, 2.5f},
                {"range_db", "Range", 0.f, 60.f, 18.f},
            };
        case A::kGate:
            return {
                {"threshold_db", "Threshold", -80.f, 0.f, -42.f},
                {"hold_samples", "Hold", 1.f, 8192.f, 256.f},
            };
        case A::kLimiter:
            return {
                {"ceiling_db", "Ceiling", -12.f, 0.f, -1.f},
                {"release", "Release", 0.f, 0.1f, 0.002f},
            };
        case A::kBitcrush:
            return {
                {"bit_depth", "Bit depth", 1.f, 16.f, 10.f},
                {"downsample_factor", "Downsample", 1.f, 8.f, 2.f},
            };
        case A::kTube:
            return {
                {"drive", "Drive", 0.f, 8.f, 2.4f},
                {"output_gain", "Output gain", 0.f, 2.f, 0.85f},
            };
        case A::kChorus:
            return {
                {"rate_hz", "Rate", 0.05f, 8.f, 0.8f},
                {"depth_ms", "Depth ms", 0.5f, 20.f, 4.f},
            };
        case A::kFlanger:
            return {
                {"rate_hz", "Rate", 0.05f, 8.f, 0.35f},
                {"depth_ms", "Depth ms", 0.05f, 2.f, 0.4f},
                {"feedback", "Feedback", 0.f, 0.95f, 0.25f},
            };
        case A::kPhaser:
            return {
                {"rate_hz", "Rate", 0.05f, 8.f, 0.3f},
                {"depth", "Depth", 0.f, 1.f, 0.7f},
                {"feedback", "Feedback", 0.f, 0.95f, 0.2f},
            };
        case A::kParametricEq:
            return {
                {"frequency_hz", "Freq", 20.f, 20000.f, 2200.f},
                {"q", "Q", 0.1f, 10.f, 0.8f},
                {"gain_db", "Gain", -24.f, 24.f, 0.f},
            };
        case A::kReverbAlgorithmic:
            return {
                {"room_size", "Room size", 0.f, 1.f, 0.6f},
                {"damping", "Damping", 0.f, 1.f, 0.3f},
            };
        case A::kPitchShift:
            return {
                {"pitch_ratio", "Ratio", 0.5f, 2.f, 1.f},
            };
        case A::kAutoTuneCorrectPitch:
            return {
                {"correction_amount", "Correction", 0.f, 1.f, 0.88f},
                {"correction_speed", "Speed", 0.f, 1.f, 0.18f},
                {"retune_speed_ms", "Retune ms", 1.f, 100.f, 18.f},
                {"humanize", "Humanize", 0.f, 1.f, 0.12f},
                {"scale_root", "Root", 0.f, 11.f, 0.f},
                {"scale_mode", "Mode", 0.f, 1.f, 0.f},
                {"presence_db", "Presence", -12.f, 12.f, 1.6f},
            };
        case A::kDelay:
            return {
                {"time_ms", "Time ms", 1.f, 2000.f, 350.f},
                {"feedback", "Feedback", -0.95f, 0.95f, 0.35f},
                {"damping", "Damping", 0.f, 0.95f, 0.35f},
            };
        case A::kTremolo:
            return {
                {"rate_hz", "Rate", 0.1f, 12.f, 4.5f},
                {"depth", "Depth", 0.f, 1.f, 0.5f},
            };
        case A::kVibrato:
            return {
                {"rate_hz", "Rate", 0.1f, 10.f, 5.f},
                {"depth_ms", "Depth ms", 0.5f, 12.f, 4.f},
            };
        case A::kRingMod:
            return {
                {"frequency_hz", "Freq", 1.f, 2000.f, 80.f},
                {"mix", "Mix", 0.f, 1.f, 1.f},
            };
        default:
            return {};
    }
}

std::string FxRackState::AlgorithmDisplayName(A algorithm) {
    switch (algorithm) {
        case A::kPassthrough: return "Passthrough";
        case A::kCompressor: return "Compressor";
        case A::kExpander: return "Expander";
        case A::kGate: return "Gate";
        case A::kLimiter: return "Limiter";
        case A::kBitcrush: return "Bitcrush";
        case A::kTube: return "Tube";
        case A::kChorus: return "Chorus";
        case A::kFlanger: return "Flanger";
        case A::kPhaser: return "Phaser";
        case A::kParametricEq: return "Parametric EQ";
        case A::kReverbAlgorithmic: return "Reverb";
        case A::kPitchShift: return "Pitch Shift";
        case A::kAutoTuneCorrectPitch: return "AutoTune";
        case A::kDelay: return "Delay";
        case A::kTremolo: return "Tremolo";
        case A::kVibrato: return "Vibrato";
        case A::kRingMod: return "Ring Mod";
        default: return XER::Audio::FX::CustomEffectAlgorithmToString(algorithm);
    }
}

bool FxRackState::SupportsWetMix(A algorithm) {
    return algorithm == A::kReverbAlgorithmic || algorithm == A::kDelay;
}

std::vector<A> FxRackState::AvailableAlgorithms() {
    return {
        A::kCompressor, A::kExpander, A::kGate, A::kLimiter, A::kBitcrush,
        A::kTube, A::kChorus, A::kFlanger, A::kPhaser, A::kParametricEq,
        A::kReverbAlgorithmic, A::kPitchShift, A::kAutoTuneCorrectPitch, A::kDelay,
        A::kTremolo, A::kVibrato, A::kRingMod,
    };
}

FxNodeState FxRackState::MakeDefaultNode(A algorithm) {
    FxNodeState node;
    node.algorithm = algorithm;
    node.enabled = true;
    node.wet_mix = 1.0f;
    for (const auto& spec : ParamSpecsFor(algorithm)) {
        node.params.push_back(XER::Audio::FX::CustomEffectParameter{spec.name, spec.default_value});
    }
    return node;
}

namespace {

constexpr absl::string_view kCacheScope = "ui";
constexpr absl::string_view kCacheKey = "fx_rack";
constexpr absl::string_view kCacheName = "state";
constexpr const char* kSerializeMagic = "xer_fx_rack_v1";

} // namespace

std::string FxRackState::Serialize() const {
    std::string out;
    out.reserve(512 + nodes.size() * 96);
    out += kSerializeMagic;
    out += '\n';

    char line[64];
    std::snprintf(line, sizeof(line), "enabled=%d\n", enabled ? 1 : 0);
    out += line;
    std::snprintf(line, sizeof(line), "bass=%.4f\n", bass_boost_db);
    out += line;
    std::snprintf(line, sizeof(line), "reverb_mix=%.4f\n", reverb_mix);
    out += line;
    std::snprintf(line, sizeof(line), "reverb_room=%.4f\n", reverb_room_size);
    out += line;
    std::snprintf(line, sizeof(line), "reverb_damp=%.4f\n", reverb_damping);
    out += line;

    out += "eq=";
    for (std::size_t i = 0; i < eq_bands.size(); ++i) {
        const auto& band = eq_bands[i];
        std::snprintf(line, sizeof(line), "%.2f,%.3f,%.3f%s",
                      band.frequency_hz, band.q, band.gain_db,
                      i + 1 < eq_bands.size() ? ";" : "\n");
        out += line;
    }

    for (const auto& node : nodes) {
        out += "node=";
        out += std::to_string(static_cast<int>(node.algorithm));
        out += ',';
        out += node.enabled ? '1' : '0';
        char tail[64];
        std::snprintf(tail, sizeof(tail), ",%.4f", node.wet_mix);
        out += tail;
        for (const auto& param : node.params) {
            std::snprintf(tail, sizeof(tail), ",%.5f", param.value);
            out += tail;
        }
        out += '\n';
    }
    return out;
}

bool FxRackState::Deserialize(const std::string& text) {
    if (text.rfind(kSerializeMagic, 0) != 0) {
        return false;
    }

    FxRackState parsed;
    std::istringstream stream(text);
    std::string line;
    bool saw_any = false;

    while (std::getline(stream, line)) {
        if (line.rfind("enabled=", 0) == 0) {
            parsed.enabled = std::atoi(line.c_str() + 8) != 0;
            saw_any = true;
        } else if (line.rfind("bass=", 0) == 0) {
            parsed.bass_boost_db = std::strtof(line.c_str() + 5, nullptr);
            saw_any = true;
        } else if (line.rfind("reverb_mix=", 0) == 0) {
            parsed.reverb_mix = std::strtof(line.c_str() + 11, nullptr);
            saw_any = true;
        } else if (line.rfind("reverb_room=", 0) == 0) {
            parsed.reverb_room_size = std::strtof(line.c_str() + 12, nullptr);
            saw_any = true;
        } else if (line.rfind("reverb_damp=", 0) == 0) {
            parsed.reverb_damping = std::strtof(line.c_str() + 12, nullptr);
            saw_any = true;
        } else if (line.rfind("eq=", 0) == 0) {
            const char* p = line.c_str() + 3;
            for (std::size_t i = 0; i < parsed.eq_bands.size(); ++i) {
                float f = 0.0f, q = 1.0f, g = 0.0f;
                if (xer_sscanf(p, "%f,%f,%f", &f, &q, &g) != 3) {
                    break;
                }
                parsed.eq_bands[i] = FxEqBandState{f, q, g};
                const char* semi = std::strchr(p, ';');
                if (semi == nullptr) {
                    break;
                }
                p = semi + 1;
            }
            saw_any = true;
        } else if (line.rfind("node=", 0) == 0) {
            const auto algorithms = AvailableAlgorithms();
            int algo = 0, enabled_flag = 1;
            float wet = 1.0f;
            int consumed = 0;
            if (xer_sscanf(line.c_str() + 5, "%d,%d,%f%n", &algo, &enabled_flag, &wet,
                            &consumed) < 3) {
                continue;
            }
            const auto algorithm = static_cast<A>(algo);
            if (std::find(algorithms.begin(), algorithms.end(), algorithm) ==
                algorithms.end()) {
                continue;
            }
            FxNodeState node = MakeDefaultNode(algorithm);
            node.enabled = enabled_flag != 0;
            node.wet_mix = wet;
            const char* p = line.c_str() + 5 + consumed;
            if (*p == ',') {
                ++p;
            }
            for (auto& param : node.params) {
                float value = 0.0f;
                int used = 0;
                if (xer_sscanf(p, "%f%n", &value, &used) != 1) {
                    break;
                }
                param.value = value;
                p += used;
                if (*p == ',') {
                    ++p;
                }
            }
            parsed.nodes.push_back(std::move(node));
            saw_any = true;
        }
    }

    if (!saw_any) {
        return false;
    }
    *this = std::move(parsed);
    return true;
}

bool FxRackState::SaveToCache() const {
    auto& cache = XER::Cache::CacheManager::Instance();
    std::string error;
    return cache.WritePersistentText(kCacheScope, kCacheKey, kCacheName,
                                     Serialize(), &error);
}

bool FxRackState::LoadFromCache() {
    auto& cache = XER::Cache::CacheManager::Instance();
    std::string text;
    std::string error;
    if (!cache.ReadPersistentText(kCacheScope, kCacheKey, kCacheName, &text, &error) ||
        text.empty()) {
        return false;
    }
    return Deserialize(text);
}

} // namespace XER::UI::Widgets
