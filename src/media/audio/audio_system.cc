#include "media/audio/audio_system.h"

#include "media/audio/audio_manager.h"

namespace XER::Media {
namespace {

// Minimal system view: one default output, no inputs until the WASAPI
// enumerator bridge lands.
class PlatformAudioSystem final : public AudioSystem {
public:
    void GetInputDevices(DeviceCallback cb) override { cb({}); }
    void GetOutputDevices(DeviceCallback cb) override {
        cb({{"default", "Default Output", true, false}});
    }
    AudioParameters DefaultInputParams() const override { return {}; }
    AudioParameters DefaultOutputParams() const override {
        return AudioManager::Get()->DefaultOutputParameters();
    }
    bool HasActiveStream() const override { return false; }
};

PlatformAudioSystem g_system;

}  // namespace

AudioSystem& AudioSystem::Default() { return g_system; }

}  // namespace XER::Media
