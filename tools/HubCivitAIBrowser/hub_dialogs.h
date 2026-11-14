#pragma once

#include "content/archive/archive_manager.h"
#include "content/civitaimodels/civitai_model_downloader.h"
#include "ui/widgets/dialog.h"
#include "ui/widgets/widget.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace XER::Tools::HubBrowser {

// Shared state for an active or completed model/archive download, observed by
// both DownloadModelDialogContent and the Downloads workspace tab.
struct DownloadJob {
    std::string model_id;
    std::string model_name;
    std::string version_id;
    std::string version_name;
    std::string file_name;
    std::string file_format;
    std::string file_type;
    std::string sha256;
    std::string target_path;

    std::atomic<uint64_t> done_bytes{0};
    std::atomic<uint64_t> total_bytes{0};
    std::atomic<double> speed_bps{0.0};
    std::atomic<bool> active{false};
    std::atomic<bool> finished{false};
    std::atomic<bool> ok{false};
    std::atomic<bool> dirty{false};

    std::chrono::steady_clock::time_point started_at =
        std::chrono::steady_clock::now();

    mutable std::mutex mu;
    std::string status_text = "Queued...";
    std::string error_text;
    std::string final_path;

    // Populated via XER::Content::Archive::ArchiveManager::Probe + List.
    bool archive_probed = false;
    bool is_valid_archive = false;
    XER::Content::Archive::ArchiveInfo archive_info;
    std::vector<XER::Content::Archive::ArchiveEntryInfo> archive_entries;
    uint64_t archive_unpacked_bytes = 0;
    std::string archive_summary;
};

// Probes `file_path` with XER::Content::Archive::ArchiveManager (synchronously;
// safe to call on IOThreadPool) and populates `job`'s archive fields.
void InspectJobArchiveSync(DownloadJob& job, const std::string& file_path);

// Schedules async archive inspection on IOThreadPool.
void InspectJobArchiveAsync(const std::shared_ptr<DownloadJob>& job,
                            const std::string& file_path);

// Schedules async archive extraction into `<archive_stem>_extracted/` next to
// the archive file on IOThreadPool.
void ExtractJobArchiveAsync(const std::shared_ptr<DownloadJob>& job);

// Formats a human-readable byte size (e.g. "6.5 GB", "218 MB", "42 KB").
std::string FormatByteSize(uint64_t bytes);

// Builds ready-to-run `sd.exe generate.json` text from an example image's meta.
std::string BuildSdGenerateJson(
    const std::string& model_name,
    const std::string& primary_file_name,
    const content::civitai::CivitaiModelImage& img);

// Modal dialog content widget showing live model download progress +
// ArchiveManager ZIP/archive contents inspection & extraction.
class DownloadModelDialogContent final : public XER::UI::Widgets::Widget {
public:
    explicit DownloadModelDialogContent(
        std::shared_ptr<DownloadJob> job,
        std::function<void(const std::string&)> on_status = {});

    void Render(XER::UI::Rendering::Renderer& r,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    void Layout(const XER::UI::Layout::Rect& bounds,
                const XER::UI::Theme::Tokens& tokens) override;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override;
    XER::UI::Layout::Size GetPreferredSize() const override;
    XER::UI::Layout::Size GetMaxSize() const override;

    static std::shared_ptr<XER::UI::Widgets::Dialog> CreateDialog(
        std::shared_ptr<DownloadJob> job,
        std::function<void(const std::string&)> on_status = {});

private:
    std::shared_ptr<DownloadJob> job_;
    std::function<void(const std::string&)> on_status_;
    std::shared_ptr<XER::UI::Widgets::Widget> progress_bar_;
    std::shared_ptr<XER::UI::Widgets::Widget> copy_path_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> inspect_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> extract_btn_;
    XER::UI::Layout::Rect archive_list_rect_{};
    float scroll_y_ = 0.0f;
};

// Modal lightbox dialog content widget displaying generated example artworks
// for a Civitai model version with aspect-preserving rendering, thumbnail
// strip, prompt/sampler/steps/CFG/seed readout, and Copy Prompt / sd.exe JSON.
class ImagePreviewDialogContent final : public XER::UI::Widgets::Widget {
public:
    ImagePreviewDialogContent(
        std::string model_name,
        std::string primary_file_name,
        std::vector<content::civitai::CivitaiModelImage> images,
        int initial_index = 0,
        std::function<void(const std::string&)> on_status = {});

    void Render(XER::UI::Rendering::Renderer& r,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    void Layout(const XER::UI::Layout::Rect& bounds,
                const XER::UI::Theme::Tokens& tokens) override;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override;
    XER::UI::Layout::Size GetPreferredSize() const override;
    XER::UI::Layout::Size GetMaxSize() const override;

    static std::shared_ptr<XER::UI::Widgets::Dialog> CreateDialog(
        std::string model_name,
        std::string primary_file_name,
        std::vector<content::civitai::CivitaiModelImage> images,
        int initial_index = 0,
        std::function<void(const std::string&)> on_status = {});

private:
    void StepImage(int delta);

    std::string model_name_;
    std::string primary_file_name_;
    std::vector<content::civitai::CivitaiModelImage> images_;
    int index_ = 0;
    std::function<void(const std::string&)> on_status_;

    std::shared_ptr<XER::UI::Widgets::Widget> prev_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> next_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> copy_prompt_btn_;
    std::shared_ptr<XER::UI::Widgets::Widget> copy_json_btn_;

    XER::UI::Layout::Rect stage_rect_{};
    XER::UI::Layout::Rect strip_rect_{};
    XER::UI::Layout::Rect meta_rect_{};
    std::vector<XER::UI::Layout::Rect> thumb_rects_;
};

}  // namespace XER::Tools::HubBrowser
