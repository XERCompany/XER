// New-sequence presets for the VideoEditor core — the Premiere-style
// "New Sequence" preset list (HD/UHD/vertical/square at standard
// rates). UI-free: the dialog layer renders BuiltinSequencePresets()
// and ApplySequencePreset() stamps the choice onto a Project.
#pragma once

#include <string>
#include <vector>

#include "timeline_model.h"

namespace XER::VideoEditor {

// One factory preset row: display name plus the canvas size and
// rational frame rate a new/reconfigured sequence takes. Field names
// mirror Project::width/height/fps_num/fps_den.
struct SequencePreset {
    std::string name;
    int w = 0;
    int h = 0;
    int fps_num = 0;
    int fps_den = 1;
};

// Factory presets, ordered roughly resolution-then-rate like
// Premiere's dialog: 720p 25/30, 1080p 24/25/30/60, UHD 4K 24/25/30,
// then the social formats (vertical, square). All rates are integer
// (den == 1) — the fractional-NTSC variants are deliberately absent
// for now, matching the non-drop timecode scope in timecode.h.
std::vector<SequencePreset> BuiltinSequencePresets();

// Stamps preset.w/h/fps_num/fps_den onto project->width/height/
// fps_num/fps_den. Tracks/clips/markers are untouched — this is the
// same write the settings dialog performs, just preset-driven.
// Returns false (project untouched) for a null project or an
// incomplete preset (non-positive dimension or rate field).
bool ApplySequencePreset(const SequencePreset& preset, Project* project);

// Reverse lookup for settings display ("current sequence matches
// preset X"): returns the first builtin with exactly these
// dimensions and rate, or a default-constructed preset — check
// `.name.empty()` — when nothing matches (custom sequence).
SequencePreset FindPreset(int w, int h, int fps_num, int fps_den);

}  // namespace XER::VideoEditor
