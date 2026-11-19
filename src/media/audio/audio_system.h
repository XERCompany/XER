/**
 * AudioSystem — device enumeration and capability queries (design doc:
 * the browser process answers "how many input/output devices, what are
 * the default params, is a stream active"). Windows impl delegates to
 * the WASAPI engine; a fake implementation exists for tests.
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "media/audio/audio_parameters.h"

namespace XER::Media {

struct AudioDeviceDescription {
    std::string id;      // stable device id
    std::string name;    // user-facing label
    bool is_default = false;
    bool is_input = false;
};

class AudioSystem {
public:
    virtual ~AudioSystem() = default;

    using DeviceCallback =
        std::function<void(std::vector<AudioDeviceDescription>)>;

    virtual void GetInputDevices(DeviceCallback cb) = 0;
    virtual void GetOutputDevices(DeviceCallback cb) = 0;
    virtual AudioParameters DefaultInputParams() const = 0;
    virtual AudioParameters DefaultOutputParams() const = 0;
    virtual bool HasActiveStream() const = 0;

    // Process accessor; WASAPI-backed on Windows by default.
    static AudioSystem& Default();
};

}  // namespace XER::Media
