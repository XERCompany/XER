// MixerPanel — the M3 "Mix" right-dock tab of the ui2 rebuild
// (spec §6): the audio mixer — one vertical strip per timeline
// AUDIO track plus a master strip:
//
//   [A1          ] [A2          ] ... [Master   ]
//    name          name                name
//    [M][S]        [M][S]               —
//    pan knob      pan knob             —
//    fader|meter   fader|meter          fader|meter
//
// Each strip: track-name Label, M(ute)/S(olo) toggle Buttons,
// bipolar pan Knob (-1..1, anchored at 0), a locally-drawn vertical
// fader (-60..+12 dB), and a kit LevelMeter (2ch, vertical, dB
// taper). Mute wires through history's SetTrackMuted; solo has NO
// Track model field — the S button reports the honest deferred
// state through on_status instead of faking a flag.
//
// Ownership rules (surface_hooks.h / spec §6): the panel reads the
// Project through a borrowed const pointer (SetDocument + Refresh —
// same push model as the inspector's provider) and NEVER mutates
// it. Fader/pan/mute gestures emit MixerHooks; the shell maps them
// to TimelineHistory::SetTrackGainDb/SetTrackPan/SetTrackMuted/
// SetMasterGainDb, which coalesce continuous fader drags into one
// undo step per gesture.
//
// Levels seam: SetLevelsProvider(track_index -> {peakL, peakR}
// linear 0..1+, kMasterBus for the master strip) is polled by each
// strip's LevelMeter::SetFrameSource in Tick — pull-based, so the
// shell can point master at PreviewBinding::PeakLevels. Per-track
// meters get an empty reading until a per-track peak seam exists
// (documented: PreviewEngine mixes to a stereo bus; track-level
// taps are not wired yet).
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "layout/geometry.h"
#include "timeline_model.h"   // Project, Track
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Knob;
class Label;
class LevelMeter;
class Widget;
}

namespace XER::VideoEditor::Ui2 {

struct MixerHooks {
    // Commits — the shell maps these onto TimelineHistory and then
    // NotifyTimelineChanged. Live fader/knob drags fire repeatedly;
    // history coalescing keeps them one undo step.
    std::function<void(int track_index, float db)> on_track_gain;
    std::function<void(int track_index, float pan)> on_track_pan;
    std::function<void(int track_index, bool muted)> on_track_mute;
    std::function<void(int track_index, bool solo)> on_track_solo;
    std::function<void(float db)> on_master_gain;
    // Gesture bracketing for fader/knob drags (BeginMacro/EndMacro).
    std::function<void()> on_begin_macro;
    std::function<void()> on_end_macro;
    std::function<void(const std::string&, bool is_error)> on_status;
};

class MixerPanel final : public XER::UI::Widgets::Widget {
public:
    // Levels-provider index for the master strip (track indices are
    // >= 0 — Project::tracks order).
    static constexpr int kMasterBus = -1;

    static std::shared_ptr<MixerPanel> Create();

    void SetHooks(MixerHooks hooks);

    // Document seam — borrowed; Refresh() rebuilds the strip set to
    // match the current audio-track layout (one strip per
    // TrackKind::Audio track + master last). Called from the shell's
    // NotifyTimelineChanged.
    void SetDocument(const Project* project);
    void Refresh();

    // Pull seam for strip meters — returns {peakL, peakR} linear
    // amplitudes (may exceed 1.0 on hot signals); an empty/absent
    // reading decays the meter. kMasterBus addresses the master
    // strip. One provider serves all strips.
    void SetLevelsProvider(
        std::function<std::pair<float, float>(int)> provider);

    // ---- commit verbs (fader/knob callbacks AND the smoke drive
    // these — the whole hook->history->undo pipeline runs through
    // them) ----------------------------------------------------------
    void ApplyTrackGain(int track_index, float db);
    void ApplyTrackPan(int track_index, float pan);
    void ApplyTrackMute(int track_index, bool muted);
    void ApplyTrackSolo(int track_index, bool solo);
    void ApplyMasterGain(float db);

    // ---- discovery / smoke surface ---------------------------------
    int StripCount() const { return static_cast<int>(strips_.size()); }
    // Strip order: audio tracks in Project::tracks order, master
    // last. The track index a strip controls (-1 = master).
    int StripTrack(int strip_index) const;
    XER::UI::Widgets::Widget* StripWidget(int strip_index) const;

    // ---- widget contract ----
    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event&) override {
        return false;
    }
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {200.0f, 180.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {320.0f, 300.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    explicit MixerPanel();

    class Strip;   // pimpl-ish nested strip widget (see .cc)
    std::shared_ptr<Strip> MakeStrip(int track_index,
                                     const std::string& name,
                                     bool master);
    void SyncStripsFromDocument();
    void BeginGesture();
    void EndGesture();
    const Track* TrackAt(int track_index) const;

    MixerHooks hooks_;
    const Project* doc_ = nullptr;
    std::string doc_sig_;   // audio-track signature for strip rebuilds
    bool in_gesture_ = false;
    bool refreshing_ = false;
    std::function<std::pair<float, float>(int)> levels_provider_;
    // Strips: audio tracks in Project order, master strip LAST.
    std::vector<std::shared_ptr<Strip>> strips_;
};

}  // namespace XER::VideoEditor::Ui2
