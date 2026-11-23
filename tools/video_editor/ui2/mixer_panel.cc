#include "mixer_panel.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/knob.h"
#include "widgets/label.h"
#include "widgets/level_meter.h"

// windows.h may have re-defined DrawText before we got here.
#ifdef DrawText
#undef DrawText
#endif

namespace XER::VideoEditor::Ui2 {
namespace {

namespace W = XER::UI::Widgets;
namespace Layout = XER::UI::Layout;
namespace Rendering = XER::UI::Rendering;

constexpr float kStripW = 92.0f;
constexpr float kNameH = 18.0f;
constexpr float kMsH = 22.0f;
constexpr float kKnobH = 46.0f;
constexpr float kMeterW = 18.0f;

// Fader law (console parity): -60..+12 dB, 0 dB at ~83% up.
constexpr float kFaderMinDb = -60.0f;
constexpr float kFaderMaxDb = 12.0f;

inline float DbFromLinear(float linear) {
    return 20.0f * std::log10(std::max(1e-6f, linear));
}

// ---- VFader -------------------------------------------------------------
// The kit Slider is horizontal-only, so the strip's fader is a small
// local widget: a vertical track with a draggable thumb, dB-mapped
// travel and a subtle 0 dB tick. Live drags emit on_change per move;
// release emits on_committed once (macro bracket seam).
class VFader final : public W::Widget {
public:
    VFader() { SetFocusable(true); }
    // Keep the 2-arg Widget::Layout visible beside the 1-arg override —
    // StripRow calls Layout(bounds, tokens) on a shared_ptr<VFader>.
    using W::Widget::Layout;

    void SetDbRange(float min_db, float max_db) {
        min_db_ = min_db;
        max_db_ = max_db;
        SetValue(std::clamp(value_, min_db_, max_db_));
    }
    void SetValue(float db) {
        db = std::clamp(db, min_db_, max_db_);
        if (db == value_) return;
        value_ = db;
        Invalidate();
    }
    float Value() const { return value_; }
    void SetOnChange(std::function<void(float)> cb) {
        on_change_ = std::move(cb);
    }
    void SetOnCommitted(std::function<void(float)> cb) {
        on_commit_ = std::move(cb);
    }

    void Render(Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        const Layout::Rect b = GetBounds();
        const float cx = b.x + b.width / 2.0f;
        const Layout::Rect track{cx - 2.0f, b.y + 4.0f, 4.0f,
                                 b.height - 8.0f};
        renderer.FillRect(track, tokens.background, 2.0f);
        // 0 dB tick — the unity mark on the -60..+12 travel.
        const float t0 =
            (0.0f - min_db_) / (max_db_ - min_db_);
        const float y0 = track.Bottom() - t0 * track.height;
        renderer.DrawLine({track.x - 3.0f, y0},
                          {track.Right() + 3.0f, y0}, tokens.muted,
                          1.0f);
        const float t =
            (value_ - min_db_) / (max_db_ - min_db_);
        const float ty = track.Bottom() - t * track.height;
        renderer.FillRect(
            {cx - 9.0f, ty - 4.0f, 18.0f, 8.0f},
            dragging_ ? tokens.accent : tokens.muted, 2.0f);
        renderer.StrokeRect({cx - 9.0f, ty - 4.0f, 18.0f, 8.0f},
                            tokens.border, 1.0f, 2.0f);
    }

    void Layout(const Layout::Rect& bounds) override {
        SetBounds(bounds);
    }

    bool HandleEvent(const W::Event& event) override {
        switch (event.type) {
            case W::Event::Type::MouseDown:
                if (event.mouse_button == W::MouseButton::Left &&
                    GetBounds().Contains(event.position)) {
                    dragging_ = true;
                    UpdateFromY(event.position.y);
                    return true;
                }
                return false;
            case W::Event::Type::MouseMove:
                if (dragging_) {
                    UpdateFromY(event.position.y);
                    return true;
                }
                return false;
            case W::Event::Type::MouseUp:
                if (dragging_) {
                    dragging_ = false;
                    if (on_commit_) on_commit_(value_);
                    return true;
                }
                return false;
            case W::Event::Type::MouseWheel:
                if (GetBounds().Contains(event.position)) {
                    const float step =
                        event.is_shift ? 0.2f : 1.0f;
                    SetValue(value_ + step *
                                        (event.wheel_delta > 0
                                             ? 1.0f
                                             : -1.0f));
                    if (on_change_) on_change_(value_);
                    if (on_commit_) on_commit_(value_);
                    return true;
                }
                return false;
            default:
                return false;
        }
    }

    Layout::Size GetMinSize() const override {
        return {20.0f, 60.0f};
    }
    Layout::Size GetPreferredSize() const override {
        return {36.0f, 140.0f};
    }
    Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    void UpdateFromY(float y) {
        const Layout::Rect b = GetBounds();
        const float top = b.y + 4.0f;
        const float bottom = b.y + b.height - 4.0f;
        const float t = std::clamp((bottom - y) /
                                       std::max(1.0f, bottom - top),
                                   0.0f, 1.0f);
        SetValue(min_db_ + t * (max_db_ - min_db_));
        if (on_change_) on_change_(value_);
    }

    float min_db_ = kFaderMinDb;
    float max_db_ = kFaderMaxDb;
    float value_ = 0.0f;
    bool dragging_ = false;
    std::function<void(float)> on_change_;
    std::function<void(float)> on_commit_;
};

}  // namespace

// ---- Strip ---------------------------------------------------------------

class MixerPanel::Strip final : public W::Widget {
public:
    Strip(MixerPanel* owner, int track_index, std::string name,
          bool master)
        : owner_(owner), track_(track_index), master_(master) {
        SetFocusable(true);
        name_label_ =
            std::make_shared<W::Label>(std::move(name), !master);
        AddChild(name_label_);

        if (!master_) {
            mute_btn_ = std::make_shared<W::Button>(
                "M", W::ButtonStyle::Secondary);
            mute_btn_->SetSize(W::ButtonSize::Small);
            mute_btn_->SetCheckable(true);
            mute_btn_->SetTooltip("Mute track");
            mute_btn_->SetOnCheckedChanged([this](bool on) {
                if (owner_ != nullptr && !owner_->refreshing_) {
                    owner_->ApplyTrackMute(track_, on);
                }
            });
            solo_btn_ = std::make_shared<W::Button>(
                "S", W::ButtonStyle::Secondary);
            solo_btn_->SetSize(W::ButtonSize::Small);
            solo_btn_->SetCheckable(true);
            solo_btn_->SetTooltip(
                "Solo track (not in the track model yet)");
            solo_btn_->SetOnCheckedChanged([this](bool on) {
                if (owner_ != nullptr && !owner_->refreshing_) {
                    owner_->ApplyTrackSolo(track_, on);
                }
            });
            AddChild(mute_btn_);
            AddChild(solo_btn_);

            pan_knob_ = W::Knob::Create();
            pan_knob_->SetRange(-1.0f, 1.0f);
            pan_knob_->SetAnchor(0.0f);
            pan_knob_->SetDetents({0.0f});
            pan_knob_->SetResetValue(0.0f);
            pan_knob_->SetLabel("Pan");
            pan_knob_->SetShowTicks(false);
            pan_knob_->SetOnValueChange([this](float v) {
                if (owner_ != nullptr && !owner_->refreshing_) {
                    owner_->BeginGesture();
                    owner_->ApplyTrackPan(track_, v);
                }
            });
            pan_knob_->SetOnValueCommitted([this](float) {
                if (owner_ != nullptr) owner_->EndGesture();
            });
            AddChild(pan_knob_);
        }

        fader_ = std::make_shared<VFader>();
        fader_->SetDbRange(kFaderMinDb, kFaderMaxDb);
        fader_->SetTooltip(master_ ? "Master gain (dB)"
                                   : "Track gain (dB)");
        fader_->SetOnChange([this](float v) {
            if (owner_ == nullptr || owner_->refreshing_) return;
            owner_->BeginGesture();
            if (master_) {
                owner_->ApplyMasterGain(v);
            } else {
                owner_->ApplyTrackGain(track_, v);
            }
        });
        fader_->SetOnCommitted([this](float) {
            if (owner_ != nullptr) owner_->EndGesture();
        });
        AddChild(fader_);

        meter_ = std::make_shared<W::LevelMeter>();
        meter_->SetChannelCount(2);
        meter_->SetOrientation(W::LevelMeter::Orientation::Vertical);
        meter_->SetTaper(W::LevelMeter::Taper::Audio);
        meter_->SetBallistics(W::LevelMeter::Ballistics::Peak);
        meter_->SetDbRange(kFaderMinDb, 6.0f);
        meter_->SetFrameSource([this] {
            if (owner_ == nullptr || !owner_->levels_provider_) {
                return std::vector<W::LevelMeter::ChannelLevels>{};
            }
            const auto [l, r] = owner_->levels_provider_(track_);
            const float pl = DbFromLinear(l);
            const float pr = DbFromLinear(r);
            return std::vector<W::LevelMeter::ChannelLevels>{
                {pl, pl}, {pr, pr}};
        });
        AddChild(meter_);
    }

    int TrackIndex() const { return track_; }

    // Push document state into controls (Refresh rebuild path — no
    // hook feedback; the refreshing_ guard covers callbacks).
    void Sync(const Project* doc) {
        float gain = 0.0f, pan = 0.0f;
        bool muted = false;
        if (master_) {
            gain = doc != nullptr ? doc->master_gain_db : 0.0f;
        } else if (doc != nullptr && track_ >= 0 &&
                   track_ < static_cast<int>(doc->tracks.size())) {
            const Track& t =
                doc->tracks[static_cast<std::size_t>(track_)];
            gain = t.gain_db;
            pan = t.pan;
            muted = t.muted;
        }
        fader_->SetValue(gain);
        if (pan_knob_ != nullptr) pan_knob_->SetValue(pan);
        if (mute_btn_ != nullptr) mute_btn_->SetChecked(muted);
    }

    void Render(Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        renderer.FillRect(GetBounds(), tokens.panel, 0.0f);
        renderer.StrokeRect(GetBounds(), tokens.border, 1.0f, 0.0f);
    }

    void Layout(const Layout::Rect& bounds) override {
        Layout(bounds, CachedLayoutTokens());
    }
    void Layout(const Layout::Rect& bounds,
                const XER::UI::Theme::Tokens& tokens) override {
        SetBounds(bounds);
        float y = bounds.y + Theme::kSpaceXs;
        const float x = bounds.x + Theme::kSpaceXs;
        const float w = bounds.width - Theme::kSpaceXs * 2.0f;
        name_label_->Layout({x, y, w, kNameH}, tokens);
        y += kNameH + 2.0f;
        if (mute_btn_ != nullptr) {
            const float bw = (w - 2.0f) / 2.0f;
            mute_btn_->Layout({x, y, bw, kMsH}, tokens);
            solo_btn_->Layout({x + bw + 2.0f, y, bw, kMsH}, tokens);
            y += kMsH + 2.0f;
            pan_knob_->Layout({x, y, w, kKnobH}, tokens);
            y += kKnobH + 2.0f;
        }
        const float fh =
            bounds.y + bounds.height - Theme::kSpaceXs - y;
        const float fw = w - kMeterW - Theme::kSpaceXs;
        fader_->Layout({x, y, fw, fh}, tokens);
        meter_->Layout({x + fw + Theme::kSpaceXs, y, kMeterW, fh},
                       tokens);
    }

    bool HandleEvent(const W::Event&) override { return false; }
    void Tick(double dt) override { meter_->Tick(dt); }

    Layout::Size GetMinSize() const override {
        return {kStripW, 160.0f};
    }
    Layout::Size GetPreferredSize() const override {
        return {kStripW, 260.0f};
    }
    Layout::Size GetMaxSize() const override {
        return {kStripW * 1.5f, 100000.0f};
    }

private:
    MixerPanel* owner_;
    int track_;
    bool master_;
    std::shared_ptr<W::Label> name_label_;
    std::shared_ptr<W::Button> mute_btn_, solo_btn_;
    std::shared_ptr<W::Knob> pan_knob_;
    std::shared_ptr<VFader> fader_;
    std::shared_ptr<W::LevelMeter> meter_;
};

// ---- panel ---------------------------------------------------------------

std::shared_ptr<MixerPanel> MixerPanel::Create() {
    return std::shared_ptr<MixerPanel>(new MixerPanel());
}

MixerPanel::MixerPanel() {
    SetFocusable(true);
    SetClipsChildren(true);
}

void MixerPanel::SetHooks(MixerHooks hooks) {
    hooks_ = std::move(hooks);
}

void MixerPanel::SetDocument(const Project* project) {
    doc_ = project;
    Refresh();
}

void MixerPanel::SetLevelsProvider(
    std::function<std::pair<float, float>(int)> provider) {
    levels_provider_ = std::move(provider);
}

const Track* MixerPanel::TrackAt(int track_index) const {
    if (doc_ == nullptr || track_index < 0 ||
        track_index >= static_cast<int>(doc_->tracks.size())) {
        return nullptr;
    }
    return &doc_->tracks[static_cast<std::size_t>(track_index)];
}

std::shared_ptr<MixerPanel::Strip> MixerPanel::MakeStrip(
    int track_index, const std::string& name, bool master) {
    return std::make_shared<Strip>(this, track_index, name, master);
}

void MixerPanel::Refresh() {
    // Rebuild the strip set only when the audio-track signature
    // (index+name pairs) changes; otherwise push the new model state
    // into the same strips — fader/pan/mute live in the document,
    // never in the widgets.
    std::string sig;
    if (doc_ != nullptr) {
        for (int i = 0; i < static_cast<int>(doc_->tracks.size());
             ++i) {
            const Track& t =
                doc_->tracks[static_cast<std::size_t>(i)];
            if (t.kind == TrackKind::Audio) {
                sig += std::to_string(i);
                sig += ':';
                sig += t.name;
                sig += ';';
            }
        }
    }
    if (sig != doc_sig_) {
        doc_sig_ = sig;
        for (const auto& s : strips_) RemoveChild(s);
        strips_.clear();
        if (doc_ != nullptr) {
            for (int i = 0;
                 i < static_cast<int>(doc_->tracks.size()); ++i) {
                const Track& t =
                    doc_->tracks[static_cast<std::size_t>(i)];
                if (t.kind != TrackKind::Audio) continue;
                const std::string name =
                    t.name.empty() ? "A" + std::to_string(i + 1)
                                   : t.name;
                auto strip =
                    MakeStrip(i, name, /*master=*/false);
                strips_.push_back(strip);
                AddChild(strip);
            }
            auto master = MakeStrip(kMasterBus, "Master",
                                    /*master=*/true);
            strips_.push_back(master);
            AddChild(master);
        }
    }

    refreshing_ = true;
    const auto restore = [this] { refreshing_ = false; };
    for (const auto& strip : strips_) {
        strip->Sync(doc_);
    }
    restore();
    RequestLayout();
    Invalidate();
}

void MixerPanel::BeginGesture() {
    if (in_gesture_) return;
    in_gesture_ = true;
    if (hooks_.on_begin_macro) hooks_.on_begin_macro();
}

void MixerPanel::EndGesture() {
    if (!in_gesture_) return;
    in_gesture_ = false;
    if (hooks_.on_end_macro) hooks_.on_end_macro();
}

// ---- verbs ---------------------------------------------------------------

void MixerPanel::ApplyTrackGain(int track_index, float db) {
    if (TrackAt(track_index) == nullptr) return;
    if (hooks_.on_track_gain) {
        hooks_.on_track_gain(track_index, db);
    }
}

void MixerPanel::ApplyTrackPan(int track_index, float pan) {
    if (TrackAt(track_index) == nullptr) return;
    if (hooks_.on_track_pan) {
        hooks_.on_track_pan(track_index, std::clamp(pan, -1.0f, 1.0f));
    }
}

void MixerPanel::ApplyTrackMute(int track_index, bool muted) {
    if (TrackAt(track_index) == nullptr) return;
    if (hooks_.on_track_mute) {
        hooks_.on_track_mute(track_index, muted);
    }
}

void MixerPanel::ApplyTrackSolo(int track_index, bool solo) {
    if (TrackAt(track_index) == nullptr) return;
    if (hooks_.on_track_solo) {
        hooks_.on_track_solo(track_index, solo);
    } else if (hooks_.on_status) {
        // Track has no solo flag — honest deferred state.
        hooks_.on_status("Solo isn't wired into the track model "
                         "yet",
                         false);
    }
}

void MixerPanel::ApplyMasterGain(float db) {
    if (hooks_.on_master_gain) {
        hooks_.on_master_gain(db);
    }
}

int MixerPanel::StripTrack(int strip_index) const {
    if (strip_index < 0 ||
        strip_index >= static_cast<int>(strips_.size())) {
        return kMasterBus;
    }
    return strips_[static_cast<std::size_t>(strip_index)]
        ->TrackIndex();
}

W::Widget* MixerPanel::StripWidget(int strip_index) const {
    if (strip_index < 0 ||
        strip_index >= static_cast<int>(strips_.size())) {
        return nullptr;
    }
    return strips_[static_cast<std::size_t>(strip_index)].get();
}

// ---- widget contract ----------------------------------------------------

void MixerPanel::Render(Rendering::Renderer& renderer,
                        const XER::UI::Theme::Tokens& tokens) {
    renderer.FillRect(GetBounds(), tokens.panel, 0.0f);
}

void MixerPanel::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    // Strips lay out left-to-right, audio tracks first, master last —
    // each at its preferred width, vertically filling the panel.
    float x = bounds.x + Theme::kSpaceXs;
    const float h = bounds.height - Theme::kSpaceXs * 2.0f;
    for (const auto& strip : strips_) {
        const float w = std::clamp(
            strip->GetPreferredSize().width,
            strip->GetMinSize().width,
            strip->GetMaxSize().width);
        strip->Layout({x, bounds.y + Theme::kSpaceXs, w, h}, tokens);
        x += w + Theme::kSpaceXs;
    }
}

void MixerPanel::Tick(double dt_seconds) {
    for (const auto& strip : strips_) {
        strip->Tick(dt_seconds);
    }
}

}  // namespace XER::VideoEditor::Ui2
