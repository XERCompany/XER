#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace XER::Audio::Core {

// One processing stage inside a cable route. Stages receive
// deinterleaved per-channel float32 buffers and process them in place;
// a stage may replace the signal entirely (generators) or pass it
// through (bypassable). Process runs on a realtime-ish pump thread —
// implement real-time safe stages only.
class CableFx {
public:
    virtual ~CableFx() = default;
    virtual void Process(float* const* channels, int channel_count,
                         uint32_t frames) = 0;
    virtual std::string name() const = 0;
    virtual bool is_bypassed() const { return false; }
    virtual void set_bypassed(bool) {}
};

// Ordered chain; Process() walks every stage in order, skipping
// bypassed ones.
class CableFxChain {
public:
    // VST3 insert: loads `plugin_path` at `sample_rate`/`max_block`.
    // Returns false on load failure (error_out receives detail).
    bool AddVst3(const std::string& plugin_path, double sample_rate,
                 uint32_t max_block, std::string* error_out = nullptr);

    // Built-in custom_fx stage. Known names: "gain" (param db),
    // "softclip" (param drive 0..1), "compressor" (optional params
    // threshold_db, ratio, attack_ms, release_ms).
    bool AddBuiltin(const std::string& effect_name,
                    const std::vector<std::pair<std::string, double>>& params,
                    double sample_rate, int channels,
                    std::string* error_out = nullptr);

    static std::vector<std::string> ListBuiltinEffects();

    void SetBypassed(size_t index, bool bypassed);
    bool IsBypassed(size_t index) const;

    // In-place: `interleaved` holds frames*channel_count samples.
    void ProcessInterleaved(float* interleaved, uint32_t frames,
                            int channel_count);

    size_t size() const { return stages_.size(); }
    bool empty() const { return stages_.empty(); }
    std::vector<std::string> Describe() const;

private:
    std::vector<std::unique_ptr<CableFx>> stages_;
    // Scratch for deinterleave/reinterleave inside ProcessInterleaved.
    std::vector<std::vector<float>> scratch_;
    std::vector<float*> scratch_ptrs_;
};

}  // namespace XER::Audio::Core
