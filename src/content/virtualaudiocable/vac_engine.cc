#include "virtualaudiocable/vac_engine.h"

#include <cstdio>
#include <cstring>
#include <thread>

#include <json/json.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "audio_core/audio_fifo_queue.h"
#include "helper/string_utils.h"
#include "virtualaudiocable/cable_wasapi.h"
#include "virtualaudiocable/virtual_audio_cable.h"

namespace XER::Audio::Core {
namespace {

std::atomic<bool>* g_quit = nullptr;

bool QuitRequested() {
    return g_quit != nullptr && g_quit->load();
}

class VacWavWriter {
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
        std::fwrite(&z, 4, 1, file_);
        std::fwrite("WAVE", 1, 4, file_);
        std::fwrite("fmt ", 1, 4, file_);
        uint32_t fmt_size = 16;
        uint16_t tag = 3;
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
        std::fwrite(&z, 4, 1, file_);
        return true;
    }
    void Write(const float* data, uint32_t frames) {
        if (file_ != nullptr) {
            std::fwrite(data, sizeof(float),
                        static_cast<size_t>(frames) * channels_, file_);
            data_frames_ += frames;
        }
    }
    ~VacWavWriter() {
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

}  // namespace

void SetVacServiceQuitFlag(std::atomic<bool>* flag) {
    g_quit = flag;
}

std::optional<VacEndpoint> ParseVacEndpoint(const std::string& spec) {
    VacEndpoint e;
    const auto colon = spec.find(':');
    const std::string scheme =
        colon == std::string::npos ? spec : spec.substr(0, colon);
    e.arg = colon == std::string::npos ? "" : spec.substr(colon + 1);
    if (scheme == "cable") e.type = VacEndpoint::Type::kCable;
    else if (scheme == "device") e.type = VacEndpoint::Type::kRender;
    else if (scheme == "loopback") e.type = VacEndpoint::Type::kLoopback;
    else if (scheme == "capture" || scheme == "mic")
        e.type = VacEndpoint::Type::kCapture;
    else if (scheme == "wav") e.type = VacEndpoint::Type::kWav;
    else if (scheme == "null") e.type = VacEndpoint::Type::kNull;
    else return std::nullopt;
    return e;
}

struct VacRoute::Impl {
    std::unique_ptr<VirtualAudioCable> src_cable;
    std::unique_ptr<WasapiCaptureSource> src_wasapi;
    AudioFIFOQueue src_queue{256 * 1024};

    CableFxChain chain;

    std::unique_ptr<VirtualAudioCable> dst_cable;
    std::unique_ptr<WasapiRenderSink> dst_wasapi;
    AudioFIFOQueue dst_queue{256 * 1024};
    std::unique_ptr<VacWavWriter> dst_wav;

    uint32_t rate = 48000;
    uint32_t channels = 2;
    std::thread pump;
    std::atomic<bool> stop{false};
    std::atomic<bool> running{false};
    std::atomic<uint64_t> pumped{0};
};

bool BuildVacChain(const Json::Value& chain_json, CableFxChain* chain,
                   double rate, int channels, std::string* error_out) {
    if (chain_json.isNull()) {
        return true;
    }
    for (const auto& stage : chain_json) {
        const std::string type = stage.get("type", "").asString();
        if (type == "vst3") {
            const std::string path = stage.get("path", "").asString();
            if (!chain->AddVst3(path, rate, 4096, error_out)) {
                return false;
            }
        } else if (type == "fx") {
            const std::string name = stage.get("name", "").asString();
            std::vector<std::pair<std::string, double>> params;
            if (stage.isMember("params")) {
                for (const auto& key : stage["params"].getMemberNames()) {
                    params.emplace_back(
                        key, stage["params"][key].asDouble());
                }
            }
            if (!chain->AddBuiltin(name, params, rate, channels,
                                   error_out)) {
                return false;
            }
        } else if (type == "bypass") {
            const int index = stage.get("index", -1).asInt();
            if (index >= 0) {
                chain->SetBypassed(static_cast<size_t>(index), true);
            }
        }
    }
    return true;
}

VacRoute::~VacRoute() { Stop(); }

std::unique_ptr<VacRoute> VacRoute::Start(
    const VacEndpoint& src, const VacEndpoint& dst,
    const Json::Value& chain_json, std::string* error_out) {
    auto route = std::unique_ptr<VacRoute>(new VacRoute());
    route->impl_ = std::make_unique<Impl>();
    Impl* r = route->impl_.get();

    if (src.type == VacEndpoint::Type::kCable) {
        r->src_cable = VirtualAudioCable::OpenRead(src.arg, error_out);
        if (!r->src_cable) {
            return nullptr;
        }
        r->rate = r->src_cable->config().sample_rate;
        r->channels = r->src_cable->config().channels;
    } else if (src.type == VacEndpoint::Type::kLoopback ||
               src.type == VacEndpoint::Type::kCapture) {
        r->src_wasapi = std::make_unique<WasapiCaptureSource>();
        WasapiEndpoint ep;
        ep.device_id = Helper::String::Utf8ToWide(src.arg);
        if (!r->src_wasapi->Start(
                ep, src.type == VacEndpoint::Type::kLoopback,
                [r](const float* data, uint32_t frames) {
                    r->src_queue.Write(data, frames);
                },
                error_out)) {
            return nullptr;
        }
        r->rate = r->src_wasapi->sample_rate();
        r->channels = r->src_wasapi->channels();
    } else {
        if (error_out) *error_out = "unsupported source for a route";
        return nullptr;
    }

    if (!BuildVacChain(chain_json, &r->chain, r->rate,
                       static_cast<int>(r->channels), error_out)) {
        return nullptr;
    }

    if (dst.type == VacEndpoint::Type::kCable) {
        r->dst_cable = VirtualAudioCable::OpenWrite(dst.arg, error_out);
        if (!r->dst_cable) {
            return nullptr;
        }
    } else if (dst.type == VacEndpoint::Type::kRender) {
        r->dst_wasapi = std::make_unique<WasapiRenderSink>();
        WasapiEndpoint ep;
        ep.device_id = Helper::String::Utf8ToWide(dst.arg);
        if (!r->dst_wasapi->Start(
                ep,
                [r](float* data, uint32_t frames) {
                    const size_t got = r->dst_queue.Read(data, frames);
                    if (got < frames) {
                        std::memset(
                            data + got * r->channels, 0,
                            (frames - got) * r->channels *
                                sizeof(float));
                    }
                },
                error_out)) {
            return nullptr;
        }
        if (r->dst_wasapi->sample_rate() != r->rate ||
            r->dst_wasapi->channels() != r->channels) {
            r->dst_wasapi.reset();
            if (error_out) {
                *error_out = "render sink format mismatch (route is " +
                             std::to_string(r->rate) + "Hz/" +
                             std::to_string(r->channels) + "ch)";
            }
            return nullptr;
        }
    } else if (dst.type == VacEndpoint::Type::kWav) {
        r->dst_wav = std::make_unique<VacWavWriter>();
        if (!r->dst_wav->Open(dst.arg, r->rate, r->channels)) {
            if (error_out) *error_out = "cannot open " + dst.arg;
            return nullptr;
        }
    } else if (dst.type != VacEndpoint::Type::kNull) {
        if (error_out) *error_out = "unsupported sink for a route";
        return nullptr;
    }

    r->running.store(true);
    r->pump = std::thread([r]() {
        const uint32_t block = 480;
        std::vector<float> buf(
            static_cast<size_t>(block) * r->channels);
        while (!r->stop.load() && !QuitRequested()) {
            uint32_t got = 0;
            if (r->src_cable) {
                got = r->src_cable->Read(buf.data(), block, 50);
            } else {
                got = static_cast<uint32_t>(
                    r->src_queue.Read(buf.data(), block));
                if (got == 0) {
#ifdef _WIN32
                    Sleep(1);
#else
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(1));
#endif
                    continue;
                }
            }
            if (got == 0) {
                continue;
            }
            r->chain.ProcessInterleaved(buf.data(), got,
                                        static_cast<int>(r->channels));
            r->pumped.fetch_add(got, std::memory_order_relaxed);
            if (r->dst_cable) {
                r->dst_cable->Write(buf.data(), got, 50);
            } else if (r->dst_wasapi) {
                r->dst_queue.Write(buf.data(), got);
            } else if (r->dst_wav) {
                r->dst_wav->Write(buf.data(), got);
            }
        }
        r->running.store(false);
    });
    return route;
}

void VacRoute::Stop() {
    if (!impl_) {
        return;
    }
    impl_->stop.store(true);
    if (impl_->src_cable) {
        impl_->src_cable->SignalClose();
    }
    if (impl_->src_wasapi) {
        impl_->src_wasapi->Stop();
    }
    if (impl_->pump.joinable()) {
        impl_->pump.join();
    }
    impl_->dst_wasapi.reset();
}

bool VacRoute::running() const {
    return impl_ && impl_->running.load();
}
uint32_t VacRoute::sample_rate() const {
    return impl_ ? impl_->rate : 0;
}
uint32_t VacRoute::channels() const {
    return impl_ ? impl_->channels : 0;
}
uint64_t VacRoute::frames_pumped() const {
    return impl_ ? impl_->pumped.load(std::memory_order_relaxed) : 0;
}
std::vector<std::string> VacRoute::DescribeChain() const {
    return impl_ ? impl_->chain.Describe()
                 : std::vector<std::string>{};
}

}  // namespace XER::Audio::Core
