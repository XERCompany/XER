#include <iostream>
#include <string>

namespace xer {
namespace audio {

struct AudioConfig {
    int sample_rate = 48000;
    int channels = 2;
    int frames_per_buffer = 512;
};

class AudioEngine {
public:
    explicit AudioEngine(AudioConfig config) : config_(config) {}

    bool Initialize() {
        std::cout << "Audio engine initialized: "
                  << config_.sample_rate << " Hz, "
                  << config_.channels << " channels, "
                  << config_.frames_per_buffer << " frames/buffer\n";
        return true;
    }

    void PlayTone(float frequency, float amplitude) {
        std::cout << "Playing tone: frequency=" << frequency
                  << ", amplitude=" << amplitude << "\n";
    }

    void Stop() {
        std::cout << "Audio engine stopped\n";
    }

private:
    AudioConfig config_;
};

}  // namespace audio
}  // namespace xer

int main() {
    xer::audio::AudioConfig config;
    config.sample_rate = 48000;
    config.channels = 2;
    config.frames_per_buffer = 512;

    xer::audio::AudioEngine engine(config);
    if (!engine.Initialize()) {
        return 1;
    }

    engine.PlayTone(440.0f, 0.8f);
    engine.Stop();
    return 0;
}
