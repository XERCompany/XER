#ifndef XER_UI_WIDGETS_FX_RACK_WIDGET_H
#define XER_UI_WIDGETS_FX_RACK_WIDGET_H

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "widgets/widget.h"
#include "widgets/button.h"
#include "widgets/slider.h"
#include "widgets/label.h"
#include "widgets/switch.h"
#include "widgets/select.h"
#include "widgets/fx_rack_state.h"
#include "widgets/text_input.h"
#include "widgets/eq_editor.h"
#include "widgets/chain_map_widget.h"

namespace XER::App {
class AudioController;
}

namespace XER::UI::Widgets {

// One discovered VST3 plugin, as reported by FxPluginHost::ScanPlugins.
struct FxPluginEntry {
    std::string path;    // reference passed to LoadPlugin (bundle dir/file)
    std::string name;
    std::string vendor;
    std::string category;
};

// Bridge to a VST3 plugin host, kept UI-side so the widget layer does not
// depend on the audio/plugin libraries. Implemented by the app layer.
class FxPluginHost {
public:
    virtual ~FxPluginHost() = default;
    virtual bool IsLoaded() const = 0;
    // Scans the standard VST3 locations; may take a moment (loads each
    // module's factory). Returns discovered plugins, empty on failure.
    virtual std::vector<FxPluginEntry> ScanPlugins() = 0;
    virtual bool LoadPlugin(const std::string& path, std::string* error_out) = 0;
    virtual void UnloadPlugin() = 0;
    virtual std::string GetPluginName() const = 0;
    virtual bool HasEditorView() const = 0;
    // parent_hwnd is the FX window HWND; rects are its client-area pixels.
    virtual bool AttachEditorView(void* parent_hwnd, int left, int top, int right, int bottom,
                                  std::string* error_out) = 0;
    virtual void SetEditorFrame(int left, int top, int right, int bottom) = 0;
    virtual void RemoveEditorView() = 0;
};

// Full FX-rack editor hosted in its own window: master enable, 5-band EQ
// (gain/freq/Q per band), bass boost, reverb (mix/room/damping) and a
// user-assembled chain of effect nodes with every engine parameter exposed.
// Edits FxRackState and applies the compiled package live, throttled.
class FxRackWidget final : public Widget {
public:
    FxRackWidget(XER::App::AudioController* controller, std::shared_ptr<FxRackState> state,
                 FxPluginHost* plugin_host = nullptr, void* editor_parent_hwnd = nullptr);
    ~FxRackWidget() override;

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;
    float GetContentHeight() const override { return content_height_; }

    // Frame callback entry point: applies pending FX changes (~8 Hz max).
    void Tick();

private:
    struct NodeUi {
        std::shared_ptr<Label> title_label;
        std::shared_ptr<Switch> enable_switch;
        std::shared_ptr<Button> remove_button;
        std::shared_ptr<Button> up_button;
        std::shared_ptr<Button> down_button;
        std::vector<std::shared_ptr<Label>> param_labels;
        std::vector<std::shared_ptr<Slider>> param_sliders;
        std::shared_ptr<Label> wet_mix_label;
        std::shared_ptr<Slider> wet_mix_slider;
    };

    void MarkDirty();
    void ApplyIfNeeded();
    void RebuildNodeUi();
    void RewireNodeCallbacks();
    void AddNode(XER::Audio::FX::CustomEffectAlgorithm algorithm);
    void RemoveNode(std::size_t index);
    void MoveNode(std::size_t index, int delta);
    void AttachAllChildren();
    void DetachAllChildren();
    void AttachNodeChildren(const NodeUi& node);
    void DetachNodeChildren(const NodeUi& node);
    void LoadPluginClicked();
    void UnloadPluginClicked();
    void ScanPluginsClicked();
    void UpdatePluginUi();
    void SyncEditorFrame();
    bool IsEffectivelyVisible() const;
    void DrawCard(Rendering::Renderer& renderer, const Theme::Tokens& tokens,
                  Layout::Rect rect);
    float EqResponseDb(float frequency_hz) const;

    static std::string FormatValue(float value, float min, float max);

    XER::App::AudioController* controller_;
    std::shared_ptr<FxRackState> state_;
    FxPluginHost* plugin_host_ = nullptr;
    void* editor_parent_hwnd_ = nullptr;

    // VST3 plugin section.
    std::shared_ptr<Label> plugin_header_;
    std::shared_ptr<TextInput> plugin_path_input_;
    std::shared_ptr<Select> plugin_select_;
    std::shared_ptr<Button> plugin_scan_button_;
    std::shared_ptr<Button> plugin_load_button_;
    std::shared_ptr<Button> plugin_unload_button_;
    std::shared_ptr<Label> plugin_status_label_;
    std::vector<FxPluginEntry> scanned_plugins_;
    bool plugin_loaded_ = false;
    bool editor_attached_ = false;
    bool editor_attach_pending_ = false;
    Layout::Rect plugin_card_rect_{};
    Layout::Rect plugin_editor_rect_{};
    // Last frame sent to the plug-view via onSize; avoids redundant calls.
    Layout::Rect last_editor_frame_{};

    std::shared_ptr<Switch> master_switch_;
    std::shared_ptr<Label> master_label_;
    std::shared_ptr<Label> live_label_;
    std::shared_ptr<Label> eq_header_;
    std::shared_ptr<EqEditorWidget> eq_editor_;

    std::shared_ptr<Label> bass_header_;
    std::shared_ptr<Label> bass_value_label_;
    std::shared_ptr<Slider> bass_slider_;

    std::shared_ptr<Label> reverb_header_;
    std::vector<std::shared_ptr<Label>> reverb_row_labels_;
    std::vector<std::shared_ptr<Slider>> reverb_sliders_;

    std::shared_ptr<Label> chain_map_header_;
    std::shared_ptr<ChainMapWidget> chain_map_;

    std::shared_ptr<Label> chain_header_;
    std::shared_ptr<Select> add_select_;
    std::shared_ptr<Button> add_button_;
    std::vector<NodeUi> node_ui_;

    bool children_attached_ = false;
    bool dirty_ = true;
    std::chrono::steady_clock::time_point last_apply_{};
    std::chrono::steady_clock::time_point last_cache_save_{};

    // Card geometry computed during Layout, consumed by Render.
    Layout::Rect eq_card_rect_{};
    Layout::Rect bass_card_rect_{};
    Layout::Rect reverb_card_rect_{};
    Layout::Rect map_card_rect_{};
    Layout::Rect chain_card_rect_{};
    std::vector<Layout::Rect> node_card_rects_;
    // Total height of the scrollable content, updated by Layout().
    float content_height_ = 780.0f;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_FX_RACK_WIDGET_H
