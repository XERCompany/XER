#pragma once

#include "ui/widgets/app_title_bar.h"
#include "ui/widgets/widget.h"

#include "content/civitaimodels/civitai_model_downloader.h"

#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace XER::Tools::HubBrowser {

// Civitai model hub browser: IDE-style studio layout built on src/ui —
// custom AppTitleBar (workspace, menus, command pill, panel toggles, avatar,
// accent action pill, window controls), filter toolbar, collapsible category
// sidebar, list/grid results, model inspector with real download progress,
// and a status bar with cursor/page pagination.
class HubBrowserWidget final : public XER::UI::Widgets::Widget {
public:
    enum class ViewMode { List, Grid };

    struct Category {
        std::string label;
        std::string api_type;  // "" = all types
    };

    HubBrowserWidget();
    ~HubBrowserWidget() override;

    // Embedded titlebar — the owner wires window commands, theme toggle and
    // Window::SetTitleBarHitTest through this accessor.
    XER::UI::Widgets::AppTitleBar* TitleBar() const { return title_bar_.get(); }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    void Layout(const XER::UI::Layout::Rect& bounds,
                const XER::UI::Theme::Tokens& tokens) override;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override;
    XER::UI::Layout::Size GetPreferredSize() const override;
    XER::UI::Layout::Size GetMaxSize() const override;

private:
    void BuildTitleBar();
    void BuildToolbar();
    void BuildInspectorControls();

    void RunSearch();
    void ResetPagination();
    void SelectRow(int index);
    void SelectFile(int file_index);
    void StartDownload();
    void CopyModelId();
    void SetStatus(const std::string& text);
    void SetViewMode(ViewMode mode);
    void SetLeftPanelVisible(bool visible);
    void SetRightPanelVisible(bool visible);
    void SyncInspectorVisibility();

    // Hit zones computed during Layout (no renderer needed).
    int CategoryAt(float y) const;
    int RowAt(float y) const;
    int GridCellAt(const XER::UI::Layout::Point& p) const;
    int FileRowAt(float y) const;
    void ScrollTo(float y);

    void LayoutChildren(const XER::UI::Layout::Rect& b,
                        const XER::UI::Theme::Tokens& tokens);
    void RecomputeHitRects();

    // Drawing helpers
    void DrawToolbar(XER::UI::Rendering::Renderer& r,
                     const XER::UI::Theme::Tokens& tokens);
    void DrawCategorySidebar(XER::UI::Rendering::Renderer& r,
                             const XER::UI::Theme::Tokens& tokens);
    void DrawResults(XER::UI::Rendering::Renderer& r,
                     const XER::UI::Theme::Tokens& tokens);
    void DrawInspector(XER::UI::Rendering::Renderer& r,
                       const XER::UI::Theme::Tokens& tokens);
    void DrawStatusBar(XER::UI::Rendering::Renderer& r,
                       const XER::UI::Theme::Tokens& tokens);
    void DrawBadge(XER::UI::Rendering::Renderer& r,
                   const XER::UI::Theme::Tokens& tokens,
                   const std::string& text, XER::UI::Layout::Rect rect,
                   XER::UI::Theme::Color bg, XER::UI::Theme::Color fg);
    void DrawChevron(XER::UI::Rendering::Renderer& r,
                     const XER::UI::Theme::Tokens& tokens,
                     XER::UI::Layout::Rect box,
                     XER::UI::Theme::Color color);
    XER::UI::Theme::Color TypeColor(const std::string& type) const;
    std::string FmtCount(uint64_t v) const;
    std::string FmtBytes(uint64_t b) const;

    // ---- state ----
    std::string query_;
    std::string types_;
    std::string sort_ = "Highest Rated";
    bool nsfw_ = false;
    int limit_ = 24;
    int page_ = 1;
    std::string cursor_;
    std::string next_cursor_;
    bool has_next_ = false;
    std::vector<std::string> cursor_history_;
    int cursor_pos_ = 0;

    std::vector<content::civitai::CivitaiModel> results_;
    int selected_ = -1;
    int selected_file_ = -1;
    float scroll_ = 0.0f;
    ViewMode view_mode_ = ViewMode::List;
    bool left_panel_visible_ = true;
    bool right_panel_visible_ = true;
    int active_category_ = 0;

    // async state
    std::future<std::pair<std::vector<content::civitai::CivitaiModel>,
                          std::string>> search_future_;
    std::future<std::pair<bool, std::string>> dl_state_;
    std::atomic<float> dl_progress_{0.0f};
    std::atomic<bool> dl_active_{false};
    bool searching_ = false;
    bool downloading_ = false;
    std::string status_ = "Ready. Press Search or pick a category.";
    std::string last_error_;

    // layout rects (widget-local DIP)
    XER::UI::Layout::Rect titlebar_rect_;
    XER::UI::Layout::Rect toolbar_rect_;
    XER::UI::Layout::Rect sidebar_rect_;
    XER::UI::Layout::Rect center_rect_;
    XER::UI::Layout::Rect results_header_rect_;
    XER::UI::Layout::Rect inspector_rect_;
    XER::UI::Layout::Rect statusbar_rect_;
    XER::UI::Layout::Rect list_rect_;
    XER::UI::Layout::Rect scrollbar_rect_;
    XER::UI::Layout::Rect scrollbar_thumb_rect_;
    std::vector<XER::UI::Layout::Rect> category_rects_;
    std::vector<XER::UI::Layout::Rect> row_rects_;
    std::vector<XER::UI::Layout::Rect> grid_rects_;
    std::vector<XER::UI::Layout::Rect> file_rects_;
    int grid_start_idx_ = 0;
    XER::UI::Layout::Rect nsfw_switch_rect_;
    XER::UI::Layout::Rect nsfw_label_rect_;
    XER::UI::Layout::Rect prev_rect_;
    XER::UI::Layout::Rect next_rect_;
    XER::UI::Layout::Rect view_seg_rect_;
    XER::UI::Layout::Rect view_list_rect_;
    XER::UI::Layout::Rect view_grid_rect_;

    // drag state
    bool scrollbar_dragging_ = false;
    float scrollbar_grab_ = 0.0f;

    // child widgets
    std::shared_ptr<XER::UI::Widgets::AppTitleBar> title_bar_;
    std::shared_ptr<XER::UI::Widgets::Widget> search_input_;
    std::shared_ptr<XER::UI::Widgets::Widget> search_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> type_select_;
    std::shared_ptr<XER::UI::Widgets::Widget> sort_select_;
    std::shared_ptr<XER::UI::Widgets::Widget> nsfw_switch_;
    std::shared_ptr<XER::UI::Widgets::Widget> refresh_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> prev_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> next_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> version_select_;
    std::shared_ptr<XER::UI::Widgets::Widget> download_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> copy_id_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> progress_;
};

}  // namespace XER::Tools::HubBrowser
