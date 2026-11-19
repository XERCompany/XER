#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "virtualaudiocable/cable_fx_chain.h"

#include "json/value.h"

namespace XER::Audio::Core {

// Endpoint spec shared by the XERVac CLI and the VAC UI app:
//   cable:NAME | device[:ID] | loopback[:ID] | capture[:ID] |
//   wav:PATH | null
struct VacEndpoint {
    enum class Type { kCable, kRender, kLoopback, kCapture, kWav, kNull };
    Type type = Type::kNull;
    std::string arg;  // cable name, WASAPI device id (utf8), or path
};

std::optional<VacEndpoint> ParseVacEndpoint(const std::string& spec);

// A running route: source -> FX chain -> sink on its own pump thread.
// Construct via StartVacRoute; stop with StopVacRoute (also safe on
// destruction).
class VacRoute {
public:
    ~VacRoute();
    VacRoute(const VacRoute&) = delete;
    VacRoute& operator=(const VacRoute&) = delete;

    struct Impl;

    // chain_json (Json::Value array) may be null — an empty chain then.
    static std::unique_ptr<VacRoute> Start(
        const VacEndpoint& source, const VacEndpoint& sink,
        const Json::Value& chain_json, std::string* error_out = nullptr);
    static std::unique_ptr<VacRoute> Start(
        const VacEndpoint& source, const VacEndpoint& sink,
        std::string* error_out = nullptr) {
        return Start(source, sink, Json::Value(), error_out);
    }

    void Stop();

    bool running() const;
    uint32_t sample_rate() const;
    uint32_t channels() const;
    // Frames pumped through the route so far (diagnostics/UI meter).
    uint64_t frames_pumped() const;
    std::vector<std::string> DescribeChain() const;

private:
    VacRoute() = default;
    std::unique_ptr<Impl> impl_;
};

// Builds an FX chain from a Json array:
//   {"type":"vst3","path":"..."}
//   {"type":"fx","name":"gain|softclip|compressor|gate|limiter",
//    "params":{...}}
//   {"type":"bypass","index":N}
bool BuildVacChain(const Json::Value& chain_json, CableFxChain* chain,
                   double rate, int channels,
                   std::string* error_out = nullptr);

// App-level quit flag consulted by route pump threads; the embedding
// app sets it from its own lifecycle (Ctrl+C / window close).
void SetVacServiceQuitFlag(std::atomic<bool>* flag);

}  // namespace XER::Audio::Core
