#include "virtualaudiocable/cable_fx_chain.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "effects_rack/fx_dynamics_compressor.h"
#include "effects_rack/fx_dynamics_gate.h"
#include "effects_rack/fx_dynamics_limiter.h"
#include "plugin_wrappers/vst3_host_interface.h"

namespace XER::Audio::Core {
namespace {

class BypassableFx : public CableFx {
public:
    bool is_bypassed() const override { return bypassed_; }
    void set_bypassed(bool b) override { bypassed_ = b; }
private:
    bool bypassed_ = false;
};

class Vst3CableFx final : public BypassableFx {
public:
    Plugin::Vst3HostInterface host;
    std::string label;
    void Process(float* const* channels, int channel_count,
                 uint32_t frames) override {
        host.ProcessChannels(channels, channels, channel_count, frames);
    }
    std::string name() const override {
        return "vst3:" + (label.empty() ? std::string("plugin") : label);
    }
};

class GainFx final : public BypassableFx {
public:
    double db = 0.0;
    float lin = 1.0f;
    void ApplyParams() { lin = static_cast<float>(std::pow(10.0, db / 20.0)); }
    void Process(float* const* channels, int channel_count,
                 uint32_t frames) override {
        for (int c = 0; c < channel_count; ++c) {
            float* b = channels[c];
            for (uint32_t i = 0; i < frames; ++i) {
                b[i] *= lin;
            }
        }
    }
    std::string name() const override { return "gain"; }
};

class SoftClipFx final : public BypassableFx {
public:
    double drive = 0.5;  // 0..1 -> pre-gain 1..40 dB
    void Process(float* const* channels, int channel_count,
                 uint32_t frames) override {
        const float pre =
            static_cast<float>(std::pow(10.0, drive * 40.0 / 20.0));
        for (int c = 0; c < channel_count; ++c) {
            float* b = channels[c];
            for (uint32_t i = 0; i < frames; ++i) {
                b[i] = std::tanh(b[i] * pre);
            }
        }
    }
    std::string name() const override { return "softclip"; }
};

// Mono dynamics processors get one instance per channel.
template <typename Proc>
class PerChannelFx : public BypassableFx {
public:
    explicit PerChannelFx(int channels) : procs_(channels) {}
    template <typename Fn>
    void Configure(Fn fn) {
        for (auto& p : procs_) {
            fn(p);
        }
    }
    void Process(float* const* channels, int channel_count,
                 uint32_t frames) override {
        const int n = std::min(channel_count,
                               static_cast<int>(procs_.size()));
        for (int c = 0; c < n; ++c) {
            procs_[static_cast<size_t>(c)].ProcessBlock(
                channels[c], frames, channels[c]);
        }
    }
private:
    std::vector<Proc> procs_;
};

class CompressorFx final
    : public PerChannelFx<FX::DynamicsCompressor> {
public:
    explicit CompressorFx(int channels) : PerChannelFx(channels) {}
    std::string name() const override { return "compressor"; }
};
class GateFx final : public PerChannelFx<FX::DynamicsGate> {
public:
    explicit GateFx(int channels) : PerChannelFx(channels) {}
    std::string name() const override { return "gate"; }
};
class LimiterFx final : public PerChannelFx<FX::DynamicsLimiter> {
public:
    explicit LimiterFx(int channels) : PerChannelFx(channels) {}
    std::string name() const override { return "limiter"; }
};

double ParamOr(const std::vector<std::pair<std::string, double>>& p,
               const std::string& key, double fallback) {
    for (const auto& kv : p) {
        if (kv.first == key) {
            return kv.second;
        }
    }
    return fallback;
}

}  // namespace

bool CableFxChain::AddVst3(const std::string& plugin_path,
                           double sample_rate, uint32_t max_block,
                           std::string* error_out) {
    auto stage = std::make_unique<Vst3CableFx>();
    stage->host.Initialize(sample_rate, max_block);
    if (!stage->host.LoadPlugin(plugin_path)) {
        if (error_out) {
            *error_out = stage->host.GetLastLoadError();
            if (error_out->empty()) {
                *error_out = "VST3 load failed: " + plugin_path;
            }
        }
        return false;
    }
    stage->label = stage->host.GetPluginName();
    stages_.push_back(std::move(stage));
    return true;
}

bool CableFxChain::AddBuiltin(
    const std::string& effect_name,
    const std::vector<std::pair<std::string, double>>& params,
    double sample_rate, int channels, std::string* error_out) {
    std::unique_ptr<CableFx> stage;
    if (effect_name == "gain") {
        auto fx = std::make_unique<GainFx>();
        fx->db = ParamOr(params, "db", ParamOr(params, "amount", 0.0));
        fx->ApplyParams();
        stage = std::move(fx);
    } else if (effect_name == "softclip") {
        auto fx = std::make_unique<SoftClipFx>();
        fx->drive = std::clamp(
            ParamOr(params, "drive", ParamOr(params, "amount", 0.5)),
            0.0, 1.0);
        stage = std::move(fx);
    } else if (effect_name == "compressor") {
        auto fx = std::make_unique<CompressorFx>(channels);
        const double thr = ParamOr(params, "threshold_db", -18.0);
        const double ratio = ParamOr(params, "ratio", 4.0);
        const double atk = ParamOr(params, "attack_ms", 10.0);
        const double rel = ParamOr(params, "release_ms", 100.0);
        fx->Configure([&](FX::DynamicsCompressor& p) {
            p.SetSampleRate(static_cast<float>(sample_rate));
            p.SetThresholdDb(static_cast<float>(thr));
            p.SetRatio(static_cast<float>(ratio));
            p.SetAttackMs(static_cast<float>(atk));
            p.SetReleaseMs(static_cast<float>(rel));
        });
        stage = std::move(fx);
    } else if (effect_name == "gate") {
        auto fx = std::make_unique<GateFx>(channels);
        const double thr = ParamOr(params, "threshold_db", -50.0);
        fx->Configure([&](FX::DynamicsGate& p) {
            p.SetThresholdDb(static_cast<float>(thr));
        });
        stage = std::move(fx);
    } else if (effect_name == "limiter") {
        auto fx = std::make_unique<LimiterFx>(channels);
        const double ceil = ParamOr(params, "ceiling_db", -1.0);
        fx->Configure([&](FX::DynamicsLimiter& p) {
            p.SetCeilingDb(static_cast<float>(ceil));
        });
        stage = std::move(fx);
    } else {
        if (error_out) {
            *error_out = "unknown builtin effect: " + effect_name;
        }
        return false;
    }
    stages_.push_back(std::move(stage));
    return true;
}

std::vector<std::string> CableFxChain::ListBuiltinEffects() {
    return {"gain", "softclip", "compressor", "gate", "limiter"};
}

void CableFxChain::SetBypassed(size_t index, bool bypassed) {
    if (index < stages_.size()) {
        stages_[index]->set_bypassed(bypassed);
    }
}
bool CableFxChain::IsBypassed(size_t index) const {
    return index < stages_.size() && stages_[index]->is_bypassed();
}

void CableFxChain::ProcessInterleaved(float* interleaved, uint32_t frames,
                                      int channel_count) {
    if (stages_.empty() || interleaved == nullptr || frames == 0 ||
        channel_count <= 0) {
        return;
    }
    const auto ch = static_cast<size_t>(channel_count);
    if (scratch_.size() != ch || scratch_[0].size() < frames) {
        scratch_.assign(ch, std::vector<float>(frames));
        scratch_ptrs_.assign(ch, nullptr);
    }
    for (size_t c = 0; c < ch; ++c) {
        scratch_[c].resize(frames);
        scratch_ptrs_[c] = scratch_[c].data();
        for (uint32_t f = 0; f < frames; ++f) {
            scratch_[c][f] = interleaved[f * ch + c];
        }
    }
    for (auto& stage : stages_) {
        if (!stage->is_bypassed()) {
            stage->Process(scratch_ptrs_.data(), channel_count, frames);
        }
    }
    for (size_t c = 0; c < ch; ++c) {
        for (uint32_t f = 0; f < frames; ++f) {
            interleaved[f * ch + c] = scratch_[c][f];
        }
    }
}

std::vector<std::string> CableFxChain::Describe() const {
    std::vector<std::string> out;
    out.reserve(stages_.size());
    for (const auto& s : stages_) {
        std::string d = s->name();
        if (s->is_bypassed()) {
            d += " (bypassed)";
        }
        out.push_back(std::move(d));
    }
    return out;
}

}  // namespace XER::Audio::Core
