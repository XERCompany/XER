// Sequence presets — see sequence_preset.h for the contract.
#include "sequence_preset.h"

namespace XER::VideoEditor {

std::vector<SequencePreset> BuiltinSequencePresets() {
    return {
        {"HD 720p 25",            1280,  720, 25, 1},
        {"HD 720p 30",            1280,  720, 30, 1},
        {"Full HD 1080p 24",      1920, 1080, 24, 1},
        {"Full HD 1080p 25",      1920, 1080, 25, 1},
        {"Full HD 1080p 30",      1920, 1080, 30, 1},
        {"Full HD 1080p 60",      1920, 1080, 60, 1},
        {"UHD 4K 24",             3840, 2160, 24, 1},
        {"UHD 4K 25",             3840, 2160, 25, 1},
        {"UHD 4K 30",             3840, 2160, 30, 1},
        {"Vertical 1080x1920 30", 1080, 1920, 30, 1},
        {"Square 1080x1080 30",   1080, 1080, 30, 1},
    };
}

bool ApplySequencePreset(const SequencePreset& preset, Project* project) {
    if (project == nullptr) return false;
    if (preset.w <= 0 || preset.h <= 0 || preset.fps_num <= 0 ||
        preset.fps_den <= 0) {
        return false;
    }
    project->width = preset.w;
    project->height = preset.h;
    project->fps_num = preset.fps_num;
    project->fps_den = preset.fps_den;
    return true;
}

SequencePreset FindPreset(int w, int h, int fps_num, int fps_den) {
    for (const SequencePreset& preset : BuiltinSequencePresets()) {
        if (preset.w == w && preset.h == h &&
            preset.fps_num == fps_num && preset.fps_den == fps_den) {
            return preset;
        }
    }
    return {};   // empty name = custom sequence
}

}  // namespace XER::VideoEditor
