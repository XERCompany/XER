// XERVac — Virtual Audio Cable service and CLI for the XER audio stack.
//
// A "cable" is a named cross-process PCM endpoint backed by shared
// memory (src/audio/audio_core/virtual_audio_cable). Cables connect
// XER-aware apps to each other; WASAPI bridges connect them to real
// system audio. Any route can carry an FX chain (VST3 plugins and/or
// built-in custom_fx stages) between its source and sink.
//
// Usage:
//   XERVac --config vac.json            full service: cables + routes
//   XERVac --devices                    list WASAPI endpoints
//   XERVac --list                       list live cables
//   XERVac --create NAME [--rate N] [--ch N] [--ms N]
//                                       create a cable, hold it open
//   XERVac --send CABLE FILE [--loop]   stream an audio file into a cable
//   XERVac --recv CABLE OUT.wav [--seconds N]
//   XERVac --play CABLE                 render a cable on the default
//                                       output device
//   XERVac --cap CABLE  [--device ID]   mic/capture endpoint -> cable
//   XERVac --tap CABLE  [--device ID]   render-device loopback -> cable
//   XERVac --route SRC DST [--vst3 P]... [--fx NAME[:k=v,...]]...
//          SRC/DST: cable:NAME | device[:ID] | loopback[:ID] | wav:PATH
//
// Config file shape:
//   {
//     "cables": [ {"name":"VAC1","rate":48000,"channels":2,"buffer_ms":250} ],
//     "routes": [
//       { "source": {"type":"loopback"},
//         "chain":  [ {"type":"vst3","path":"C:/.../fx.vst3"},
//                     {"type":"fx","name":"compressor",
//                      "params":{"threshold_db":-20}} ],
//         "sink":   {"type":"cable","name":"VAC1"} }
//     ]
//   }

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <json/json.h>

#include "audio_core/audio_fifo_queue.h"
#include "audio_core/audio_source_loader.h"
#include "virtualaudiocable/vac_engine.h"
#include "virtualaudiocable/cable_wasapi.h"
#include "virtualaudiocable/virtual_audio_cable.h"
#include "helper/string_utils.h"

namespace {

using namespace XER::Audio;
using Core::VirtualAudioCable;
using Core::VirtualCableConfig;
using Core::VirtualCableInfo;
using Core::WasapiCaptureSource;
using Core::WasapiEndpoint;
using Core::WasapiRenderSink;
using Core::VacEndpoint;
using Core::VacRoute;
using Core::ParseVacEndpoint;
using Core::SetVacServiceQuitFlag;

std::atomic<bool> g_stop{false};

BOOL WINAPI ConsoleCtrl(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ||
        type == CTRL_CLOSE_EVENT) {
        g_stop.store(true);
        return TRUE;
    }
    return FALSE;
}

void InstallCtrlHandler() {
    SetConsoleCtrlHandler(ConsoleCtrl, TRUE);
}

// ---------------- WAV writer (float32 PCM RIFF) --------------------

class WavWriter {
public:
    bool Open(const std::string& path, uint32_t rate, uint32_t channels) {
        file_ = std::fopen(path.c_str(), "wb");
        if (file_ == nullptr) {
            return false;
        }
        rate_ = rate;
        channels_ = channels;
        std::fwrite("RIFF", 1, 4, file_);
        uint32_t z = 0;
        std::fwrite(&z, 4, 1, file_);  // size placeholder
        std::fwrite("WAVE", 1, 4, file_);
        std::fwrite("fmt ", 1, 4, file_);
        uint32_t fmt_size = 16;
        uint16_t tag = 3;  // IEEE float
        uint16_t ch = static_cast<uint16_t>(channels);
        uint32_t sr = rate;
        uint32_t byte_rate = rate * channels * 4;
        uint16_t align = static_cast<uint16_t>(channels * 4);
        uint16_t bits = 32;
        std::fwrite(&fmt_size, 4, 1, file_);
        std::fwrite(&tag, 2, 1, file_);
        std::fwrite(&ch, 2, 1, file_);
        std::fwrite(&sr, 4, 1, file_);
        std::fwrite(&byte_rate, 4, 1, file_);
        std::fwrite(&align, 2, 1, file_);
        std::fwrite(&bits, 2, 1, file_);
        std::fwrite("data", 1, 4, file_);
        std::fwrite(&z, 4, 1, file_);  // size placeholder
        return true;
    }
    void Write(const float* data, uint32_t frames) {
        if (file_ != nullptr) {
            std::fwrite(data, sizeof(float),
                        static_cast<size_t>(frames) * channels_, file_);
            data_frames_ += frames;
        }
    }
    ~WavWriter() {
        if (file_ == nullptr) {
            return;
        }
        const uint32_t data_size = data_frames_ * channels_ * 4;
        const uint32_t riff_size = 36 + data_size;
        std::fseek(file_, 4, SEEK_SET);
        std::fwrite(&riff_size, 4, 1, file_);
        std::fseek(file_, 40, SEEK_SET);
        std::fwrite(&data_size, 4, 1, file_);
        std::fclose(file_);
    }
private:
    FILE* file_ = nullptr;
    uint32_t rate_ = 0;
    uint32_t channels_ = 0;
    uint32_t data_frames_ = 0;
};

// ---------------- CLI helpers ---------------------------------------

void PrintUsage() {
    std::fputs(
        "XERVac — XER Virtual Audio Cable service\n"
        "  --config FILE     run cables+routes service from JSON config\n"
        "  --devices         list WASAPI endpoints\n"
        "  --list            list live virtual cables\n"
        "  --create NAME [--rate N] [--ch N] [--ms N]\n"
        "  --send CABLE FILE [--loop]\n"
        "  --recv CABLE OUT.wav [--seconds N]\n"
        "  --play CABLE      render cable on default output\n"
        "  --cap CABLE [--device ID]   capture endpoint -> cable\n"
        "  --tap CABLE [--device ID]   render loopback -> cable\n"
        "  --route SRC DST [--vst3 P]... [--fx NAME[:k=v,...]]...\n",
        stderr);
}

int CmdDevices() {
    std::string err;
    for (const auto& d : Core::EnumerateWasapiDevices(&err)) {
        std::printf("%s %s%s  id=%ls\n",
                    d.render ? "render " : "capture",
                    d.is_default ? "[default] " : "          ",
                    d.name.c_str(), d.id.c_str());
    }
    if (!err.empty()) {
        std::fprintf(stderr, "warning: %s\n", err.c_str());
    }
    return 0;
}

int CmdList() {
    const auto cables = VirtualAudioCable::Enumerate();
    if (cables.empty()) {
        std::puts("no live virtual cables");
        return 0;
    }
    for (const auto& c : cables) {
        std::printf("%-16s %u Hz  %u ch  %u frames  pid %u\n",
                    c.name.c_str(), c.sample_rate, c.channels,
                    c.capacity_frames, c.host_pid);
    }
    return 0;
}

int CmdCreate(const std::string& name, uint32_t rate, uint32_t ch,
              uint32_t ms) {
    VirtualCableConfig cfg;
    cfg.name = name;
    cfg.sample_rate = rate;
    cfg.channels = ch;
    cfg.buffer_ms = ms;
    std::string err;
    auto cable = VirtualAudioCable::Create(cfg, &err);
    if (!cable) {
        std::fprintf(stderr, "create failed: %s\n", err.c_str());
        return 1;
    }
    std::printf("cable '%s' live (%u Hz, %u ch, %u ms) — Ctrl+C to close\n",
                name.c_str(), rate, ch, ms);
    InstallCtrlHandler();
    while (!g_stop.load()) {
        Sleep(200);
    }
    return 0;
}

int CmdSend(const std::string& cable_name, const std::string& file,
            bool loop) {
    std::string err;
    auto cable = VirtualAudioCable::OpenWrite(cable_name, &err);
    if (!cable) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    const auto& cfg = cable->config();
    const uint32_t ch = cfg.channels;
    Core::AudioSourceLoadOptions opts;
    opts.input_path = XER::Helper::String::Utf8ToWide(file);
    opts.target_sample_rate = static_cast<int>(cfg.sample_rate);
    opts.target_channels = static_cast<int>(ch);
    Core::AudioSourceBuffer buf;
    std::string load_err;
    if (!Core::AudioSourceLoader::Load(opts, &buf, &load_err)) {
        std::fprintf(stderr, "decode failed: %s\n", load_err.c_str());
        return 1;
    }
    std::fprintf(stderr, "decoded %s: %d Hz %d ch %zu frames\n",
                 file.c_str(), buf.sample_rate, buf.channels,
                 buf.frame_count);
    InstallCtrlHandler();
    uint32_t written_total = 0;
    do {
        size_t pos = 0;
        while (pos < buf.frame_count && !g_stop.load()) {
            const uint32_t chunk = static_cast<uint32_t>(std::min<size_t>(
                buf.frame_count - pos, 1024));
            written_total += cable->Write(buf.samples.data() + pos * ch,
                                          chunk, INFINITE);
            pos += chunk;
        }
    } while (loop && !g_stop.load());
    std::printf("sent %u frames to '%s'\n", written_total,
                cable_name.c_str());
    return 0;
}

int CmdRecv(const std::string& cable_name, const std::string& out,
            double seconds) {
    std::string err;
    auto cable = VirtualAudioCable::OpenRead(cable_name, &err);
    if (!cable) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    WavWriter wav;
    if (!wav.Open(out, cable->config().sample_rate,
                  cable->config().channels)) {
        std::fprintf(stderr, "cannot open %s\n", out.c_str());
        return 1;
    }
    InstallCtrlHandler();
    const uint32_t ch = cable->config().channels;
    const uint32_t limit =
        seconds > 0
            ? static_cast<uint32_t>(seconds * cable->config().sample_rate)
            : UINT32_MAX;
    std::vector<float> buf(1024 * ch);
    uint32_t total = 0;
    while (total < limit && !g_stop.load()) {
        const uint32_t got =
            cable->Read(buf.data(),
                        std::min<uint32_t>(1024, limit - total), 100);
        wav.Write(buf.data(), got);
        total += got;
    }
    std::printf("recorded %u frames (%.2f s) -> %s\n", total,
                total / static_cast<double>(cable->config().sample_rate),
                out.c_str());
    return 0;
}

int CmdPlay(const std::string& cable_name) {
    std::string err;
    auto cable = VirtualAudioCable::OpenRead(cable_name, &err);
    if (!cable) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    const uint32_t ch = cable->config().channels;
    Core::AudioFIFOQueue queue(1 << 20);
    std::atomic<bool> run{true};
    std::thread feeder([&] {
        std::vector<float> buf(480 * ch);
        while (run.load()) {
            const uint32_t got = cable->Read(buf.data(), 480, 50);
            if (got > 0) {
                queue.Write(buf.data(), got);
            }
        }
    });
    WasapiRenderSink sink;
    if (!sink.Start(WasapiEndpoint{},
                    [&](float* data, uint32_t frames) {
                        const size_t got = queue.Read(data, frames);
                        if (got < frames) {
                            std::memset(data + got * ch, 0,
                                        (frames - got) * ch *
                                            sizeof(float));
                        }
                    },
                    &err)) {
        run.store(false);
        feeder.join();
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    if (sink.channels() != ch) {
        std::fprintf(stderr,
                     "warning: sink is %u ch, cable is %u ch\n",
                     sink.channels(), ch);
    }
    std::printf("playing '%s' (%u Hz %u ch) — Ctrl+C to stop\n",
                cable_name.c_str(), cable->config().sample_rate, ch);
    InstallCtrlHandler();
    while (!g_stop.load()) {
        Sleep(200);
    }
    run.store(false);
    sink.Stop();
    cable->SignalClose();
    feeder.join();
    return 0;
}

int CmdWasapiToCable(const std::string& cable_name,
                     const std::string& device_id, bool loopback) {
    std::string err;
    auto cable = VirtualAudioCable::OpenWrite(cable_name, &err);
    if (!cable) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    const uint32_t cable_rate = cable->config().sample_rate;
    const uint32_t cable_ch = cable->config().channels;
    WasapiCaptureSource src;
    WasapiEndpoint ep;
    ep.device_id = XER::Helper::String::Utf8ToWide(device_id);
    if (!src.Start(ep, loopback,
                   [&](const float* data, uint32_t frames) {
                       cable->Write(data, frames, 0);
                   },
                   &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    if (src.sample_rate() != cable_rate || src.channels() != cable_ch) {
        std::fprintf(
            stderr,
            "warning: endpoint %u Hz/%u ch vs cable %u Hz/%u ch — "
            "samples are written raw (pitch shift possible)\n",
            src.sample_rate(), src.channels(), cable_rate, cable_ch);
    }
    std::printf("%s -> '%s' — Ctrl+C to stop\n",
                loopback ? "loopback" : "capture", cable_name.c_str());
    InstallCtrlHandler();
    while (!g_stop.load()) {
        Sleep(200);
    }
    src.Stop();
    return 0;
}

int CmdRoute(const VacEndpoint& src, const VacEndpoint& dst,
             const Json::Value& chain_json) {
    std::string err;
    auto route = VacRoute::Start(src, dst, chain_json, &err);
    if (!route) {
        std::fprintf(stderr, "route failed: %s\n", err.c_str());
        return 1;
    }
    std::printf("route running — Ctrl+C to stop\n");
    InstallCtrlHandler();
    while (!g_stop.load()) {
        Sleep(200);
    }
    route->Stop();
    return 0;
}

int CmdConfig(const std::string& path) {
    Json::Value root;
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "cannot open %s\n", path.c_str());
            return 1;
        }
        Json::CharReaderBuilder rb;
        rb["allowTrailingCommas"] = true;
        std::string errs;
        if (!Json::parseFromStream(rb, in, &root, &errs)) {
            std::fprintf(stderr, "config parse: %s\n", errs.c_str());
            return 1;
        }
    }
    std::vector<std::unique_ptr<VirtualAudioCable>> cables;
    for (const auto& c : root["cables"]) {
        VirtualCableConfig cfg;
        cfg.name = c.get("name", "").asString();
        cfg.sample_rate = c.get("rate", 48000).asUInt();
        cfg.channels = c.get("channels", 2).asUInt();
        cfg.buffer_ms = c.get("buffer_ms", 250).asUInt();
        std::string err;
        auto cable = VirtualAudioCable::Create(cfg, &err);
        if (!cable) {
            std::fprintf(stderr, "cable '%s': %s\n", cfg.name.c_str(),
                         err.c_str());
            return 1;
        }
        std::printf("cable '%s' up (%u Hz %u ch %u ms)\n",
                    cfg.name.c_str(), cfg.sample_rate, cfg.channels,
                    cfg.buffer_ms);
        cables.push_back(std::move(cable));
    }
    std::vector<std::unique_ptr<VacRoute>> routes;
    for (const auto& r : root["routes"]) {
        auto parse_ep = [](const Json::Value& v) -> VacEndpoint {
            VacEndpoint e;
            const std::string type = v.get("type", "").asString();
            if (type == "cable") e.type = VacEndpoint::Type::kCable;
            else if (type == "render" || type == "device")
                e.type = VacEndpoint::Type::kRender;
            else if (type == "loopback")
                e.type = VacEndpoint::Type::kLoopback;
            else if (type == "capture" || type == "mic")
                e.type = VacEndpoint::Type::kCapture;
            else if (type == "wav") e.type = VacEndpoint::Type::kWav;
            else e.type = VacEndpoint::Type::kNull;
            e.arg = v.get("arg", v.get("name", "")).asString();
            if (e.arg.empty() && v.isMember("device")) {
                e.arg = v["device"].asString();
            }
            if (e.arg.empty() && v.isMember("path")) {
                e.arg = v["path"].asString();
            }
            return e;
        };
        std::string err;
        auto route = VacRoute::Start(parse_ep(r["source"]),
                                     parse_ep(r["sink"]),
                                     r["chain"], &err);
        if (!route) {
            std::fprintf(stderr, "route failed: %s\n", err.c_str());
            routes.clear();
            return 1;
        }
        routes.push_back(std::move(route));
        std::printf("route %zu up\n", routes.size());
    }
    std::printf("XERVac service running: %zu cable(s), %zu route(s) — "
                "Ctrl+C to stop\n",
                cables.size(), routes.size());
    InstallCtrlHandler();
    while (!g_stop.load()) {
        Sleep(200);
    }
    routes.clear();
    return 0;
}

Json::Value CliChainJson(const std::vector<std::string>& vst3,
                         const std::vector<std::string>& fx) {
    Json::Value chain(Json::arrayValue);
    for (const auto& p : vst3) {
        Json::Value s;
        s["type"] = "vst3";
        s["path"] = p;
        chain.append(s);
    }
    for (const auto& f : fx) {
        const auto colon = f.find(':');
        Json::Value s;
        s["type"] = "fx";
        s["name"] = f.substr(0, colon);
        if (colon != std::string::npos) {
            // k=v,k=v list
            std::string rest = f.substr(colon + 1);
            Json::Value params;
            size_t pos = 0;
            while (pos < rest.size()) {
                const auto comma = rest.find(',', pos);
                const auto eq = rest.find('=', pos);
                if (eq == std::string::npos) break;
                const auto end =
                    comma == std::string::npos ? rest.size() : comma;
                params[rest.substr(pos, eq - pos)] =
                    std::atof(rest.substr(eq + 1, end - eq - 1).c_str());
                pos = end + 1;
            }
            s["params"] = params;
        }
        chain.append(s);
    }
    return chain;
}

}  // namespace

int main(int argc, char** argv) {
    SetVacServiceQuitFlag(&g_stop);
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        PrintUsage();
        return 1;
    }
    const std::string& cmd = args[0];
    auto arg_after = [&](size_t i, const char* def = "") -> std::string {
        return i + 1 < args.size() ? args[i + 1] : def;
    };
    auto opt = [&](const char* name) -> std::string {
        for (size_t i = 1; i + 1 < args.size(); ++i) {
            if (args[i] == name) {
                return args[i + 1];
            }
        }
        return "";
    };
    auto optu = [&](const char* name, uint32_t def) -> uint32_t {
        const std::string v = opt(name);
        return v.empty() ? def
                         : static_cast<uint32_t>(std::atoi(v.c_str()));
    };
    auto optall = [&](const char* name) {
        std::vector<std::string> out;
        for (size_t i = 1; i + 1 < args.size(); ++i) {
            if (args[i] == name) {
                out.push_back(args[i + 1]);
            }
        }
        return out;
    };
    auto flag = [&](const char* name) {
        return std::find(args.begin(), args.end(), name) != args.end();
    };

    auto need = [&](size_t n) -> bool {
        if (args.size() >= n) {
            return true;
        }
        PrintUsage();
        return false;
    };

    if (cmd == "--config") {
        return need(2) ? CmdConfig(args[1]) : 1;
    }
    if (cmd == "--devices") return CmdDevices();
    if (cmd == "--list") return CmdList();
    if (cmd == "--create") {
        return need(2) ? CmdCreate(arg_after(0), optu("--rate", 48000),
                                   optu("--ch", 2), optu("--ms", 250))
                       : 1;
    }
    if (cmd == "--send") {
        return need(3) ? CmdSend(args[1], args[2], flag("--loop")) : 1;
    }
    if (cmd == "--recv") {
        return need(3) ? CmdRecv(args[1], args[2],
                                 std::atof(opt("--seconds").c_str()))
                       : 1;
    }
    if (cmd == "--play") return need(2) ? CmdPlay(args[1]) : 1;
    if (cmd == "--cap") {
        return need(2)
                   ? CmdWasapiToCable(args[1], opt("--device"), false)
                   : 1;
    }
    if (cmd == "--tap") {
        return need(2)
                   ? CmdWasapiToCable(args[1], opt("--device"), true)
                   : 1;
    }
    if (cmd == "--route") {
        if (!need(3)) {
            return 1;
        }
        auto src = ParseVacEndpoint(args[1]);
        auto dst = ParseVacEndpoint(args[2]);
        if (!src || !dst) {
            std::fprintf(stderr, "bad endpoint spec\n");
            PrintUsage();
            return 1;
        }
        return CmdRoute(*src, *dst,
                        CliChainJson(optall("--vst3"), optall("--fx")));
    }
    PrintUsage();
    return 1;
}
