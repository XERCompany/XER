#include "hub_browser_widget.h"

#include "async_io/io_thread_pool.h"
#include "cache/cache_manager.h"
#include "content/clipboard/clipboard.h"
#include "helper/env_utils.h"
#include "ui/layout/flex.h"
#include "ui/rendering/renderer.h"
#include "ui/widgets/button.h"
#include "ui/widgets/dialog.h"
#include "ui/widgets/select.h"
#include "ui/widgets/switch.h"
#include "ui/widgets/text_input.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <json/json.h>
#include <sstream>
#include <system_error>

namespace XER::Tools::HubBrowser {

using namespace XER::UI;
using XER::UI::Widgets::AppTitleBar;
using XER::UI::Widgets::Button;
using XER::UI::Widgets::ButtonStyle;
using XER::UI::Widgets::Event;
using XER::UI::Widgets::MouseButton;
using XER::UI::Widgets::Select;
using XER::UI::Widgets::Switch;
using XER::UI::Widgets::Tabs;
using XER::UI::Widgets::TextInput;
using XER::UI::Widgets::Widget;

namespace {

constexpr float kTitleBarH = 38.0f;
constexpr float kTabBarH = 38.0f;
constexpr float kToolbarH = 52.0f;
constexpr float kStatusH = 28.0f;
constexpr float kSidebarW = 204.0f;
constexpr float kInspectorW = 340.0f;
constexpr float kRowH = 68.0f;
constexpr float kRowGap = 6.0f;
constexpr float kGridCardW = 236.0f;
constexpr float kGridCardH = 284.0f;
constexpr float kGridGapX = 12.0f;
constexpr float kGridGapY = 14.0f;
constexpr float kFileRowH = 28.0f;
constexpr float kCatRowH = 30.0f;
constexpr float kDlRowH = 54.0f;

constexpr const char* kSettingsScope = "hub_settings";
constexpr const char* kSettingsKey = "config";
constexpr const char* kSettingsFile = "settings.json";

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

const char* WorkspaceTabName(HubBrowserWidget::WorkspaceTab tab) {
    switch (tab) {
        case HubBrowserWidget::WorkspaceTab::Hub: return "Civitai Hub";
        case HubBrowserWidget::WorkspaceTab::Inspector: return "Model Inspector";
        case HubBrowserWidget::WorkspaceTab::Downloads: return "Downloads";
        case HubBrowserWidget::WorkspaceTab::Settings: return "Settings";
    }
    return "Civitai Hub";
}

}  // namespace

HubBrowserWidget::HubBrowserWidget() {
    api_key_ = XER::Helper::GetEnv("XER_CIVITAI_API_KEY");
    LoadSettingsFromCache();

    BuildTitleBar();
    BuildTabBar();
    BuildToolbar();
    BuildInspectorControls();
    BuildStudioInspectorControls();
    BuildDownloadsControls();
    BuildSettingsControls();

    AddChild(title_bar_);
    AddChild(workspace_tabs_);

    // Hub tab children
    AddChild(search_input_);
    AddChild(search_btn_);
    AddChild(refresh_btn_);
    AddChild(type_select_);
    AddChild(sort_select_);
    AddChild(nsfw_switch_);
    AddChild(prev_btn_);
    AddChild(next_btn_);
    AddChild(version_select_);
    AddChild(download_btn_);
    AddChild(preview_btn_);
    AddChild(copy_id_btn_);

    // Studio Inspector tab children
    AddChild(studio_prev_img_btn_);
    AddChild(studio_next_img_btn_);
    AddChild(studio_zoom_btn_);
    AddChild(studio_copy_prompt_btn_);
    AddChild(studio_copy_json_btn_);
    AddChild(studio_version_select_);
    AddChild(studio_download_btn_);
    AddChild(studio_inspect_zip_btn_);

    // Downloads tab children
    AddChild(dl_rescan_btn_);
    AddChild(dl_open_dlg_btn_);
    AddChild(dl_inspect_zip_btn_);
    AddChild(dl_extract_zip_btn_);
    AddChild(dl_copy_path_btn_);

    // Settings tab children
    AddChild(set_api_input_);
    AddChild(set_toggle_key_btn_);
    AddChild(set_save_btn_);
    AddChild(set_tls_switch_);
    AddChild(set_limit_select_);
    AddChild(set_preview_switch_);
    AddChild(set_nsfw_switch_);
    AddChild(set_theme_select_);
    AddChild(set_copy_cache_btn_);
    AddChild(set_refresh_cache_btn_);
    AddChild(set_clear_previews_btn_);

    SyncChildVisibility();
    RefreshCacheStatsAsync();
    RunSearch();
}

HubBrowserWidget::~HubBrowserWidget() = default;

void HubBrowserWidget::SetOnThemeRequest(std::function<void(bool)> callback) {
    on_theme_request_ = std::move(callback);
    if (on_theme_request_) {
        on_theme_request_(dark_theme_);
    }
}

// ---------------------------------------------------------------------------
// Persistent Settings (XER::Cache::CacheManager)
// ---------------------------------------------------------------------------

void HubBrowserWidget::LoadSettingsFromCache() {
    std::string raw;
    std::string err;
    if (!XER::Cache::CacheManager::Instance().ReadPersistentText(
            kSettingsScope, kSettingsKey, kSettingsFile, &raw, &err) ||
        raw.empty()) {
        return;
    }

    Json::CharReaderBuilder rb;
    Json::Value root;
    std::string jerr;
    std::istringstream ss(raw);
    if (!Json::parseFromStream(rb, ss, &root, &jerr) || !root.isObject()) {
        return;
    }

    if (root["api_key"].isString() && !root["api_key"].asString().empty()) {
        api_key_ = root["api_key"].asString();
        content::civitai::CivitaiModelDownloader::Instance().SetApiKey(api_key_);
    }
    if (root["verify_tls"].isBool()) {
        verify_tls_ = root["verify_tls"].asBool();
        content::civitai::CivitaiModelDownloader::Instance().SetSkipVerify(!verify_tls_);
    }
    if (root["auto_previews"].isBool()) {
        auto_previews_ = root["auto_previews"].asBool();
    }
    if (root["nsfw"].isBool()) {
        nsfw_ = root["nsfw"].asBool();
    }
    if (root["dark_theme"].isBool()) {
        dark_theme_ = root["dark_theme"].asBool();
    }
    if (root["limit"].isInt()) {
        const int l = root["limit"].asInt();
        if (l == 12 || l == 24 || l == 48) limit_ = l;
    }
}

void HubBrowserWidget::SaveSettingsToCache() {
    Json::Value root(Json::objectValue);
    root["api_key"] = api_key_;
    root["verify_tls"] = verify_tls_;
    root["auto_previews"] = auto_previews_;
    root["nsfw"] = nsfw_;
    root["dark_theme"] = dark_theme_;
    root["limit"] = limit_;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "  ";
    const std::string text = Json::writeString(wb, root);
    std::string err;
    XER::Cache::CacheManager::Instance().WritePersistentText(
        kSettingsScope, kSettingsKey, kSettingsFile, text, &err);
}

void HubBrowserWidget::RefreshCacheStatsAsync() {
    if (scanning_cache_) return;
    scanning_cache_ = true;
    cache_scan_future_ = std::async(std::launch::async, []() {
        auto& pc = HubPreviewCache::Instance();
        return std::make_pair(pc.ComputeSummary(), pc.ScanCachedModels());
    });
}

// ---------------------------------------------------------------------------
// Construction helpers
// ---------------------------------------------------------------------------

void HubBrowserWidget::BuildTitleBar() {
    title_bar_ = std::make_shared<AppTitleBar>("Hub");
    auto* tb = title_bar_.get();

    tb->SetWorkspaceLabel("Civitai Hub");
    RebuildWorkspaceMenu();
    tb->SetNavActions(false, false,
                      [this]() { NavigateHistory(-1); },
                      [this]() { NavigateHistory(1); });
    RebuildMenus();

    tb->SetCenterCommand("Search Civitai models...",
                         [this]() {
                             SwitchTab(WorkspaceTab::Hub);
                             static_cast<TextInput*>(search_input_.get())->SetFocused(true);
                         },
                         true);

    tb->SetLeftPanelToggle(true, left_panel_visible_, [this]() {
        SetLeftPanelVisible(!left_panel_visible_);
    });
    tb->SetRightPanelToggle(true, right_panel_visible_, [this]() {
        SetRightPanelVisible(!right_panel_visible_);
    });
    tb->SetThemeToggle(dark_theme_, nullptr);

    tb->SetAvatar("Civitai User", {
        {"Settings & API Key", "", [this]() { SwitchTab(WorkspaceTab::Settings); }},
        {"Downloads & ZIP Cache", "", [this]() { SwitchTab(WorkspaceTab::Downloads); }},
        {"API Key Status", "", [this]() {
            SetStatus(api_key_.empty()
                ? "No API key set — configure in Settings tab or XER_CIVITAI_API_KEY."
                : "Civitai API key active.");
        }, true, false, false, true},
    });

    tb->SetActionPill("Quick Fetch", [this]() {
        SwitchTab(WorkspaceTab::Hub);
        RunSearch();
    }, false);
}

void HubBrowserWidget::RebuildWorkspaceMenu() {
    if (!title_bar_) return;
    title_bar_->SetWorkspaceLabel(WorkspaceTabName(active_tab_));
    title_bar_->SetWorkspaceItems({
        {"Civitai Hub", "", [this]() { SwitchTab(WorkspaceTab::Hub); },
         true, active_tab_ == WorkspaceTab::Hub, false, false},
        {"Model Inspector", "", [this]() { SwitchTab(WorkspaceTab::Inspector); },
         true, active_tab_ == WorkspaceTab::Inspector, false, true},
        {"Downloads", "", [this]() { SwitchTab(WorkspaceTab::Downloads); },
         true, active_tab_ == WorkspaceTab::Downloads, false, false},
        {"Settings", "", [this]() { SwitchTab(WorkspaceTab::Settings); },
         true, active_tab_ == WorkspaceTab::Settings, false, true},
    });
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
        {"Search Models", "Ctrl+K", [this]() {
            SwitchTab(WorkspaceTab::Hub);
            static_cast<TextInput*>(search_input_.get())->SetFocused(true);
        }},
        {"Refresh Models", "F5", [this]() { RunSearch(); }, true, false, false, true},
        {"Downloads & ZIP Viewer", "", [this]() { SwitchTab(WorkspaceTab::Downloads); }},
        {"Settings", "", [this]() { SwitchTab(WorkspaceTab::Settings); }, true, false, false, true},
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
        {"Civitai Hub Tab", "", [this]() { SwitchTab(WorkspaceTab::Hub); },
         true, active_tab_ == WorkspaceTab::Hub},
        {"Model Inspector Tab", "", [this]() { SwitchTab(WorkspaceTab::Inspector); },
         true, active_tab_ == WorkspaceTab::Inspector},
        {"Downloads Tab", "", [this]() { SwitchTab(WorkspaceTab::Downloads); },
         true, active_tab_ == WorkspaceTab::Downloads},
        {"Settings Tab", "", [this]() { SwitchTab(WorkspaceTab::Settings); },
         true, active_tab_ == WorkspaceTab::Settings},
        {"List View", "", [this]() { SetViewMode(ViewMode::List); },
         true, view_mode_ == ViewMode::List, false, true},
        {"Grid View", "", [this]() { SetViewMode(ViewMode::Grid); },
         true, view_mode_ == ViewMode::Grid},
        {"Left Sidebar", "", [this]() { SetLeftPanelVisible(!left_panel_visible_); },
         true, left_panel_visible_, false, true},
        {"Right Inspector", "", [this]() { SetRightPanelVisible(!right_panel_visible_); },
         true, right_panel_visible_},
    });
    add_menu("Help", {
        {"Open Artwork Preview Dialog", "", [this]() { OpenPreviewDialog(); }},
        {"Refresh Local Cache Stats", "", [this]() { RefreshCacheStatsAsync(); }},
        {"About Civitai Studio Hub", "", [this]() {
            SetStatus("Civitai Studio Hub — XER src/ui + CacheManager + AsyncIO + ArchiveManager + XER_Network.");
        }, true, false, false, true},
    });
    title_bar_->SetMenus(std::move(menus));
}

void HubBrowserWidget::BuildTabBar() {
    workspace_tabs_ = std::make_shared<Tabs>();
    workspace_tabs_->SetStyle(Tabs::Style::Underline);
    workspace_tabs_->AddTab("Civitai Hub");
    workspace_tabs_->AddTab("Model Inspector");
    workspace_tabs_->AddTab("Downloads");
    workspace_tabs_->AddTab("Settings");
    workspace_tabs_->SetActiveIndex(0);
    workspace_tabs_->SetOnChanged([this](int idx) {
        SwitchTab(static_cast<WorkspaceTab>(std::clamp(idx, 0, 3)));
    });
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

    nsfw_switch_ = std::make_shared<Switch>(nsfw_);
    static_cast<Switch*>(nsfw_switch_.get())->SetOnChanged([this](bool v) {
        if (nsfw_ == v) return;
        nsfw_ = v;
        if (set_nsfw_switch_) {
            static_cast<Switch*>(set_nsfw_switch_.get())->SetChecked(v);
        }
        SaveSettingsToCache();
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
        } else if (cursor_pos_ > 0) {
            --cursor_pos_;
            cursor_ = cursor_history_[static_cast<std::size_t>(cursor_pos_)];
            RunSearch();
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
    version_select_ = std::make_shared<Select>(std::vector<std::string>{"-"}, -1);
    static_cast<Select*>(version_select_.get())->SetOnChanged([this](int idx) {
        selected_file_ = -1;
        preview_image_idx_ = 0;
        if (studio_version_select_) {
            static_cast<Select*>(studio_version_select_.get())->SetSelectedIndex(idx);
        }
        RecomputeHitRects();
        Invalidate();
    });

    download_btn_ = std::make_shared<Button>("Download", ButtonStyle::Primary);
    static_cast<Button*>(download_btn_.get())->SetOnClick(
        [this]() { StartDownload(true); });

    preview_btn_ = std::make_shared<Button>("Gallery", ButtonStyle::Secondary);
    static_cast<Button*>(preview_btn_.get())->SetOnClick(
        [this]() { OpenPreviewDialog(); });

    copy_id_btn_ = std::make_shared<Button>("Copy ID", ButtonStyle::Ghost);
    static_cast<Button*>(copy_id_btn_.get())->SetOnClick(
        [this]() { CopyModelId(); });
}

void HubBrowserWidget::BuildStudioInspectorControls() {
    studio_prev_img_btn_ = std::make_shared<Button>("< Prev", ButtonStyle::Secondary);
    studio_next_img_btn_ = std::make_shared<Button>("Next >", ButtonStyle::Secondary);
    studio_zoom_btn_ = std::make_shared<Button>("Lightbox Dialog", ButtonStyle::Secondary);
    studio_copy_prompt_btn_ = std::make_shared<Button>("Copy Prompt", ButtonStyle::Primary);
    studio_copy_json_btn_ = std::make_shared<Button>("Copy sd.exe JSON", ButtonStyle::Secondary);

    auto step_img = [this](int delta) {
        const auto* ver = SelectedVersion();
        if (!ver || ver->images.empty()) return;
        const int n = static_cast<int>(ver->images.size());
        preview_image_idx_ = (preview_image_idx_ + delta + n) % n;
        Invalidate();
    };
    static_cast<Button*>(studio_prev_img_btn_.get())->SetOnClick([step_img]() { step_img(-1); });
    static_cast<Button*>(studio_next_img_btn_.get())->SetOnClick([step_img]() { step_img(1); });
    static_cast<Button*>(studio_zoom_btn_.get())->SetOnClick([this]() { OpenPreviewDialog(); });
    static_cast<Button*>(studio_copy_prompt_btn_.get())->SetOnClick([this]() {
        const auto* ver = SelectedVersion();
        if (!ver || ver->images.empty()) return;
        const int idx = std::clamp(preview_image_idx_, 0, static_cast<int>(ver->images.size()) - 1);
        const auto& img = ver->images[static_cast<std::size_t>(idx)];
        XER::Content::Clipboard::SetText(img.prompt.empty() ? results_[selected_].name : img.prompt);
        SetStatus("Copied artwork prompt to clipboard.");
    });
    static_cast<Button*>(studio_copy_json_btn_.get())->SetOnClick([this]() {
        const auto* ver = SelectedVersion();
        if (!ver || ver->images.empty() || selected_ < 0) return;
        const int idx = std::clamp(preview_image_idx_, 0, static_cast<int>(ver->images.size()) - 1);
        XER::Content::Clipboard::SetText(BuildSdGenerateJson(
            results_[selected_].name, PrimaryFileNameForSelected(),
            ver->images[static_cast<std::size_t>(idx)]));
        SetStatus("Copied sd.exe generate.json configuration to clipboard.");
    });

    studio_version_select_ = std::make_shared<Select>(std::vector<std::string>{"-"}, -1);
    static_cast<Select*>(studio_version_select_.get())->SetOnChanged([this](int idx) {
        selected_file_ = -1;
        preview_image_idx_ = 0;
        static_cast<Select*>(version_select_.get())->SetSelectedIndex(idx);
        RecomputeHitRects();
        Invalidate();
    });

    studio_download_btn_ = std::make_shared<Button>("Download Package", ButtonStyle::Primary);
    static_cast<Button*>(studio_download_btn_.get())->SetOnClick([this]() { StartDownload(true); });

    studio_inspect_zip_btn_ = std::make_shared<Button>("Download / ZIP Dialog", ButtonStyle::Secondary);
    static_cast<Button*>(studio_inspect_zip_btn_.get())->SetOnClick([this]() {
        if (!download_jobs_.empty()) {
            OpenDownloadDialog(download_jobs_.front());
        } else {
            StartDownload(true);
        }
    });
}

void HubBrowserWidget::BuildDownloadsControls() {
    dl_rescan_btn_ = std::make_shared<Button>("Rescan Cache", ButtonStyle::Secondary);
    dl_open_dlg_btn_ = std::make_shared<Button>("Progress Dialog", ButtonStyle::Primary);
    dl_inspect_zip_btn_ = std::make_shared<Button>("Inspect ZIP", ButtonStyle::Secondary);
    dl_extract_zip_btn_ = std::make_shared<Button>("Extract ZIP", ButtonStyle::Secondary);
    dl_copy_path_btn_ = std::make_shared<Button>("Copy File Path", ButtonStyle::Ghost);

    static_cast<Button*>(dl_rescan_btn_.get())->SetOnClick([this]() {
        RefreshCacheStatsAsync();
        SetStatus("Scanning CacheManager models directory...");
    });
    static_cast<Button*>(dl_open_dlg_btn_.get())->SetOnClick([this]() {
        const int n_jobs = static_cast<int>(download_jobs_.size());
        if (selected_dl_index_ >= 0 && selected_dl_index_ < n_jobs) {
            OpenDownloadDialog(download_jobs_[static_cast<std::size_t>(selected_dl_index_)]);
        } else if (inspected_disk_job_) {
            OpenDownloadDialog(inspected_disk_job_);
        } else if (!download_jobs_.empty()) {
            OpenDownloadDialog(download_jobs_.front());
        } else {
            SetStatus("Select a download or cached package first.");
        }
    });
    static_cast<Button*>(dl_inspect_zip_btn_.get())->SetOnClick([this]() {
        const int n_jobs = static_cast<int>(download_jobs_.size());
        std::shared_ptr<DownloadJob> job;
        if (selected_dl_index_ >= 0 && selected_dl_index_ < n_jobs) {
            job = download_jobs_[static_cast<std::size_t>(selected_dl_index_)];
        } else {
            job = inspected_disk_job_;
        }
        if (!job) {
            SetStatus("Select an item in Downloads to inspect with ArchiveManager.");
            return;
        }
        std::string p;
        {
            std::lock_guard<std::mutex> lock(job->mu);
            p = job->final_path.empty() ? job->target_path : job->final_path;
        }
        InspectJobArchiveAsync(job, p);
    });
    static_cast<Button*>(dl_extract_zip_btn_.get())->SetOnClick([this]() {
        const int n_jobs = static_cast<int>(download_jobs_.size());
        std::shared_ptr<DownloadJob> job =
            (selected_dl_index_ >= 0 && selected_dl_index_ < n_jobs)
                ? download_jobs_[static_cast<std::size_t>(selected_dl_index_)]
                : inspected_disk_job_;
        if (!job) return;
        ExtractJobArchiveAsync(job);
    });
    static_cast<Button*>(dl_copy_path_btn_.get())->SetOnClick([this]() {
        const int n_jobs = static_cast<int>(download_jobs_.size());
        std::shared_ptr<DownloadJob> job =
            (selected_dl_index_ >= 0 && selected_dl_index_ < n_jobs)
                ? download_jobs_[static_cast<std::size_t>(selected_dl_index_)]
                : inspected_disk_job_;
        if (!job) return;
        std::string p;
        {
            std::lock_guard<std::mutex> lock(job->mu);
            p = job->final_path.empty() ? job->target_path : job->final_path;
        }
        XER::Content::Clipboard::SetText(p);
        SetStatus("Copied path: " + p);
    });
}

void HubBrowserWidget::BuildSettingsControls() {
    set_api_input_ = std::make_shared<TextInput>(api_key_);
    auto* api_ti = static_cast<TextInput*>(set_api_input_.get());
    api_ti->SetPlaceholder("Paste Civitai API token (optional)...");
    api_ti->SetPasswordMode(true);
    api_ti->SetOnValueChanged([this](const std::string& v) { api_key_ = v; });

    set_toggle_key_btn_ = std::make_shared<Button>("Show", ButtonStyle::Secondary);
    static_cast<Button*>(set_toggle_key_btn_.get())->SetOnClick([this]() {
        api_key_visible_ = !api_key_visible_;
        static_cast<TextInput*>(set_api_input_.get())->SetPasswordMode(!api_key_visible_);
        static_cast<Button*>(set_toggle_key_btn_.get())->SetText(
            api_key_visible_ ? "Hide" : "Show");
    });

    set_save_btn_ = std::make_shared<Button>("Save & Apply", ButtonStyle::Primary);
    static_cast<Button*>(set_save_btn_.get())->SetOnClick([this]() {
        content::civitai::CivitaiModelDownloader::Instance().SetApiKey(api_key_);
        content::civitai::CivitaiModelDownloader::Instance().SetSkipVerify(!verify_tls_);
        SaveSettingsToCache();
        SetStatus("Settings saved to XER CacheManager (" +
                  std::string(api_key_.empty() ? "anonymous mode" : "API key active") + ").");
    });

    set_tls_switch_ = std::make_shared<Switch>(verify_tls_);
    static_cast<Switch*>(set_tls_switch_.get())->SetOnChanged([this](bool v) {
        verify_tls_ = v;
        content::civitai::CivitaiModelDownloader::Instance().SetSkipVerify(!v);
        SaveSettingsToCache();
    });

    const int limit_idx = (limit_ == 12) ? 0 : (limit_ == 48 ? 2 : 1);
    set_limit_select_ = std::make_shared<Select>(
        std::vector<std::string>{"12 per page", "24 per page", "48 per page"},
        limit_idx);
    static_cast<Select*>(set_limit_select_.get())->SetOnChanged([this](int idx) {
        static const int kLimits[] = {12, 24, 48};
        limit_ = kLimits[std::clamp(idx, 0, 2)];
        SaveSettingsToCache();
        ResetPagination();
        RunSearch();
    });

    set_preview_switch_ = std::make_shared<Switch>(auto_previews_);
    static_cast<Switch*>(set_preview_switch_.get())->SetOnChanged([this](bool v) {
        auto_previews_ = v;
        SaveSettingsToCache();
        Invalidate();
    });

    set_nsfw_switch_ = std::make_shared<Switch>(nsfw_);
    static_cast<Switch*>(set_nsfw_switch_.get())->SetOnChanged([this](bool v) {
        if (nsfw_ == v) return;
        nsfw_ = v;
        static_cast<Switch*>(nsfw_switch_.get())->SetChecked(v);
        SaveSettingsToCache();
        RebuildMenus();
        ResetPagination();
        RunSearch();
    });

    set_theme_select_ = std::make_shared<Select>(
        std::vector<std::string>{"Dark Studio", "Light Zinc"},
        dark_theme_ ? 0 : 1);
    static_cast<Select*>(set_theme_select_.get())->SetOnChanged([this](int idx) {
        dark_theme_ = (idx == 0);
        SaveSettingsToCache();
        if (on_theme_request_) on_theme_request_(dark_theme_);
    });

    set_copy_cache_btn_ = std::make_shared<Button>("Copy Cache Root", ButtonStyle::Secondary);
    static_cast<Button*>(set_copy_cache_btn_.get())->SetOnClick([this]() {
        const std::string p = XER::Cache::CacheManager::Instance().RootDirectory().string();
        XER::Content::Clipboard::SetText(p);
        SetStatus("Copied CacheManager root: " + p);
    });

    set_refresh_cache_btn_ = std::make_shared<Button>("Refresh Stats", ButtonStyle::Secondary);
    static_cast<Button*>(set_refresh_cache_btn_.get())->SetOnClick([this]() {
        RefreshCacheStatsAsync();
    });

    set_clear_previews_btn_ = std::make_shared<Button>("Clear Preview Cache", ButtonStyle::Destructive);
    static_cast<Button*>(set_clear_previews_btn_.get())->SetOnClick([this]() {
        const std::size_t n = HubPreviewCache::Instance().ClearPreviewCache();
        RefreshCacheStatsAsync();
        SetStatus("Cleared " + std::to_string(n) + " cached preview image(s).");
    });
}

// ---------------------------------------------------------------------------
// Workspace tab navigation
// ---------------------------------------------------------------------------

void HubBrowserWidget::SwitchTab(WorkspaceTab tab, bool record_history) {
    if (active_tab_ == tab) return;
    active_tab_ = tab;
    workspace_tabs_->SetActiveIndex(static_cast<int>(tab));

    if (record_history) {
        if (tab_history_pos_ + 1 < static_cast<int>(tab_history_.size())) {
            tab_history_.erase(tab_history_.begin() + tab_history_pos_ + 1,
                               tab_history_.end());
        }
        tab_history_.push_back(tab);
        tab_history_pos_ = static_cast<int>(tab_history_.size()) - 1;
    }

    title_bar_->SetNavActions(
        tab_history_pos_ > 0,
        tab_history_pos_ + 1 < static_cast<int>(tab_history_.size()),
        [this]() { NavigateHistory(-1); },
        [this]() { NavigateHistory(1); });

    if (tab == WorkspaceTab::Downloads || tab == WorkspaceTab::Settings) {
        RefreshCacheStatsAsync();
    }

    RebuildWorkspaceMenu();
    RebuildMenus();
    SyncChildVisibility();
    RequestLayout();
    Invalidate();
}

void HubBrowserWidget::NavigateHistory(int delta) {
    const int next = tab_history_pos_ + delta;
    if (next < 0 || next >= static_cast<int>(tab_history_.size())) return;
    tab_history_pos_ = next;
    SwitchTab(tab_history_[static_cast<std::size_t>(next)], false);
    title_bar_->SetNavActions(
        tab_history_pos_ > 0,
        tab_history_pos_ + 1 < static_cast<int>(tab_history_.size()),
        [this]() { NavigateHistory(-1); },
        [this]() { NavigateHistory(1); });
}

void HubBrowserWidget::SyncChildVisibility() {
    const bool is_hub = active_tab_ == WorkspaceTab::Hub;
    const bool is_insp = active_tab_ == WorkspaceTab::Inspector;
    const bool is_dl = active_tab_ == WorkspaceTab::Downloads;
    const bool is_set = active_tab_ == WorkspaceTab::Settings;
    const bool has_sel = selected_ >= 0 &&
                         selected_ < static_cast<int>(results_.size());

    // Hub controls
    search_input_->SetVisible(is_hub);
    search_btn_->SetVisible(is_hub);
    refresh_btn_->SetVisible(is_hub);
    type_select_->SetVisible(is_hub);
    sort_select_->SetVisible(is_hub);
    nsfw_switch_->SetVisible(is_hub);
    prev_btn_->SetVisible(is_hub);
    next_btn_->SetVisible(is_hub);

    const bool show_quick_insp = is_hub && right_panel_visible_ && has_sel;
    version_select_->SetVisible(show_quick_insp);
    download_btn_->SetVisible(show_quick_insp);
    preview_btn_->SetVisible(show_quick_insp);
    copy_id_btn_->SetVisible(show_quick_insp);

    // Studio Inspector controls
    const bool show_studio = is_insp && has_sel;
    studio_prev_img_btn_->SetVisible(show_studio);
    studio_next_img_btn_->SetVisible(show_studio);
    studio_zoom_btn_->SetVisible(show_studio);
    studio_copy_prompt_btn_->SetVisible(show_studio);
    studio_copy_json_btn_->SetVisible(show_studio);
    studio_version_select_->SetVisible(show_studio);
    studio_download_btn_->SetVisible(show_studio);
    studio_inspect_zip_btn_->SetVisible(show_studio);

    // Downloads controls
    dl_rescan_btn_->SetVisible(is_dl);
    dl_open_dlg_btn_->SetVisible(is_dl);
    dl_inspect_zip_btn_->SetVisible(is_dl);
    dl_extract_zip_btn_->SetVisible(is_dl);
    dl_copy_path_btn_->SetVisible(is_dl);

    // Settings controls
    set_api_input_->SetVisible(is_set);
    set_toggle_key_btn_->SetVisible(is_set);
    set_save_btn_->SetVisible(is_set);
    set_tls_switch_->SetVisible(is_set);
    set_limit_select_->SetVisible(is_set);
    set_preview_switch_->SetVisible(is_set);
    set_nsfw_switch_->SetVisible(is_set);
    set_theme_select_->SetVisible(is_set);
    set_copy_cache_btn_->SetVisible(is_set);
    set_refresh_cache_btn_->SetVisible(is_set);
    set_clear_previews_btn_->SetVisible(is_set);
}

// ---------------------------------------------------------------------------
// Search / selection / downloads / dialogs
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
    SetStatus("Searching Civitai API v1...");

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

const content::civitai::CivitaiModelVersion*
HubBrowserWidget::SelectedVersion() const {
    if (selected_ < 0 || selected_ >= static_cast<int>(results_.size())) {
        return nullptr;
    }
    const auto& m = results_[static_cast<std::size_t>(selected_)];
    if (m.versions.empty()) return nullptr;
    auto* vs = static_cast<Select*>(version_select_.get());
    const int vi = std::clamp(vs->GetSelectedIndex(), 0,
                              static_cast<int>(m.versions.size()) - 1);
    return &m.versions[static_cast<std::size_t>(vi)];
}

std::string HubBrowserWidget::PrimaryFileNameForSelected() const {
    const auto* ver = SelectedVersion();
    if (!ver || ver->files.empty()) return {};
    if (selected_file_ >= 0 &&
        selected_file_ < static_cast<int>(ver->files.size())) {
        return ver->files[static_cast<std::size_t>(selected_file_)].name;
    }
    for (const auto& f : ver->files) {
        if (f.primary && f.type == "Model") return f.name;
    }
    return ver->files.front().name;
}

void HubBrowserWidget::SelectRow(int index) {
    if (index < 0 || index >= static_cast<int>(results_.size())) return;
    selected_ = index;
    selected_file_ = -1;
    preview_image_idx_ = 0;

    const auto& m = results_[static_cast<std::size_t>(index)];
    std::vector<std::string> opts;
    opts.reserve(m.versions.size());
    for (const auto& v : m.versions) opts.push_back(v.name);
    if (opts.empty()) opts.push_back("-");

    auto* vs1 = static_cast<Select*>(version_select_.get());
    auto* vs2 = static_cast<Select*>(studio_version_select_.get());
    vs1->SetOptions(opts);
    vs1->SetSelectedIndex(0);
    vs2->SetOptions(opts);
    vs2->SetSelectedIndex(0);

    SyncChildVisibility();
    RequestLayout();
    RecomputeHitRects();
    Invalidate();
}

void HubBrowserWidget::SelectFile(int file_index) {
    const auto* ver = SelectedVersion();
    if (!ver) return;
    const int nf = static_cast<int>(ver->files.size());
    selected_file_ = std::clamp(file_index, -1, nf - 1);
    RecomputeHitRects();
    Invalidate();
}

void HubBrowserWidget::OpenPreviewDialog() {
    if (selected_ < 0 || selected_ >= static_cast<int>(results_.size())) {
        SetStatus("Select a model first to view its generated example artworks.");
        return;
    }
    const auto* ver = SelectedVersion();
    if (!ver || ver->images.empty()) {
        SetStatus("No example images reported by Civitai for this version.");
        return;
    }
    auto dlg = ImagePreviewDialogContent::CreateDialog(
        results_[static_cast<std::size_t>(selected_)].name,
        PrimaryFileNameForSelected(),
        ver->images,
        preview_image_idx_,
        [this](const std::string& msg) { SetStatus(msg); });
    ShowOverlay(dlg);
}

void HubBrowserWidget::OpenDownloadDialog(
    const std::shared_ptr<DownloadJob>& job) {
    if (!job) return;
    auto dlg = DownloadModelDialogContent::CreateDialog(
        job, [this](const std::string& msg) { SetStatus(msg); });
    ShowOverlay(dlg);
}

void HubBrowserWidget::StartDownload(bool open_dialog) {
    if (selected_ < 0 || selected_ >= static_cast<int>(results_.size())) {
        SetStatus("Select a model first.");
        return;
    }
    const auto& m = results_[static_cast<std::size_t>(selected_)];
    const auto* ver = SelectedVersion();
    if (!ver || ver->files.empty()) {
        SetStatus("Selected model version has no downloadable files.");
        return;
    }

    const content::civitai::CivitaiModelFile* file = nullptr;
    if (selected_file_ >= 0 &&
        selected_file_ < static_cast<int>(ver->files.size())) {
        file = &ver->files[static_cast<std::size_t>(selected_file_)];
    } else {
        for (const auto& f : ver->files) {
            if (f.primary && f.type == "Model") { file = &f; break; }
        }
        if (!file) {
            for (const auto& f : ver->files) {
                if (f.type == "Model") { file = &f; break; }
            }
        }
        if (!file) file = &ver->files.front();
    }

    auto job = std::make_shared<DownloadJob>();
    job->model_id = m.id;
    job->model_name = m.name;
    job->version_id = ver->id;
    job->version_name = ver->name;
    job->file_name = file->name;
    job->file_format = file->format;
    job->file_type = file->type;
    job->sha256 = file->sha256;
    job->total_bytes.store(file->size_bytes);
    job->active.store(true);
    job->started_at = std::chrono::steady_clock::now();
    job->target_path = XER::Cache::CacheManager::Instance()
        .PathFor("models", "civitai/" + m.id + "/" + ver->id, file->name)
        .string();
    {
        std::lock_guard<std::mutex> lock(job->mu);
        job->status_text = "Connecting to Civitai CDN...";
        job->archive_summary =
            "Waiting for download to finish before probing ZIP/archive contents...";
    }

    download_jobs_.insert(download_jobs_.begin(), job);
    selected_dl_index_ = 0;
    SetStatus("Downloading " + file->name + " ...");

    auto& dl = content::civitai::CivitaiModelDownloader::Instance();
    dl.DownloadModelFile(
        *file, m.id, ver->id,
        [job](uint64_t done, uint64_t total) {
            job->done_bytes.store(done);
            if (total > 0) job->total_bytes.store(total);
            const auto elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - job->started_at).count();
            if (elapsed > 0.15) {
                job->speed_bps.store(static_cast<double>(done) / elapsed);
            }
            {
                std::lock_guard<std::mutex> lock(job->mu);
                job->status_text = "Downloading...";
            }
            job->dirty.store(true);
        },
        [job](bool ok, const std::string& err,
              const std::filesystem::path& path) {
            job->active.store(false);
            job->finished.store(true);
            job->ok.store(ok);
            if (ok && job->total_bytes.load() > 0) {
                job->done_bytes.store(job->total_bytes.load());
            }
            const std::string resolved =
                path.empty() ? job->target_path : path.string();
            {
                std::lock_guard<std::mutex> lock(job->mu);
                job->final_path = resolved;
                job->error_text = err;
                job->status_text = ok ? "Completed & Verified"
                                      : ("Failed: " + err);
            }
            if (ok && !resolved.empty()) {
                // Automatically inspect ZIP / archive entries on this IO worker!
                InspectJobArchiveSync(*job, resolved);
            } else {
                std::lock_guard<std::mutex> lock(job->mu);
                job->archive_summary = "Download did not complete: " + err;
                job->dirty.store(true);
            }
        });

    if (open_dialog) {
        OpenDownloadDialog(job);
    }
    Invalidate();
}

void HubBrowserWidget::SelectDownloadItem(int index) {
    const int n_jobs = static_cast<int>(download_jobs_.size());
    const int n_disk = static_cast<int>(cached_disk_files_.size());
    if (index < 0 || index >= n_jobs + n_disk) return;
    selected_dl_index_ = index;
    dl_arch_scroll_ = 0.0f;

    if (index >= n_jobs) {
        const auto& df = cached_disk_files_[static_cast<std::size_t>(index - n_jobs)];
        auto job = std::make_shared<DownloadJob>();
        job->model_name = df.relative_key.empty() ? "Cached Package" : df.relative_key;
        job->file_name = df.file_name;
        job->file_format = df.is_archive ? "Archive" : "Model";
        job->target_path = df.full_path;
        job->final_path = df.full_path;
        job->done_bytes.store(df.size_bytes);
        job->total_bytes.store(df.size_bytes);
        job->finished.store(true);
        job->ok.store(true);
        {
            std::lock_guard<std::mutex> lock(job->mu);
            job->status_text = "Cached on Disk";
        }
        inspected_disk_job_ = job;
        InspectJobArchiveAsync(inspected_disk_job_, df.full_path);
    }
    Invalidate();
}

void HubBrowserWidget::CopyModelId() {
    if (selected_ < 0 || selected_ >= static_cast<int>(results_.size())) {
        SetStatus("Select a model first.");
        return;
    }
    const auto& m = results_[static_cast<std::size_t>(selected_)];
    XER::Content::Clipboard::SetText(m.id);
    SetStatus("Copied model ID " + m.id);
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
    SyncChildVisibility();
    RecomputeHitRects();
    RequestLayout();
    Invalidate();
}

// ---------------------------------------------------------------------------
// Tick — async completion polling (UI thread)
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
        preview_image_idx_ = 0;
        scroll_ = 0.0f;

        has_next_ = query_.empty()
            ? (static_cast<int>(results_.size()) >= limit_)
            : !next_cursor_.empty();

        char buf[192];
        std::snprintf(buf, sizeof(buf),
                      "%zu model(s) — page %d%s", results_.size(), page_,
                      results_.empty() ? " (try another filter)" : "");
        SetStatus(buf);
        SyncChildVisibility();
        RecomputeHitRects();
        Invalidate();
    }

    if (scanning_cache_ && cache_scan_future_.valid() &&
        cache_scan_future_.wait_for(std::chrono::milliseconds(0)) ==
            std::future_status::ready) {
        auto res = cache_scan_future_.get();
        cache_summary_ = std::move(res.first);
        cached_disk_files_ = std::move(res.second);
        scanning_cache_ = false;
        RecomputeHitRects();
        Invalidate();
    }

    if (HubPreviewCache::Instance().ConsumeCompleted()) {
        Invalidate();
    }

    bool any_dl_dirty = false;
    for (const auto& j : download_jobs_) {
        if (j->active.load() || j->dirty.exchange(false)) {
            any_dl_dirty = true;
            if (j->finished.load()) {
                RefreshCacheStatsAsync();
            }
        }
    }
    if (inspected_disk_job_ && inspected_disk_job_->dirty.exchange(false)) {
        any_dl_dirty = true;
    }
    if (any_dl_dirty) {
        Invalidate();
    }
}

// ---------------------------------------------------------------------------
// Hit testing helpers
// ---------------------------------------------------------------------------

int HubBrowserWidget::CategoryAt(float y) const {
    for (std::size_t i = 0; i < category_rects_.size(); ++i) {
        const auto& r = category_rects_[i];
        if (y >= r.y && y < r.y + r.height) return static_cast<int>(i);
    }
    return -1;
}

int HubBrowserWidget::RowAt(float y) const {
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
    const auto& rects = (active_tab_ == WorkspaceTab::Inspector)
        ? studio_file_rects_
        : file_rects_;
    for (std::size_t i = 0; i < rects.size(); ++i) {
        const auto& r = rects[i];
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

    tabbar_rect_ = {b.x, titlebar_rect_.Bottom(), b.width, kTabBarH};
    workspace_tabs_->Layout(
        {tabbar_rect_.x + 12.0f, tabbar_rect_.y + 2.0f,
         std::max(240.0f, tabbar_rect_.width - 300.0f), kTabBarH - 2.0f},
        tokens);
    active_dl_pill_rect_ = {tabbar_rect_.Right() - 256.0f,
                            tabbar_rect_.y + 6.0f, 244.0f, 26.0f};

    const float sy = b.Bottom() - kStatusH;
    statusbar_rect_ = {b.x, sy, b.width, kStatusH};
    body_rect_ = {b.x, tabbar_rect_.Bottom(), b.width,
                  std::max(0.0f, sy - tabbar_rect_.Bottom())};

    // Status bar pagination buttons
    const float syy = statusbar_rect_.y + (kStatusH - 22.0f) * 0.5f;
    next_rect_ = {statusbar_rect_.Right() - 78.0f, syy, 70.0f, 22.0f};
    prev_rect_ = {statusbar_rect_.Right() - 156.0f, syy, 70.0f, 22.0f};
    next_btn_->Layout(next_rect_, tokens);
    prev_btn_->Layout(prev_rect_, tokens);

    // ---- Tab 0: Civitai Hub ----
    toolbar_rect_ = {body_rect_.x, body_rect_.y, body_rect_.width, kToolbarH};
    const float iy = toolbar_rect_.Bottom();
    const float side_w = left_panel_visible_ ? kSidebarW : 0.0f;
    const float insp_w = right_panel_visible_ ? kInspectorW : 0.0f;
    sidebar_rect_ = {b.x, iy, side_w, std::max(0.0f, sy - iy)};
    inspector_rect_ = {b.Right() - insp_w, iy, insp_w, std::max(0.0f, sy - iy)};
    center_rect_ = {b.x + side_w, iy,
                    std::max(0.0f, b.width - side_w - insp_w),
                    std::max(0.0f, sy - iy)};

    results_header_rect_ = {center_rect_.x + 12.0f, center_rect_.y + 6.0f,
                            std::max(0.0f, center_rect_.width - 24.0f), 24.0f};
    list_rect_ = {center_rect_.x + 12.0f, center_rect_.y + 34.0f,
                  std::max(0.0f, center_rect_.width - 24.0f),
                  std::max(0.0f, center_rect_.height - 42.0f)};
    scrollbar_rect_ = {list_rect_.Right() - 6.0f, list_rect_.y,
                       6.0f, list_rect_.height};

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

    if (right_panel_visible_ && inspector_rect_.width > 0.0f) {
        const float ix = inspector_rect_.x + 12.0f;
        const float iw = inspector_rect_.width - 24.0f;
        inspector_preview_rect_ = {ix, inspector_rect_.y + 82.0f, iw, 152.0f};
        inspector_prev_img_rect_ = {inspector_preview_rect_.x + 6.0f,
                                    inspector_preview_rect_.Bottom() - 28.0f,
                                    26.0f, 22.0f};
        inspector_next_img_rect_ = {inspector_preview_rect_.Right() - 32.0f,
                                    inspector_preview_rect_.Bottom() - 28.0f,
                                    26.0f, 22.0f};

        const float fb = inspector_rect_.Bottom() - 104.0f;
        version_select_->Layout({ix, fb + 22.0f, iw, 30.0f}, tokens);
        const auto act_slots = Layout::DistributeRow(
            {ix, fb + 60.0f, iw, 32.0f},
            {120.0f, 86.0f, 78.0f},
            {1.0f, 0.0f, 0.0f},
            6.0f,
            Layout::FlexAlign::Start);
        if (act_slots.size() == 3) {
            download_btn_->Layout(act_slots[0], tokens);
            preview_btn_->Layout(act_slots[1], tokens);
            copy_id_btn_->Layout(act_slots[2], tokens);
        }
    }

    // ---- Tab 1: Studio Model Inspector ----
    {
        const float pad = 16.0f;
        const float left_w = std::max(320.0f, (body_rect_.width - pad * 3.0f) * 0.56f);
        const float right_x = body_rect_.x + pad * 2.0f + left_w;
        const float right_w = std::max(260.0f, body_rect_.Right() - pad - right_x);

        const float stage_h = std::max(180.0f, body_rect_.height - 236.0f);
        studio_stage_rect_ = {body_rect_.x + pad, body_rect_.y + pad, left_w, stage_h};

        const float ctrl_y = studio_stage_rect_.Bottom() + 8.0f;
        studio_prev_img_btn_->Layout({studio_stage_rect_.x, ctrl_y, 76.0f, 30.0f}, tokens);
        studio_next_img_btn_->Layout({studio_stage_rect_.x + 82.0f, ctrl_y, 76.0f, 30.0f}, tokens);
        studio_zoom_btn_->Layout({studio_stage_rect_.x + 166.0f, ctrl_y, 126.0f, 30.0f}, tokens);
        studio_copy_json_btn_->Layout({studio_stage_rect_.Right() - 138.0f, ctrl_y, 138.0f, 30.0f}, tokens);
        studio_copy_prompt_btn_->Layout({studio_stage_rect_.Right() - 256.0f, ctrl_y, 112.0f, 30.0f}, tokens);

        studio_thumb_rects_.clear();
        if (const auto* ver = SelectedVersion()) {
            const std::size_t n = std::min<std::size_t>(ver->images.size(), 10);
            float tx = studio_stage_rect_.x;
            for (std::size_t i = 0; i < n; ++i) {
                studio_thumb_rects_.push_back({tx, ctrl_y + 38.0f, 46.0f, 44.0f});
                tx += 52.0f;
            }
        }

        studio_version_select_->Layout({right_x + 14.0f, body_rect_.y + 168.0f,
                                        right_w - 28.0f, 32.0f}, tokens);
        const auto rbtns = Layout::DistributeRow(
            {right_x + 14.0f, body_rect_.Bottom() - pad - 46.0f, right_w - 28.0f, 34.0f},
            {160.0f, 160.0f}, {1.0f, 1.0f}, 8.0f, Layout::FlexAlign::Start);
        if (rbtns.size() == 2) {
            studio_download_btn_->Layout(rbtns[0], tokens);
            studio_inspect_zip_btn_->Layout(rbtns[1], tokens);
        }
    }

    // ---- Tab 2: Downloads & Archive Viewer ----
    {
        const float pad = 16.0f;
        const float act_y = body_rect_.y + 92.0f;
        const auto dbtns = Layout::DistributeRow(
            {body_rect_.x + pad, act_y,
             std::min(760.0f, body_rect_.width - 2.0f * pad), 32.0f},
            {126.0f, 140.0f, 120.0f, 120.0f, 126.0f},
            {0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
            8.0f, Layout::FlexAlign::Start);
        if (dbtns.size() == 5) {
            dl_rescan_btn_->Layout(dbtns[0], tokens);
            dl_open_dlg_btn_->Layout(dbtns[1], tokens);
            dl_inspect_zip_btn_->Layout(dbtns[2], tokens);
            dl_extract_zip_btn_->Layout(dbtns[3], tokens);
            dl_copy_path_btn_->Layout(dbtns[4], tokens);
        }

        const float pane_top = act_y + 44.0f;
        const float pane_h = std::max(120.0f, body_rect_.Bottom() - pad - pane_top);
        const float left_w = (body_rect_.width - pad * 3.0f) * 0.46f;
        dl_left_list_rect_ = {body_rect_.x + pad, pane_top, left_w, pane_h};
        dl_right_arch_rect_ = {dl_left_list_rect_.Right() + pad, pane_top,
                               body_rect_.Right() - pad - (dl_left_list_rect_.Right() + pad),
                               pane_h};
    }

    // ---- Tab 3: Settings ----
    {
        const float pad = 20.0f;
        const float col_w = std::max(300.0f, (body_rect_.width - pad * 3.0f) * 0.5f);
        const float c1x = body_rect_.x + pad;
        const float c2x = c1x + col_w + pad;
        const float top_y = body_rect_.y + pad;

        // Left column: API + Network card (height 190) & Appearance card (height 130)
        set_api_input_->Layout({c1x + 16.0f, top_y + 62.0f,
                                std::max(120.0f, col_w - 196.0f), 32.0f}, tokens);
        set_toggle_key_btn_->Layout({c1x + col_w - 172.0f, top_y + 62.0f, 64.0f, 32.0f}, tokens);
        set_save_btn_->Layout({c1x + col_w - 102.0f, top_y + 62.0f, 86.0f, 32.0f}, tokens);
        set_tls_switch_->Layout({c1x + col_w - 54.0f, top_y + 122.0f, 38.0f, 20.0f}, tokens);

        const float app_y = top_y + 206.0f;
        set_theme_select_->Layout({c1x + col_w - 186.0f, app_y + 52.0f, 170.0f, 32.0f}, tokens);

        // Right column: Browsing & Previews card (height 190) & Storage/Cache card
        set_limit_select_->Layout({c2x + col_w - 186.0f, top_y + 46.0f, 170.0f, 30.0f}, tokens);
        set_preview_switch_->Layout({c2x + col_w - 54.0f, top_y + 98.0f, 38.0f, 20.0f}, tokens);
        set_nsfw_switch_->Layout({c2x + col_w - 54.0f, top_y + 140.0f, 38.0f, 20.0f}, tokens);

        const float st_y = top_y + 206.0f;
        const auto cbtns = Layout::DistributeRow(
            {c2x + 16.0f, st_y + 132.0f, col_w - 32.0f, 32.0f},
            {130.0f, 120.0f, 150.0f}, {1.0f, 1.0f, 1.0f}, 8.0f,
            Layout::FlexAlign::Start);
        if (cbtns.size() == 3) {
            set_copy_cache_btn_->Layout(cbtns[0], tokens);
            set_refresh_cache_btn_->Layout(cbtns[1], tokens);
            set_clear_previews_btn_->Layout(cbtns[2], tokens);
        }
    }

    RecomputeHitRects();
}

void HubBrowserWidget::RecomputeHitRects() {
    category_rects_.clear();
    row_rects_.clear();
    grid_rects_.clear();
    file_rects_.clear();
    studio_file_rects_.clear();
    dl_row_rects_.clear();

    if (left_panel_visible_ && sidebar_rect_.width > 0.0f) {
        float y = sidebar_rect_.y + 40.0f;
        for (std::size_t i = 0; i < Categories().size(); ++i) {
            category_rects_.push_back(
                {sidebar_rect_.x + 8.0f, y, sidebar_rect_.width - 16.0f, kCatRowH});
            y += kCatRowH + 2.0f;
        }
    }

    const float content_h = list_rect_.height;
    if (view_mode_ == ViewMode::List) {
        const float pitch = kRowH + kRowGap;
        const int first = static_cast<int>(scroll_ / pitch);
        const int visible = static_cast<int>(std::ceil(content_h / pitch)) + 2;
        for (int i = first; i < first + visible; ++i) {
            if (i < 0 || i >= static_cast<int>(results_.size())) break;
            const float ry = list_rect_.y + static_cast<float>(i) * pitch - scroll_;
            row_rects_.push_back({list_rect_.x, ry, list_rect_.width - 12.0f, kRowH});
        }
        const float total = static_cast<float>(results_.size()) * pitch;
        if (total > content_h && total > 0.0f) {
            const float thumb_h = std::max(24.0f, content_h * content_h / total);
            const float max_scroll = total - content_h;
            const float ty = list_rect_.y + (scroll_ / max_scroll) * (content_h - thumb_h);
            scrollbar_thumb_rect_ = {scrollbar_rect_.x, ty, scrollbar_rect_.width, thumb_h};
        } else {
            scrollbar_thumb_rect_ = {};
        }
    } else {
        const float avail_w = list_rect_.width - 12.0f;
        const int cols = std::max(1, static_cast<int>(
            (avail_w + kGridGapX) / (kGridCardW + kGridGapX)));
        const float row_pitch_y = kGridCardH + kGridGapY;
        const int first_row = static_cast<int>(scroll_ / row_pitch_y);
        grid_start_idx_ = first_row * cols;
        const int visible_rows = static_cast<int>(std::ceil(content_h / row_pitch_y)) + 1;
        for (int r = first_row; r < first_row + visible_rows; ++r) {
            const int start_idx = r * cols;
            if (start_idx >= static_cast<int>(results_.size())) break;
            for (int c = 0; c < cols; ++c) {
                const int idx = start_idx + c;
                if (idx >= static_cast<int>(results_.size())) break;
                const float gx = list_rect_.x + static_cast<float>(c) * (kGridCardW + kGridGapX);
                const float gy = list_rect_.y + static_cast<float>(r) * row_pitch_y - scroll_;
                grid_rects_.push_back({gx, gy, kGridCardW, kGridCardH});
            }
        }
        const int rows_total = (static_cast<int>(results_.size()) + cols - 1) / cols;
        const float total = static_cast<float>(rows_total) * row_pitch_y;
        if (total > content_h && total > 0.0f) {
            const float thumb_h = std::max(24.0f, content_h * content_h / total);
            const float max_scroll = total - content_h;
            const float ty = list_rect_.y + (scroll_ / max_scroll) * (content_h - thumb_h);
            scrollbar_thumb_rect_ = {scrollbar_rect_.x, ty, scrollbar_rect_.width, thumb_h};
        } else {
            scrollbar_thumb_rect_ = {};
        }
    }

    // Quick Inspector file rows
    if (const auto* ver = SelectedVersion()) {
        if (right_panel_visible_ && inspector_rect_.width > 0.0f) {
            const float files_top = inspector_rect_.y + 324.0f;
            const float files_bottom = inspector_rect_.Bottom() - 108.0f;
            const int max_f = std::max(0, static_cast<int>((files_bottom - files_top - 20.0f) / kFileRowH));
            const int shown = std::min(static_cast<int>(ver->files.size()), max_f);
            for (int i = 0; i < shown; ++i) {
                file_rects_.push_back({inspector_rect_.x + 12.0f,
                                       files_top + 20.0f + static_cast<float>(i) * kFileRowH,
                                       inspector_rect_.width - 24.0f, kFileRowH - 3.0f});
            }
        }

        // Studio Inspector file rows
        const float pad = 16.0f;
        const float left_w = std::max(320.0f, (body_rect_.width - pad * 3.0f) * 0.56f);
        const float right_x = body_rect_.x + pad * 2.0f + left_w;
        const float right_w = std::max(260.0f, body_rect_.Right() - pad - right_x);
        const float sf_top = body_rect_.y + 232.0f;
        const float sf_bot = body_rect_.Bottom() - pad - 56.0f;
        const int max_sf = std::max(0, static_cast<int>((sf_bot - sf_top) / 32.0f));
        const int shown_sf = std::min(static_cast<int>(ver->files.size()), max_sf);
        for (int i = 0; i < shown_sf; ++i) {
            studio_file_rects_.push_back(
                {right_x + 14.0f, sf_top + static_cast<float>(i) * 32.0f,
                 right_w - 28.0f, 28.0f});
        }
    }

    // Downloads list rows
    const int total_dl = static_cast<int>(download_jobs_.size() + cached_disk_files_.size());
    for (int i = 0; i < total_dl; ++i) {
        const float ry = dl_left_list_rect_.y + 30.0f +
                         static_cast<float>(i) * kDlRowH - dl_list_scroll_;
        dl_row_rects_.push_back({dl_left_list_rect_.x + 8.0f, ry,
                                 dl_left_list_rect_.width - 16.0f, kDlRowH - 4.0f});
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
    if (event.type == Event::Type::KeyDown) {
        if (event.key == Widgets::KeyCode::F5) {
            RunSearch();
            return true;
        }
        if (event.is_ctrl && !event.is_alt &&
            (event.key == Widgets::KeyCode::K ||
             event.key == Widgets::KeyCode::F ||
             event.key == Widgets::KeyCode::P)) {
            SwitchTab(WorkspaceTab::Hub);
            static_cast<TextInput*>(search_input_.get())->SetFocused(true);
            return true;
        }
    }

    // Active download pill in the tab bar (clickable from any tab)
    if (event.type == Event::Type::Click &&
        event.mouse_button == MouseButton::Left &&
        !download_jobs_.empty() &&
        active_dl_pill_rect_.Contains(event.position)) {
        OpenDownloadDialog(download_jobs_.front());
        return true;
    }

    if (active_tab_ == WorkspaceTab::Inspector) {
        if (event.type == Event::Type::Click &&
            event.mouse_button == MouseButton::Left) {
            for (std::size_t i = 0; i < studio_thumb_rects_.size(); ++i) {
                if (studio_thumb_rects_[i].Contains(event.position)) {
                    preview_image_idx_ = static_cast<int>(i);
                    Invalidate();
                    return true;
                }
            }
            if (studio_stage_rect_.Contains(event.position)) {
                OpenPreviewDialog();
                return true;
            }
            const int fi = FileRowAt(event.position.y);
            if (fi >= 0 && event.position.x > studio_stage_rect_.Right()) {
                SelectFile(fi);
                return true;
            }
        }
        return false;
    }

    if (active_tab_ == WorkspaceTab::Downloads) {
        if (event.type == Event::Type::MouseWheel) {
            if (dl_left_list_rect_.Contains(event.position)) {
                const float total_h = static_cast<float>(dl_row_rects_.size()) * kDlRowH + 36.0f;
                const float max_s = std::max(0.0f, total_h - dl_left_list_rect_.height);
                dl_list_scroll_ = std::clamp(
                    dl_list_scroll_ - static_cast<float>(event.wheel_delta) * 0.45f,
                    0.0f, max_s);
                RecomputeHitRects();
                Invalidate();
                return true;
            }
            if (dl_right_arch_rect_.Contains(event.position)) {
                dl_arch_scroll_ = std::max(
                    0.0f, dl_arch_scroll_ - static_cast<float>(event.wheel_delta) * 0.45f);
                Invalidate();
                return true;
            }
        }
        if (event.type == Event::Type::MouseDown &&
            event.mouse_button == MouseButton::Left &&
            dl_left_list_rect_.Contains(event.position)) {
            for (std::size_t i = 0; i < dl_row_rects_.size(); ++i) {
                if (dl_row_rects_[i].Contains(event.position)) {
                    SelectDownloadItem(static_cast<int>(i));
                    return true;
                }
            }
        }
        return false;
    }

    if (active_tab_ == WorkspaceTab::Settings) {
        return false;
    }

    // ---- WorkspaceTab::Hub events ----
    if (event.type == Event::Type::MouseWheel) {
        const Layout::Point p = event.position;
        if (center_rect_.Contains(p) || list_rect_.Contains(p)) {
            ScrollTo(scroll_ - static_cast<float>(event.wheel_delta) * 0.5f);
            return true;
        }
        return false;
    }

    if (event.type == Event::Type::MouseDown &&
        event.mouse_button == MouseButton::Left) {
        const Layout::Point p = event.position;

        if (!scrollbar_thumb_rect_.width && scrollbar_rect_.Contains(p)) {
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

        if (left_panel_visible_ && sidebar_rect_.Contains(p)) {
            const int cat = CategoryAt(p.y);
            if (cat >= 0 && cat < static_cast<int>(Categories().size()) &&
                cat != active_category_) {
                active_category_ = cat;
                types_ = Categories()[static_cast<std::size_t>(cat)].api_type;
                RebuildMenus();
                ResetPagination();
                RunSearch();
                static_cast<Select*>(type_select_.get())->SetSelectedIndex(cat);
                return true;
            }
            if (cat >= 0) return true;
        }

        if (center_rect_.Contains(p) || list_rect_.Contains(p)) {
            if (view_mode_ == ViewMode::List) {
                const int row = RowAt(p.y);
                if (row >= 0 && row < static_cast<int>(row_rects_.size())) {
                    const float pitch = kRowH + kRowGap;
                    const int first = static_cast<int>(scroll_ / pitch);
                    SelectRow(first + row);
                    return true;
                }
            } else if (GridCellAt(p) >= 0) {
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

        if (right_panel_visible_ && inspector_rect_.Contains(p)) {
            const int fi = FileRowAt(p.y);
            if (fi >= 0) { SelectFile(fi); return true; }
        }
        return false;
    }

    if (event.type == Event::Type::MouseMove && scrollbar_dragging_) {
        const float content_h = list_rect_.height;
        float total = 0.0f;
        if (view_mode_ == ViewMode::List) {
            total = static_cast<float>(results_.size()) * (kRowH + kRowGap);
        } else {
            const float avail_w = list_rect_.width - 12.0f;
            const int cols = std::max(1, static_cast<int>(
                (avail_w + kGridGapX) / (kGridCardW + kGridGapX)));
            const int rows = (static_cast<int>(results_.size()) + cols - 1) / cols;
            total = static_cast<float>(rows) * (kGridCardH + kGridGapY);
        }
        const float max_scroll = std::max(1.0f, total - content_h);
        const float travel = std::max(1.0f, content_h - scrollbar_thumb_rect_.height);
        const float frac = std::clamp(
            (event.position.y - scrollbar_grab_ - list_rect_.y) / travel,
            0.0f, 1.0f);
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
        if (nsfw_label_rect_.Contains(p)) {
            auto* sw = static_cast<Switch*>(nsfw_switch_.get());
            sw->SetChecked(!sw->IsChecked());
            return true;
        }
        if (view_list_rect_.Contains(p)) { SetViewMode(ViewMode::List); return true; }
        if (view_grid_rect_.Contains(p)) { SetViewMode(ViewMode::Grid); return true; }

        if (right_panel_visible_ && selected_ >= 0 &&
            inspector_preview_rect_.Contains(p)) {
            const auto* ver = SelectedVersion();
            if (ver && !ver->images.empty()) {
                const int n = static_cast<int>(ver->images.size());
                if (inspector_prev_img_rect_.Contains(p)) {
                    preview_image_idx_ = (preview_image_idx_ - 1 + n) % n;
                    Invalidate();
                    return true;
                }
                if (inspector_next_img_rect_.Contains(p)) {
                    preview_image_idx_ = (preview_image_idx_ + 1) % n;
                    Invalidate();
                    return true;
                }
                OpenPreviewDialog();
                return true;
            }
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

    DrawTabBarExtras(r, tokens);

    switch (active_tab_) {
        case WorkspaceTab::Hub:
            DrawToolbar(r, tokens);
            DrawCategorySidebar(r, tokens);
            DrawResults(r, tokens);
            DrawInspector(r, tokens);
            r.DrawLine({b.x, toolbar_rect_.Bottom()},
                       {b.Right(), toolbar_rect_.Bottom()},
                       tokens.border, 1.0f);
            break;
        case WorkspaceTab::Inspector:
            DrawStudioInspectorTab(r, tokens);
            break;
        case WorkspaceTab::Downloads:
            DrawDownloadsTab(r, tokens);
            break;
        case WorkspaceTab::Settings:
            DrawSettingsTab(r, tokens);
            break;
    }

    DrawStatusBar(r, tokens);
    r.DrawLine({b.x, statusbar_rect_.y}, {b.Right(), statusbar_rect_.y},
               tokens.border, 1.0f);
}

void HubBrowserWidget::DrawTabBarExtras(Rendering::Renderer& r,
                                        const Theme::Tokens& tokens) {
    r.FillRect(tabbar_rect_, tokens.surface, 0.0f);
    r.DrawLine({tabbar_rect_.x, tabbar_rect_.Bottom()},
               {tabbar_rect_.Right(), tabbar_rect_.Bottom()},
               tokens.border, 1.0f);

    // Right-aligned live download / cache telemetry pill
    int active_cnt = 0;
    float active_pct = 0.0f;
    for (const auto& j : download_jobs_) {
        if (j->active.load()) {
            ++active_cnt;
            const uint64_t t = j->total_bytes.load();
            if (t > 0) {
                active_pct = static_cast<float>(j->done_bytes.load() * 100.0 / t);
            }
        }
    }

    std::string pill_txt;
    Theme::Color pill_bg = tokens.secondary;
    Theme::Color pill_fg = tokens.muted;
    if (active_cnt > 0) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "↓ %d downloading (%.0f%%) — View",
                      active_cnt, active_pct);
        pill_txt = buf;
        pill_bg = tokens.accent;
        pill_fg = tokens.accent_foreground;
    } else if (!download_jobs_.empty()) {
        pill_txt = "✓ " + std::to_string(download_jobs_.size()) +
                   " download(s) — Open Dialog";
        pill_fg = tokens.foreground;
    } else {
        const auto workers =
            XER::AsyncIO::IO::Async::IOThreadPool::GetSharedInstance()
                .GetThreadCount();
        pill_txt = "AsyncIO: " + std::to_string(workers) +
                   " threads • Cache: " + std::to_string(cache_summary_.model_count) +
                   " models";
    }

    r.FillRect(active_dl_pill_rect_, pill_bg, 13.0f);
    if (active_cnt == 0) {
        r.StrokeRect(active_dl_pill_rect_, tokens.border, 1.0f, 13.0f);
    }
    const Rendering::TextStyle st{pill_fg, 11.0f, 600};
    r.DrawText(pill_txt,
               Layout::CenteredTextRect(r, pill_txt, st, active_dl_pill_rect_,
                                        8.0f, true),
               st);
}

void HubBrowserWidget::DrawToolbar(Rendering::Renderer& r,
                                   const Theme::Tokens& tokens) {
    const Rendering::TextStyle nsfw_style{
        nsfw_ ? tokens.foreground : tokens.muted, 12.0f, 500};
    const auto nsfw_txt = Layout::CenteredTextRect(
        r, "NSFW", nsfw_style, nsfw_label_rect_, 0.0f, false);
    r.DrawText("NSFW", nsfw_txt, nsfw_style);

    r.FillRect(view_seg_rect_, tokens.surface, tokens.radius_medium);
    r.StrokeRect(view_seg_rect_, tokens.border, 1.0f, tokens.radius_medium);
    auto draw_seg = [&](const Layout::Rect& rect, const char* label, bool active) {
        if (active) r.FillRect(rect, tokens.accent, tokens.radius_small);
        const Rendering::TextStyle st{
            active ? tokens.accent_foreground : tokens.muted,
            11.5f, active ? 600 : 500};
        r.DrawText(label,
                   Layout::CenteredTextRect(r, label, st, rect, 2.0f, true),
                   st);
    };
    draw_seg(view_list_rect_, "List", view_mode_ == ViewMode::List);
    draw_seg(view_grid_rect_, "Grid", view_mode_ == ViewMode::Grid);
}

void HubBrowserWidget::DrawBadge(Rendering::Renderer& r,
                                 const Theme::Tokens&,
                                 const std::string& text,
                                 Layout::Rect rect,
                                 Theme::Color bg, Theme::Color fg) {
    if (text.empty() || rect.width <= 0.0f) return;
    r.FillRect(rect, bg, rect.height * 0.5f);
    const Rendering::TextStyle style{fg, 10.0f, 600};
    r.DrawText(text, Layout::CenteredTextRect(r, text, style, rect, 5.0f, true),
               style);
}

XER::UI::Theme::Color HubBrowserWidget::TypeColor(const std::string& type) const {
    static const std::pair<const char*, Theme::Color> kMap[] = {
        {"Checkpoint", {0.51f, 0.55f, 0.97f, 1.0f}},
        {"LORA", {0.30f, 0.85f, 0.85f, 1.0f}},
        {"TextualInversion", {0.29f, 0.87f, 0.50f, 1.0f}},
        {"Controlnet", {0.98f, 0.60f, 0.24f, 1.0f}},
        {"Hypernetwork", {0.65f, 0.55f, 0.98f, 1.0f}},
        {"AestheticGradient", {0.95f, 0.45f, 0.65f, 1.0f}},
        {"Poses", {0.98f, 0.82f, 0.30f, 1.0f}},
        {"Wildcards", {0.63f, 0.66f, 0.72f, 1.0f}},
        {"Workflows", {0.45f, 0.75f, 0.98f, 1.0f}},
        {"MotionModule", {0.80f, 0.50f, 0.95f, 1.0f}},
        {"Utility", {0.55f, 0.60f, 0.68f, 1.0f}},
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
    return FormatByteSize(b);
}

void HubBrowserWidget::DrawCategorySidebar(Rendering::Renderer& r,
                                           const Theme::Tokens& tokens) {
    if (!left_panel_visible_ || sidebar_rect_.width <= 0.0f) return;
    const auto sr = sidebar_rect_;
    r.FillRect(sr, tokens.surface, 0.0f);
    r.DrawLine({sr.Right(), sr.y}, {sr.Right(), sr.Bottom()}, tokens.border, 1.0f);

    const Rendering::TextStyle hdr{tokens.muted, 10.5f, 700};
    r.DrawText("CATEGORIES",
               Layout::CenteredTextRect(
                   r, "CATEGORIES", hdr,
                   {sr.x + 12.0f, sr.y + 10.0f, sr.width - 24.0f, 18.0f}, 0.0f, false),
               hdr);

    for (std::size_t i = 0;
         i < category_rects_.size() && i < Categories().size(); ++i) {
        const auto& cr = category_rects_[i];
        const bool active = static_cast<int>(i) == active_category_;
        if (active) r.FillRect(cr, tokens.accent, 6.0f);
        const Rendering::TextStyle style{
            active ? tokens.accent_foreground : tokens.foreground,
            12.0f, active ? 600 : 450};
        r.DrawText(Categories()[i].label,
                   Layout::CenteredTextRect(r, Categories()[i].label, style, cr,
                                            12.0f, false),
                   style);
    }
}

void HubBrowserWidget::DrawResults(Rendering::Renderer& r,
                                   const Theme::Tokens& tokens) {
    const auto cr = center_rect_;
    if (cr.width <= 0.0f || cr.height <= 0.0f) return;

    const Rendering::TextStyle count_style{tokens.muted, 11.5f, 600};
    std::string count_txt = searching_
        ? "Searching..."
        : (results_.empty() ? "No models"
                            : std::to_string(results_.size()) + " models");
    const std::string& cat_label =
        (active_category_ >= 0 &&
         active_category_ < static_cast<int>(Categories().size()))
            ? Categories()[static_cast<std::size_t>(active_category_)].label
            : Categories()[0].label;
    count_txt += "  •  " + cat_label + "  •  " + sort_;
    r.DrawText(count_txt,
               Layout::CenteredTextRect(r, count_txt, count_style,
                                        results_header_rect_, 2.0f, false),
               count_style);

    if (searching_ && results_.empty()) {
        const Rendering::TextStyle st{tokens.muted, 13.0f, 450};
        r.DrawText("Contacting Civitai API...",
                   Layout::CenteredTextRect(r, "Contacting Civitai API...", st,
                                            list_rect_, 0.0f, true),
                   st);
        return;
    }
    if (results_.empty() && !searching_) {
        const Rendering::TextStyle st{tokens.muted, 13.0f, 450};
        r.DrawText("No models found. Try another query or filter.",
                   Layout::CenteredTextRect(
                       r, "No models found. Try another query or filter.", st,
                       list_rect_, 0.0f, true),
                   st);
        return;
    }

    r.PushClip(list_rect_);

    if (view_mode_ == ViewMode::List) {
        for (std::size_t i = 0; i < row_rects_.size(); ++i) {
            const int model_idx = static_cast<int>(i) +
                static_cast<int>(scroll_ / (kRowH + kRowGap));
            if (model_idx < 0 || model_idx >= static_cast<int>(results_.size()))
                continue;
            const auto& m = results_[static_cast<std::size_t>(model_idx)];
            const auto& rr = row_rects_[i];
            const bool sel = model_idx == selected_;

            r.FillRect(rr, sel ? tokens.accent : tokens.surface, tokens.radius_medium);
            if (!sel) r.StrokeRect(rr, tokens.border, 1.0f, tokens.radius_medium);

            // Cover thumbnail (52x52) with fallback to colored type glyph
            const Layout::Rect thumb{rr.x + 8.0f, rr.y + 8.0f, 52.0f, 52.0f};
            const std::string thumb_path = m.cover_url.empty()
                ? std::string{}
                : HubPreviewCache::Instance().GetOrRequest(
                      m.cover_url, 512, auto_previews_);
            if (thumb_path.empty() || !r.DrawImage(thumb_path, thumb, 1.0f, 10.0f)) {
                r.FillRect(thumb, TypeColor(m.type), 10.0f);
                const Rendering::TextStyle glyph{tokens.background, 18.0f, 700};
                const std::string letter = m.type.empty() ? "?" : m.type.substr(0, 1);
                r.DrawText(letter,
                           Layout::CenteredTextRect(r, letter, glyph, thumb,
                                                    0.0f, true),
                           glyph);
            }

            const float tx = rr.x + 70.0f;
            const float tw = std::max(40.0f, rr.width - 96.0f);

            const Rendering::TextStyle name_style{
                sel ? tokens.accent_foreground : tokens.foreground, 13.5f, 600};
            r.DrawText(m.name,
                       Layout::CenteredTextRect(r, m.name, name_style,
                                                {tx, rr.y + 7.0f, tw, 20.0f},
                                                0.0f, false),
                       name_style);

            float bx = tx;
            const float by = rr.y + 29.0f;
            const Theme::Color badge_bg = sel
                ? Theme::Color{1.0f, 1.0f, 1.0f, 0.22f}
                : Theme::Color{tokens.muted.r * 0.35f, tokens.muted.g * 0.35f,
                               tokens.muted.b * 0.35f, 1.0f};
            const Theme::Color badge_fg = sel ? tokens.accent_foreground : tokens.foreground;
            const Rendering::TextStyle bs{badge_fg, 10.0f, 600};
            auto badge_w = [&](const std::string& t) {
                return r.MeasureText(t, bs).width + 12.0f;
            };
            const float tbw = badge_w(m.type);
            DrawBadge(r, tokens, m.type, {bx, by, tbw, 16.0f}, badge_bg, badge_fg);
            bx += tbw + 5.0f;
            if (!m.versions.empty() && !m.versions[0].base_model.empty()) {
                const std::string& bm = m.versions[0].base_model;
                const float bmw = badge_w(bm);
                DrawBadge(r, tokens, bm, {bx, by, bmw, 16.0f}, badge_bg, badge_fg);
                bx += bmw + 5.0f;
            }
            if (m.nsfw) {
                DrawBadge(r, tokens, "NSFW", {bx, by, 42.0f, 16.0f},
                          {0.86f, 0.15f, 0.15f, 1.0f}, tokens.danger_foreground);
            }

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
            r.DrawText(stats,
                       Layout::CenteredTextRect(r, stats, st,
                                                {tx, rr.y + 48.0f, tw, 14.0f},
                                                0.0f, false),
                       st);

            DrawChevron(r, tokens,
                        {rr.Right() - 18.0f, rr.y + kRowH * 0.5f - 7.0f, 14.0f, 14.0f},
                        sel ? tokens.accent_foreground : tokens.muted);
        }
    } else {
        for (std::size_t i = 0; i < grid_rects_.size(); ++i) {
            const int idx = grid_start_idx_ + static_cast<int>(i);
            if (idx < 0 || idx >= static_cast<int>(results_.size())) continue;
            const auto& m = results_[static_cast<std::size_t>(idx)];
            const auto& gr = grid_rects_[i];
            const bool sel = idx == selected_;

            r.FillRect(gr, sel ? tokens.accent : tokens.surface, tokens.radius_large);
            r.StrokeRect(gr, sel ? tokens.accent : tokens.border, 1.0f, tokens.radius_large);

            const float head_h = 148.0f;
            const Layout::Rect cover_rect{gr.x + 2.0f, gr.y + 2.0f,
                                          gr.width - 4.0f, head_h};
            const std::string cover_path = m.cover_url.empty()
                ? std::string{}
                : HubPreviewCache::Instance().GetOrRequest(
                      m.cover_url, 512, auto_previews_);
            if (cover_path.empty() ||
                !r.DrawImage(cover_path, cover_rect, 1.0f, tokens.radius_large - 2.0f)) {
                const Theme::Color tc = TypeColor(m.type);
                r.FillRect(cover_rect, tc, tokens.radius_large - 2.0f);
                const Rendering::TextStyle glyph{tokens.background, 34.0f, 800};
                const std::string letter = m.type.empty() ? "?" : m.type.substr(0, 1);
                r.DrawText(letter,
                           Layout::CenteredTextRect(r, letter, glyph, cover_rect,
                                                    0.0f, true),
                           glyph);
            }

            // Floating type badge on cover corner
            const Rendering::TextStyle bs{tokens.background, 9.5f, 700};
            const float tbw = r.MeasureText(m.type, bs).width + 12.0f;
            DrawBadge(r, tokens, m.type,
                      {gr.x + 8.0f, gr.y + 8.0f, tbw, 18.0f},
                      TypeColor(m.type), tokens.background);
            if (m.nsfw) {
                DrawBadge(r, tokens, "NSFW",
                          {gr.Right() - 48.0f, gr.y + 8.0f, 40.0f, 18.0f},
                          {0.86f, 0.15f, 0.15f, 1.0f}, tokens.danger_foreground);
            }

            const float bx = gr.x + 10.0f;
            const float bw = gr.width - 20.0f;
            const Rendering::TextStyle name_style{
                sel ? tokens.accent_foreground : tokens.foreground, 12.5f, 600};
            std::string n1 = m.name, n2;
            if (n1.size() > 30) {
                n2 = n1.substr(30);
                n1 = n1.substr(0, 30);
                if (n2.size() > 28) n2 = n2.substr(0, 26) + "..";
            }
            r.DrawText(n1,
                       Layout::CenteredTextRect(
                           r, n1, name_style,
                           {bx, gr.y + head_h + 10.0f, bw, 18.0f}, 0.0f, false),
                       name_style);
            if (!n2.empty()) {
                r.DrawText(n2,
                           Layout::CenteredTextRect(
                               r, n2, name_style,
                               {bx, gr.y + head_h + 28.0f, bw, 18.0f}, 0.0f, false),
                           name_style);
            }

            const Rendering::TextStyle st{
                sel ? tokens.accent_foreground : tokens.muted, 10.5f, 500};
            std::string stats = "↓ " + FmtCount(m.download_count) +
                                "   ♥ " + FmtCount(m.thumbs_up_count);
            if (!m.versions.empty() && !m.versions[0].base_model.empty()) {
                stats += "   •   " + m.versions[0].base_model;
            }
            r.DrawText(stats,
                       Layout::CenteredTextRect(
                           r, stats, st,
                           {bx, gr.Bottom() - 26.0f, bw, 16.0f}, 0.0f, false),
                       st);
        }
    }

    r.FillRect(scrollbar_rect_, tokens.secondary, 3.0f);
    if (scrollbar_thumb_rect_.width > 0.0f) {
        r.FillRect(scrollbar_thumb_rect_, tokens.muted, 3.0f);
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
        const Rendering::TextStyle st{tokens.muted, 12.5f, 450};
        r.DrawText("Select a model to preview artworks & files.",
                   Layout::CenteredTextRect(
                       r, "Select a model to preview artworks & files.",
                       st, ir, 16.0f, true),
                   st);
        return;
    }

    const auto& m = results_[static_cast<std::size_t>(selected_)];
    const auto* ver = SelectedVersion();

    // Title + badges + creator
    const Rendering::TextStyle title_st{tokens.foreground, 14.0f, 700};
    r.DrawText(m.name,
               Layout::CenteredTextRect(r, m.name, title_st,
                                        {ix, ir.y + 10.0f, iw, 20.0f}, 0.0f, false),
               title_st);

    float bx = ix;
    const float by = ir.y + 34.0f;
    const Theme::Color badge_bg{tokens.muted.r * 0.35f, tokens.muted.g * 0.35f,
                                tokens.muted.b * 0.35f, 1.0f};
    const Rendering::TextStyle bs{tokens.foreground, 10.0f, 600};
    auto badge_w = [&](const std::string& t) {
        return r.MeasureText(t, bs).width + 12.0f;
    };
    DrawBadge(r, tokens, m.type, {bx, by, badge_w(m.type), 16.0f},
              badge_bg, tokens.foreground);
    bx += badge_w(m.type) + 5.0f;
    if (ver && !ver->base_model.empty()) {
        DrawBadge(r, tokens, ver->base_model, {bx, by, badge_w(ver->base_model), 16.0f},
                  badge_bg, tokens.foreground);
        bx += badge_w(ver->base_model) + 5.0f;
    }
    if (m.nsfw) {
        DrawBadge(r, tokens, "NSFW", {bx, by, 42.0f, 16.0f},
                  {0.86f, 0.15f, 0.15f, 1.0f}, tokens.danger_foreground);
    }

    const Rendering::TextStyle meta{tokens.muted, 11.0f, 450};
    const std::string meta_txt = "by " + (m.creator.empty() ? "unknown" : m.creator) +
        "  •  " + std::to_string(m.versions.size()) + " version(s)";
    r.DrawText(meta_txt,
               Layout::CenteredTextRect(r, meta_txt, meta,
                                        {ix, ir.y + 56.0f, iw, 16.0f}, 0.0f, false),
               meta);

    // Generated Example Artwork Preview Box (click -> Lightbox Dialog!)
    r.FillRect(inspector_preview_rect_, tokens.background, tokens.radius_medium);
    r.StrokeRect(inspector_preview_rect_, tokens.border, 1.0f, tokens.radius_medium);
    if (ver && !ver->images.empty()) {
        const int n_img = static_cast<int>(ver->images.size());
        const int idx = std::clamp(preview_image_idx_, 0, n_img - 1);
        const auto& img = ver->images[static_cast<std::size_t>(idx)];
        const std::string path =
            HubPreviewCache::Instance().GetOrRequest(img.url, 512, auto_previews_);
        const Layout::Rect inner{inspector_preview_rect_.x + 4.0f,
                                 inspector_preview_rect_.y + 4.0f,
                                 inspector_preview_rect_.width - 8.0f,
                                 inspector_preview_rect_.height - 8.0f};
        const Layout::Rect fit =
            HubPreviewCache::ContainRect(inner, img.width, img.height);
        if (path.empty() || !r.DrawImage(path, fit, 1.0f, 6.0f)) {
            const Rendering::TextStyle ld{tokens.muted, 11.0f, 500};
            r.DrawText("Loading example artwork...",
                       Layout::CenteredTextRect(r, "Loading example artwork...", ld,
                                                inspector_preview_rect_, 8.0f, true),
                       ld);
        }

        // Overlay < | 1/N (Click to zoom) | >
        const Theme::Color pill_c{0.05f, 0.07f, 0.10f, 0.78f};
        const Rendering::TextStyle nav_st{{1.0f, 1.0f, 1.0f, 0.95f}, 10.5f, 700};
        r.FillRect(inspector_prev_img_rect_, pill_c, 6.0f);
        r.DrawText("<",
                   Layout::CenteredTextRect(r, "<", nav_st, inspector_prev_img_rect_,
                                            0.0f, true),
                   nav_st);
        r.FillRect(inspector_next_img_rect_, pill_c, 6.0f);
        r.DrawText(">",
                   Layout::CenteredTextRect(r, ">", nav_st, inspector_next_img_rect_,
                                            0.0f, true),
                   nav_st);

        const std::string cnt = std::to_string(idx + 1) + " / " + std::to_string(n_img) +
                                "  •  Zoom";
        const float cw = r.MeasureText(cnt, nav_st).width + 16.0f;
        const Layout::Rect cnt_r{
            inspector_preview_rect_.x + (inspector_preview_rect_.width - cw) * 0.5f,
            inspector_preview_rect_.Bottom() - 26.0f, cw, 20.0f};
        r.FillRect(cnt_r, pill_c, 10.0f);
        r.DrawText(cnt, Layout::CenteredTextRect(r, cnt, nav_st, cnt_r, 4.0f, true),
                   nav_st);
    } else {
        const Rendering::TextStyle no_img{tokens.muted, 11.0f, 450};
        r.DrawText("No example artworks on this version",
                   Layout::CenteredTextRect(r, "No example artworks on this version",
                                            no_img, inspector_preview_rect_, 8.0f, true),
                   no_img);
    }

    // Compact 2x2 stats row
    const float sy = inspector_preview_rect_.Bottom() + 10.0f;
    const float col_w = iw * 0.5f;
    const std::string bm = (ver && !ver->base_model.empty()) ? ver->base_model : "-";
    const char* labels[4] = {"Base Model", "Downloads", "Likes", "Model ID"};
    const std::string values[4] = {
        bm, FmtCount(m.download_count), FmtCount(m.thumbs_up_count),
        m.id.size() > 12 ? m.id.substr(0, 10) + ".." : m.id};
    for (int i = 0; i < 4; ++i) {
        const float cx = ix + static_cast<float>(i % 2) * col_w;
        const float cyy = sy + static_cast<float>(i / 2) * 34.0f;
        const Rendering::TextStyle l_st{tokens.muted, 10.0f, 500};
        const Rendering::TextStyle v_st{tokens.foreground, 12.5f, 600};
        r.DrawText(labels[i],
                   Layout::CenteredTextRect(r, labels[i], l_st,
                                            {cx, cyy, col_w - 6.0f, 12.0f}, 0.0f, false),
                   l_st);
        r.DrawText(values[i],
                   Layout::CenteredTextRect(r, values[i], v_st,
                                            {cx, cyy + 12.0f, col_w - 6.0f, 18.0f},
                                            0.0f, false),
                   v_st);
    }

    // Files list
    const float files_top = ir.y + 324.0f;
    const Rendering::TextStyle sec{tokens.muted, 10.5f, 700};
    r.DrawText("PACKAGE FILES",
               Layout::CenteredTextRect(r, "PACKAGE FILES", sec,
                                        {ix, files_top, iw, 16.0f}, 0.0f, false),
               sec);
    if (ver) {
        for (std::size_t i = 0; i < file_rects_.size() && i < ver->files.size(); ++i) {
            const auto& f = ver->files[i];
            const auto& fr = file_rects_[i];
            if (static_cast<int>(i) == selected_file_) {
                r.FillRect(fr, tokens.secondary, tokens.radius_small);
            }
            const std::string fmt = f.format.empty() ? f.type : f.format;
            const Rendering::TextStyle fbs{tokens.background, 9.5f, 600};
            const float fbw = r.MeasureText(fmt, fbs).width + 10.0f;
            DrawBadge(r, tokens, fmt, {fr.x, fr.y + 3.0f, fbw, 19.0f},
                      f.type == "Model" ? TypeColor(m.type) : tokens.muted,
                      tokens.background);

            std::string fname = f.name;
            if (fname.size() > 24) fname = fname.substr(0, 22) + "..";
            const Rendering::TextStyle fn{tokens.foreground, 11.0f, 500};
            r.DrawText(fname,
                       Layout::CenteredTextRect(
                           r, fname, fn,
                           {fr.x + fbw + 6.0f, fr.y, fr.width - fbw - 64.0f, fr.height},
                           0.0f, false),
                       fn);

            const std::string sz = FmtBytes(f.size_bytes);
            const Rendering::TextStyle fs{tokens.muted, 10.0f, 500};
            const float szw = r.MeasureText(sz, fs).width;
            r.DrawText(sz,
                       Layout::CenteredTextRect(
                           r, sz, fs,
                           {fr.Right() - szw - 4.0f, fr.y, szw + 4.0f, fr.height},
                           0.0f, false),
                       fs);
        }
    }

    const float fb = ir.Bottom() - 104.0f;
    r.DrawText("VERSION",
               Layout::CenteredTextRect(r, "VERSION", sec,
                                        {ix, fb + 4.0f, iw, 14.0f}, 0.0f, false),
               sec);
}

// ---------------------------------------------------------------------------
// Tab 1: Full-Screen Studio Model Inspector
// ---------------------------------------------------------------------------

void HubBrowserWidget::DrawStudioInspectorTab(Rendering::Renderer& r,
                                              const Theme::Tokens& tokens) {
    if (selected_ < 0 || selected_ >= static_cast<int>(results_.size())) {
        const Rendering::TextStyle st{tokens.muted, 14.0f, 500};
        r.DrawText("Select a model in the 'Civitai Hub' tab first to open it in Model Inspector.",
                   Layout::CenteredTextRect(
                       r, "Select a model in the 'Civitai Hub' tab first to open it in Model Inspector.",
                       st, body_rect_, 24.0f, true),
                   st);
        return;
    }

    const auto& m = results_[static_cast<std::size_t>(selected_)];
    const auto* ver = SelectedVersion();

    // Left artwork stage
    r.FillRect(studio_stage_rect_, tokens.surface, tokens.radius_large);
    r.StrokeRect(studio_stage_rect_, tokens.border, 1.0f, tokens.radius_large);

    if (ver && !ver->images.empty()) {
        const int n = static_cast<int>(ver->images.size());
        const int idx = std::clamp(preview_image_idx_, 0, n - 1);
        const auto& cur = ver->images[static_cast<std::size_t>(idx)];
        std::string path = HubPreviewCache::Instance().GetOrRequest(cur.url, 1024, true);
        if (path.empty()) {
            path = HubPreviewCache::Instance().GetOrRequest(cur.url, 512, auto_previews_);
        }
        const Layout::Rect inner{studio_stage_rect_.x + 8.0f, studio_stage_rect_.y + 8.0f,
                                 studio_stage_rect_.width - 16.0f,
                                 studio_stage_rect_.height - 16.0f};
        const Layout::Rect fit =
            HubPreviewCache::ContainRect(inner, cur.width, cur.height);
        if (path.empty() || !r.DrawImage(path, fit, 1.0f, 8.0f)) {
            const Rendering::TextStyle ld{tokens.muted, 13.0f, 500};
            r.DrawText("Fetching high-resolution example artwork...",
                       Layout::CenteredTextRect(
                           r, "Fetching high-resolution example artwork...",
                           ld, studio_stage_rect_, 16.0f, true),
                       ld);
        }

        // Thumbnails
        for (std::size_t i = 0; i < studio_thumb_rects_.size(); ++i) {
            const auto& tr = studio_thumb_rects_[i];
            const bool sel = static_cast<int>(i) == idx;
            r.FillRect(tr, tokens.surface, 6.0f);
            const std::string tp = HubPreviewCache::Instance().GetOrRequest(
                ver->images[i].url, 512, auto_previews_);
            if (!tp.empty()) r.DrawImage(tp, tr, sel ? 1.0f : 0.7f, 6.0f);
            r.StrokeRect(tr, sel ? tokens.accent : tokens.border,
                         sel ? 2.0f : 1.0f, 6.0f);
        }

        // Prompt & Generation Metadata box below filmstrip
        const float meta_y = studio_stage_rect_.Bottom() + 92.0f;
        const Layout::Rect meta_box{
            studio_stage_rect_.x, meta_y, studio_stage_rect_.width,
            std::max(72.0f, body_rect_.Bottom() - 16.0f - meta_y)};
        r.FillRect(meta_box, tokens.surface, tokens.radius_medium);
        r.StrokeRect(meta_box, tokens.border, 1.0f, tokens.radius_medium);

        const Rendering::TextStyle hdr{tokens.accent, 10.5f, 700};
        const std::string hdr_txt = "GENERATION PROMPT  •  IMAGE " +
            std::to_string(idx + 1) + "/" + std::to_string(n) +
            (cur.width > 0 ? ("  (" + std::to_string(cur.width) + "×" +
                              std::to_string(cur.height) + ")") : "");
        r.DrawText(hdr_txt,
                   Layout::CenteredTextRect(
                       r, hdr_txt, hdr,
                       {meta_box.x + 12.0f, meta_box.y + 8.0f,
                        meta_box.width - 24.0f, 16.0f}, 0.0f, false),
                   hdr);

        std::string prompt = cur.prompt.empty()
            ? "(No prompt metadata embedded in this Civitai showcase image)"
            : cur.prompt;
        if (prompt.size() > 240) prompt = prompt.substr(0, 237) + "...";
        const Rendering::TextStyle pt{tokens.foreground, 11.5f, 450};
        r.DrawText(prompt,
                   {meta_box.x + 12.0f, meta_box.y + 28.0f,
                    meta_box.width - 24.0f, meta_box.height - 52.0f},
                   pt);

        std::ostringstream ps;
        if (!cur.sampler.empty()) ps << "Sampler: " << cur.sampler << "   ";
        if (cur.steps > 0) ps << "Steps: " << cur.steps << "   ";
        if (cur.cfg_scale > 0.0) {
            char cb[32];
            std::snprintf(cb, sizeof(cb), "CFG: %.1f   ", cur.cfg_scale);
            ps << cb;
        }
        if (!cur.seed.empty()) ps << "Seed: " << cur.seed;
        const Rendering::TextStyle ms{tokens.muted, 11.0f, 500};
        const std::string pstr = ps.str();
        r.DrawText(pstr,
                   Layout::CenteredTextRect(
                       r, pstr, ms,
                       {meta_box.x + 12.0f, meta_box.Bottom() - 22.0f,
                        meta_box.width - 24.0f, 16.0f}, 0.0f, false),
                   ms);
    }

    // Right Model & Package Inspector card
    const float pad = 16.0f;
    const float right_x = studio_stage_rect_.Right() + pad;
    const float right_w = std::max(260.0f, body_rect_.Right() - pad - right_x);
    const Layout::Rect rcard{right_x, body_rect_.y + pad, right_w,
                             body_rect_.height - 2.0f * pad};
    r.FillRect(rcard, tokens.surface, tokens.radius_large);
    r.StrokeRect(rcard, tokens.border, 1.0f, tokens.radius_large);

    const Rendering::TextStyle t_st{tokens.foreground, 16.0f, 700};
    r.DrawText(m.name,
               Layout::CenteredTextRect(
                   r, m.name, t_st,
                   {rcard.x + 14.0f, rcard.y + 14.0f, rcard.width - 28.0f, 22.0f},
                   0.0f, false),
               t_st);

    const Rendering::TextStyle sub_st{tokens.muted, 12.0f, 500};
    const std::string sub = m.type + "  •  @" +
        (m.creator.empty() ? "unknown" : m.creator) + "  •  ↓ " +
        FmtCount(m.download_count) + "  •  ♥ " + FmtCount(m.thumbs_up_count) +
        "  •  ID " + m.id;
    r.DrawText(sub,
               Layout::CenteredTextRect(
                   r, sub, sub_st,
                   {rcard.x + 14.0f, rcard.y + 42.0f, rcard.width - 28.0f, 18.0f},
                   0.0f, false),
               sub_st);

    const Rendering::TextStyle sec_st{tokens.muted, 10.5f, 700};
    r.DrawText("MODEL VERSION",
               Layout::CenteredTextRect(
                   r, "MODEL VERSION", sec_st,
                   {rcard.x + 14.0f, body_rect_.y + 146.0f, rcard.width - 28.0f, 16.0f},
                   0.0f, false),
               sec_st);
    r.DrawText("ATTACHED FILES & ARCHIVES",
               Layout::CenteredTextRect(
                   r, "ATTACHED FILES & ARCHIVES", sec_st,
                   {rcard.x + 14.0f, body_rect_.y + 210.0f, rcard.width - 28.0f, 16.0f},
                   0.0f, false),
               sec_st);

    if (ver) {
        for (std::size_t i = 0;
             i < studio_file_rects_.size() && i < ver->files.size(); ++i) {
            const auto& f = ver->files[i];
            const auto& fr = studio_file_rects_[i];
            const bool fsel = static_cast<int>(i) == selected_file_;
            r.FillRect(fr, fsel ? tokens.accent : tokens.background, 6.0f);
            if (!fsel) r.StrokeRect(fr, tokens.border, 1.0f, 6.0f);

            content::civitai::ModelFileDownloader::Options chk;
            chk.scope = "models";
            chk.key = "civitai/" + m.id + "/" + ver->id;
            chk.file_name = f.name;
            chk.expected_size = f.size_bytes;
            chk.verify_checksum = false;
            const bool cached = content::civitai::ModelFileDownloader::IsCached(chk);

            const std::string tag = cached
                ? "CACHED"
                : (f.format.empty() ? f.type : f.format);
            const Rendering::TextStyle ts{tokens.background, 9.5f, 700};
            const float tw = r.MeasureText(tag, ts).width + 10.0f;
            DrawBadge(r, tokens, tag, {fr.x + 6.0f, fr.y + 5.0f, tw, 18.0f},
                      cached ? Theme::Color{0.28f, 0.85f, 0.50f, 1.0f}
                             : TypeColor(m.type),
                      tokens.background);

            const Rendering::TextStyle fn{
                fsel ? tokens.accent_foreground : tokens.foreground, 11.5f, 500};
            r.DrawText(f.name,
                       Layout::CenteredTextRect(
                           r, f.name, fn,
                           {fr.x + tw + 12.0f, fr.y, fr.width - tw - 86.0f, fr.height},
                           0.0f, false),
                       fn);

            const std::string sz = FmtBytes(f.size_bytes);
            const Rendering::TextStyle ss{
                fsel ? tokens.accent_foreground : tokens.muted, 10.5f, 600};
            const float szw = r.MeasureText(sz, ss).width;
            r.DrawText(sz,
                       Layout::CenteredTextRect(
                           r, sz, ss,
                           {fr.Right() - szw - 8.0f, fr.y, szw + 4.0f, fr.height},
                           0.0f, false),
                       ss);
        }
    }
}

// ---------------------------------------------------------------------------
// Tab 2: Downloads & ArchiveManager ZIP Viewer
// ---------------------------------------------------------------------------

void HubBrowserWidget::DrawDownloadsTab(Rendering::Renderer& r,
                                        const Theme::Tokens& tokens) {
    const float pad = 16.0f;
    // Top 4 telemetry cards
    int active_cnt = 0;
    int done_cnt = 0;
    for (const auto& j : download_jobs_) {
        if (j->active.load()) ++active_cnt;
        else if (j->finished.load() && j->ok.load()) ++done_cnt;
    }
    const auto workers =
        XER::AsyncIO::IO::Async::IOThreadPool::GetSharedInstance().GetThreadCount();

    const auto stat_cards = Layout::DistributeRow(
        {body_rect_.x + pad, body_rect_.y + 14.0f,
         body_rect_.width - 2.0f * pad, 64.0f},
        {200.0f, 200.0f, 200.0f, 200.0f},
        {1.0f, 1.0f, 1.0f, 1.0f}, 12.0f, Layout::FlexAlign::Start);
    const char* titles[4] = {
        "ACTIVE DOWNLOADS", "COMPLETED SESSION",
        "CACHED MODELS (DISK)", "ASYNC I/O POOL"};
    const std::string vals[4] = {
        std::to_string(active_cnt),
        std::to_string(done_cnt),
        std::to_string(cache_summary_.model_count) + "  (" +
            FmtBytes(cache_summary_.model_bytes) + ")",
        std::to_string(workers) + " workers"};
    for (std::size_t i = 0; i < stat_cards.size(); ++i) {
        const auto& sc = stat_cards[i];
        r.FillRect(sc, tokens.surface, tokens.radius_medium);
        r.StrokeRect(sc, tokens.border, 1.0f, tokens.radius_medium);
        const Rendering::TextStyle k_st{tokens.muted, 10.0f, 700};
        const Rendering::TextStyle v_st{tokens.foreground, 15.0f, 700};
        r.DrawText(titles[i],
                   Layout::CenteredTextRect(
                       r, titles[i], k_st,
                       {sc.x + 14.0f, sc.y + 10.0f, sc.width - 28.0f, 14.0f},
                       0.0f, false),
                   k_st);
        r.DrawText(vals[i],
                   Layout::CenteredTextRect(
                       r, vals[i], v_st,
                       {sc.x + 14.0f, sc.y + 28.0f, sc.width - 28.0f, 24.0f},
                       0.0f, false),
                   v_st);
    }

    // Left pane: Downloads & Cached Packages
    r.FillRect(dl_left_list_rect_, tokens.surface, tokens.radius_large);
    r.StrokeRect(dl_left_list_rect_, tokens.border, 1.0f, tokens.radius_large);

    const Rendering::TextStyle hdr_st{tokens.muted, 10.5f, 700};
    r.DrawText("DOWNLOAD JOBS & CACHED PACKAGES",
               Layout::CenteredTextRect(
                   r, "DOWNLOAD JOBS & CACHED PACKAGES", hdr_st,
                   {dl_left_list_rect_.x + 12.0f, dl_left_list_rect_.y + 8.0f,
                    dl_left_list_rect_.width - 24.0f, 18.0f}, 0.0f, false),
               hdr_st);

    if (dl_row_rects_.empty()) {
        const Rendering::TextStyle em{tokens.muted, 12.5f, 450};
        r.DrawText("No downloads yet. Download a model from Civitai Hub.",
                   Layout::CenteredTextRect(
                       r, "No downloads yet. Download a model from Civitai Hub.",
                       em, dl_left_list_rect_, 16.0f, true),
                   em);
    } else {
        r.PushClip({dl_left_list_rect_.x + 2.0f, dl_left_list_rect_.y + 28.0f,
                    dl_left_list_rect_.width - 4.0f,
                    dl_left_list_rect_.height - 30.0f});
        const int n_jobs = static_cast<int>(download_jobs_.size());
        for (std::size_t i = 0; i < dl_row_rects_.size(); ++i) {
            const auto& rr = dl_row_rects_[i];
            if (rr.Bottom() < dl_left_list_rect_.y + 28.0f ||
                rr.y > dl_left_list_rect_.Bottom()) {
                continue;
            }
            const bool sel = static_cast<int>(i) == selected_dl_index_;
            r.FillRect(rr, sel ? tokens.accent : tokens.background, 8.0f);
            if (!sel) r.StrokeRect(rr, tokens.border, 1.0f, 8.0f);

            std::string title;
            std::string sub;
            float frac = 1.0f;
            bool is_zip = false;
            if (static_cast<int>(i) < n_jobs) {
                const auto& j = download_jobs_[i];
                title = j->file_name;
                const uint64_t d = j->done_bytes.load();
                const uint64_t t = j->total_bytes.load();
                frac = (j->finished.load() && j->ok.load())
                    ? 1.0f
                    : (t > 0 ? static_cast<float>(static_cast<double>(d) / t) : 0.0f);
                std::lock_guard<std::mutex> lock(j->mu);
                is_zip = j->is_valid_archive ||
                         HubPreviewCache::HasArchiveExtension(j->file_name);
                sub = j->model_name + "  •  " + j->status_text + "  •  " +
                      FmtBytes(t > 0 ? t : d);
            } else {
                const auto& df = cached_disk_files_[i - static_cast<std::size_t>(n_jobs)];
                title = df.file_name;
                is_zip = df.is_archive;
                sub = (df.relative_key.empty() ? "Cache" : df.relative_key) +
                      "  •  Cached on disk  •  " + FmtBytes(df.size_bytes);
            }

            if (is_zip) {
                DrawBadge(r, tokens, "ZIP",
                          {rr.Right() - 44.0f, rr.y + 6.0f, 36.0f, 16.0f},
                          {0.98f, 0.60f, 0.24f, 1.0f}, tokens.background);
            }

            const Rendering::TextStyle ts{
                sel ? tokens.accent_foreground : tokens.foreground, 12.0f, 600};
            r.DrawText(title,
                       Layout::CenteredTextRect(
                           r, title, ts,
                           {rr.x + 10.0f, rr.y + 5.0f, rr.width - 58.0f, 18.0f},
                           0.0f, false),
                       ts);

            const Rendering::TextStyle ss{
                sel ? tokens.accent_foreground : tokens.muted, 10.5f, 450};
            r.DrawText(sub,
                       Layout::CenteredTextRect(
                           r, sub, ss,
                           {rr.x + 10.0f, rr.y + 24.0f, rr.width - 20.0f, 15.0f},
                           0.0f, false),
                       ss);

            if (frac < 1.0f) {
                const Layout::Rect pb{rr.x + 10.0f, rr.Bottom() - 6.0f,
                                      rr.width - 20.0f, 3.0f};
                r.FillRect(pb, tokens.secondary, 1.5f);
                r.FillRect({pb.x, pb.y, pb.width * std::clamp(frac, 0.0f, 1.0f), pb.height},
                           sel ? tokens.accent_foreground : tokens.accent, 1.5f);
            }
        }
        r.PopClip();
    }

    // Right pane: ArchiveManager ZIP / Package Inspector
    r.FillRect(dl_right_arch_rect_, tokens.surface, tokens.radius_large);
    r.StrokeRect(dl_right_arch_rect_, tokens.border, 1.0f, tokens.radius_large);

    r.DrawText("PACKAGE & ZIP CONTENTS (XER::Content::Archive::ArchiveManager)",
               Layout::CenteredTextRect(
                   r, "PACKAGE & ZIP CONTENTS (XER::Content::Archive::ArchiveManager)",
                   hdr_st,
                   {dl_right_arch_rect_.x + 14.0f, dl_right_arch_rect_.y + 8.0f,
                    dl_right_arch_rect_.width - 28.0f, 18.0f}, 0.0f, false),
               hdr_st);

    const int n_jobs = static_cast<int>(download_jobs_.size());
    std::shared_ptr<DownloadJob> cur_job;
    if (selected_dl_index_ >= 0 && selected_dl_index_ < n_jobs) {
        cur_job = download_jobs_[static_cast<std::size_t>(selected_dl_index_)];
    } else if (selected_dl_index_ >= n_jobs) {
        cur_job = inspected_disk_job_;
    } else if (!download_jobs_.empty()) {
        cur_job = download_jobs_.front();
    }

    if (!cur_job) {
        const Rendering::TextStyle em{tokens.muted, 12.5f, 450};
        r.DrawText("Select a downloaded or cached file on the left to inspect its ZIP contents.",
                   Layout::CenteredTextRect(
                       r, "Select a downloaded or cached file on the left to inspect its ZIP contents.",
                       em, dl_right_arch_rect_, 20.0f, true),
                   em);
        return;
    }

    std::string summary;
    std::string path_str;
    std::vector<XER::Content::Archive::ArchiveEntryInfo> entries;
    {
        std::lock_guard<std::mutex> lock(cur_job->mu);
        summary = cur_job->archive_summary;
        path_str = cur_job->final_path.empty() ? cur_job->target_path : cur_job->final_path;
        entries = cur_job->archive_entries;
    }

    const Rendering::TextStyle sum_st{tokens.accent, 11.5f, 600};
    r.DrawText(summary.empty() ? cur_job->file_name : summary,
               Layout::CenteredTextRect(
                   r, summary.empty() ? cur_job->file_name : summary, sum_st,
                   {dl_right_arch_rect_.x + 14.0f, dl_right_arch_rect_.y + 30.0f,
                    dl_right_arch_rect_.width - 28.0f, 18.0f}, 0.0f, false),
               sum_st);

    const Rendering::TextStyle p_st{tokens.muted, 10.5f, 450};
    r.DrawText(path_str,
               Layout::CenteredTextRect(
                   r, path_str, p_st,
                   {dl_right_arch_rect_.x + 14.0f, dl_right_arch_rect_.y + 50.0f,
                    dl_right_arch_rect_.width - 28.0f, 16.0f}, 0.0f, false),
               p_st);

    const Layout::Rect table_r{
        dl_right_arch_rect_.x + 12.0f, dl_right_arch_rect_.y + 74.0f,
        dl_right_arch_rect_.width - 24.0f,
        std::max(40.0f, dl_right_arch_rect_.height - 86.0f)};
    r.FillRect(table_r, tokens.background, tokens.radius_medium);
    r.StrokeRect(table_r, tokens.border, 1.0f, tokens.radius_medium);

    if (entries.empty()) {
        const std::string msg = summary.empty()
            ? "Click 'Inspect ZIP' above to list archive entries."
            : summary;
        const Rendering::TextStyle em{tokens.muted, 12.0f, 450};
        r.DrawText(msg, Layout::CenteredTextRect(r, msg, em, table_r, 16.0f, true),
                   em);
        return;
    }

    r.PushClip({table_r.x + 2.0f, table_r.y + 2.0f,
                table_r.width - 4.0f, table_r.height - 4.0f});
    constexpr float kEh = 26.0f;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const float ey = table_r.y + 4.0f + static_cast<float>(i) * kEh - dl_arch_scroll_;
        if (ey + kEh < table_r.y || ey > table_r.Bottom()) continue;
        if (i % 2 == 1) {
            r.FillRect({table_r.x + 4.0f, ey, table_r.width - 8.0f, kEh},
                       tokens.surface, 4.0f);
        }
        const auto& e = entries[i];
        DrawBadge(r, tokens, e.is_dir ? "DIR" : "FILE",
                  {table_r.x + 8.0f, ey + 5.0f, 38.0f, 16.0f},
                  e.is_dir ? tokens.secondary : tokens.accent,
                  e.is_dir ? tokens.muted : tokens.accent_foreground);

        const Rendering::TextStyle en{tokens.foreground, 11.5f, 450};
        r.DrawText(e.path,
                   Layout::CenteredTextRect(
                       r, e.path, en,
                       {table_r.x + 54.0f, ey, table_r.width - 140.0f, kEh},
                       0.0f, false),
                   en);
        if (!e.is_dir) {
            const std::string sz = FmtBytes(e.size > 0 ? static_cast<uint64_t>(e.size) : 0);
            const Rendering::TextStyle es{tokens.muted, 10.5f, 500};
            const float szw = r.MeasureText(sz, es).width;
            r.DrawText(sz,
                       Layout::CenteredTextRect(
                           r, sz, es,
                           {table_r.Right() - szw - 10.0f, ey, szw + 4.0f, kEh},
                           0.0f, false),
                       es);
        }
    }
    r.PopClip();
}

// ---------------------------------------------------------------------------
// Tab 3: Settings (persisted via XER::Cache::CacheManager)
// ---------------------------------------------------------------------------

void HubBrowserWidget::DrawSettingsTab(Rendering::Renderer& r,
                                       const Theme::Tokens& tokens) {
    const float pad = 20.0f;
    const float col_w = std::max(300.0f, (body_rect_.width - pad * 3.0f) * 0.5f);
    const float c1x = body_rect_.x + pad;
    const float c2x = c1x + col_w + pad;
    const float top_y = body_rect_.y + pad;

    const Rendering::TextStyle card_hdr{tokens.foreground, 14.0f, 700};
    const Rendering::TextStyle card_sub{tokens.muted, 11.5f, 450};
    const Rendering::TextStyle row_lbl{tokens.foreground, 12.5f, 500};

    // Card 1: Civitai API & Network
    const Layout::Rect c1{c1x, top_y, col_w, 186.0f};
    r.FillRect(c1, tokens.surface, tokens.radius_large);
    r.StrokeRect(c1, tokens.border, 1.0f, tokens.radius_large);
    r.DrawText("Civitai API & Network (XER_Network)",
               Layout::CenteredTextRect(
                   r, "Civitai API & Network (XER_Network)", card_hdr,
                   {c1.x + 16.0f, c1.y + 14.0f, col_w - 32.0f, 20.0f}, 0.0f, false),
               card_hdr);
    r.DrawText("Optional personal API key unlocks higher rate limits & gated downloads.",
               Layout::CenteredTextRect(
                   r, "Optional personal API key unlocks higher rate limits & gated downloads.",
                   card_sub, {c1.x + 16.0f, c1.y + 36.0f, col_w - 32.0f, 16.0f},
                   0.0f, false),
               card_sub);
    r.DrawText("Verify TLS Certificates (OpenSSL)",
               Layout::CenteredTextRect(
                   r, "Verify TLS Certificates (OpenSSL)", row_lbl,
                   {c1.x + 16.0f, c1.y + 118.0f, col_w - 90.0f, 28.0f}, 0.0f, false),
               row_lbl);

    // Card 2: Appearance
    const Layout::Rect c2{c1x, top_y + 206.0f, col_w, 108.0f};
    r.FillRect(c2, tokens.surface, tokens.radius_large);
    r.StrokeRect(c2, tokens.border, 1.0f, tokens.radius_large);
    r.DrawText("Appearance & Studio Theme",
               Layout::CenteredTextRect(
                   r, "Appearance & Studio Theme", card_hdr,
                   {c2.x + 16.0f, c2.y + 14.0f, col_w - 32.0f, 20.0f}, 0.0f, false),
               card_hdr);
    r.DrawText("Color Theme",
               Layout::CenteredTextRect(
                   r, "Color Theme", row_lbl,
                   {c2.x + 16.0f, c2.y + 52.0f, col_w - 220.0f, 32.0f}, 0.0f, false),
               row_lbl);

    // Card 3: Browsing & Image Previews
    const Layout::Rect c3{c2x, top_y, col_w, 186.0f};
    r.FillRect(c3, tokens.surface, tokens.radius_large);
    r.StrokeRect(c3, tokens.border, 1.0f, tokens.radius_large);
    r.DrawText("Browsing & Artwork Previews",
               Layout::CenteredTextRect(
                   r, "Browsing & Artwork Previews", card_hdr,
                   {c3.x + 16.0f, c3.y + 14.0f, col_w - 32.0f, 20.0f}, 0.0f, false),
               card_hdr);
    r.DrawText("Results Per Page",
               Layout::CenteredTextRect(
                   r, "Results Per Page", row_lbl,
                   {c3.x + 16.0f, c3.y + 46.0f, col_w - 220.0f, 30.0f}, 0.0f, false),
               row_lbl);
    r.DrawText("Auto-download Generated Example Artworks",
               Layout::CenteredTextRect(
                   r, "Auto-download Generated Example Artworks", row_lbl,
                   {c3.x + 16.0f, c3.y + 94.0f, col_w - 90.0f, 28.0f}, 0.0f, false),
               row_lbl);
    r.DrawText("Include Mature / NSFW Models",
               Layout::CenteredTextRect(
                   r, "Include Mature / NSFW Models", row_lbl,
                   {c3.x + 16.0f, c3.y + 136.0f, col_w - 90.0f, 28.0f}, 0.0f, false),
               row_lbl);

    // Card 4: Storage & CacheManager
    const Layout::Rect c4{c2x, top_y + 206.0f, col_w, 182.0f};
    r.FillRect(c4, tokens.surface, tokens.radius_large);
    r.StrokeRect(c4, tokens.border, 1.0f, tokens.radius_large);
    r.DrawText("Storage & Cache (XER::Cache::CacheManager)",
               Layout::CenteredTextRect(
                   r, "Storage & Cache (XER::Cache::CacheManager)", card_hdr,
                   {c4.x + 16.0f, c4.y + 14.0f, col_w - 32.0f, 20.0f}, 0.0f, false),
               card_hdr);

    const std::string root_line = "Root: " +
        (cache_summary_.root_dir.empty()
             ? XER::Cache::CacheManager::Instance().RootDirectory().string()
             : cache_summary_.root_dir);
    r.DrawText(root_line,
               Layout::CenteredTextRect(
                   r, root_line, card_sub,
                   {c4.x + 16.0f, c4.y + 44.0f, col_w - 32.0f, 18.0f}, 0.0f, false),
               card_sub);

    const std::string prev_stat = "Artwork Preview Cache: " +
        std::to_string(cache_summary_.preview_count) + " file(s)  •  " +
        FmtBytes(cache_summary_.preview_bytes);
    r.DrawText(prev_stat,
               Layout::CenteredTextRect(
                   r, prev_stat, row_lbl,
                   {c4.x + 16.0f, c4.y + 70.0f, col_w - 32.0f, 20.0f}, 0.0f, false),
               row_lbl);

    const std::string mod_stat = "Model Weights & ZIP Cache: " +
        std::to_string(cache_summary_.model_count) + " file(s)  •  " +
        FmtBytes(cache_summary_.model_bytes);
    r.DrawText(mod_stat,
               Layout::CenteredTextRect(
                   r, mod_stat, row_lbl,
                   {c4.x + 16.0f, c4.y + 96.0f, col_w - 32.0f, 20.0f}, 0.0f, false),
               row_lbl);
}

void HubBrowserWidget::DrawStatusBar(Rendering::Renderer& r,
                                     const Theme::Tokens& tokens) {
    const auto sr = statusbar_rect_;
    r.FillRect(sr, tokens.surface, 0.0f);

    const Theme::Color dot = searching_
        ? tokens.accent
        : (results_.empty() ? tokens.muted
                            : Theme::Color{0.30f, 0.85f, 0.45f, 1.0f});
    r.FillRect({sr.x + 10.0f, sr.y + sr.height * 0.5f - 4.0f, 8.0f, 8.0f},
               dot, 4.0f);

    const Rendering::TextStyle api_st{tokens.muted, 11.0f, 500};
    r.DrawText("Civitai API v1",
               Layout::CenteredTextRect(r, "Civitai API v1", api_st,
                                        {sr.x + 24.0f, sr.y + 4.0f, 108.0f, 20.0f},
                                        0.0f, false),
               api_st);

    const Rendering::TextStyle st{tokens.foreground, 11.5f, 450};
    const float right_reserve = (active_tab_ == WorkspaceTab::Hub) ? 390.0f : 160.0f;
    r.DrawText(status_,
               Layout::CenteredTextRect(
                   r, status_, st,
                   {sr.x + 136.0f, sr.y + 4.0f,
                    std::max(80.0f, sr.width - right_reserve), 20.0f},
                   0.0f, false),
               st);

    if (active_tab_ == WorkspaceTab::Hub && !results_.empty()) {
        const Rendering::TextStyle pg{tokens.muted, 11.0f, 500};
        const std::string page_txt = "Page " +
            std::to_string(query_.empty() ? page_ : (cursor_pos_ + 1));
        r.DrawText(page_txt,
                   Layout::CenteredTextRect(
                       r, page_txt, pg,
                       {prev_rect_.x - 76.0f, sr.y + 4.0f, 72.0f, 20.0f},
                       0.0f, false),
                   pg);
    }
}

void HubBrowserWidget::DrawChevron(Rendering::Renderer& r,
                                   const Theme::Tokens&,
                                   Layout::Rect box, Theme::Color color) {
    const float cx = box.x + box.width * 0.5f;
    const float cy = box.y + box.height * 0.5f;
    const float s = std::min(box.width, box.height) * 0.3f;
    r.DrawLine({cx - s * 0.6f, cy - s}, {cx + s * 0.4f, cy}, color, 1.6f);
    r.DrawLine({cx + s * 0.4f, cy}, {cx - s * 0.6f, cy + s}, color, 1.6f);
}

Layout::Size HubBrowserWidget::GetMinSize() const {
    return {820.0f, 560.0f};
}
Layout::Size HubBrowserWidget::GetPreferredSize() const {
    return {1320.0f, 840.0f};
}
Layout::Size HubBrowserWidget::GetMaxSize() const {
    return {4096.0f, 4096.0f};
}

}  // namespace XER::Tools::HubBrowser
