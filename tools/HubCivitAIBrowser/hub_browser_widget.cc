#include "hub_browser_widget.h"

#include "ui/layout/flex.h"
#include "ui/rendering/renderer.h"
#include "ui/widgets/button.h"
#include "ui/widgets/progress.h"
#include "ui/widgets/select.h"
#include "ui/widgets/switch.h"
#include "ui/widgets/text_input.h"

#include "content/clipboard/clipboard.h"
#include "helper/env_utils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace XER::Tools::HubBrowser {

using namespace XER::UI;
using XER::UI::Widgets::AppTitleBar;
using XER::UI::Widgets::Button;
using XER::UI::Widgets::ButtonStyle;
using XER::UI::Widgets::Event;
using XER::UI::Widgets::MouseButton;
using XER::UI::Widgets::Progress;
using XER::UI::Widgets::Select;
using XER::UI::Widgets::Switch;
using XER::UI::Widgets::TextInput;
using XER::UI::Widgets::Widget;

namespace {

constexpr float kTitleBarH = 38.0f;
constexpr float kToolbarH = 54.0f;
constexpr float kStatusH = 28.0f;
constexpr float kSidebarW = 208.0f;
constexpr float kInspectorW = 330.0f;
constexpr float kRowH = 64.0f;
constexpr float kRowGap = 6.0f;
constexpr float kGridCardW = 236.0f;
constexpr float kGridCardH = 262.0f;
constexpr float kGridGapX = 12.0f;
constexpr float kGridGapY = 14.0f;
constexpr float kFileRowH = 30.0f;
constexpr float kCatRowH = 30.0f;

const std::vector<HubBrowserWidget::Category>& Categories() {
    static const std::vector<HubBrowserWidget::Category> cats = {
        {"All Models", ""},
        {"Checkpoints", "Checkpoint"},
        {"LoRA", "LORA"},
        {"Textual Inversion", "TextualInversion"},
        {"ControlNet", "Controlnet"},
        {"Hypernetwork", "Hypernetwork"},
        {"Aesthetic Gradient", "AestheticGradient"},
        {"Poses", "Poses"},
        {"Wildcards", "Wildcards"},
        {"Workflows", "Workflows"},
        {"Motion Modules", "MotionModule"},
        {"Utilities", "Utility"},
    };
    return cats;
}

}  // namespace

HubBrowserWidget::HubBrowserWidget() {
    BuildTitleBar();
    BuildToolbar();
    BuildInspectorControls();

    AddChild(title_bar_);
    AddChild(search_input_);
    AddChild(search_btn_);
    AddChild(type_select_);
    AddChild(sort_select_);
    AddChild(nsfw_switch_);
    AddChild(refresh_btn_);
    AddChild(prev_btn_);
    AddChild(next_btn_);
    AddChild(version_select_);
    AddChild(download_btn_);
    AddChild(copy_id_btn_);
    AddChild(progress_);

    SyncInspectorVisibility();

    // Kick off an initial browse of popular models.
    RunSearch();
}

HubBrowserWidget::~HubBrowserWidget() = default;

// ---------------------------------------------------------------------------
// Construction helpers
// ---------------------------------------------------------------------------

void HubBrowserWidget::BuildTitleBar() {
    title_bar_ = std::make_shared<AppTitleBar>("Hub");
    auto* tb = title_bar_.get();

    tb->SetWorkspaceLabel("Civitai Hub");
    tb->SetWorkspaceItems({
        {"Civitai Hub", "", nullptr, true, true, false, false},
        {"Model Inspector", "", nullptr, true, false, false, true},
        {"Downloads", "", nullptr, true, false, false, true},
        {"Settings", "", nullptr, true, false, false, true},
    });

    tb->SetNavActions(false, false, nullptr, nullptr);
    RebuildMenus();

    tb->SetCenterCommand("Search Civitai models...",
                        [this]() {
                            auto* ti = static_cast<TextInput*>(search_input_.get());
                            ti->SetFocused(true);
                        },
                        true);

    tb->SetLeftPanelToggle(true, left_panel_visible_, [this]() {
        SetLeftPanelVisible(!left_panel_visible_);
    });
    tb->SetRightPanelToggle(true, right_panel_visible_, [this]() {
        SetRightPanelVisible(!right_panel_visible_);
    });
    tb->SetThemeToggle(true, nullptr);  // wired by owner via TitleBar()

    tb->SetAvatar("Civitai User", {
        {"Profile & Settings", "", nullptr},
        {"API Key Status", "", [this]() {
            const std::string key = XER::Helper::GetEnv("XER_CIVITAI_API_KEY");
            SetStatus(key.empty()
                ? "No API key (XER_CIVITAI_API_KEY unset) — rate-limited browsing."
                : "Civitai API key configured.");
        }},
        {"Sign Out", "", nullptr, false, false, false, true},
    });

    tb->SetActionPill("Quick Fetch", [this]() { RunSearch(); }, false);
}

void HubBrowserWidget::RebuildMenus() {
    if (!title_bar_) return;
    std::vector<AppTitleBar::Menu> menus;
    auto add_menu = [&](const char* label, std::vector<AppTitleBar::MenuItem> items) {
        AppTitleBar::Menu m;
        m.label = label;
        m.items = std::move(items);
        menus.push_back(std::move(m));
    };
    add_menu("File", {
        {"New Search", "Ctrl+K", [this]() {
            static_cast<TextInput*>(search_input_.get())->SetFocused(true);
        }},
        {"Refresh Models", "F5", [this]() { RunSearch(); }, true, false, false, true},
        {"Copy API Endpoint", "", [this]() {
            XER::Content::Clipboard::SetText("https://civitai.com/api/v1");
            SetStatus("Copied API endpoint to clipboard.");
        }},
    });
    add_menu("Filter", {
        {"All Types", "", [this]() {
            active_category_ = 0; types_ = Categories()[0].api_type;
            static_cast<Select*>(type_select_.get())->SetSelectedIndex(0);
            RebuildMenus(); ResetPagination(); RunSearch();
        }, true, active_category_ == 0},
        {"Checkpoints Only", "", [this]() {
            active_category_ = 1; types_ = "Checkpoint";
            static_cast<Select*>(type_select_.get())->SetSelectedIndex(1);
            RebuildMenus(); ResetPagination(); RunSearch();
        }, true, active_category_ == 1},
        {"LoRA Only", "", [this]() {
            active_category_ = 2; types_ = "LORA";
            static_cast<Select*>(type_select_.get())->SetSelectedIndex(2);
            RebuildMenus(); ResetPagination(); RunSearch();
        }, true, active_category_ == 2},
        {"NSFW Content", "", [this]() {
            auto* sw = static_cast<Switch*>(nsfw_switch_.get());
            sw->SetChecked(!sw->IsChecked());
            RebuildMenus();
            SetStatus(nsfw_ ? "NSFW content enabled." : "NSFW content hidden.");
        }, true, nsfw_, false, true},
    });
    auto set_sort = [this](const char* s, int idx) {
        sort_ = s;
        static_cast<Select*>(sort_select_.get())->SetSelectedIndex(idx);
        RebuildMenus();
        ResetPagination();
        RunSearch();
    };
    add_menu("Sort", {
        {"Highest Rated", "", [set_sort]() { set_sort("Highest Rated", 0); }, true, sort_ == "Highest Rated"},
        {"Most Downloaded", "", [set_sort]() { set_sort("Most Downloaded", 1); }, true, sort_ == "Most Downloaded"},
        {"Newest", "", [set_sort]() { set_sort("Newest", 2); }, true, sort_ == "Newest"},
        {"Oldest", "", [set_sort]() { set_sort("Oldest", 3); }, true, sort_ == "Oldest"},
        {"Name", "", [set_sort]() { set_sort("Name", 4); }, true, sort_ == "Name"},
    });
    add_menu("View", {
        {"List View", "", [this]() { SetViewMode(ViewMode::List); }, true, view_mode_ == ViewMode::List},
        {"Grid View", "", [this]() { SetViewMode(ViewMode::Grid); }, true, view_mode_ == ViewMode::Grid},
        {"Left Sidebar", "", [this]() { SetLeftPanelVisible(!left_panel_visible_); }, true, left_panel_visible_, false, true},
        {"Right Inspector", "", [this]() { SetRightPanelVisible(!right_panel_visible_); }, true, right_panel_visible_},
    });
    add_menu("Help", {
        {"About Civitai Hub", "", [this]() {
            SetStatus("Civitai Hub — model browser powered by XER src/ui + Civitai API v1.");
        }},
        {"API Documentation", "", [this]() {
            SetStatus("API: https://civitai.com/api/v1  (token via XER_CIVITAI_API_KEY)");
        }},
    });
    title_bar_->SetMenus(std::move(menus));
}

void HubBrowserWidget::BuildToolbar() {
    search_input_ = std::make_shared<TextInput>("");
    auto* ti = static_cast<TextInput*>(search_input_.get());
    ti->SetPlaceholder("Search checkpoints, LoRA, ControlNet...");
    ti->SetOnValueChanged([this](const std::string& v) {
        query_ = v;
        ResetPagination();
    });
    ti->SetOnSubmit([this]() { RunSearch(); });

    search_btn_ = std::make_shared<Button>("Search", ButtonStyle::Primary);
    static_cast<Button*>(search_btn_.get())->SetOnClick([this]() { RunSearch(); });

    type_select_ = std::make_shared<Select>(
        std::vector<std::string>{
            "All Types", "Checkpoint", "LORA", "TextualInversion",
            "Controlnet", "Hypernetwork", "AestheticGradient", "Poses",
            "Wildcards", "Workflows", "MotionModule", "Utility"},
        0);
    static_cast<Select*>(type_select_.get())->SetOnChanged([this](int i) {
        static const char* names[] = {
            "", "Checkpoint", "LORA", "TextualInversion", "Controlnet",
            "Hypernetwork", "AestheticGradient", "Poses", "Wildcards",
            "Workflows", "MotionModule", "Utility"};
        types_ = names[std::clamp(i, 0, 11)];
        for (std::size_t c = 0; c < Categories().size(); ++c) {
            if (Categories()[c].api_type == types_) { active_category_ = static_cast<int>(c); break; }
        }
        RebuildMenus();
        ResetPagination();
        if (!results_.empty()) RunSearch();
    });

    sort_select_ = std::make_shared<Select>(
        std::vector<std::string>{"Highest Rated", "Most Downloaded",
                                 "Newest", "Oldest", "Name"},
        0);
    static_cast<Select*>(sort_select_.get())->SetOnChanged([this](int i) {
        static const char* names[] = {"Highest Rated", "Most Downloaded",
                                      "Newest", "Oldest", "Name"};
        sort_ = names[std::clamp(i, 0, 4)];
        RebuildMenus();
        ResetPagination();
        if (!results_.empty()) RunSearch();
    });

    nsfw_switch_ = std::make_shared<Switch>(false);
    static_cast<Switch*>(nsfw_switch_.get())->SetOnChanged([this](bool v) {
        nsfw_ = v;
        RebuildMenus();
        ResetPagination();
        if (!results_.empty()) RunSearch();
    });

    refresh_btn_ = std::make_shared<Button>("Refresh", ButtonStyle::Ghost);
    static_cast<Button*>(refresh_btn_.get())->SetOnClick([this]() { RunSearch(); });

    prev_btn_ = std::make_shared<Button>("< Prev", ButtonStyle::Ghost);
    next_btn_ = std::make_shared<Button>("Next >", ButtonStyle::Ghost);
    static_cast<Button*>(prev_btn_.get())->SetOnClick([this]() {
        if (query_.empty()) {
            if (page_ > 1) { --page_; RunSearch(); }
        } else {
            if (cursor_pos_ > 0) {
                --cursor_pos_;
                cursor_ = cursor_history_[cursor_pos_];
                RunSearch();
            }
        }
    });
    static_cast<Button*>(next_btn_.get())->SetOnClick([this]() {
        if (!has_next_ || searching_) return;
        if (query_.empty()) {
            ++page_;
        } else {
            cursor_history_.push_back(next_cursor_);
            cursor_pos_ = static_cast<int>(cursor_history_.size()) - 1;
            cursor_ = next_cursor_;
        }
        RunSearch();
    });
}

void HubBrowserWidget::BuildInspectorControls() {
    version_select_ = std::make_shared<Select>(
        std::vector<std::string>{"-"}, -1);
    static_cast<Select*>(version_select_.get())->SetOnChanged(
        [this](int) { selected_file_ = -1; RecomputeHitRects(); Invalidate(); });

    download_btn_ = std::make_shared<Button>("Download", ButtonStyle::Primary);
    static_cast<Button*>(download_btn_.get())->SetOnClick(
        [this]() { StartDownload(); });

    copy_id_btn_ = std::make_shared<Button>("Copy ID", ButtonStyle::Ghost);
    static_cast<Button*>(copy_id_btn_.get())->SetOnClick(
        [this]() { CopyModelId(); });

    progress_ = std::make_shared<Progress>(0.0f);
}

// ---------------------------------------------------------------------------
// Search / pagination
// ---------------------------------------------------------------------------

void HubBrowserWidget::ResetPagination() {
    page_ = 1;
    cursor_.clear();
    next_cursor_.clear();
    cursor_history_.clear();
    cursor_history_.push_back("");
    cursor_pos_ = 0;
    has_next_ = false;
}

void HubBrowserWidget::RunSearch() {
    if (searching_) return;
    searching_ = true;
    last_error_.clear();
    SetStatus("Searching Civitai...");

    const std::string q = query_;
    const std::string types = types_;
    const std::string sort = sort_;
    const bool nsfw = nsfw_;
    const int limit = limit_;
    const int page = page_;
    const std::string cursor_in = cursor_;

    search_future_ = std::async(std::launch::async,
        [q, types, sort, nsfw, limit, page, cursor_in]() {
            auto& dl = content::civitai::CivitaiModelDownloader::Instance();
            std::string next;
            auto models = dl.Client().SearchModels(
                q, limit, types, sort, page, cursor_in, nsfw, &next);
            return std::make_pair(std::move(models), std::move(next));
        });
}

// ---------------------------------------------------------------------------
// Selection / download
// ---------------------------------------------------------------------------

void HubBrowserWidget::SelectRow(int index) {
    if (index < 0 || index >= static_cast<int>(results_.size())) return;
    selected_ = index;
    selected_file_ = -1;

    auto& m = results_[index];
    auto* vs = static_cast<Select*>(version_select_.get());
    std::vector<std::string> opts;
    opts.reserve(m.versions.size());
    for (const auto& v : m.versions) opts.push_back(v.name);
    if (opts.empty()) opts.push_back("-");
    vs->SetOptions(opts);
    vs->SetSelectedIndex(0);
    SyncInspectorVisibility();
    RecomputeHitRects();
    Invalidate();
}

void HubBrowserWidget::SelectFile(int file_index) {
    if (selected_ < 0 || selected_ >= static_cast<int>(results_.size())) return;
    const auto& m = results_[selected_];
    auto* vs = static_cast<Select*>(version_select_.get());
    const int vi = std::clamp(vs->GetSelectedIndex(), 0,
                              static_cast<int>(m.versions.size()) - 1);
    if (vi < 0 || vi >= static_cast<int>(m.versions.size())) return;
    const int nf = static_cast<int>(m.versions[vi].files.size());
    selected_file_ = std::clamp(file_index, -1, nf - 1);
    RecomputeHitRects();
    Invalidate();
}

void HubBrowserWidget::StartDownload() {
    if (downloading_) {
        SetStatus("Download already in progress.");
        return;
    }
    if (selected_ < 0 || selected_ >= static_cast<int>(results_.size())) {
        SetStatus("Select a model first.");
        return;
    }
    auto& m = results_[selected_];
    if (m.versions.empty()) {
        SetStatus("Model has no downloadable versions.");
        return;
    }
    auto* vs = static_cast<Select*>(version_select_.get());
    const int vi = std::clamp(vs->GetSelectedIndex(), 0,
                              static_cast<int>(m.versions.size()) - 1);
    const auto& ver = m.versions[vi];
    if (ver.files.empty()) {
        SetStatus("Selected version has no files.");
        return;
    }

    // Pick the selected file, else the primary "Model" file, else first file.
    const content::civitai::CivitaiModelFile* file = nullptr;
    if (selected_file_ >= 0 &&
        selected_file_ < static_cast<int>(ver.files.size())) {
        file = &ver.files[selected_file_];
    } else {
        for (const auto& f : ver.files) {
            if (f.primary && f.type == "Model") { file = &f; break; }
        }
        if (!file) {
            for (const auto& f : ver.files) {
                if (f.type == "Model") { file = &f; break; }
            }
        }
        if (!file) file = &ver.files.front();
    }

    downloading_ = true;
    dl_progress_.store(0.0f);
    static_cast<Progress*>(progress_.get())->SetValue(0.0f);
    SetStatus("Downloading " + file->name + " ...");

    auto& dl = content::civitai::CivitaiModelDownloader::Instance();
    auto state = std::make_shared<std::promise<std::pair<bool, std::string>>>();
    auto fut = state->get_future();
    dl.DownloadModelFile(
        *file, m.id, ver.id,
        [this](uint64_t done, uint64_t total) {
            dl_progress_.store(total > 0
                ? static_cast<float>(done) / static_cast<float>(total)
                : 0.0f);
        },
        [state, name = file->name](bool ok, const std::string& err,
                                   const std::filesystem::path& path) {
            state->set_value({ok, err.empty() ? path.filename().string() : err});
            (void)name;
        });
    dl_state_ = std::async(std::launch::async, [fut = std::move(fut)]() mutable {
        return fut.get();
    });
}

void HubBrowserWidget::CopyModelId() {
    if (selected_ < 0 || selected_ >= static_cast<int>(results_.size())) {
        SetStatus("Select a model first.");
        return;
    }
    const auto& m = results_[selected_];
    XER::Content::Clipboard::SetText(m.id);
    SetStatus("Copied model id " + m.id);
}

void HubBrowserWidget::SetStatus(const std::string& text) {
    status_ = text;
    Invalidate();
}

void HubBrowserWidget::SetViewMode(ViewMode mode) {
    if (view_mode_ == mode) return;
    view_mode_ = mode;
    scroll_ = 0.0f;
    RebuildMenus();
    RecomputeHitRects();
    Invalidate();
}

void HubBrowserWidget::SetLeftPanelVisible(bool visible) {
    left_panel_visible_ = visible;
    title_bar_->SetLeftPanelToggle(true, left_panel_visible_, [this]() {
        SetLeftPanelVisible(!left_panel_visible_);
    });
    RebuildMenus();
    RecomputeHitRects();
    RequestLayout();
    Invalidate();
}

void HubBrowserWidget::SetRightPanelVisible(bool visible) {
    right_panel_visible_ = visible;
    title_bar_->SetRightPanelToggle(true, right_panel_visible_, [this]() {
        SetRightPanelVisible(!right_panel_visible_);
    });
    RebuildMenus();
    SyncInspectorVisibility();
    RecomputeHitRects();
    RequestLayout();
    Invalidate();
}

void HubBrowserWidget::SyncInspectorVisibility() {
    const bool show = right_panel_visible_ &&
                      selected_ >= 0 &&
                      selected_ < static_cast<int>(results_.size());
    version_select_->SetVisible(show);
    download_btn_->SetVisible(show);
    copy_id_btn_->SetVisible(show);
    progress_->SetVisible(show);
}

// ---------------------------------------------------------------------------
// Tick — async completion polling (UI thread only)
// ---------------------------------------------------------------------------

void HubBrowserWidget::Tick(double) {
    if (searching_ && search_future_.valid() &&
        search_future_.wait_for(std::chrono::milliseconds(0)) ==
            std::future_status::ready) {
        auto res = search_future_.get();
        results_ = std::move(res.first);
        next_cursor_ = res.second;
        searching_ = false;
        selected_ = -1;
        selected_file_ = -1;
        scroll_ = 0.0f;
        SyncInspectorVisibility();

        if (query_.empty()) {
            has_next_ = static_cast<int>(results_.size()) >= limit_;
        } else {
            has_next_ = !next_cursor_.empty();
        }
        char buf[192];
        std::snprintf(buf, sizeof(buf),
                      "%zu model(s) — page %d%s", results_.size(), page_,
                      results_.empty() ? " (try a different filter)" : "");
        SetStatus(buf);
        RecomputeHitRects();
        Invalidate();
    }

    if (downloading_) {
        const float p = dl_progress_.load();
        static_cast<Progress*>(progress_.get())->SetValue(p);
        if (dl_state_.valid() &&
            dl_state_.wait_for(std::chrono::milliseconds(0)) ==
                std::future_status::ready) {
            auto st = dl_state_.get();
            downloading_ = false;
            dl_progress_.store(st.first ? 1.0f : p);
            static_cast<Progress*>(progress_.get())->SetValue(
                st.first ? 1.0f : p);
            SetStatus(st.first ? ("Downloaded: " + st.second)
                               : ("Download failed: " + st.second));
        }
    }
}

// ---------------------------------------------------------------------------
// Hit testing helpers
// ---------------------------------------------------------------------------

int HubBrowserWidget::CategoryAt(float y) const {
    // Caller has already verified x is inside sidebar_rect_.
    for (std::size_t i = 0; i < category_rects_.size(); ++i) {
        const auto& r = category_rects_[i];
        if (y >= r.y && y < r.y + r.height) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int HubBrowserWidget::RowAt(float y) const {
    // Caller has already verified x is inside the results area.
    for (std::size_t i = 0; i < row_rects_.size(); ++i) {
        const auto& r = row_rects_[i];
        if (y >= r.y && y < r.y + r.height) return static_cast<int>(i);
    }
    return -1;
}

int HubBrowserWidget::GridCellAt(const Layout::Point& p) const {
    for (std::size_t i = 0; i < grid_rects_.size(); ++i) {
        if (grid_rects_[i].Contains(p)) return static_cast<int>(i);
    }
    return -1;
}

int HubBrowserWidget::FileRowAt(float y) const {
    for (std::size_t i = 0; i < file_rects_.size(); ++i) {
        const auto& r = file_rects_[i];
        if (y >= r.y && y < r.y + r.height) return static_cast<int>(i);
    }
    return -1;
}

void HubBrowserWidget::ScrollTo(float y) {
    scroll_ = std::max(0.0f, y);
    RecomputeHitRects();
    Invalidate();
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void HubBrowserWidget::LayoutChildren(const Layout::Rect& b,
                                      const Theme::Tokens& tokens) {
    titlebar_rect_ = {b.x, b.y, b.width, kTitleBarH};
    title_bar_->Layout(titlebar_rect_, tokens);

    const float ty = b.y + kTitleBarH;
    toolbar_rect_ = {b.x, ty, b.width, kToolbarH};
    const float iy = ty + kToolbarH;
    const float sy = b.y + b.height - kStatusH;

    const float side_w = left_panel_visible_ ? kSidebarW : 0.0f;
    const float insp_w = right_panel_visible_ ? kInspectorW : 0.0f;
    sidebar_rect_ = {b.x, iy, side_w, sy - iy};
    inspector_rect_ = {b.Right() - insp_w, iy, insp_w, sy - iy};
    center_rect_ = {b.x + side_w, iy,
                    std::max(0.0f, b.width - side_w - insp_w),
                    std::max(0.0f, sy - iy)};
    statusbar_rect_ = {b.x, sy, b.width, kStatusH};

    // Dedicated 30px results header above the scrollable card viewport so
    // the model count and active filters never collide with the first card.
    results_header_rect_ = {center_rect_.x + 12.0f, center_rect_.y + 6.0f,
                            std::max(0.0f, center_rect_.width - 24.0f), 24.0f};
    list_rect_ = {center_rect_.x + 12.0f, center_rect_.y + 34.0f,
                  std::max(0.0f, center_rect_.width - 24.0f),
                  std::max(0.0f, center_rect_.height - 42.0f)};
    scrollbar_rect_ = {list_rect_.Right() - 6.0f, list_rect_.y,
                       6.0f, list_rect_.height};

    // Toolbar layout: right-anchor [List | Grid] and [Switch + NSFW], then
    // distribute the left/middle filter controls across the remaining span
    // with search_input_ flexing to fill available width.
    const float cy = toolbar_rect_.y + (kToolbarH - 34.0f) * 0.5f;
    constexpr float kSegW = 132.0f;
    constexpr float kNsfwSwitchW = 36.0f;
    constexpr float kNsfwLabelW = 46.0f;
    constexpr float kNsfwClusterW = kNsfwSwitchW + 6.0f + kNsfwLabelW;

    const float right_edge = toolbar_rect_.Right() - 12.0f;
    const float seg_x = right_edge - kSegW;
    view_seg_rect_ = {seg_x, cy, kSegW, 34.0f};
    view_list_rect_ = {seg_x + 3.0f, cy + 3.0f, 61.0f, 28.0f};
    view_grid_rect_ = {seg_x + 68.0f, cy + 3.0f, 61.0f, 28.0f};

    const float nsfw_x = seg_x - 14.0f - kNsfwClusterW;
    nsfw_switch_rect_ = {nsfw_x, cy + 7.0f, kNsfwSwitchW, 20.0f};
    nsfw_switch_->Layout(nsfw_switch_rect_, tokens);
    nsfw_label_rect_ = {nsfw_x + kNsfwSwitchW + 6.0f, cy, kNsfwLabelW, 34.0f};

    const float left_x = toolbar_rect_.x + 12.0f;
    const float left_span = std::max(200.0f, nsfw_x - 14.0f - left_x);
    const auto toolbar_slots = Layout::DistributeRow(
        Layout::Rect{left_x, cy, left_span, 34.0f},
        {260.0f, 88.0f, 88.0f, 164.0f, 158.0f},
        {1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        8.0f,
        Layout::FlexAlign::Start);
    if (toolbar_slots.size() == 5) {
        search_input_->Layout(toolbar_slots[0], tokens);
        search_btn_->Layout(toolbar_slots[1], tokens);
        refresh_btn_->Layout(toolbar_slots[2], tokens);
        type_select_->Layout(toolbar_slots[3], tokens);
        sort_select_->Layout(toolbar_slots[4], tokens);
    }

    // Status bar buttons (right aligned)
    const float syy = statusbar_rect_.y + (kStatusH - 22.0f) * 0.5f;
    next_btn_->Layout({statusbar_rect_.Right() - 78.0f, syy, 70.0f, 22.0f},
                      tokens);
    prev_btn_->Layout({statusbar_rect_.Right() - 156.0f, syy, 70.0f, 22.0f},
                      tokens);
    next_rect_ = {statusbar_rect_.Right() - 78.0f, syy, 70.0f, 22.0f};
    prev_rect_ = {statusbar_rect_.Right() - 156.0f, syy, 70.0f, 22.0f};

    // Inspector children
    if (right_panel_visible_) {
        const float ix = inspector_rect_.x + 12.0f;
        const float iw = inspector_rect_.width - 24.0f;

        // Bottom block (150px): version select, download status, progress,
        // download + copy buttons.
        const float files_bottom = inspector_rect_.Bottom() - 150.0f;
        version_select_->Layout({ix, files_bottom + 34.0f, iw, 30.0f},
                                tokens);
        progress_->Layout({ix, files_bottom + 76.0f, iw, 8.0f}, tokens);
        download_btn_->Layout({ix, files_bottom + 90.0f, iw * 0.62f, 32.0f},
                              tokens);
        copy_id_btn_->Layout({ix + iw * 0.62f + 8.0f, files_bottom + 90.0f,
                              iw * 0.38f - 8.0f, 32.0f}, tokens);
    }

    RecomputeHitRects();
}

void HubBrowserWidget::RecomputeHitRects() {
    category_rects_.clear();
    row_rects_.clear();
    grid_rects_.clear();

    // Category rows inside sidebar
    if (left_panel_visible_ && sidebar_rect_.width > 0.0f) {
        float y = sidebar_rect_.y + 44.0f;  // below "Categories" header
        for (std::size_t i = 0; i < Categories().size(); ++i) {
            category_rects_.push_back(
                {sidebar_rect_.x + 8.0f, y, sidebar_rect_.width - 16.0f,
                 kCatRowH});
            y += kCatRowH + 2.0f;
        }
    }

    // Results rows / grid cells inside list_rect_
    const float content_h = list_rect_.height;
    if (view_mode_ == ViewMode::List) {
        const float pitch = kRowH + kRowGap;
        const int first = static_cast<int>(scroll_ / pitch);
        const int visible =
            static_cast<int>(std::ceil(content_h / pitch)) + 2;
        for (int i = first; i < first + visible; ++i) {
            if (i < 0 || i >= static_cast<int>(results_.size())) break;
            const float ry = list_rect_.y + i * pitch - scroll_;
            row_rects_.push_back(
                {list_rect_.x, ry, list_rect_.width - 12.0f, kRowH});
        }
        // scrollbar thumb
        const float total = results_.size() * pitch;
        if (total > content_h && total > 0.0f) {
            const float thumb_h = std::max(24.0f,
                content_h * content_h / total);
            const float max_scroll = total - content_h;
            const float ty = list_rect_.y +
                (scroll_ / max_scroll) * (content_h - thumb_h);
            scrollbar_thumb_rect_ = {scrollbar_rect_.x, ty,
                                     scrollbar_rect_.width, thumb_h};
        } else {
            scrollbar_thumb_rect_ = {};
        }
    } else {
        // grid: columns of kGridCardW + gap
        const float avail_w = list_rect_.width - 12.0f;
        const int cols = std::max(1, static_cast<int>(
            (avail_w + kGridGapX) / (kGridCardW + kGridGapX)));
        const float row_pitch_y = kGridCardH + kGridGapY;
        const int first_row = static_cast<int>(scroll_ / row_pitch_y);
        grid_start_idx_ = first_row * cols;
        const int visible_rows =
            static_cast<int>(std::ceil(content_h / row_pitch_y)) + 1;
        for (int r = first_row; r < first_row + visible_rows; ++r) {
            const int start_idx = r * cols;
            if (start_idx >= static_cast<int>(results_.size())) break;
            for (int c = 0; c < cols; ++c) {
                const int idx = start_idx + c;
                if (idx >= static_cast<int>(results_.size())) break;
                const float gx = list_rect_.x + c * (kGridCardW + kGridGapX);
                const float gy = list_rect_.y + r * row_pitch_y - scroll_;
                grid_rects_.push_back({gx, gy, kGridCardW, kGridCardH});
            }
        }
        const int rows_total =
            (static_cast<int>(results_.size()) + cols - 1) / cols;
        const float total = rows_total * row_pitch_y;
        if (total > content_h && total > 0.0f) {
            const float thumb_h = std::max(24.0f,
                content_h * content_h / total);
            const float max_scroll = total - content_h;
            const float ty = list_rect_.y +
                (scroll_ / max_scroll) * (content_h - thumb_h);
            scrollbar_thumb_rect_ = {scrollbar_rect_.x, ty,
                                     scrollbar_rect_.width, thumb_h};
        } else {
            scrollbar_thumb_rect_ = {};
        }
    }

    // Inspector file rows (selected version's files)
    file_rects_.clear();
    if (right_panel_visible_ && inspector_rect_.width > 0.0f &&
        selected_ >= 0 && selected_ < static_cast<int>(results_.size())) {
        const auto& m = results_[selected_];
        if (!m.versions.empty()) {
            auto* vs = static_cast<Select*>(version_select_.get());
            const int vi = std::clamp(vs->GetSelectedIndex(), 0,
                                      static_cast<int>(m.versions.size()) - 1);
            if (vi >= 0) {
                const float files_top = inspector_rect_.y + 160.0f;
                const float files_bottom = inspector_rect_.Bottom() - 150.0f;
                const float max_files = std::max(0.0f,
                    (files_bottom - files_top - 22.0f) / kFileRowH);
                const int shown = std::min(
                    static_cast<int>(m.versions[vi].files.size()),
                    static_cast<int>(max_files));
                for (int i = 0; i < shown; ++i) {
                    const float fy = files_top + 22.0f + i * kFileRowH;
                    file_rects_.push_back(
                        {inspector_rect_.x + 12.0f, fy,
                         inspector_rect_.width - 24.0f, kFileRowH - 4.0f});
                }
            }
        }
    }
}

void HubBrowserWidget::Layout(const Layout::Rect& bounds) {
    Layout(bounds, CachedLayoutTokens());
}

void HubBrowserWidget::Layout(const Layout::Rect& bounds,
                              const Theme::Tokens& tokens) {
    CacheLayoutTokens(tokens);
    SetBounds(bounds);
    LayoutChildren(bounds, tokens);
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

bool HubBrowserWidget::HandleEvent(const Event& event) {
    // Manual zones first (they don't overlap child widgets).
    if (event.type == Event::Type::MouseWheel) {
        const Layout::Point p = event.position;
        if (sidebar_rect_.Contains(p) && left_panel_visible_) {
            ScrollTo(scroll_ - event.wheel_delta * 0.5f);
            return true;
        }
        if (center_rect_.Contains(p) || list_rect_.Contains(p)) {
            ScrollTo(scroll_ - event.wheel_delta * 0.5f);
            return true;
        }
        if (inspector_rect_.Contains(p) && right_panel_visible_) {
            // inspector scroll handled via file rows clip; no-op for now
            return true;
        }
        return false;
    }

    if (event.type == Event::Type::MouseDown &&
        event.mouse_button == MouseButton::Left) {
        const Layout::Point p = event.position;

        // scrollbar drag
        if (!scrollbar_thumb_rect_.width && scrollbar_rect_.Contains(p)) {
            // click on track: page scroll
            ScrollTo(scroll_ + (p.y < scrollbar_thumb_rect_.y
                                    ? -list_rect_.height * 0.8f
                                    : list_rect_.height * 0.8f));
            return true;
        }
        if (scrollbar_thumb_rect_.width && scrollbar_thumb_rect_.Contains(p)) {
            scrollbar_dragging_ = true;
            scrollbar_grab_ = p.y - scrollbar_thumb_rect_.y;
            return true;
        }
        if (scrollbar_dragging_) return true;

        // category click
        if (left_panel_visible_ && sidebar_rect_.Contains(p)) {
            const int cat = CategoryAt(p.y);
            if (cat >= 0 && cat < static_cast<int>(Categories().size()) &&
                cat != active_category_) {
                active_category_ = cat;
                types_ = Categories()[cat].api_type;
                RebuildMenus();
                ResetPagination();
                RunSearch();
                // Sync toolbar select last — its on_changed re-triggers
                // RunSearch which early-outs because searching_ == true.
                static_cast<Select*>(type_select_.get())
                    ->SetSelectedIndex(cat);
                return true;
            }
            if (cat >= 0) return true;
        }

        // result row / grid click
        if (center_rect_.Contains(p) || list_rect_.Contains(p)) {
            if (view_mode_ == ViewMode::List) {
                const int row = RowAt(p.y);
                // row_rects_ store absolute y; RowAt compares against stored y
                if (row >= 0 && row < static_cast<int>(row_rects_.size())) {
                    // map visible index back to model index
                    const float pitch = kRowH + kRowGap;
                    const int first = static_cast<int>(scroll_ / pitch);
                    SelectRow(first + row);
                    return true;
                }
            } else {
                if (GridCellAt(p) >= 0) {
                    const float avail_w = list_rect_.width - 12.0f;
                    const int cols = std::max(1, static_cast<int>(
                        (avail_w + kGridGapX) / (kGridCardW + kGridGapX)));
                    const float row_pitch_y = kGridCardH + kGridGapY;
                    const int first_row = static_cast<int>(scroll_ / row_pitch_y);
                    const int col = static_cast<int>(std::clamp(
                        (p.x - list_rect_.x) / (kGridCardW + kGridGapX),
                        0.0f, static_cast<float>(cols - 1)));
                    const int row = static_cast<int>(std::clamp(
                        (p.y - list_rect_.y + scroll_) / row_pitch_y,
                        0.0f, 1e9f));
                    const int idx = (first_row + row) * cols + col;
                    if (idx >= 0 && idx < static_cast<int>(results_.size())) {
                        SelectRow(idx);
                        return true;
                    }
                }
            }
        }

        // file row click in inspector
        if (right_panel_visible_ && inspector_rect_.Contains(p)) {
            const int fi = FileRowAt(p.y);
            if (fi >= 0) { SelectFile(fi); return true; }
        }
        return false;
    }

    if (event.type == Event::Type::MouseMove && scrollbar_dragging_) {
        // drag scrollbar thumb
        const float content_h = list_rect_.height;
        float total;
        if (view_mode_ == ViewMode::List) {
            total = results_.size() * (kRowH + kRowGap);
        } else {
            const float avail_w = list_rect_.width - 12.0f;
            const int cols = std::max(1, static_cast<int>(
                (avail_w + kGridGapX) / (kGridCardW + kGridGapX)));
            const int rows = (static_cast<int>(results_.size()) + cols - 1) / cols;
            total = rows * (kGridCardH + kGridGapY);
        }
        const float max_scroll = std::max(1.0f, total - content_h);
        const float thumb_h = scrollbar_thumb_rect_.height;
        const float travel = std::max(1.0f, content_h - thumb_h);
        const float new_y = event.position.y - scrollbar_grab_;
        const float frac = std::clamp(
            (new_y - list_rect_.y) / travel, 0.0f, 1.0f);
        ScrollTo(frac * max_scroll);
        return true;
    }

    if (event.type == Event::Type::MouseUp &&
        event.mouse_button == MouseButton::Left) {
        scrollbar_dragging_ = false;
        return false;
    }

    if (event.type == Event::Type::Click &&
        event.mouse_button == MouseButton::Left) {
        const Layout::Point p = event.position;

        // Clicking the "NSFW" text label toggles the adjacent switch.
        if (nsfw_label_rect_.Contains(p)) {
            auto* sw = static_cast<Switch*>(nsfw_switch_.get());
            sw->SetChecked(!sw->IsChecked());
            return true;
        }

        // view mode segmented toggle
        if (view_list_rect_.Contains(p)) { SetViewMode(ViewMode::List); return true; }
        if (view_grid_rect_.Contains(p)) { SetViewMode(ViewMode::Grid); return true; }

        return false;
    }

    if (event.type == Event::Type::KeyDown) {
        if (event.key == Widgets::KeyCode::F5) {
            RunSearch();
            return true;
        }
        if (event.is_ctrl && !event.is_alt &&
            (event.key == Widgets::KeyCode::K ||
             event.key == Widgets::KeyCode::F ||
             event.key == Widgets::KeyCode::P)) {
            static_cast<TextInput*>(search_input_.get())->SetFocused(true);
            return true;
        }
    }

    return false;
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void HubBrowserWidget::Render(Rendering::Renderer& r,
                              const Theme::Tokens& tokens) {
    const auto b = GetBounds();
    r.FillRect(b, tokens.background, 0.0f);

    DrawToolbar(r, tokens);
    DrawCategorySidebar(r, tokens);
    DrawResults(r, tokens);
    DrawInspector(r, tokens);
    DrawStatusBar(r, tokens);

    // Toolbar & status bar separator hairlines
    r.DrawLine({b.x, toolbar_rect_.Bottom()}, {b.Right(), toolbar_rect_.Bottom()},
               tokens.border, 1.0f);
    r.DrawLine({b.x, statusbar_rect_.y}, {b.Right(), statusbar_rect_.y},
               tokens.border, 1.0f);
}

void HubBrowserWidget::DrawToolbar(Rendering::Renderer& r,
                                   const Theme::Tokens& tokens) {
    // NSFW label vertically centered next to the switch
    const Rendering::TextStyle nsfw_style{
        nsfw_ ? tokens.foreground : tokens.muted, 12.0f, 500};
    const auto nsfw_txt = Layout::CenteredTextRect(
        r, "NSFW", nsfw_style, nsfw_label_rect_, 0.0f, false);
    r.DrawText("NSFW", nsfw_txt, nsfw_style);

    // Segmented view toggle [List | Grid]
    r.FillRect(view_seg_rect_, tokens.surface, tokens.radius_medium);
    r.StrokeRect(view_seg_rect_, tokens.border, 1.0f, tokens.radius_medium);
    auto draw_seg = [&](const Layout::Rect& rect, const char* label,
                        bool active) {
        if (active) {
            r.FillRect(rect, tokens.accent, tokens.radius_small);
        }
        const Rendering::TextStyle st{
            active ? tokens.accent_foreground : tokens.muted,
            11.5f, active ? 600 : 500};
        const auto tr = Layout::CenteredTextRect(
            r, label, st, rect, 2.0f, true);
        r.DrawText(label, tr, st);
    };
    draw_seg(view_list_rect_, "List", view_mode_ == ViewMode::List);
    draw_seg(view_grid_rect_, "Grid", view_mode_ == ViewMode::Grid);
}

void HubBrowserWidget::DrawBadge(Rendering::Renderer& r,
                                 const Theme::Tokens& tokens,
                                 const std::string& text,
                                 Layout::Rect rect,
                                 Theme::Color bg, Theme::Color fg) {
    if (text.empty() || rect.width <= 0.0f) return;
    r.FillRect(rect, bg, rect.height * 0.5f);
    const Rendering::TextStyle style{fg, 10.0f, 600};
    const auto tr = Layout::CenteredTextRect(r, text, style, rect, 6.0f,
                                             true);
    r.DrawText(text, tr, style);
}

XER::UI::Theme::Color HubBrowserWidget::TypeColor(const std::string& type) const {
    // Deterministic accent per model type.
    static const std::pair<const char*, Theme::Color> kMap[] = {
        {"Checkpoint", {0.51f, 0.55f, 0.97f, 1.0f}},   // indigo
        {"LORA", {0.30f, 0.85f, 0.85f, 1.0f}},         // cyan
        {"TextualInversion", {0.29f, 0.87f, 0.50f, 1.0f}}, // green
        {"Controlnet", {0.98f, 0.60f, 0.24f, 1.0f}},   // orange
        {"Hypernetwork", {0.65f, 0.55f, 0.98f, 1.0f}}, // violet
        {"AestheticGradient", {0.95f, 0.45f, 0.65f, 1.0f}}, // pink
        {"Poses", {0.98f, 0.82f, 0.30f, 1.0f}},        // yellow
        {"Wildcards", {0.63f, 0.66f, 0.72f, 1.0f}},    // slate
        {"Workflows", {0.45f, 0.75f, 0.98f, 1.0f}},    // sky
        {"MotionModule", {0.80f, 0.50f, 0.95f, 1.0f}}, // purple
        {"Utility", {0.55f, 0.60f, 0.68f, 1.0f}},      // gray
    };
    for (const auto& [k, c] : kMap) {
        if (type == k) return c;
    }
    return {0.55f, 0.60f, 0.68f, 1.0f};
}

std::string HubBrowserWidget::FmtCount(uint64_t v) const {
    if (v >= 1000000) return std::to_string(v / 1000000) + "M";
    if (v >= 1000) return std::to_string(v / 1000) + "k";
    return std::to_string(v);
}

std::string HubBrowserWidget::FmtBytes(uint64_t b) const {
    if (b >= (1ULL << 30)) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f GB", b / 1073741824.0);
        return buf;
    }
    if (b >= (1ULL << 20)) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f MB", b / 1048576.0);
        return buf;
    }
    if (b >= (1ULL << 10)) return std::to_string(b >> 10) + " KB";
    return std::to_string(b) + " B";
}

void HubBrowserWidget::DrawCategorySidebar(Rendering::Renderer& r,
                                           const Theme::Tokens& tokens) {
    if (!left_panel_visible_ || sidebar_rect_.width <= 0.0f) return;
    const auto sr = sidebar_rect_;
    r.FillRect(sr, tokens.surface, 0.0f);
    r.DrawLine({sr.Right(), sr.y}, {sr.Right(), sr.Bottom()}, tokens.border,
               1.0f);

    const Rendering::TextStyle hdr{tokens.muted, 11.0f, 600};
    const auto hdr_r = Layout::CenteredTextRect(
        r, "CATEGORIES", hdr, {sr.x + 12.0f, sr.y + 12.0f, sr.width - 24.0f, 18.0f},
        0.0f, false);
    r.DrawText("CATEGORIES", hdr_r, hdr);

    for (std::size_t i = 0; i < category_rects_.size() &&
                        i < Categories().size(); ++i) {
        const auto& cr = category_rects_[i];
        const bool active = static_cast<int>(i) == active_category_;
        if (active) {
            r.FillRect({cr.x, cr.y, cr.width, cr.height}, tokens.accent, 6.0f);
        }
        const Rendering::TextStyle style{
            active ? tokens.accent_foreground : tokens.foreground,
            12.0f, active ? 600 : 450};
        const auto tr = Layout::CenteredTextRect(
            r, Categories()[i].label, style, cr, 12.0f, false);
        r.DrawText(Categories()[i].label, tr, style);
    }

    // Footer: API status
    const Rendering::TextStyle foot{tokens.muted, 10.5f, 400};
    r.DrawText("Civitai API v1", {sr.x + 12.0f, sr.Bottom() - 34.0f,
                                  sr.width - 24.0f, 14.0f}, foot);
    r.DrawText(nsfw_ ? "NSFW: visible" : "NSFW: hidden",
               {sr.x + 12.0f, sr.Bottom() - 18.0f, sr.width - 24.0f, 14.0f},
               foot);
}

void HubBrowserWidget::DrawResults(Rendering::Renderer& r,
                                   const Theme::Tokens& tokens) {
    const auto cr = center_rect_;
    if (cr.width <= 0.0f || cr.height <= 0.0f) return;

    // Dedicated results header band (above list_rect_, never overlapped).
    const Rendering::TextStyle count_style{tokens.muted, 11.5f, 600};
    std::string count_txt = searching_
        ? "Searching..."
        : (results_.empty() ? "No models"
                            : std::to_string(results_.size()) + " models");
    const std::string& cat_label =
        (active_category_ >= 0 &&
         active_category_ < static_cast<int>(Categories().size()))
            ? Categories()[active_category_].label
            : Categories()[0].label;
    count_txt += "  •  " + cat_label + "  •  " + sort_;
    const auto count_r = Layout::CenteredTextRect(
        r, count_txt, count_style, results_header_rect_, 2.0f, false);
    r.DrawText(count_txt, count_r, count_style);

    if (searching_ && results_.empty()) {
        const Rendering::TextStyle st{tokens.muted, 13.0f, 400};
        const auto tr = Layout::CenteredTextRect(
            r, "Contacting Civitai API...", st, list_rect_, 0.0f, true);
        r.DrawText("Contacting Civitai API...", tr, st);
        return;
    }
    if (results_.empty() && !searching_) {
        const Rendering::TextStyle st{tokens.muted, 13.0f, 400};
        const auto tr = Layout::CenteredTextRect(
            r, "No models found. Try another query or filter.", st,
            list_rect_, 0.0f, true);
        r.DrawText("No models found. Try another query or filter.", tr, st);
        return;
    }

    r.PushClip(list_rect_);

    if (view_mode_ == ViewMode::List) {
        for (std::size_t i = 0; i < row_rects_.size(); ++i) {
            const int model_idx = static_cast<int>(i) +
                static_cast<int>(scroll_ / (kRowH + kRowGap));
            if (model_idx < 0 ||
                model_idx >= static_cast<int>(results_.size())) continue;
            const auto& m = results_[model_idx];
            const auto& rr = row_rects_[i];
            const bool sel = model_idx == selected_;

            r.FillRect(rr, sel ? tokens.accent : tokens.surface,
                       tokens.radius_medium);
            if (!sel) {
                r.StrokeRect(rr, tokens.border, 1.0f, tokens.radius_medium);
            }

            // Type glyph block
            const Theme::Color tc = TypeColor(m.type);
            r.FillRect({rr.x + 8.0f, rr.y + 8.0f, 48.0f, 48.0f},
                       tc, 10.0f);
            const Rendering::TextStyle glyph{tokens.background, 18.0f, 700};
            const std::string letter = m.type.empty() ? "?"
                : m.type.substr(0, 1);
            const auto gr = Layout::CenteredTextRect(
                r, letter, glyph,
                {rr.x + 8.0f, rr.y + 8.0f, 48.0f, 48.0f}, 0.0f, true);
            r.DrawText(letter, gr, glyph);

            const float tx = rr.x + 66.0f;
            const float tw = rr.width - 66.0f - 26.0f;

            // Line 1: name
            const Rendering::TextStyle name_style{
                sel ? tokens.accent_foreground : tokens.foreground,
                13.5f, 600};
            const auto nr = Layout::CenteredTextRect(
                r, m.name, name_style,
                {tx, rr.y + 6.0f, tw, 20.0f}, 0.0f, false);
            r.DrawText(m.name, nr, name_style);

            // Line 2: badges (type, base model, NSFW) + creator
            float bx = tx;
            const float by = rr.y + 28.0f;
            const Theme::Color badge_bg = sel
                ? Theme::Color{1, 1, 1, 0.22f}
                : Theme::Color{tokens.muted.r * 0.35f,
                               tokens.muted.g * 0.35f,
                               tokens.muted.b * 0.35f, 1.0f};
            const Theme::Color badge_fg = sel
                ? tokens.accent_foreground : tokens.foreground;
            const Rendering::TextStyle bs{badge_fg, 10.0f, 600};
            auto badge_w = [&](const std::string& t) {
                return r.MeasureText(t, bs).width + 12.0f;
            };
            const float type_bw = badge_w(m.type);
            DrawBadge(r, tokens, m.type, {bx, by, type_bw, 16.0f},
                      badge_bg, badge_fg);
            bx += type_bw + 5.0f;
            if (!m.versions.empty() && !m.versions[0].base_model.empty()) {
                const std::string bm = m.versions[0].base_model;
                const float bmw = badge_w(bm);
                DrawBadge(r, tokens, bm, {bx, by, bmw, 16.0f},
                          badge_bg, badge_fg);
                bx += bmw + 5.0f;
            }
            if (m.nsfw) {
                DrawBadge(r, tokens, "NSFW", {bx, by, 40.0f, 16.0f},
                          {0.86f, 0.15f, 0.15f, 1.0f},
                          tokens.danger_foreground);
            }

            // Line 3: stats
            const Rendering::TextStyle st{
                sel ? tokens.accent_foreground : tokens.muted, 11.0f, 450};
            std::string stats;
            if (m.rating > 0.0) {
                stats += "★ " + std::to_string(m.rating).substr(0, 4) + "   ";
            }
            stats += "↓ " + FmtCount(m.download_count) +
                     "   ♥ " + FmtCount(m.thumbs_up_count);
            uint64_t sz = 0;
            if (!m.versions.empty()) {
                for (const auto& f : m.versions[0].files) {
                    if (f.type == "Model") { sz = f.size_bytes; break; }
                }
            }
            if (sz) stats += "   " + FmtBytes(sz);
            if (!m.creator.empty()) stats += "   @" + m.creator;
            const auto sr2 = Layout::CenteredTextRect(
                r, stats, st, {tx, rr.y + 46.0f, tw, 14.0f}, 0.0f, false);
            r.DrawText(stats, sr2, st);

            // chevron
            DrawChevron(r, tokens, {rr.Right() - 18.0f, rr.y + kRowH * 0.5f - 7.0f, 14.0f, 14.0f},
                        sel ? tokens.accent_foreground : tokens.muted);
        }
    } else {
        // Grid cards
        for (std::size_t i = 0; i < grid_rects_.size(); ++i) {
            const int model_idx = static_cast<int>(i);  // grid_rects_ pushed in order
            // recover actual model index: cells were pushed row-major from
            // first_row*cols — but we stored only visible cells; index i maps
            // to the i-th visible cell, whose model index = first_visible + i
            // We stored them in order, so model index = grid_start_idx + i.
            // grid_start_idx computed during RecomputeHitRects — store it:
            // (see grid_start_idx_ member — added below)
            const int idx = grid_start_idx_ + static_cast<int>(i);
            if (idx < 0 || idx >= static_cast<int>(results_.size())) continue;
            const auto& m = results_[idx];
            const auto& gr = grid_rects_[i];
            const bool sel = idx == selected_;

            r.FillRect(gr, sel ? tokens.accent : tokens.surface,
                       tokens.radius_large);
            r.StrokeRect(gr, sel ? tokens.accent : tokens.border, 1.0f,
                         tokens.radius_large);

            // Header block with type initial
            const Theme::Color tc = TypeColor(m.type);
            const float head_h = 96.0f;
            r.FillRect({gr.x, gr.y, gr.width, head_h}, tc, tokens.radius_large);
            // cover the bottom corners of header
            r.FillRect({gr.x, gr.y + head_h - tokens.radius_large,
                        gr.width, tokens.radius_large}, tc, 0.0f);
            const Rendering::TextStyle glyph{tokens.background, 34.0f, 800};
            const std::string letter = m.type.empty() ? "?"
                : m.type.substr(0, 1);
            const auto grr = Layout::CenteredTextRect(
                r, letter, glyph,
                {gr.x, gr.y + head_h * 0.5f - 30.0f, gr.width, 60.0f},
                0.0f, true);
            r.DrawText(letter, grr, glyph);
            const Rendering::TextStyle type_st{tokens.background, 10.5f, 600};
            const auto tr2 = Layout::CenteredTextRect(
                r, m.type, type_st,
                {gr.x, gr.y + head_h - 24.0f, gr.width, 18.0f}, 0.0f, true);
            r.DrawText(m.type, tr2, type_st);

            // Body text
            const float bx = gr.x + 10.0f;
            const float bw = gr.width - 20.0f;
            const Rendering::TextStyle name_style{
                sel ? tokens.accent_foreground : tokens.foreground,
                12.5f, 600};
            // two-line name: truncate to ~34 chars per line
            std::string n1 = m.name, n2;
            if (n1.size() > 34) {
                n2 = n1.substr(34);
                n1 = n1.substr(0, 34);
                if (n2.size() > 30) { n2 = n2.substr(0, 30) + "..."; }
            }
            const auto nr = Layout::CenteredTextRect(
                r, n1, name_style, {bx, gr.y + head_h + 8.0f, bw, 16.0f},
                0.0f, false);
            r.DrawText(n1, nr, name_style);
            if (!n2.empty()) {
                const auto nr2 = Layout::CenteredTextRect(
                    r, n2, name_style, {bx, gr.y + head_h + 24.0f, bw, 16.0f},
                    0.0f, false);
                r.DrawText(n2, nr2, name_style);
            }
            const Rendering::TextStyle st{
                sel ? tokens.accent_foreground : tokens.muted, 10.5f, 450};
            std::string stats;
            if (m.rating > 0.0) {
                stats += "★ " + std::to_string(m.rating).substr(0, 4) + "  ";
            }
            stats += "↓ " + FmtCount(m.download_count) +
                     "  ♥ " + FmtCount(m.thumbs_up_count);
            if (!m.versions.empty() && !m.versions[0].base_model.empty()) {
                stats += "  " + m.versions[0].base_model;
            }
            const auto sr3 = Layout::CenteredTextRect(
                r, stats, st, {bx, gr.y + gr.height - 40.0f, bw, 14.0f},
                0.0f, false);
            r.DrawText(stats, sr3, st);
            if (m.nsfw) {
                DrawBadge(r, tokens, "NSFW",
                          {gr.Right() - 48.0f, gr.y + head_h + 8.0f, 40.0f, 15.0f},
                          {0.86f, 0.15f, 0.15f, 1.0f}, tokens.danger_foreground);
            }
        }
    }

    // Scrollbar
    if (!scrollbar_thumb_rect_.width) {
        r.FillRect(scrollbar_rect_, tokens.secondary, 4.0f);
    } else {
        r.FillRect(scrollbar_rect_, tokens.secondary, 4.0f);
        r.FillRect(scrollbar_thumb_rect_, tokens.muted, 4.0f);
    }

    r.PopClip();
}

void HubBrowserWidget::DrawInspector(Rendering::Renderer& r,
                                     const Theme::Tokens& tokens) {
    if (!right_panel_visible_ || inspector_rect_.width <= 0.0f) return;
    const auto ir = inspector_rect_;
    r.FillRect(ir, tokens.surface, 0.0f);
    r.DrawLine({ir.x, ir.y}, {ir.x, ir.Bottom()}, tokens.border, 1.0f);

    const float ix = ir.x + 12.0f;
    const float iw = ir.width - 24.0f;

    if (selected_ < 0 || selected_ >= static_cast<int>(results_.size())) {
        const Rendering::TextStyle st{tokens.muted, 12.5f, 400};
        const auto tr = Layout::CenteredTextRect(
            r, "Select a model to inspect it.", st, ir, 16.0f, true);
        r.DrawText("Select a model to inspect it.", tr, st);
        return;
    }

    const auto& m = results_[selected_];

    // Title
    const Rendering::TextStyle title_st{tokens.foreground, 14.0f, 700};
    const auto title_r = Layout::CenteredTextRect(
        r, m.name, title_st, {ix, ir.y + 12.0f, iw, 20.0f}, 0.0f, false);
    r.DrawText(m.name, title_r, title_st);

    // Badges row
    float bx = ix;
    const float by = ir.y + 38.0f;
    const Theme::Color badge_bg{tokens.muted.r * 0.35f,
                                tokens.muted.g * 0.35f,
                                tokens.muted.b * 0.35f, 1.0f};
    const Rendering::TextStyle bs{tokens.foreground, 10.0f, 600};
    auto badge_w = [&](const std::string& t) {
        return r.MeasureText(t, bs).width + 12.0f;
    };
    DrawBadge(r, tokens, m.type, {bx, by, badge_w(m.type), 16.0f},
              badge_bg, tokens.foreground);
    bx += badge_w(m.type) + 5.0f;
    if (!m.versions.empty() && !m.versions[0].base_model.empty()) {
        const std::string bm = m.versions[0].base_model;
        DrawBadge(r, tokens, bm, {bx, by, badge_w(bm), 16.0f},
                  badge_bg, tokens.foreground);
        bx += badge_w(bm) + 5.0f;
    }
    if (m.nsfw) {
        DrawBadge(r, tokens, "NSFW", {bx, by, 42.0f, 16.0f},
                  {0.86f, 0.15f, 0.15f, 1.0f}, tokens.danger_foreground);
    }

    // Creator + stats
    const Rendering::TextStyle meta{tokens.muted, 11.5f, 450};
    std::string meta_txt = "by " + (m.creator.empty() ? "unknown" : m.creator) +
        "  •  " + std::to_string(m.versions.size()) + " version(s)";
    const auto meta_r = Layout::CenteredTextRect(
        r, meta_txt, meta, {ix, ir.y + 60.0f, iw, 16.0f}, 0.0f, false);
    r.DrawText(meta_txt, meta_r, meta);

    // Stats grid (2x2)
    const float sy = ir.y + 84.0f;
    const float col_w = iw * 0.5f;
    const std::string base_model =
        (!m.versions.empty() && !m.versions[0].base_model.empty())
            ? m.versions[0].base_model
            : "-";
    const char* labels[4] = {
        m.rating > 0.0 ? "Rating" : "Base Model",
        "Downloads", "Likes", "Model ID"};
    std::string values[4] = {
        m.rating > 0.0 ? std::to_string(m.rating).substr(0, 4) : base_model,
        FmtCount(m.download_count),
        FmtCount(m.thumbs_up_count),
        m.id.size() > 12 ? m.id.substr(0, 10) + "..." : m.id,
    };
    for (int i = 0; i < 4; ++i) {
        const float cx = ix + (i % 2) * col_w;
        const float cyy = sy + (i / 2) * 34.0f;
        const Rendering::TextStyle l_st{tokens.muted, 10.0f, 500};
        const auto lr = Layout::CenteredTextRect(
            r, labels[i], l_st, {cx, cyy, col_w - 8.0f, 12.0f}, 0.0f, false);
        r.DrawText(labels[i], lr, l_st);
        const Rendering::TextStyle v_st{tokens.foreground, 13.0f, 600};
        const auto vr = Layout::CenteredTextRect(
            r, values[i], v_st, {cx, cyy + 12.0f, col_w - 8.0f, 18.0f},
            0.0f, false);
        r.DrawText(values[i], vr, v_st);
    }

    // "Files" section header
    const float files_top = ir.y + 160.0f;
    const Rendering::TextStyle sec{tokens.muted, 11.0f, 600};
    const auto sec_r = Layout::CenteredTextRect(
        r, "FILES", sec, {ix, files_top, iw, 16.0f}, 0.0f, false);
    r.DrawText("FILES", sec_r, sec);

    // File rows (selected version)
    auto* vs = static_cast<Select*>(version_select_.get());
    const int vi = std::clamp(vs->GetSelectedIndex(), 0,
                              static_cast<int>(m.versions.size()) - 1);
    if (vi >= 0 && vi < static_cast<int>(m.versions.size())) {
        const auto& ver = m.versions[vi];
        const float files_bottom = ir.Bottom() - 150.0f;
        const float max_files = std::max(0.0f,
            (files_bottom - files_top - 22.0f) / kFileRowH);
        const int nfiles = static_cast<int>(ver.files.size());
        const int shown = std::min(nfiles, static_cast<int>(max_files));
        for (int i = 0; i < shown; ++i) {
            const auto& f = ver.files[i];
            const float fy = files_top + 22.0f + i * kFileRowH;
            const bool fsel = i == selected_file_;
            const Layout::Rect fr{ix, fy, iw, kFileRowH - 4.0f};
            if (fsel) {
                r.FillRect(fr, tokens.secondary, tokens.radius_small);
            }
            // format badge
            const std::string fmt = f.format.empty() ? f.type : f.format;
            const Theme::Color fbg = f.type == "Model"
                ? TypeColor(m.type)
                : Theme::Color{tokens.muted.r * 0.4f,
                               tokens.muted.g * 0.4f,
                               tokens.muted.b * 0.4f, 1.0f};
            const Rendering::TextStyle fbs{tokens.background, 9.5f, 600};
            const float fbw = r.MeasureText(fmt, fbs).width + 10.0f;
            DrawBadge(r, tokens, fmt, {fr.x, fr.y + 4.0f, fbw, 20.0f},
                      fbg, tokens.background);
            // file name (truncated)
            const Rendering::TextStyle fn{tokens.foreground, 11.5f, 500};
            std::string fname = f.name;
            if (fname.size() > 26) fname = fname.substr(0, 24) + "..";
            const auto fnr = Layout::CenteredTextRect(
                r, fname, fn,
                {fr.x + fbw + 8.0f, fr.y, fr.width - fbw - 60.0f, fr.height},
                0.0f, false);
            r.DrawText(fname, fnr, fn);
            // size right-aligned
            const Rendering::TextStyle fs{tokens.muted, 10.5f, 450};
            const std::string sz = FmtBytes(f.size_bytes);
            const auto szr = Layout::CenteredTextRect(
                r, sz, fs,
                {fr.Right() - 70.0f, fr.y, 66.0f, fr.height}, 0.0f, false);
            r.DrawText(sz, szr, fs);
        }
        if (nfiles > shown) {
            const Rendering::TextStyle more{tokens.muted, 10.0f, 400};
            const std::string mt = "... and " + std::to_string(nfiles - shown) +
                                   " more file(s)";
            const auto mtr = Layout::CenteredTextRect(
                r, mt, more,
                {ix, files_top + 22.0f + shown * kFileRowH, iw, 14.0f},
                0.0f, false);
            r.DrawText(mt, mtr, more);
        }
    }

    // "Version" header above version_select_
    const float fb = ir.Bottom() - 150.0f;
    const auto ver_hdr_r = Layout::CenteredTextRect(
        r, "VERSION", sec, {ix, fb + 14.0f, iw, 16.0f}, 0.0f, false);
    r.DrawText("VERSION", ver_hdr_r, sec);

    // Download status / progress text between version select and progress bar
    if (downloading_) {
        const Rendering::TextStyle dst{tokens.accent, 10.5f, 600};
        const std::string dt = "Downloading... " +
            std::to_string(static_cast<int>(dl_progress_.load() * 100.0f)) + "%";
        const auto dtr = Layout::CenteredTextRect(
            r, dt, dst, {ix, fb + 64.0f, iw, 10.0f}, 0.0f, false);
        r.DrawText(dt, dtr, dst);
    }
}

void HubBrowserWidget::DrawStatusBar(Rendering::Renderer& r,
                                     const Theme::Tokens& tokens) {
    const auto sr = statusbar_rect_;
    r.FillRect(sr, tokens.surface, 0.0f);

    // Connection dot + API label
    const Theme::Color dot = searching_
        ? tokens.accent
        : (results_.empty() ? tokens.muted
                            : Theme::Color{0.30f, 0.85f, 0.45f, 1.0f});
    r.FillRect({sr.x + 10.0f, sr.y + sr.height * 0.5f - 4.0f, 8.0f, 8.0f},
               dot, 4.0f);

    const Rendering::TextStyle api_st{tokens.muted, 11.0f, 500};
    const auto api_r = Layout::CenteredTextRect(
        r, "Civitai API v1", api_st, {sr.x + 24.0f, sr.y + 4.0f, 110.0f, 18.0f},
        0.0f, false);
    r.DrawText("Civitai API v1", api_r, api_st);

    // Status text (center-left)
    const Rendering::TextStyle st{tokens.foreground, 11.5f, 450};
    const auto str_ = Layout::CenteredTextRect(
        r, status_, st, {sr.x + 140.0f, sr.y + 4.0f,
                         sr.width - 420.0f, 18.0f}, 0.0f, false);
    r.DrawText(status_, str_, st);

    // Page info right of buttons
    const Rendering::TextStyle pg{tokens.muted, 11.0f, 500};
    std::string page_txt;
    if (!results_.empty()) {
        if (query_.empty()) {
            page_txt = "Page " + std::to_string(page_);
        } else {
            page_txt = "Page " + std::to_string(cursor_pos_ + 1);
        }
    }
    const auto pgr = Layout::CenteredTextRect(
        r, page_txt, pg, {prev_rect_.x - 80.0f, sr.y + 4.0f, 76.0f, 18.0f},
        0.0f, false);
    r.DrawText(page_txt, pgr, pg);
}

// Chevron helper (small vector arrow)
void HubBrowserWidget::DrawChevron(Rendering::Renderer& r,
                                   const Theme::Tokens& tokens,
                                   Layout::Rect box, Theme::Color color) {
    const float cx = box.x + box.width * 0.5f;
    const float cy = box.y + box.height * 0.5f;
    const float s = std::min(box.width, box.height) * 0.3f;
    r.DrawLine({cx - s * 0.6f, cy - s}, {cx + s * 0.4f, cy}, color, 1.6f);
    r.DrawLine({cx + s * 0.4f, cy}, {cx - s * 0.6f, cy + s}, color, 1.6f);
}

// FileRowAt is defined above (hit-tests file_rects_ computed in
// RecomputeHitRects).

Layout::Size HubBrowserWidget::GetMinSize() const {
    return {760.0f, 520.0f};
}
Layout::Size HubBrowserWidget::GetPreferredSize() const {
    return {1280.0f, 800.0f};
}
Layout::Size HubBrowserWidget::GetMaxSize() const {
    return {4096.0f, 4096.0f};
}

}  // namespace XER::Tools::HubBrowser
