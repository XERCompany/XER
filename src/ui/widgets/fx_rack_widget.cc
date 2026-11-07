#include "fx_rack_widget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app/app_audio_controller.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

std::string Fmt(float value, int precision = 2) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.*f", precision, value);
    return buf;
}

Theme::Color WithAlpha(Theme::Color color, float alpha) {
    color.a *= alpha;
    return color;
}

// Approximate peaking-EQ magnitude in dB at `frequency_hz`, as a gaussian
// bump in the log-frequency domain. Good enough for a response preview.
float BandResponseDb(float frequency_hz, float center_hz, float q, float gain_db) {
    if (center_hz <= 0.0f || gain_db == 0.0f) {
        return 0.0f;
    }
    const float sigma = 1.0f / (2.0f * std::sqrt(std::max(q, 0.1f)));
    const float x = std::log2(frequency_hz / center_hz) / sigma;
    return gain_db * std::exp(-0.5f * x * x);
}

float ParamValue(const std::vector<XER::Engine::Audio::FX::CustomEffectParameter>& params,
                 const char* name, float fallback) {
    for (const auto& p : params) {
        if (p.name == name) {
            return p.value;
        }
    }
    return fallback;
}

} // namespace

FxRackWidget::FxRackWidget(XER::App::AudioController* controller, std::shared_ptr<FxRackState> state,
                           FxPluginHost* plugin_host, void* editor_parent_hwnd)
    : controller_(controller)
    , state_(state ? std::move(state) : std::make_shared<FxRackState>())
    , plugin_host_(plugin_host)
    , editor_parent_hwnd_(editor_parent_hwnd) {

    // Restore the last rack configuration from src/cache before building
    // widgets, so sliders reflect the persisted values.
    state_->LoadFromCache();

    if (plugin_host_ != nullptr) {
        plugin_header_ = std::make_shared<Label>("VST3 Plugin");
        plugin_path_input_ = std::make_shared<TextInput>();
        plugin_select_ = std::make_shared<Select>(std::vector<std::string>{}, -1);
        plugin_select_->SetPlaceholder("Scan or enter a path...");
        plugin_scan_button_ = std::make_shared<Button>("Scan", ButtonStyle::Secondary);
        plugin_load_button_ = std::make_shared<Button>("Load", ButtonStyle::Secondary);
        plugin_unload_button_ = std::make_shared<Button>("Unload", ButtonStyle::Outline);
        plugin_status_label_ = std::make_shared<Label>("no plugin loaded", true);
        plugin_load_button_->SetOnClick([this]() { LoadPluginClicked(); });
        plugin_unload_button_->SetOnClick([this]() { UnloadPluginClicked(); });
        plugin_scan_button_->SetOnClick([this]() { ScanPluginsClicked(); });
        plugin_select_->SetOnChanged([this](int index) {
            if (index >= 0 && index < static_cast<int>(scanned_plugins_.size())) {
                plugin_path_input_->SetValue(scanned_plugins_[static_cast<std::size_t>(index)].path);
            }
        });
    }

    master_switch_ = std::make_shared<Switch>(state_->enabled);
    master_label_ = std::make_shared<Label>("FX Rack");
    live_label_ = std::make_shared<Label>("Idle", true);
    eq_header_ = std::make_shared<Label>("Equalizer");
    bass_header_ = std::make_shared<Label>("Bass Boost");
    reverb_header_ = std::make_shared<Label>("Reverb");
    chain_header_ = std::make_shared<Label>("Effect Chain");
    chain_map_header_ = std::make_shared<Label>("Signal Flow");
    chain_map_ = std::make_shared<ChainMapWidget>(&state_->nodes);

    master_switch_->SetOnChanged([this](bool enabled) {
        state_->enabled = enabled;
        MarkDirty();
    });

    // --- EQ: graphical editor over the 5 band states ---
    eq_editor_ = std::make_shared<EqEditorWidget>(
        &state_->eq_bands,
        [this](float frequency_hz) { return EqResponseDb(frequency_hz); },
        [this]() {
            MarkDirty();
            Invalidate();
        });

    // --- Bass boost ---
    bass_value_label_ = std::make_shared<Label>(Fmt(state_->bass_boost_db, 1) + " dB");
    bass_slider_ = std::make_shared<Slider>(0.0f, 12.0f, state_->bass_boost_db);
    bass_slider_->SetOnValueChanged([this](float value) {
        state_->bass_boost_db = value;
        bass_value_label_->SetText(Fmt(value, 1) + " dB");
        MarkDirty();
    });

    // --- Reverb: mix / room size / damping ---
    static const char* kReverbNames[3] = {"Mix", "Room size", "Damping"};
    for (int i = 0; i < 3; ++i) {
        reverb_row_labels_.push_back(std::make_shared<Label>(kReverbNames[i]));
    }
    reverb_row_labels_[0]->SetText("Mix " + Fmt(state_->reverb_mix, 2));
    reverb_row_labels_[1]->SetText("Room " + Fmt(state_->reverb_room_size, 2));
    reverb_row_labels_[2]->SetText("Damp " + Fmt(state_->reverb_damping, 2));

    reverb_sliders_.push_back(std::make_shared<Slider>(0.0f, 1.0f, state_->reverb_mix));
    reverb_sliders_.push_back(std::make_shared<Slider>(0.0f, 1.0f, state_->reverb_room_size));
    reverb_sliders_.push_back(std::make_shared<Slider>(0.0f, 1.0f, state_->reverb_damping));
    reverb_sliders_[0]->SetOnValueChanged([this](float value) {
        state_->reverb_mix = value;
        reverb_row_labels_[0]->SetText("Mix " + Fmt(value, 2));
        MarkDirty();
    });
    reverb_sliders_[1]->SetOnValueChanged([this](float value) {
        state_->reverb_room_size = value;
        reverb_row_labels_[1]->SetText("Room " + Fmt(value, 2));
        MarkDirty();
    });
    reverb_sliders_[2]->SetOnValueChanged([this](float value) {
        state_->reverb_damping = value;
        reverb_row_labels_[2]->SetText("Damp " + Fmt(value, 2));
        MarkDirty();
    });

    // --- Effect chain ---
    std::vector<std::string> algo_names;
    for (const auto algo : FxRackState::AvailableAlgorithms()) {
        algo_names.push_back(FxRackState::AlgorithmDisplayName(algo));
    }
    add_select_ = std::make_shared<Select>(algo_names, -1);
    add_select_->SetPlaceholder("Choose effect...");
    add_button_ = std::make_shared<Button>("Add", ButtonStyle::Secondary);
    add_button_->SetOnClick([this]() {
        const int index = add_select_->GetSelectedIndex();
        if (index < 0) {
            return;
        }
        const auto algorithms = FxRackState::AvailableAlgorithms();
        AddNode(algorithms[static_cast<std::size_t>(index)]);
        add_select_->SetSelectedIndex(-1);
    });

    RebuildNodeUi();
}

FxRackWidget::~FxRackWidget() {
    if (plugin_host_ != nullptr && (editor_attached_ || editor_attach_pending_)) {
        plugin_host_->RemoveEditorView();
    }
    state_->SaveToCache();
}

void FxRackWidget::LoadPluginClicked() {
    if (plugin_host_ == nullptr || plugin_path_input_ == nullptr) {
        return;
    }
    const std::string path = plugin_path_input_->GetValue();
    if (path.empty()) {
        plugin_status_label_->SetText("enter a .vst3 path first");
        Invalidate();
        return;
    }
    std::string error;
    if (!plugin_host_->LoadPlugin(path, &error)) {
        plugin_loaded_ = false;
        editor_attached_ = false;
        editor_attach_pending_ = false;
        plugin_status_label_->SetText(error.empty() ? "load failed" : error);
        Invalidate();
        return;
    }
    plugin_loaded_ = true;
    editor_attached_ = false;
    editor_attach_pending_ = plugin_host_->HasEditorView();
    UpdatePluginUi();
    // Card height changes once the editor area appears — request a relayout
    // so Layout() computes plugin_editor_rect_ before SyncEditorFrame runs.
    RequestLayout();
}

void FxRackWidget::ScanPluginsClicked() {
    if (plugin_host_ == nullptr) {
        return;
    }
    plugin_status_label_->SetText("scanning VST3...");
    Invalidate();
    scanned_plugins_ = plugin_host_->ScanPlugins();

    std::vector<std::string> options;
    options.reserve(scanned_plugins_.size());
    for (const auto& entry : scanned_plugins_) {
        std::string label = entry.name.empty() ? entry.path : entry.name;
        if (!entry.vendor.empty()) {
            label += " — " + entry.vendor;
        }
        options.push_back(std::move(label));
    }
    plugin_select_->SetOptions(std::move(options));
    plugin_select_->SetSelectedIndex(-1);

    if (scanned_plugins_.empty()) {
        plugin_status_label_->SetText("no VST3 plugins found");
    } else {
        plugin_status_label_->SetText(
            std::to_string(scanned_plugins_.size()) + " plugin(s) found — pick one or paste a path");
    }
    Invalidate();
}

void FxRackWidget::UnloadPluginClicked() {
    if (plugin_host_ == nullptr) {
        return;
    }
    if (editor_attached_ || editor_attach_pending_) {
        plugin_host_->RemoveEditorView();
    }
    plugin_host_->UnloadPlugin();
    plugin_loaded_ = false;
    editor_attached_ = false;
    editor_attach_pending_ = false;
    last_editor_frame_ = {};
    UpdatePluginUi();
    RequestLayout();
}

void FxRackWidget::UpdatePluginUi() {
    if (plugin_host_ == nullptr || plugin_status_label_ == nullptr) {
        return;
    }
    if (!plugin_loaded_) {
        plugin_status_label_->SetText("no plugin loaded");
        return;
    }
    const std::string name = plugin_host_->GetPluginName();
    if (editor_attached_) {
        plugin_status_label_->SetText(name + " — editor open");
    } else if (editor_attach_pending_) {
        plugin_status_label_->SetText(name + " — opening editor...");
    } else if (plugin_host_->HasEditorView()) {
        plugin_status_label_->SetText(name + " — editor available");
    } else {
        plugin_status_label_->SetText(name + " — no editor view");
    }
}

bool FxRackWidget::IsEffectivelyVisible() const {
    if (!IsVisible()) {
        return false;
    }
    for (auto p = GetParent(); p != nullptr; p = p->GetParent()) {
        if (!p->IsVisible()) {
            return false;
        }
    }
    return true;
}

void FxRackWidget::SyncEditorFrame() {
    if (plugin_host_ == nullptr || editor_parent_hwnd_ == nullptr ||
        !IsEffectivelyVisible()) {
        return;
    }
    // The plug-view is a child HWND — it is NOT clipped by the renderer, so
    // keep its frame inside the visible viewport (set by the ScrollArea).
    const auto visible = plugin_editor_rect_.Intersect(GetVisibleRect());
    const bool has_rect = visible.width > 1.0f && visible.height > 1.0f;
    const Layout::Rect frame = has_rect ? visible
        : Layout::Rect{GetVisibleRect().x, GetVisibleRect().y, 1.0f, 1.0f};
    const int l = static_cast<int>(frame.x);
    const int t = static_cast<int>(frame.y);
    const int r = static_cast<int>(frame.x + frame.width);
    const int b = static_cast<int>(frame.y + frame.height);
    if (editor_attach_pending_) {
        if (!has_rect && plugin_editor_rect_.width <= 1.0f) {
            return; // Editor area not laid out yet.
        }
        std::string error;
        if (plugin_host_->AttachEditorView(editor_parent_hwnd_, l, t, r, b, &error)) {
            editor_attached_ = true;
            editor_attach_pending_ = false;
            last_editor_frame_ = {static_cast<float>(l), static_cast<float>(t),
                                  static_cast<float>(r), static_cast<float>(b)};
        } else {
            editor_attach_pending_ = false;
            plugin_status_label_->SetText(error.empty() ? "editor attach failed" : error);
        }
        UpdatePluginUi();
        Invalidate();
        return;
    }
    if (editor_attached_) {
        const Layout::Rect scaled{static_cast<float>(l), static_cast<float>(t),
                                  static_cast<float>(r), static_cast<float>(b)};
        if (last_editor_frame_.x != scaled.x || last_editor_frame_.y != scaled.y ||
            last_editor_frame_.width != scaled.width || last_editor_frame_.height != scaled.height) {
            plugin_host_->SetEditorFrame(l, t, r, b);
            last_editor_frame_ = scaled;
        }
    }
}

void FxRackWidget::RebuildNodeUi() {
    for (auto& node : node_ui_) {
        DetachNodeChildren(node);
    }
    node_ui_.clear();

    for (std::size_t i = 0; i < state_->nodes.size(); ++i) {
        const auto& fx_node = state_->nodes[i];
        NodeUi ui;
        ui.title_label = std::make_shared<Label>(
            FxRackState::AlgorithmDisplayName(fx_node.algorithm) + " #" + std::to_string(i + 1));
        ui.enable_switch = std::make_shared<Switch>(fx_node.enabled);
        ui.remove_button = std::make_shared<Button>("Remove", ButtonStyle::Outline);
        ui.up_button = std::make_shared<Button>("^", ButtonStyle::Outline);
        ui.down_button = std::make_shared<Button>("v", ButtonStyle::Outline);

        const auto specs = FxRackState::ParamSpecsFor(fx_node.algorithm);
        for (std::size_t p = 0; p < specs.size(); ++p) {
            const float value = p < fx_node.params.size() ? fx_node.params[p].value : specs[p].default_value;
            ui.param_labels.push_back(std::make_shared<Label>(
                specs[p].label + " " + FormatValue(value, specs[p].minimum, specs[p].maximum)));
            auto slider = std::make_shared<Slider>(specs[p].minimum, specs[p].maximum, value);
            ui.param_sliders.push_back(slider);
        }

        if (FxRackState::SupportsWetMix(fx_node.algorithm)) {
            ui.wet_mix_label = std::make_shared<Label>("Mix " + Fmt(fx_node.wet_mix, 2));
            ui.wet_mix_slider = std::make_shared<Slider>(0.0f, 1.0f, fx_node.wet_mix);
        }

        node_ui_.push_back(std::move(ui));
    }
    RewireNodeCallbacks();

    if (children_attached_) {
        for (auto& node : node_ui_) {
            AttachNodeChildren(node);
        }
    }
}

void FxRackWidget::AttachNodeChildren(const NodeUi& node) {
    AddChild(node.title_label);
    AddChild(node.enable_switch);
    AddChild(node.remove_button);
    AddChild(node.up_button);
    AddChild(node.down_button);
    for (const auto& label : node.param_labels) {
        AddChild(label);
    }
    for (const auto& slider : node.param_sliders) {
        AddChild(slider);
    }
    if (node.wet_mix_label != nullptr) {
        AddChild(node.wet_mix_label);
    }
    if (node.wet_mix_slider != nullptr) {
        AddChild(node.wet_mix_slider);
    }
}

void FxRackWidget::DetachNodeChildren(const NodeUi& node) {
    RemoveChild(node.title_label);
    RemoveChild(node.enable_switch);
    RemoveChild(node.remove_button);
    RemoveChild(node.up_button);
    RemoveChild(node.down_button);
    for (const auto& label : node.param_labels) {
        RemoveChild(label);
    }
    for (const auto& slider : node.param_sliders) {
        RemoveChild(slider);
    }
    if (node.wet_mix_label != nullptr) {
        RemoveChild(node.wet_mix_label);
    }
    if (node.wet_mix_slider != nullptr) {
        RemoveChild(node.wet_mix_slider);
    }
}

void FxRackWidget::RewireNodeCallbacks() {
    for (std::size_t i = 0; i < node_ui_.size() && i < state_->nodes.size(); ++i) {
        auto& ui = node_ui_[i];
        ui.enable_switch->SetOnChanged([this, i](bool enabled) {
            if (i < state_->nodes.size()) {
                state_->nodes[i].enabled = enabled;
                MarkDirty();
            }
        });
        ui.remove_button->SetOnClick([this, i]() { RemoveNode(i); });
        ui.up_button->SetOnClick([this, i]() { MoveNode(i, -1); });
        ui.down_button->SetOnClick([this, i]() { MoveNode(i, +1); });
        const auto specs = FxRackState::ParamSpecsFor(state_->nodes[i].algorithm);
        for (std::size_t p = 0; p < ui.param_sliders.size(); ++p) {
            ui.param_sliders[p]->SetOnValueChanged([this, i, p, specs](float value) {
                if (i < state_->nodes.size() && p < state_->nodes[i].params.size() &&
                    p < node_ui_.size() && p < node_ui_[i].param_labels.size()) {
                    state_->nodes[i].params[p].value = value;
                    const auto& spec = specs[p];
                    node_ui_[i].param_labels[p]->SetText(
                        spec.label + " " + FormatValue(value, spec.minimum, spec.maximum));
                    MarkDirty();
                }
            });
        }
        if (ui.wet_mix_slider != nullptr) {
            ui.wet_mix_slider->SetOnValueChanged([this, i](float value) {
                if (i < state_->nodes.size()) {
                    state_->nodes[i].wet_mix = value;
                    if (i < node_ui_.size() && node_ui_[i].wet_mix_label != nullptr) {
                        node_ui_[i].wet_mix_label->SetText("Mix " + Fmt(value, 2));
                    }
                    MarkDirty();
                }
            });
        }
    }
}

void FxRackWidget::AddNode(XER::Engine::Audio::FX::CustomEffectAlgorithm algorithm) {
    state_->nodes.push_back(FxRackState::MakeDefaultNode(algorithm));
    RebuildNodeUi();
    RequestLayout();
    Invalidate();
    MarkDirty();
}

void FxRackWidget::RemoveNode(std::size_t index) {
    if (index >= state_->nodes.size()) {
        return;
    }
    state_->nodes.erase(state_->nodes.begin() + static_cast<std::ptrdiff_t>(index));
    RebuildNodeUi();
    RequestLayout();
    Invalidate();
    MarkDirty();
}

void FxRackWidget::MoveNode(std::size_t index, int delta) {
    if (index >= state_->nodes.size()) {
        return;
    }
    const std::size_t target = static_cast<std::size_t>(static_cast<int>(index) + delta);
    if (target >= state_->nodes.size() || delta == 0) {
        return;
    }
    std::swap(state_->nodes[index], state_->nodes[target]);
    RebuildNodeUi();
    RequestLayout();
    Invalidate();
    MarkDirty();
}

void FxRackWidget::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    if (!children_attached_) {
        AttachAllChildren();
    }
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.background, 0.0f);

    if (plugin_host_ != nullptr) {
        DrawCard(renderer, tokens, plugin_card_rect_);
        // When the native plug-view is attached it covers plugin_editor_rect_
        // with its own child HWND; nothing is drawn into that area.
    }
    DrawCard(renderer, tokens, eq_card_rect_);
    DrawCard(renderer, tokens, bass_card_rect_);
    DrawCard(renderer, tokens, reverb_card_rect_);
    DrawCard(renderer, tokens, map_card_rect_);
    DrawCard(renderer, tokens, chain_card_rect_);

    for (const auto& rect : node_card_rects_) {
        renderer.FillRect(rect, tokens.secondary, tokens.radius_medium);
        renderer.StrokeRect(rect, WithAlpha(tokens.border, 0.6f), 1.0f, tokens.radius_medium);
    }
}

void FxRackWidget::DrawCard(Rendering::Renderer& renderer, const Theme::Tokens& tokens,
                            Layout::Rect rect) {
    if (rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    renderer.FillRect(rect, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(rect, tokens.border, 1.0f, tokens.radius_large);
}

float FxRackWidget::EqResponseDb(float frequency_hz) const {
    double db = 0.0;
    for (const auto& band : state_->eq_bands) {
        db += BandResponseDb(frequency_hz, band.frequency_hz, band.q, band.gain_db);
    }
    if (state_->bass_boost_db > 0.01f) {
        db += BandResponseDb(frequency_hz, 110.0f, 0.7f, state_->bass_boost_db);
    }
    for (const auto& node : state_->nodes) {
        if (!node.enabled ||
            node.algorithm != XER::Engine::Audio::FX::CustomEffectAlgorithm::kParametricEq) {
            continue;
        }
        const float freq = ParamValue(node.params, "frequency_hz", 1000.0f);
        const float q = ParamValue(node.params, "q", 1.0f);
        const float gain = ParamValue(node.params, "gain_db", 0.0f);
        db += BandResponseDb(frequency_hz, freq, q, gain);
    }
    return static_cast<float>(std::clamp(db, -18.0, 18.0));
}

void FxRackWidget::AttachAllChildren() {
    children_attached_ = true;
    AddChild(master_switch_);
    AddChild(master_label_);
    AddChild(live_label_);
    if (plugin_host_ != nullptr) {
        AddChild(plugin_header_);
        AddChild(plugin_path_input_);
        AddChild(plugin_select_);
        AddChild(plugin_scan_button_);
        AddChild(plugin_load_button_);
        AddChild(plugin_unload_button_);
        AddChild(plugin_status_label_);
    }
    AddChild(eq_header_);
    AddChild(eq_editor_);
    AddChild(bass_header_);
    AddChild(bass_value_label_);
    AddChild(bass_slider_);
    AddChild(reverb_header_);
    for (auto& l : reverb_row_labels_) AddChild(l);
    for (auto& s : reverb_sliders_) AddChild(s);
    AddChild(chain_map_header_);
    AddChild(chain_map_);
    AddChild(chain_header_);
    AddChild(add_select_);
    AddChild(add_button_);
    for (auto& node : node_ui_) {
        AttachNodeChildren(node);
    }
}

void FxRackWidget::Layout(const Layout::Rect& bounds) {
    Layout(bounds, Theme::Tokens{});
}

void FxRackWidget::Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) {
    if (!children_attached_) {
        AttachAllChildren();
    }
    SetBounds(bounds);
    node_card_rects_.clear();

    const float spacing = tokens.spacing_large;
    const float card_gap = 12.0f;
    // `pad` is the x-origin; `y` is the vertical cursor. Both derive from
    // bounds so the widget works when embedded in a ScrollArea whose
    // content rect is offset by -scroll_y.
    const float pad = bounds.x + spacing;
    const float content_w = bounds.width - 2.0f * spacing;
    const float inner_w = content_w - 32.0f;
    const float inner_x = pad + 16.0f;
    float y = bounds.y + spacing;

    // --- Master row (flat, above cards) ---
    master_switch_->Layout(Layout::Rect{pad, y + 2.0f, 36.0f, 20.0f});
    master_label_->Layout(Layout::Rect{pad + 44.0f, y, 160.0f, 24.0f});
    live_label_->Layout(Layout::Rect{pad + content_w - 80.0f, y + 3.0f, 80.0f, 18.0f});
    y += 34.0f + card_gap;

    // --- VST3 plugin card (only when a plugin host bridge is provided) ---
    if (plugin_host_ != nullptr) {
        const bool show_editor = editor_attached_ || editor_attach_pending_;
        const float plugin_h = 16.0f + 24.0f + 8.0f + 30.0f + 8.0f + 30.0f + 8.0f +
                                (show_editor ? 308.0f : 20.0f) + 8.0f;
        plugin_card_rect_ = Layout::Rect{pad, y, content_w, plugin_h};
        float py = y + 16.0f;
        plugin_header_->Layout(Layout::Rect{inner_x, py, 200.0f, 24.0f});
        plugin_status_label_->Layout(Layout::Rect{inner_x + 210.0f, py + 3.0f,
                                                  inner_w - 210.0f, 18.0f});
        py += 32.0f;
        // Row 1: scanned-plugin dropdown + Load/Unload.
        plugin_select_->Layout(Layout::Rect{inner_x, py, inner_w - 148.0f, 30.0f});
        plugin_load_button_->Layout(Layout::Rect{inner_x + inner_w - 140.0f, py, 68.0f, 30.0f});
        plugin_unload_button_->Layout(Layout::Rect{inner_x + inner_w - 68.0f, py, 64.0f, 30.0f});
        py += 38.0f;
        // Row 2: manual path entry + Scan.
        plugin_path_input_->Layout(Layout::Rect{inner_x, py, inner_w - 92.0f, 30.0f});
        plugin_scan_button_->Layout(Layout::Rect{inner_x + inner_w - 84.0f, py, 80.0f, 30.0f});
        py += 40.0f;
        if (show_editor) {
            plugin_editor_rect_ = Layout::Rect{inner_x, py, inner_w, 300.0f};
            py += 308.0f;
        } else {
            plugin_editor_rect_ = Layout::Rect{};
            // Status text sits in the header row; keep a small footer row so
            // the card has breathing room under the input row.
            py += 0.0f;
        }
        y += plugin_h + card_gap;
    } else {
        plugin_card_rect_ = Layout::Rect{};
        plugin_editor_rect_ = Layout::Rect{};
    }
    SyncEditorFrame();

    auto slider_row = [&](float x, float w, float sy, const std::shared_ptr<Label>& label,
                          const std::shared_ptr<Slider>& slider) {
        label->Layout(Layout::Rect{x, sy, 92.0f, 24.0f});
        slider->Layout(Layout::Rect{x + 98.0f, sy + 1.0f, w - 98.0f, 22.0f});
    };

    // --- EQ card: header + graphical editor ---
    eq_card_rect_ = Layout::Rect{pad, y, content_w, 232.0f};
    float cy = y + 16.0f;
    eq_header_->Layout(Layout::Rect{inner_x, cy, 200.0f, 24.0f});
    cy += 28.0f;
    eq_editor_->Layout(Layout::Rect{inner_x, cy, inner_w, 176.0f});
    y += 232.0f + card_gap;

    // --- Bass card ---
    bass_card_rect_ = Layout::Rect{pad, y, content_w, 94.0f};
    cy = y + 16.0f;
    bass_header_->Layout(Layout::Rect{inner_x, cy, 200.0f, 24.0f});
    cy += 32.0f;
    slider_row(inner_x, inner_w, cy, bass_value_label_, bass_slider_);
    y += 94.0f + card_gap;

    // --- Reverb card ---
    reverb_card_rect_ = Layout::Rect{pad, y, content_w, 150.0f};
    cy = y + 16.0f;
    reverb_header_->Layout(Layout::Rect{inner_x, cy, 200.0f, 24.0f});
    cy += 32.0f;
    for (int i = 0; i < 3; ++i) {
        slider_row(inner_x, inner_w, cy + static_cast<float>(i) * 28.0f,
                   reverb_row_labels_[static_cast<std::size_t>(i)], reverb_sliders_[static_cast<std::size_t>(i)]);
    }
    y += 150.0f + card_gap;

    // --- Signal-flow map card (read-only node graph of the chain) ---
    map_card_rect_ = Layout::Rect{pad, y, content_w, 78.0f};
    cy = y + 14.0f;
    chain_map_header_->Layout(Layout::Rect{inner_x, cy, 200.0f, 20.0f});
    chain_map_->Layout(Layout::Rect{inner_x, cy + 24.0f, inner_w, 32.0f});
    y += 78.0f + card_gap;

    // --- Chain card (height depends on node count) ---
    float chain_h = 16.0f + 24.0f + 12.0f + 30.0f + 12.0f + 16.0f;
    for (const auto& node : node_ui_) {
        chain_h += 8.0f + 26.0f + 8.0f +
                   26.0f * static_cast<float>(node.param_sliders.size()) +
                   (node.wet_mix_slider != nullptr ? 30.0f : 0.0f) + 10.0f;
    }
    chain_card_rect_ = Layout::Rect{pad, y, content_w, chain_h};
    cy = y + 16.0f;
    chain_header_->Layout(Layout::Rect{inner_x, cy, 200.0f, 24.0f});
    cy += 30.0f;
    add_select_->Layout(Layout::Rect{inner_x, cy, inner_w - 76.0f, 30.0f});
    add_button_->Layout(Layout::Rect{inner_x + inner_w - 68.0f, cy, 68.0f, 30.0f});
    cy += 42.0f;

    for (std::size_t n = 0; n < node_ui_.size(); ++n) {
        auto& node = node_ui_[n];
        const float node_h = 8.0f + 26.0f + 8.0f +
                             26.0f * static_cast<float>(node.param_sliders.size()) +
                             (node.wet_mix_slider != nullptr ? 30.0f : 0.0f) + 10.0f;
        node_card_rects_.push_back(Layout::Rect{inner_x, cy, inner_w, node_h});

        node.title_label->Layout(Layout::Rect{inner_x + 12.0f, cy + 8.0f, inner_w - 260.0f, 24.0f});
        node.up_button->Layout(Layout::Rect{inner_x + inner_w - 236.0f, cy + 8.0f, 26.0f, 24.0f});
        node.down_button->Layout(Layout::Rect{inner_x + inner_w - 206.0f, cy + 8.0f, 26.0f, 24.0f});
        node.enable_switch->Layout(Layout::Rect{inner_x + inner_w - 172.0f, cy + 10.0f, 36.0f, 20.0f});
        node.remove_button->Layout(Layout::Rect{inner_x + inner_w - 128.0f, cy + 8.0f, 56.0f, 24.0f});
        float ny = cy + 36.0f;

        for (std::size_t p = 0; p < node.param_sliders.size(); ++p) {
            slider_row(inner_x + 16.0f, inner_w - 16.0f, ny + static_cast<float>(p) * 26.0f,
                       node.param_labels[p], node.param_sliders[p]);
        }
        ny += 26.0f * static_cast<float>(node.param_sliders.size());
        if (node.wet_mix_slider != nullptr) {
            slider_row(inner_x + 16.0f, inner_w - 16.0f, ny, node.wet_mix_label, node.wet_mix_slider);
            ny += 30.0f;
        }
        cy += node_h;
    }

    // Total scrollable content height, relative to this widget's top edge.
    // The chain card is always last; its rect already includes inner padding.
    content_height_ = chain_card_rect_.Bottom() + spacing - bounds.y;
}

bool FxRackWidget::HandleEvent(const Event&) {
    return false;
}

Layout::Size FxRackWidget::GetMinSize() const {
    return {560.0f, 480.0f};
}

Layout::Size FxRackWidget::GetPreferredSize() const {
    return {800.0f, 780.0f};
}

Layout::Size FxRackWidget::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

void FxRackWidget::Tick() {
    ApplyIfNeeded();

    // The plug-view is a real child HWND of the main window — it does not
    // follow widget visibility, so detach it while the FX tab is hidden and
    // let SyncEditorFrame re-attach (createView + attach) on return.
    if (!IsEffectivelyVisible()) {
        if (plugin_host_ != nullptr && (editor_attached_ || editor_attach_pending_)) {
            const bool can_recreate = plugin_host_->HasEditorView();
            plugin_host_->RemoveEditorView();
            editor_attached_ = false;
            editor_attach_pending_ = can_recreate;
        }
        return;
    }

    SyncEditorFrame();
    const bool live = state_->enabled && controller_ != nullptr &&
                      controller_->IsEffectLiveActive();
    live_label_->SetText(live ? "● Live" : "○ Idle");
}

void FxRackWidget::MarkDirty() {
    dirty_ = true;
}

void FxRackWidget::ApplyIfNeeded() {
    if (!dirty_ || controller_ == nullptr) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - last_apply_ < std::chrono::milliseconds(120)) {
        return;
    }
    last_apply_ = now;
    dirty_ = false;

    const auto package = state_->BuildPackage();
    if (package.nodes.empty()) {
        controller_->DisableEffectLive();
        return;
    }
    std::string error;
    if (controller_->ApplyEffectLive(package, &error)) {
        // Persist at most every ~3 s — the apply path itself is throttled,
        // but a disk write per slider tick is still too frequent.
        if (now - last_cache_save_ > std::chrono::seconds(3)) {
            last_cache_save_ = now;
            state_->SaveToCache();
        }
    }
}

std::string FxRackWidget::FormatValue(float value, float minimum, float maximum) {
    if (maximum - minimum >= 100.0f) {
        return Fmt(value, 0);
    }
    if (maximum - minimum >= 10.0f) {
        return Fmt(value, 1);
    }
    return Fmt(value, 2);
}

} // namespace XER::UI::Widgets
