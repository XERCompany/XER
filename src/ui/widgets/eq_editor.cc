#include "eq_editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

Theme::Color WithAlpha(Theme::Color color, float alpha) {
    color.a *= alpha;
    return color;
}

constexpr float kDbMin = -15.0f;
constexpr float kDbMax = 15.0f;
constexpr float kMinHz = 20.0f;
constexpr float kMaxHz = 20000.0f;
constexpr float kHandleRadius = 7.0f;
constexpr float kHitRadius = 13.0f;

std::string FormatFreq(float hz) {
    char buf[24];
    if (hz >= 1000.0f) {
        std::snprintf(buf, sizeof(buf), "%.1fk", hz / 1000.0f);
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f", hz);
    }
    return buf;
}

} // namespace

EqEditorWidget::EqEditorWidget(std::array<FxEqBandState, 5>* bands,
                               ResponseFn response,
                               std::function<void()> on_changed)
    : bands_(bands)
    , response_(std::move(response))
    , on_changed_(std::move(on_changed)) {}

Layout::Rect EqEditorWidget::PlotRect() const {
    const auto b = GetBounds();
    // Left strip for dB labels, bottom strip for frequency labels.
    return Layout::Rect{b.x + 30.0f, b.y + 6.0f,
                        b.width - 38.0f, b.height - 26.0f};
}

float EqEditorWidget::FxToX(float frequency_hz, const Layout::Rect& plot) {
    const float t = (std::log2(std::max(frequency_hz, kMinHz)) - std::log2(kMinHz)) /
                    (std::log2(kMaxHz) - std::log2(kMinHz));
    return plot.x + std::clamp(t, 0.0f, 1.0f) * plot.width;
}

float EqEditorWidget::DbToY(float db, const Layout::Rect& plot) {
    const float t = (std::clamp(db, kDbMin, kDbMax) - kDbMin) / (kDbMax - kDbMin);
    return plot.y + plot.height - t * plot.height;
}

float EqEditorWidget::XToFreq(float x, const Layout::Rect& plot) {
    const float t = std::clamp((x - plot.x) / std::max(plot.width, 1.0f), 0.0f, 1.0f);
    return kMinHz * std::pow(kMaxHz / kMinHz, t);
}

float EqEditorWidget::YToDb(float y, const Layout::Rect& plot) {
    const float t = std::clamp((plot.y + plot.height - y) /
                               std::max(plot.height, 1.0f), 0.0f, 1.0f);
    return kDbMin + t * (kDbMax - kDbMin);
}

Layout::Point EqEditorWidget::HandlePos(int band, const Layout::Rect& plot) const {
    const auto& b = (*bands_)[static_cast<std::size_t>(band)];
    return {FxToX(b.frequency_hz, plot), DbToY(b.gain_db, plot)};
}

int EqEditorWidget::HitHandle(const Layout::Point& position,
                              const Layout::Rect& plot) const {
    if (bands_ == nullptr) {
        return -1;
    }
    int best = -1;
    float best_dist = kHitRadius;
    for (int i = 0; i < 5; ++i) {
        const auto p = HandlePos(i, plot);
        const float dx = position.x - p.x;
        const float dy = position.y - p.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return best;
}

void EqEditorWidget::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

void EqEditorWidget::Render(Rendering::Renderer& renderer,
                            const Theme::Tokens& tokens) {
    if (bands_ == nullptr) {
        return;
    }
    const auto plot = PlotRect();
    if (plot.width < 40.0f || plot.height < 30.0f) {
        return;
    }

    // --- Grid ---
    static const float kGridFreqs[] = {50.f, 100.f, 200.f, 500.f, 1000.f,
                                       2000.f, 5000.f, 10000.f};
    static const char* kGridLabels[] = {"50", "100", "200", "500", "1k",
                                        "2k", "5k", "10k"};
    for (int i = 0; i < 8; ++i) {
        const float x = FxToX(kGridFreqs[i], plot);
        renderer.DrawLine({x, plot.y}, {x, plot.y + plot.height},
                          WithAlpha(tokens.border, 0.7f), 1.0f);
        renderer.DrawText(kGridLabels[i],
                          Layout::Rect{x - 14.0f, plot.y + plot.height + 4.0f,
                                       28.0f, 14.0f},
                          Rendering::TextStyle{tokens.muted, tokens.font_small, 400});
    }
    for (float db : {-12.0f, -6.0f, 0.0f, 6.0f, 12.0f}) {
        const float y = DbToY(db, plot);
        const bool center = (db == 0.0f);
        renderer.DrawLine({plot.x, y}, {plot.x + plot.width, y},
                          center ? WithAlpha(tokens.foreground, 0.4f)
                                 : WithAlpha(tokens.border, 0.7f),
                          1.0f);
        char label[8];
        std::snprintf(label, sizeof(label), "%+.0f", db);
        renderer.DrawText(label,
                          Layout::Rect{GetBounds().x + 2.0f, y - 7.0f, 26.0f, 14.0f},
                          Rendering::TextStyle{tokens.muted, tokens.font_small, 400});
    }

    // --- Response curve + soft fill ---
    if (response_) {
        Layout::Point prev{FxToX(kMinHz, plot), DbToY(response_(kMinHz), plot)};
        const float base_y = DbToY(0.0f, plot);
        for (int i = 1; i <= 96; ++i) {
            const float t = static_cast<float>(i) / 96.0f;
            const float freq = kMinHz * std::pow(kMaxHz / kMinHz, t);
            const Layout::Point cur{FxToX(freq, plot),
                                    DbToY(response_(freq), plot)};
            renderer.DrawLine(prev, cur, tokens.accent, 2.0f);
            prev = cur;
        }
        // Translucent fill between curve and 0 dB (every 3rd sample is
        // enough at this resolution).
        for (int i = 3; i <= 96; i += 3) {
            const float t = static_cast<float>(i) / 96.0f;
            const float freq = kMinHz * std::pow(kMaxHz / kMinHz, t);
            const Layout::Point cur{FxToX(freq, plot), DbToY(response_(freq), plot)};
            renderer.DrawLine(cur, {cur.x, base_y},
                              WithAlpha(tokens.accent, 0.05f), 1.0f);
        }
    }

    // --- Band handles ---
    for (int i = 0; i < 5; ++i) {
        const auto pos = HandlePos(i, plot);
        const bool active = (i == dragging_band_ || i == hover_band_);
        renderer.FillRect(Layout::Rect{pos.x - kHandleRadius, pos.y - kHandleRadius,
                                       kHandleRadius * 2.0f, kHandleRadius * 2.0f},
                          tokens.surface, kHandleRadius);
        // Circle approximation via stroke rect is not round — draw a small
        // filled square-ish handle with accent outline instead.
        renderer.StrokeRect(Layout::Rect{pos.x - kHandleRadius, pos.y - kHandleRadius,
                                         kHandleRadius * 2.0f, kHandleRadius * 2.0f},
                            active ? tokens.accent : WithAlpha(tokens.accent, 0.8f),
                            active ? 2.0f : 1.5f, kHandleRadius * 0.6f);
    }

    ApplyHoverText(renderer, tokens, plot);

    // Hint line.
    renderer.DrawText("drag bands · wheel = Q · click = reset",
                      Layout::Rect{GetBounds().x + 4.0f, GetBounds().y + 2.0f,
                                   GetBounds().width - 8.0f, 14.0f},
                      Rendering::TextStyle{WithAlpha(tokens.muted, 0.8f),
                                           tokens.font_small, 400});
}

void EqEditorWidget::ApplyHoverText(Rendering::Renderer& renderer,
                                    const Theme::Tokens& tokens,
                                    const Layout::Rect& plot) {
    const int band = dragging_band_ >= 0 ? dragging_band_ : hover_band_;
    if (band < 0 || bands_ == nullptr) {
        return;
    }
    const auto& b = (*bands_)[static_cast<std::size_t>(band)];
    char text[96];
    std::snprintf(text, sizeof(text), "Band %d — %s Hz  %+.1f dB  Q %.2f",
                  band + 1, FormatFreq(b.frequency_hz).c_str(),
                  b.gain_db, b.q);
    const auto pos = HandlePos(band, plot);
    Layout::Rect label{pos.x - 60.0f, pos.y - 34.0f, 160.0f, 18.0f};
    // Keep the label inside the widget.
    const auto bounds = GetBounds();
    label.x = std::clamp(label.x, bounds.x + 2.0f, bounds.x + bounds.width - 162.0f);
    label.y = std::clamp(label.y, bounds.y + 2.0f, bounds.y + bounds.height - 20.0f);
    renderer.FillRect(label, WithAlpha(tokens.surface, 0.95f), tokens.radius_small);
    renderer.DrawText(text, label,
                      Rendering::TextStyle{tokens.foreground, tokens.font_small, 500});
}

bool EqEditorWidget::HandleEvent(const Event& event) {
    if (bands_ == nullptr) {
        return false;
    }
    if (!IsEnabled()) {
        // A widget disabled mid-drag must not keep writing band values.
        if (dragging_band_ >= 0) {
            dragging_band_ = -1;
            just_dragged_ = false;
        }
        return false;
    }
    const auto plot = PlotRect();
    switch (event.type) {
    case Event::Type::MouseDown:
        if (event.mouse_button == MouseButton::Left) {
            const int hit = HitHandle(event.position, plot);
            if (hit >= 0) {
                dragging_band_ = hit;
                just_dragged_ = false;
                return true;
            }
        }
        return false;
    case Event::Type::MouseMove: {
        if (dragging_band_ >= 0) {
            auto& band = (*bands_)[static_cast<std::size_t>(dragging_band_)];
            band.frequency_hz = std::clamp(XToFreq(event.position.x, plot),
                                           kMinHz, kMaxHz);
            band.gain_db = std::clamp(YToDb(event.position.y, plot),
                                      kDbMin, kDbMax);
            just_dragged_ = true;
            if (on_changed_) {
                on_changed_();
            }
            Invalidate();
            return true;
        }
        const int hit = HitHandle(event.position, plot);
        if (hit != hover_band_) {
            hover_band_ = hit;
            Invalidate();
        }
        return false;
    }
    case Event::Type::MouseUp:
        if (event.mouse_button == MouseButton::Left && dragging_band_ >= 0) {
            dragging_band_ = -1;
            if (on_changed_) {
                on_changed_();
            }
            return true;
        }
        return false;
    case Event::Type::MouseWheel:
        if (hover_band_ >= 0 || dragging_band_ >= 0) {
            const int band = dragging_band_ >= 0 ? dragging_band_ : hover_band_;
            auto& b = (*bands_)[static_cast<std::size_t>(band)];
            const float factor = event.wheel_delta > 0 ? 1.18f : 1.0f / 1.18f;
            b.q = std::clamp(b.q * factor, 0.1f, 10.0f);
            if (on_changed_) {
                on_changed_();
            }
            Invalidate();
            return true;
        }
        return false;
    case Event::Type::Click:
        // Click (not drag) on a handle resets gain and Q.
        if (!just_dragged_) {
            const int hit = HitHandle(event.position, plot);
            if (hit >= 0) {
                auto& band = (*bands_)[static_cast<std::size_t>(hit)];
                band.gain_db = 0.0f;
                band.q = 1.0f;
                if (on_changed_) {
                    on_changed_();
                }
                Invalidate();
                return true;
            }
        }
        just_dragged_ = false;
        return false;
    default:
        return false;
    }
}

} // namespace XER::UI::Widgets
