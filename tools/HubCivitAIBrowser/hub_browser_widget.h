#pragma once

#include "hub_dialogs.h"
#include "hub_preview_cache.h"

#include "content/civitaimodels/civitai_model_downloader.h"
#include "ui/widgets/app_title_bar.h"
#include "ui/widgets/tabs.h"
#include "ui/widgets/widget.h"

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace XER::Tools::HubBrowser {

// Civitai Studio Hub built on src/ui + XER Cache / AsyncIO / Archive / Network:
//  - Workspace Tab 0: Civitai Hub (search, filters, live image previews, quick inspector)
//  - Workspace Tab 1: Model Inspector (full-size generated artwork gallery + prompt/JSON export)
//  - Workspace Tab 2: Downloads (download manager, cached disk models, ArchiveManager ZIP viewer)
//  - Workspace Tab 3: Settings (API key, TLS, page size, previews, theme, CacheManager stats)
class HubBrowserWidget final : public XER::UI::Widgets::Widget {
public:
    enum class ViewMode { List, Grid };
    enum class WorkspaceTab { Hub = 0, Inspector = 1, Downloads = 2, Settings = 3 };

    struct Category {
        std::string label;
        std::string api_type;  // "" = all types
    };

    HubBrowserWidget();
    ~HubBrowserWidget() override;

    XER::UI::Widgets::AppTitleBar* TitleBar() const { return title_bar_.get(); }

    // Callback invoked when the user changes Dark/Light mode from Settings.
    void SetOnThemeRequest(std::function<void(bool)> callback);

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
    void RebuildMenus();
    void RebuildWorkspaceMenu();
    void BuildTabBar();
    void BuildToolbar();
    void BuildInspectorControls();
    void BuildStudioInspectorControls();
    void BuildDownloadsControls();
    void BuildSettingsControls();

    void SwitchTab(WorkspaceTab tab, bool record_history = true);
    void NavigateHistory(int delta);
    void SyncChildVisibility();

    void LoadSettingsFromCache();
    void SaveSettingsToCache();
    void RefreshCacheStatsAsync();

    void RunSearch();
    void ResetPagination();
    void SelectRow(int index);
    void SelectFile(int file_index);
    void StartDownload(bool open_dialog = true);
    void OpenPreviewDialog();
    void OpenDownloadDialog(const std::shared_ptr<DownloadJob>& job);
    void SelectDownloadItem(int index);
    void CopyModelId();
    void SetStatus(const std::string& text);
    void SetViewMode(ViewMode mode);
    void SetLeftPanelVisible(bool visible);
    void SetRightPanelVisible(bool visible);

    const content::civitai::CivitaiModelVersion* SelectedVersion() const;
    std::string PrimaryFileNameForSelected() const;

    // Hit testing helpers
    int CategoryAt(float y) const;
    int RowAt(float y) const;
    int GridCellAt(const XER::UI::Layout::Point& p) const;
    int FileRowAt(float y) const;
    void ScrollTo(float y);

    void LayoutChildren(const XER::UI::Layout::Rect& b,
                        const XER::UI::Theme::Tokens& tokens);
    void RecomputeHitRects();

    // Drawing helpers
    void DrawTabBarExtras(XER::UI::Rendering::Renderer& r,
                          const XER::UI::Theme::Tokens& tokens);
    void DrawToolbar(XER::UI::Rendering::Renderer& r,
                     const XER::UI::Theme::Tokens& tokens);
    void DrawCategorySidebar(XER::UI::Rendering::Renderer& r,
                             const XER::UI::Theme::Tokens& tokens);
    void DrawResults(XER::UI::Rendering::Renderer& r,
                     const XER::UI::Theme::Tokens& tokens);
    void DrawInspector(XER::UI::Rendering::Renderer& r,
                       const XER::UI::Theme::Tokens& tokens);
    void DrawStudioInspectorTab(XER::UI::Rendering::Renderer& r,
                                const XER::UI::Theme::Tokens& tokens);
    void DrawDownloadsTab(XER::UI::Rendering::Renderer& r,
                          const XER::UI::Theme::Tokens& tokens);
    void DrawSettingsTab(XER::UI::Rendering::Renderer& r,
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

    // ---- workspace & navigation state ----
    WorkspaceTab active_tab_ = WorkspaceTab::Hub;
    std::vector<WorkspaceTab> tab_history_{WorkspaceTab::Hub};
    int tab_history_pos_ = 0;
    std::function<void(bool)> on_theme_request_;

    // ---- browse/search state ----
    std::string query_;
    std::string types_;
    std::string sort_ = "Highest Rated";
    bool nsfw_ = false;
    bool auto_previews_ = true;
    bool verify_tls_ = true;
    bool dark_theme_ = true;
    bool api_key_visible_ = false;
    std::string api_key_;
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
    int preview_image_idx_ = 0;
    float scroll_ = 0.0f;
    ViewMode view_mode_ = ViewMode::List;
    bool left_panel_visible_ = true;
    bool right_panel_visible_ = true;
    int active_category_ = 0;

    // ---- async search & downloads state ----
    std::future<std::pair<std::vector<content::civitai::CivitaiModel>,
                          std::string>> search_future_;
    bool searching_ = false;
    std::string status_ = "Ready. Browse models, inspect artworks, or download packages.";

    std::vector<std::shared_ptr<DownloadJob>> download_jobs_;
    std::vector<CachedDiskFile> cached_disk_files_;
    std::shared_ptr<DownloadJob> inspected_disk_job_;
    int selected_dl_index_ = -1;
    float dl_list_scroll_ = 0.0f;
    float dl_arch_scroll_ = 0.0f;

    CacheSummary cache_summary_;
    std::future<std::pair<CacheSummary, std::vector<CachedDiskFile>>> cache_scan_future_;
    bool scanning_cache_ = false;

    // ---- layout rects (widget-local DIP) ----
    XER::UI::Layout::Rect titlebar_rect_;
    XER::UI::Layout::Rect tabbar_rect_;
    XER::UI::Layout::Rect active_dl_pill_rect_;
    XER::UI::Layout::Rect body_rect_;
    XER::UI::Layout::Rect toolbar_rect_;
    XER::UI::Layout::Rect sidebar_rect_;
    XER::UI::Layout::Rect center_rect_;
    XER::UI::Layout::Rect results_header_rect_;
    XER::UI::Layout::Rect inspector_rect_;
    XER::UI::Layout::Rect inspector_preview_rect_;
    XER::UI::Layout::Rect inspector_prev_img_rect_;
    XER::UI::Layout::Rect inspector_next_img_rect_;
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

    // Studio Inspector tab rects
    XER::UI::Layout::Rect studio_stage_rect_;
    std::vector<XER::UI::Layout::Rect> studio_thumb_rects_;
    std::vector<XER::UI::Layout::Rect> studio_file_rects_;

    // Downloads tab rects
    XER::UI::Layout::Rect dl_left_list_rect_;
    XER::UI::Layout::Rect dl_right_arch_rect_;
    std::vector<XER::UI::Layout::Rect> dl_row_rects_;

    // drag state
    bool scrollbar_dragging_ = false;
    float scrollbar_grab_ = 0.0f;

    // ---- child widgets ----
    std::shared_ptr<XER::UI::Widgets::AppTitleBar> title_bar_;
    std::shared_ptr<XER::UI::Widgets::Tabs> workspace_tabs_;
    std::shared_ptr<XER::UI::Widgets::Widget> search_input_;
    std::shared_ptr<XER::UI::Widgets::Widget> search_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> type_select_;
    std::shared_ptr<XER::UI::Widgets::Widget> sort_select_;
    std::shared_ptr<XER::UI::Widgets::Widget> nsfw_switch_;
    std::shared_ptr<XER::UI::Widgets::Widget> refresh_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> prev_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> next_btn_;

    // Hub quick-inspector controls
    std::shared_ptr<XER::UI::Widgets::Widget> version_select_;
    std::shared_ptr<XER::UI::Widgets::Widget> download_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> preview_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> copy_id_btn_;

    // Studio Inspector tab controls
    std::shared_ptr<XER::UI::Widgets::Widget> studio_prev_img_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> studio_next_img_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> studio_zoom_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> studio_copy_prompt_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> studio_copy_json_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> studio_version_select_;
    std::shared_ptr<XER::UI::Widgets::Widget> studio_download_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> studio_inspect_zip_btn_;

    // Downloads tab controls
    std::shared_ptr<XER::UI::Widgets::Widget> dl_rescan_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> dl_open_dlg_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> dl_inspect_zip_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> dl_extract_zip_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> dl_copy_path_btn_;

    // Settings tab controls
    std::shared_ptr<XER::UI::Widgets::Widget> set_api_input_;
    std::shared_ptr<XER::UI::Widgets::Widget> set_toggle_key_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> set_save_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> set_tls_switch_;
    std::shared_ptr<XER::UI::Widgets::Widget> set_limit_select_;
    std::shared_ptr<XER::UI::Widgets::Widget> set_preview_switch_;
    std::shared_ptr<XER::UI::Widgets::Widget> set_nsfw_switch_;
    std::shared_ptr<XER::UI::Widgets::Widget> set_theme_select_;
    std::shared_ptr<XER::UI::Widgets::Widget> set_copy_cache_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> set_refresh_cache_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> set_clear_previews_btn_;
};

}  // namespace XER::Tools::HubBrowser
