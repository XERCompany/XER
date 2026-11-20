#include "keyframe_editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::VideoEditor::Ui {
namespace {

using XER::UI::Layout::Point;
using XER::UI::Layout::Rect;
using XER::UI::Theme::Color;

constexpr float kHitRadius = 9.0f;
constexpr float kDiamondRadius = 5.0f;
constexpr double kKeyTimeEpsilon = 1e-4;
constexpr auto kDoubleClickWindow = std::chrono::milliseconds(450);

Color WithAlpha(Color color, float alpha) {
    color.a *= alpha;
    return color;
}

// Default display range per property (widened at draw time to fit
// out-of-range key values).
std::pair<float, float> DefaultRange(KeyProp prop) {
    switch (prop) {
        case KeyProp::PosX:
        case KeyProp::PosY:     return {-1.0f, 1.0f};
        case KeyProp::Scale:    return {0.0f, 4.0f};
        case KeyProp::Rotation: return {-180.0f, 180.0f};
        case KeyProp::Opacity:  return {0.0f, 1.0f};
        case KeyProp::GainDb:   return {-60.0f, 12.0f};
    }
    return {0.0f, 1.0f};
}

// Diamond outline (4 strokes) + center fill. The renderer has no rotated
// quad primitive, so a stroked diamond reads best at this size.
void DrawDiamond(XER::UI::Rendering::Renderer& renderer, const Point& c,
                 float r, Color outline, bool filled, Color fill) {
    const Point top{c.x, c.y - r};
    const Point right{c.x + r, c.y};
    const Point bottom{c.x, c.y + r};
    const Point left{c.x - r, c.y};
    renderer.DrawLine(top, right, outline, 1.6f);
    renderer.DrawLine(right, bottom, outline, 1.6f);
    renderer.DrawLine(bottom, left, outline, 1.6f);
    renderer.DrawLine(left, top, outline, 1.6f);
    if (filled) {
        renderer.FillRect({c.x - r * 0.4f, c.y - r * 0.4f, r * 0.8f,
                           r * 0.8f},
                          fill, 1.0f);
    }
}

KeyEase NextEase(KeyEase ease) {
    switch (ease) {
        case KeyEase::Linear:    return KeyEase::EaseIn;
        case KeyEase::EaseIn:    return KeyEase::EaseOut;
        case KeyEase::EaseOut:   return KeyEase::EaseInOut;
        case KeyEase::EaseInOut: return KeyEase::Hold;
        case KeyEase::Hold:      return KeyEase::EaseInCubic;
        case KeyEase::EaseInCubic:  return KeyEase::EaseOutCubic;
        case KeyEase::EaseOutCubic: return KeyEase::ExpoOut;
        case KeyEase::ExpoOut:      return KeyEase::Back;
        case KeyEase::Back:         return KeyEase::Elastic;
        case KeyEase::Elastic:      return KeyEase::Bounce;
        case KeyEase::Bounce:       return KeyEase::Linear;
    }
    return KeyEase::Linear;
}

// Ctrl+C/Ctrl+V curve clipboard — shared across ALL widget instances so
// a curve copied off Opacity can paste onto PosX/Scale/etc. Static and
// process-local; the editor runs on a single UI thread.
std::vector<Keyframe>& CurveClipboard() {
    static std::vector<Keyframe> keys;
    return keys;
}

}  // namespace

KeyframeEditorWidget::KeyframeEditorWidget(Hooks hooks)
    : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetTooltip(
        "click: add key · drag: move · double-click: easing · "
        "right-click: delete");
}

void KeyframeEditorWidget::SetClip(const Clip* clip, KeyProp prop) {
    prop_ = prop;
    has_clip_ = clip != nullptr;
    if (clip != nullptr) clip_ = *clip;
    hover_key_ = -1;
    selected_key_ = -1;
    drag_key_ = -1;
    just_dragged_ = false;
    added_key_ = -1;
    Invalidate();
}

void KeyframeEditorWidget::SetProperty(KeyProp prop) {
    if (prop == prop_) return;
    prop_ = prop;
    hover_key_ = -1;
    selected_key_ = -1;
    drag_key_ = -1;
    Invalidate();
}

void KeyframeEditorWidget::SetPlayhead(double timeline_seconds) {
    if (timeline_seconds == playhead_) return;
    playhead_ = timeline_seconds;
    Invalidate();
}

Rect KeyframeEditorWidget::PlotRect() const {
    const auto b = GetBounds();
    // Top strip carries the property/easing header text.
    return {b.x + 8.0f, b.y + 20.0f, b.width - 16.0f, b.height - 30.0f};
}

std::vector<Keyframe>& KeyframeEditorWidget::Keys() {
    return clip_.keyframes[prop_];
}

const std::vector<Keyframe>& KeyframeEditorWidget::Keys() const {
    static const std::vector<Keyframe> empty;
    const auto it = clip_.keyframes.find(prop_);
    return it == clip_.keyframes.end() ? empty : it->second;
}

double KeyframeEditorWidget::TimeSpan() const {
    return std::max(clip_.duration, 0.001);
}

std::pair<float, float> KeyframeEditorWidget::ValueRange() const {
    auto [lo, hi] = DefaultRange(prop_);
    const float fallback = KeyPropFallback(clip_, prop_);
    lo = std::min(lo, fallback);
    hi = std::max(hi, fallback);
    for (const Keyframe& key : Keys()) {
        lo = std::min(lo, key.value);
        hi = std::max(hi, key.value);
    }
    const float pad = std::max((hi - lo) * 0.08f, 1e-3f);
    return {lo - pad, hi + pad};
}

float KeyframeEditorWidget::TimeToX(double u, const Rect& plot) const {
    const double t = std::clamp(u / TimeSpan(), 0.0, 1.0);
    return plot.x + static_cast<float>(t) * plot.width;
}

float KeyframeEditorWidget::ValueToY(float v, const Rect& plot) const {
    const auto [lo, hi] = ValueRange();
    const float t = (v - lo) / std::max(hi - lo, 1e-6f);
    return plot.y + plot.height - std::clamp(t, -0.2f, 1.2f) * plot.height;
}

double KeyframeEditorWidget::XToTime(float x, const Rect& plot) const {
    const float t = std::clamp((x - plot.x) / std::max(plot.width, 1.0f),
                               0.0f, 1.0f);
    return static_cast<double>(t) * TimeSpan();
}

float KeyframeEditorWidget::YToValue(float y, const Rect& plot) const {
    const auto [lo, hi] = ValueRange();
    const float t = std::clamp(
        (plot.y + plot.height - y) / std::max(plot.height, 1.0f), 0.0f,
        1.0f);
    return lo + t * (hi - lo);
}

int KeyframeEditorWidget::HitKey(const Point& position,
                                 const Rect& plot) const {
    int best = -1;
    float best_dist = kHitRadius;
    const auto& keys = Keys();
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const Point p{TimeToX(keys[i].time, plot),
                      ValueToY(keys[i].value, plot)};
        const float dx = position.x - p.x;
        const float dy = position.y - p.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < best_dist) {
            best_dist = dist;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void KeyframeEditorWidget::SortDedupeKeys(const Keyframe* preferred) {
    auto& keys = Keys();
    // Stable sort so equal-time keys keep their original order — the
    // last element of a run is then the most recently positioned key.
    std::stable_sort(keys.begin(), keys.end(),
                     [](const Keyframe& a, const Keyframe& b) {
                         return a.time < b.time;
                     });
    // Merge keys that landed on (nearly) the same time — the model
    // requires unique times. std::unique would keep the FIRST of each
    // equal run; the intent is the opposite — keep the later
    // (dragged-to) value, and when `preferred` is in the run keep that
    // exact key so a user's drag is never discarded.
    std::vector<Keyframe> merged;
    merged.reserve(keys.size());
    for (std::size_t i = 0; i < keys.size();) {
        std::size_t run_end = i + 1;
        while (run_end < keys.size() &&
               std::fabs(keys[run_end].time - keys[run_end - 1].time) <
                   kKeyTimeEpsilon) {
            ++run_end;
        }
        std::size_t keep = run_end - 1;   // last wins
        if (preferred != nullptr) {
            for (std::size_t k = i; k < run_end; ++k) {
                if (std::fabs(keys[k].value - preferred->value) <
                        1e-5f &&
                    keys[k].easing == preferred->easing) {
                    keep = k;
                    break;
                }
            }
        }
        merged.push_back(keys[keep]);
        i = run_end;
    }
    keys = std::move(merged);
}

void KeyframeEditorWidget::CommitKeys(const char* status) {
    if (!has_clip_ || !hooks_.on_set_keyframes) return;
    hooks_.on_set_keyframes(clip_.id, prop_, Keys());
    SetStatus(status);
}

void KeyframeEditorWidget::SetStatus(const std::string& text,
                                     bool is_error) {
    if (hooks_.on_status) hooks_.on_status(text, is_error);
}

void KeyframeEditorWidget::ShiftKeyIndices(int erased) {
    const auto fix = [erased](int* index) {
        if (*index == erased) {
            *index = -1;
        } else if (*index > erased) {
            --*index;
        }
    };
    fix(&selected_key_);
    fix(&drag_key_);
    fix(&hover_key_);
    fix(&last_click_key_);
    fix(&added_key_);
}

void KeyframeEditorWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, WithAlpha(tokens.background, 0.5f),
                      tokens.radius_medium);
    renderer.StrokeRect(bounds, tokens.border, 1.0f,
                        tokens.radius_medium);
    if (!has_clip_ || bounds.width < 40.0f) {
        renderer.DrawText("no clip", bounds,
                          XER::UI::Rendering::TextStyle{
                              tokens.muted, tokens.font_small, 400});
        return;
    }

    // Header: prop name + key count + selected easing.
    {
        const auto& keys = Keys();
        char header[96];
        if (selected_key_ >= 0 &&
            selected_key_ < static_cast<int>(keys.size())) {
            const Keyframe& k = keys[static_cast<std::size_t>(selected_key_)];
            std::snprintf(header, sizeof(header),
                          "%s  k%d  t=%.3f  v=%.3g  %s", KeyPropName(prop_),
                          selected_key_ + 1, k.time, static_cast<double>(k.value),
                          KeyEaseName(k.easing));
        } else {
            std::snprintf(header, sizeof(header), "%s  (%zu keys)",
                          KeyPropName(prop_), keys.size());
        }
        renderer.DrawText(header,
                          {bounds.x + 8.0f, bounds.y + 3.0f,
                           bounds.width - 16.0f, 15.0f},
                          XER::UI::Rendering::TextStyle{
                              tokens.foreground, tokens.font_small, 500});
    }

    const auto plot = PlotRect();
    if (plot.width < 20.0f || plot.height < 16.0f) return;

    // Horizontal mid grid lines + value labels at range edges.
    const auto [lo, hi] = ValueRange();
    for (int i = 0; i <= 2; ++i) {
        const float v = lo + (hi - lo) * static_cast<float>(i) / 2.0f;
        const float y = ValueToY(v, plot);
        renderer.DrawLine({plot.x, y}, {plot.x + plot.width, y},
                          WithAlpha(tokens.border, i == 1 ? 0.9f : 0.55f),
                          1.0f);
    }
    renderer.PushClip(plot);

    // Curve: sample EvaluateClipProperty densely; add segment-boundary
    // samples so Hold steps render as vertical jumps, not ramps.
    {
        std::vector<double> us;
        constexpr int kSamples = 160;
        us.reserve(kSamples + Keys().size() * 2 + 2);
        for (int i = 0; i <= kSamples; ++i) {
            us.push_back(TimeSpan() * static_cast<double>(i) / kSamples);
        }
        const auto& keys = Keys();
        for (std::size_t i = 1; i < keys.size(); ++i) {
            us.push_back(std::max(0.0, keys[i].time - kKeyTimeEpsilon));
            us.push_back(keys[i].time);
        }
        std::sort(us.begin(), us.end());
        const float fallback = KeyPropFallback(clip_, prop_);
        Point prev{TimeToX(us.front(), plot),
                   ValueToY(EvaluateClipProperty(clip_, prop_, us.front(),
                                                 fallback),
                            plot)};
        for (std::size_t i = 1; i < us.size(); ++i) {
            const Point cur{
                TimeToX(us[i], plot),
                ValueToY(EvaluateClipProperty(clip_, prop_, us[i], fallback),
                         plot)};
            renderer.DrawLine(prev, cur, tokens.accent, 1.6f);
            prev = cur;
        }
    }

    // Keyframe diamonds.
    {
        const auto& keys = Keys();
        for (std::size_t i = 0; i < keys.size(); ++i) {
            const Point p{TimeToX(keys[i].time, plot),
                          ValueToY(keys[i].value, plot)};
            const int idx = static_cast<int>(i);
            const bool active = idx == selected_key_ || idx == drag_key_;
            const bool hover = idx == hover_key_;
            const Color outline = active
                ? tokens.accent
                : (hover ? tokens.foreground : tokens.accent);
            DrawDiamond(renderer, p, kDiamondRadius, outline, active || hover,
                        active ? tokens.accent
                               : WithAlpha(tokens.accent, 0.5f));
        }
    }

    // Playhead (clip-local position of the timeline playhead).
    const double u = playhead_ - clip_.timeline_start;
    if (u >= 0.0 && u <= TimeSpan()) {
        const float x = TimeToX(u, plot);
        renderer.DrawLine({x, plot.y}, {x, plot.y + plot.height},
                          tokens.foreground, 1.0f);
        renderer.DrawLine({x - 1.0f, plot.y}, {x - 1.0f, plot.y + plot.height},
                          WithAlpha(tokens.background, 0.4f), 1.0f);
    }
    renderer.PopClip();

    // Footer hint.
    renderer.DrawText(
        "click=add · drag=move · 2x=ease · rclick=del",
        {bounds.x + 8.0f, bounds.y + bounds.height - 14.0f,
         bounds.width - 16.0f, 12.0f},
        XER::UI::Rendering::TextStyle{WithAlpha(tokens.muted, 0.8f),
                                      tokens.font_small, 400});
}

void KeyframeEditorWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool KeyframeEditorWidget::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    if (!has_clip_) return false;
    const auto plot = PlotRect();

    switch (event.type) {
        case XER::UI::Widgets::Event::Type::MouseDown: {
            if (event.mouse_button ==
                XER::UI::Widgets::MouseButton::Right) {
                const int hit = HitKey(event.position, plot);
                if (hit >= 0) {
                    auto& keys = Keys();
                    keys.erase(keys.begin() + hit);
                    ShiftKeyIndices(hit);
                    CommitKeys("key removed");
                    Invalidate();
                    return true;
                }
                return false;
            }
            if (event.mouse_button ==
                XER::UI::Widgets::MouseButton::Left) {
                press_pos_ = event.position;
                press_valid_ = true;
                const int hit = HitKey(event.position, plot);
                if (hit >= 0) {
                    drag_key_ = hit;
                    selected_key_ = hit;
                    drag_moved_ = false;
                    return true;
                }
            }
            return false;
        }
        case XER::UI::Widgets::Event::Type::MouseMove: {
            if (drag_key_ >= 0) {
                auto& keys = Keys();
                if (drag_key_ < static_cast<int>(keys.size())) {
                    Keyframe& key = keys[static_cast<std::size_t>(drag_key_)];
                    key.time = std::clamp(XToTime(event.position.x, plot),
                                          0.0, TimeSpan());
                    key.value = YToValue(event.position.y, plot);
                    drag_moved_ = true;
                    Invalidate();
                }
                return true;
            }
            const int hit = HitKey(event.position, plot);
            if (hit != hover_key_) {
                hover_key_ = hit;
                Invalidate();
            }
            return false;
        }
        case XER::UI::Widgets::Event::Type::MouseUp: {
            if (event.mouse_button ==
                    XER::UI::Widgets::MouseButton::Left &&
                drag_key_ >= 0) {
                if (drag_moved_) {
                    // Reselect after sort: find the dragged key by its
                    // (new) time+value identity.
                    const auto& keys = Keys();
                    const Keyframe moved = keys[static_cast<std::size_t>(
                        drag_key_)];
                    // The dragged key wins any equal-time merge — its
                    // dragged-to value is the user's intent.
                    SortDedupeKeys(&moved);
                    const auto& sorted = Keys();
                    selected_key_ = -1;
                    for (std::size_t i = 0; i < sorted.size(); ++i) {
                        if (std::fabs(sorted[i].time - moved.time) <
                                kKeyTimeEpsilon &&
                            std::fabs(sorted[i].value - moved.value) < 1e-5f) {
                            selected_key_ = static_cast<int>(i);
                            break;
                        }
                    }
                    CommitKeys("key moved");
                }
                just_dragged_ = drag_moved_;
                drag_key_ = -1;
                drag_moved_ = false;
                Invalidate();
                return true;
            }
            return false;
        }
        case XER::UI::Widgets::Event::Type::Click: {
            if (just_dragged_) {
                just_dragged_ = false;
                press_valid_ = false;
                return true;
            }
            // A drag that started on empty space ends as a Click too —
            // ignore it when the pointer actually moved.
            if (press_valid_) {
                const float dx = event.position.x - press_pos_.x;
                const float dy = event.position.y - press_pos_.y;
                press_valid_ = false;
                if (dx * dx + dy * dy > 36.0f) return true;
            }
            const int hit = HitKey(event.position, plot);
            const auto now = std::chrono::steady_clock::now();
            if (hit >= 0) {
                const bool double_click =
                    hit == last_click_key_ &&
                    (now - last_click_time_) < kDoubleClickWindow &&
                    !(hit == added_key_ &&
                      (now - added_key_time_) < kDoubleClickWindow);
                last_click_key_ = hit;
                last_click_time_ = now;
                selected_key_ = hit;
                if (double_click) {
                    auto& keys = Keys();
                    keys[static_cast<std::size_t>(hit)].easing =
                        NextEase(keys[static_cast<std::size_t>(hit)].easing);
                    CommitKeys("easing changed");
                    last_click_key_ = -1;  // require a fresh double-click
                }
                Invalidate();
                return true;
            }
            // Empty area: add a key at the clicked (time, value). A key
            // within epsilon is replaced instead of duplicated.
            if (!plot.Contains(event.position)) return false;
            last_click_key_ = -1;
            Keyframe key;
            key.time = XToTime(event.position.x, plot);
            key.value = YToValue(event.position.y, plot);
            auto& keys = Keys();
            auto at = std::find_if(keys.begin(), keys.end(),
                                   [&](const Keyframe& k) {
                                       return std::fabs(k.time - key.time) <
                                              0.02;
                                   });
            if (at != keys.end()) {
                at->value = key.value;
                selected_key_ = static_cast<int>(at - keys.begin());
            } else {
                keys.push_back(key);
                SortDedupeKeys(&key);
                const auto& sorted = Keys();
                for (std::size_t i = 0; i < sorted.size(); ++i) {
                    if (std::fabs(sorted[i].time - key.time) <
                        kKeyTimeEpsilon) {
                        selected_key_ = static_cast<int>(i);
                        break;
                    }
                }
            }
            added_key_ = selected_key_;
            added_key_time_ = now;
            CommitKeys("key added");
            Invalidate();
            return true;
        }
        case XER::UI::Widgets::Event::Type::KeyDown: {
            // Letter codes arrive only with Ctrl held.
            if (event.key == XER::UI::Widgets::KeyCode::C) {
                CurveClipboard() = Keys();
                SetStatus("curve copied");
                return true;
            }
            if (event.key == XER::UI::Widgets::KeyCode::V &&
                !CurveClipboard().empty()) {
                const double dur = std::max(clip_.duration, 1e-6);
                auto& keys = Keys();
                keys = CurveClipboard();
                for (auto& k : keys) k.time = std::clamp(k.time, 0.0, dur);
                SortDedupeKeys();
                CommitKeys("curve pasted");
                Invalidate();
                return true;
            }
            if (event.key == XER::UI::Widgets::KeyCode::S) {
                StaggerProperties();
                return true;
            }
            if ((event.key == XER::UI::Widgets::KeyCode::Delete ||
                 event.key == XER::UI::Widgets::KeyCode::Backspace) &&
                selected_key_ >= 0) {
                auto& keys = Keys();
                if (selected_key_ < static_cast<int>(keys.size())) {
                    const int erased = selected_key_;
                    keys.erase(keys.begin() + erased);
                    ShiftKeyIndices(erased);
                    CommitKeys("key removed");
                    Invalidate();
                }
                return true;
            }
            return false;
        }
        default:
            return false;
    }
}

// Shift every OTHER animatable property's keys by a per-property step so
// curves land on different frames ("checkerboard" stagger). The current
// property stays as the reference grid; each subsequent property moves
// one step later. Keys shifted past the clip tail are merged away by
// SortDedupe, so only an in-range tail survives the move.
void KeyframeEditorWidget::StaggerProperties() {
    if (!has_clip_ || !hooks_.on_set_keyframes) return;
    const double step = std::clamp(clip_.duration / 20.0, 0.05, 0.5);
    int index = 0;
    int touched = 0;
    for (const KeyProp prop : AllKeyProps()) {
        if (prop == prop_) {
            ++index;
            continue;
        }
        auto keys = clip_.keyframes[prop];
        if (keys.empty()) {
            ++index;
            continue;
        }
        const double shift = step * index;
        for (auto& k : keys) {
            k.time = std::clamp(k.time + shift, 0.0, clip_.duration);
        }
        clip_.keyframes[prop] = keys;
        hooks_.on_set_keyframes(clip_.id, prop, keys);
        ++index;
        ++touched;
    }
    SetStatus(touched ? "keys staggered by " + std::to_string(step).substr(0, 4) + "s"
                      : "no other keyframed properties");
}

void KeyframeEditorWidget::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
