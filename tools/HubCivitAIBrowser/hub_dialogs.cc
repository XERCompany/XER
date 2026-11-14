#include "hub_dialogs.h"

#include "hub_preview_cache.h"

#include "async_io/io_thread_pool.h"
#include "content/clipboard/clipboard.h"
#include "ui/layout/flex.h"
#include "ui/rendering/renderer.h"
#include "ui/widgets/button.h"
#include "ui/widgets/progress.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <json/json.h>
#include <sstream>
#include <system_error>

namespace XER::Tools::HubBrowser {

using namespace XER::UI;
using XER::UI::Widgets::Button;
using XER::UI::Widgets::ButtonStyle;
using XER::UI::Widgets::Dialog;
using XER::UI::Widgets::Event;
using XER::UI::Widgets::MouseButton;
using XER::UI::Widgets::Progress;
using XER::UI::Widgets::Widget;

std::string FormatByteSize(uint64_t b) {
    char buf[40];
    if (b >= (1ULL << 30)) {
        std::snprintf(buf, sizeof(buf), "%.2f GB", b / 1073741824.0);
        return buf;
    }
    if (b >= (1ULL << 20)) {
        std::snprintf(buf, sizeof(buf), "%.1f MB", b / 1048576.0);
        return buf;
    }
    if (b >= (1ULL << 10)) {
        std::snprintf(buf, sizeof(buf), "%.1f KB", b / 1024.0);
        return buf;
    }
    return std::to_string(b) + " B";
}

std::string BuildSdGenerateJson(
    const std::string& model_name,
    const std::string& primary_file_name,
    const content::civitai::CivitaiModelImage& img) {
    Json::Value root(Json::objectValue);
    root["model_name"] = model_name;
    if (!primary_file_name.empty()) {
        root["model_path"] = primary_file_name;
    }
    root["prompt"] = img.prompt.empty()
        ? ("masterpiece, best quality, " + model_name)
        : img.prompt;
    root["negative_prompt"] = img.negative_prompt;
    root["width"] = img.width > 0 ? img.width : 1024;
    root["height"] = img.height > 0 ? img.height : 1024;
    root["steps"] = img.steps > 0 ? img.steps : 28;
    root["guidance_scale"] = img.cfg_scale > 0.0 ? img.cfg_scale : 7.0;
    if (!img.sampler.empty()) {
        root["scheduler"] = img.sampler;
    }
    if (!img.seed.empty()) {
        try {
            root["seed"] = static_cast<Json::Int64>(std::stoll(img.seed));
        } catch (...) {
            root["seed"] = 42;
        }
    }
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "  ";
    return Json::writeString(wb, root);
}

void InspectJobArchiveSync(DownloadJob& job, const std::string& file_path) {
    std::error_code ec;
    if (file_path.empty() || !std::filesystem::exists(file_path, ec)) {
        std::lock_guard<std::mutex> lock(job.mu);
        job.archive_probed = false;
        job.is_valid_archive = false;
        job.archive_summary =
            "Archive inspection will run automatically once the file is on disk.";
        job.dirty.store(true);
        return;
    }

    XER::Content::Archive::ArchiveInfo info{};
    std::vector<XER::Content::Archive::ArchiveEntryInfo> entries;
    std::string probe_err;
    std::string list_err;

    const bool ok_probe = XER::Content::Archive::ArchiveManager::Probe(
        file_path, &info, &probe_err);
    const bool ok_list = ok_probe &&
        XER::Content::Archive::ArchiveManager::List(
            file_path, &entries, &list_err);

    uint64_t unpacked = 0;
    std::size_t file_cnt = 0;
    std::size_t dir_cnt = 0;
    if (ok_list) {
        for (const auto& e : entries) {
            if (e.is_dir) {
                ++dir_cnt;
            } else {
                ++file_cnt;
                if (e.size > 0) unpacked += static_cast<uint64_t>(e.size);
            }
        }
    }

    std::lock_guard<std::mutex> lock(job.mu);
    job.archive_probed = true;
    if (ok_list && !entries.empty()) {
        job.is_valid_archive = true;
        job.archive_info = std::move(info);
        job.archive_entries = std::move(entries);
        job.archive_unpacked_bytes = unpacked;
        std::ostringstream ss;
        ss << "Format: "
           << (job.archive_info.format.empty() ? "Archive" : job.archive_info.format);
        if (!job.archive_info.compression.empty() &&
            job.archive_info.compression != "none") {
            ss << " (" << job.archive_info.compression << ")";
        }
        ss << "  •  " << file_cnt << " file(s)";
        if (dir_cnt > 0) ss << ", " << dir_cnt << " folder(s)";
        ss << "  •  " << FormatByteSize(unpacked) << " unpacked";
        if (job.archive_info.encrypted) ss << "  •  Encrypted";
        job.archive_summary = ss.str();
    } else {
        job.is_valid_archive = false;
        job.archive_entries.clear();
        job.archive_unpacked_bytes = 0;
        const std::string fmt = job.file_format.empty() ? "Binary" : job.file_format;
        if (HubPreviewCache::HasArchiveExtension(file_path)) {
            job.archive_summary = "Archive read error: " +
                (probe_err.empty() ? list_err : probe_err);
        } else {
            job.archive_summary = "Direct model weight file (" + fmt +
                ", " + FormatByteSize(
                    static_cast<uint64_t>(std::filesystem::file_size(file_path, ec))) +
                ") — not a ZIP/archive container.";
        }
    }
    job.dirty.store(true);
}

void InspectJobArchiveAsync(const std::shared_ptr<DownloadJob>& job,
                            const std::string& file_path) {
    if (!job) return;
    {
        std::lock_guard<std::mutex> lock(job->mu);
        job->archive_summary = "Inspecting package with ArchiveManager...";
        job->dirty.store(true);
    }
    XER::AsyncIO::IO::Async::IOThreadPool::GetSharedInstance().Enqueue(
        [job, file_path]() { InspectJobArchiveSync(*job, file_path); });
}

void ExtractJobArchiveAsync(const std::shared_ptr<DownloadJob>& job) {
    if (!job) return;
    std::string archive_path;
    {
        std::lock_guard<std::mutex> lock(job->mu);
        archive_path = job->final_path.empty() ? job->target_path : job->final_path;
        if (!job->is_valid_archive || archive_path.empty()) return;
        job->archive_summary = "Extracting archive contents...";
        job->dirty.store(true);
    }

    XER::AsyncIO::IO::Async::IOThreadPool::GetSharedInstance().Enqueue(
        [job, archive_path]() {
            const std::filesystem::path src(archive_path);
            const std::filesystem::path dest =
                src.parent_path() / (src.stem().string() + "_extracted");
            std::error_code ec;
            std::filesystem::create_directories(dest, ec);

            int64_t extracted = 0;
            std::string err;
            const bool ok = XER::Content::Archive::ArchiveManager::Extract(
                archive_path, dest.string(), {}, &extracted, &err);

            std::lock_guard<std::mutex> lock(job->mu);
            if (ok) {
                job->archive_summary = "Extracted " + std::to_string(extracted) +
                    " entries -> " + dest.filename().string();
            } else {
                job->archive_summary = "Extraction failed: " + err;
            }
            job->dirty.store(true);
        });
}

// ===========================================================================
// DownloadModelDialogContent
// ===========================================================================

DownloadModelDialogContent::DownloadModelDialogContent(
    std::shared_ptr<DownloadJob> job,
    std::function<void(const std::string&)> on_status)
    : job_(std::move(job))
    , on_status_(std::move(on_status)) {
    progress_bar_ = std::make_shared<Progress>(0.0f);
    copy_path_btn_ = std::make_shared<Button>("Copy Path", ButtonStyle::Secondary);
    inspect_btn_ = std::make_shared<Button>("Inspect ZIP", ButtonStyle::Secondary);
    extract_btn_ = std::make_shared<Button>("Extract ZIP", ButtonStyle::Primary);

    static_cast<Button*>(copy_path_btn_.get())->SetOnClick([this]() {
        if (!job_) return;
        std::string p;
        {
            std::lock_guard<std::mutex> lock(job_->mu);
            p = job_->final_path.empty() ? job_->target_path : job_->final_path;
        }
        XER::Content::Clipboard::SetText(p);
        if (on_status_) on_status_("Copied file path: " + p);
    });

    static_cast<Button*>(inspect_btn_.get())->SetOnClick([this]() {
        if (!job_) return;
        std::string p;
        {
            std::lock_guard<std::mutex> lock(job_->mu);
            p = job_->final_path.empty() ? job_->target_path : job_->final_path;
        }
        InspectJobArchiveAsync(job_, p);
    });

    static_cast<Button*>(extract_btn_.get())->SetOnClick([this]() {
        if (!job_) return;
        ExtractJobArchiveAsync(job_);
    });

    AddChild(progress_bar_);
    AddChild(copy_path_btn_);
    AddChild(inspect_btn_);
    AddChild(extract_btn_);
}

std::shared_ptr<Dialog> DownloadModelDialogContent::CreateDialog(
    std::shared_ptr<DownloadJob> job,
    std::function<void(const std::string&)> on_status) {
    const std::string title = job
        ? ("Download Model — " + job->model_name)
        : std::string("Download Model");
    auto content = std::make_shared<DownloadModelDialogContent>(
        std::move(job), std::move(on_status));
    auto dlg = Dialog::Create(title, content, "Done", "Background");
    dlg->SetPanelWidth(600.0f);
    return dlg;
}

void DownloadModelDialogContent::Tick(double) {
    if (!job_) return;
    const uint64_t done = job_->done_bytes.load();
    const uint64_t total = job_->total_bytes.load();
    float frac = 0.0f;
    if (job_->finished.load() && job_->ok.load()) {
        frac = 1.0f;
    } else if (total > 0) {
        frac = std::clamp(
            static_cast<float>(static_cast<double>(done) / static_cast<double>(total)),
            0.0f, 1.0f);
    }
    static_cast<Progress*>(progress_bar_.get())->SetValue(frac);

    bool can_extract = false;
    bool can_inspect = false;
    {
        std::lock_guard<std::mutex> lock(job_->mu);
        can_extract = job_->is_valid_archive;
        const std::string p =
            job_->final_path.empty() ? job_->target_path : job_->final_path;
        std::error_code ec;
        can_inspect = !p.empty() && std::filesystem::exists(p, ec);
    }
    extract_btn_->SetEnabled(can_extract);
    inspect_btn_->SetEnabled(can_inspect);

    if (job_->active.load() || job_->dirty.exchange(false)) {
        Invalidate();
    }
}

void DownloadModelDialogContent::Layout(const Layout::Rect& bounds) {
    Layout(bounds, CachedLayoutTokens());
}

void DownloadModelDialogContent::Layout(const Layout::Rect& bounds,
                                        const Theme::Tokens& tokens) {
    CacheLayoutTokens(tokens);
    SetBounds(bounds);

    const float x = bounds.x;
    const float w = bounds.width;
    progress_bar_->Layout({x, bounds.y + 84.0f, w, 10.0f}, tokens);

    const float btn_y = bounds.y + 124.0f;
    const auto btn_rects = Layout::DistributeRow(
        {x, btn_y, w, 30.0f},
        {130.0f, 130.0f, 130.0f},
        {1.0f, 1.0f, 1.0f},
        8.0f,
        Layout::FlexAlign::Start);
    if (btn_rects.size() == 3) {
        copy_path_btn_->Layout(btn_rects[0], tokens);
        inspect_btn_->Layout(btn_rects[1], tokens);
        extract_btn_->Layout(btn_rects[2], tokens);
    }

    archive_list_rect_ = {x, bounds.y + 186.0f, w,
                          std::max(60.0f, bounds.height - 190.0f)};
}

bool DownloadModelDialogContent::HandleEvent(const Event& event) {
    if (event.type == Event::Type::MouseWheel &&
        archive_list_rect_.Contains(event.position) && job_) {
        std::size_t entry_count = 0;
        {
            std::lock_guard<std::mutex> lock(job_->mu);
            entry_count = job_->archive_entries.size();
        }
        const float content_h = static_cast<float>(entry_count) * 24.0f + 8.0f;
        const float max_scroll = std::max(0.0f, content_h - archive_list_rect_.height);
        scroll_y_ = std::clamp(
            scroll_y_ - static_cast<float>(event.wheel_delta) * 0.4f,
            0.0f, max_scroll);
        Invalidate();
        return true;
    }
    return false;
}

void DownloadModelDialogContent::Render(Rendering::Renderer& r,
                                        const Theme::Tokens& tokens) {
    const auto b = GetBounds();
    if (!job_) return;

    const uint64_t done = job_->done_bytes.load();
    const uint64_t total = job_->total_bytes.load();
    const double speed = job_->speed_bps.load();
    const bool active = job_->active.load();
    const bool finished = job_->finished.load();
    const bool ok = job_->ok.load();

    std::string status_line;
    std::string path_line;
    std::string arch_summary;
    std::vector<XER::Content::Archive::ArchiveEntryInfo> entries;
    bool valid_archive = false;
    {
        std::lock_guard<std::mutex> lock(job_->mu);
        status_line = job_->status_text;
        path_line = job_->final_path.empty() ? job_->target_path : job_->final_path;
        arch_summary = job_->archive_summary;
        valid_archive = job_->is_valid_archive;
        entries = job_->archive_entries;
    }

    // Header card: file + version + format + sha256
    r.FillRect({b.x, b.y + 4.0f, b.width, 66.0f}, tokens.secondary,
               tokens.radius_medium);
    r.StrokeRect({b.x, b.y + 4.0f, b.width, 66.0f}, tokens.border, 1.0f,
                 tokens.radius_medium);

    const Rendering::TextStyle file_st{tokens.foreground, 13.0f, 600};
    const std::string title_line = job_->file_name +
        (job_->version_name.empty() ? "" : ("  •  " + job_->version_name));
    r.DrawText(title_line,
               Layout::CenteredTextRect(
                   r, title_line, file_st,
                   {b.x + 12.0f, b.y + 10.0f, b.width - 24.0f, 18.0f}, 0.0f, false),
               file_st);

    const Rendering::TextStyle meta_st{tokens.muted, 11.0f, 450};
    std::string sub = "Format: " + (job_->file_format.empty() ? job_->file_type : job_->file_format);
    if (!job_->sha256.empty()) {
        sub += "  •  SHA-256: " + job_->sha256.substr(0, 16) + "...";
    }
    r.DrawText(sub,
               Layout::CenteredTextRect(
                   r, sub, meta_st,
                   {b.x + 12.0f, b.y + 30.0f, b.width - 24.0f, 16.0f}, 0.0f, false),
               meta_st);

    std::string shown_path = path_line;
    if (shown_path.size() > 74) {
        shown_path = "..." + shown_path.substr(shown_path.size() - 71);
    }
    r.DrawText(shown_path,
               Layout::CenteredTextRect(
                   r, shown_path, meta_st,
                   {b.x + 12.0f, b.y + 48.0f, b.width - 24.0f, 16.0f}, 0.0f, false),
               meta_st);

    // Progress telemetry row under progress_bar_
    float pct = 0.0f;
    if (finished && ok) pct = 100.0f;
    else if (total > 0) pct = static_cast<float>(done * 100.0 / total);

    char left_buf[160];
    std::snprintf(left_buf, sizeof(left_buf), "%.1f%%   •   %s / %s",
                  pct, FormatByteSize(done).c_str(),
                  total > 0 ? FormatByteSize(total).c_str() : "?");

    std::string right_txt = status_line;
    if (active && speed > 1024.0) {
        char sp_buf[96];
        if (total > done && speed > 1.0) {
            const int eta_s = static_cast<int>((total - done) / speed);
            std::snprintf(sp_buf, sizeof(sp_buf), "%s/s  •  ETA %dm %02ds",
                          FormatByteSize(static_cast<uint64_t>(speed)).c_str(),
                          eta_s / 60, eta_s % 60);
        } else {
            std::snprintf(sp_buf, sizeof(sp_buf), "%s/s",
                          FormatByteSize(static_cast<uint64_t>(speed)).c_str());
        }
        right_txt = sp_buf;
    }

    const Theme::Color st_color = finished
        ? (ok ? Theme::Color{0.30f, 0.85f, 0.50f, 1.0f} : tokens.danger)
        : tokens.accent;
    const Rendering::TextStyle prog_left{tokens.foreground, 11.5f, 600};
    const Rendering::TextStyle prog_right{st_color, 11.5f, 600};
    r.DrawText(left_buf,
               Layout::CenteredTextRect(
                   r, left_buf, prog_left,
                   {b.x, b.y + 98.0f, b.width * 0.56f, 18.0f}, 0.0f, false),
               prog_left);
    const float rw = r.MeasureText(right_txt, prog_right).width;
    r.DrawText(right_txt,
               Layout::CenteredTextRect(
                   r, right_txt, prog_right,
                   {std::max(b.x + b.width * 0.56f, b.Right() - rw - 4.0f),
                    b.y + 98.0f, rw + 4.0f, 18.0f}, 0.0f, false),
               prog_right);

    // Archive / ZIP contents header
    const Rendering::TextStyle sec_st{tokens.muted, 11.0f, 600};
    r.DrawText("ZIP / ARCHIVE CONTENTS (XER_Archive)",
               Layout::CenteredTextRect(
                   r, "ZIP / ARCHIVE CONTENTS (XER_Archive)", sec_st,
                   {b.x, b.y + 164.0f, b.width * 0.5f, 16.0f}, 0.0f, false),
               sec_st);
    if (!arch_summary.empty()) {
        const Rendering::TextStyle sum_st{
            valid_archive ? tokens.accent : tokens.muted, 11.0f, 500};
        const float sw = std::min(b.width * 0.52f,
                                  r.MeasureText(arch_summary, sum_st).width + 4.0f);
        r.DrawText(arch_summary,
                   Layout::CenteredTextRect(
                       r, arch_summary, sum_st,
                       {b.Right() - sw, b.y + 164.0f, sw, 16.0f}, 0.0f, false),
                   sum_st);
    }

    // Archive list box
    r.FillRect(archive_list_rect_, tokens.background, tokens.radius_medium);
    r.StrokeRect(archive_list_rect_, tokens.border, 1.0f, tokens.radius_medium);

    if (entries.empty()) {
        const std::string empty_msg = arch_summary.empty()
            ? (active ? "Downloading... ZIP contents will appear once saved."
                      : "Click 'Inspect ZIP' to probe archive entries.")
            : arch_summary;
        const Rendering::TextStyle empty_st{tokens.muted, 11.5f, 450};
        r.DrawText(empty_msg,
                   Layout::CenteredTextRect(
                       r, empty_msg, empty_st, archive_list_rect_, 14.0f, true),
                   empty_st);
        return;
    }

    r.PushClip({archive_list_rect_.x + 2.0f, archive_list_rect_.y + 2.0f,
                archive_list_rect_.width - 4.0f,
                archive_list_rect_.height - 4.0f});
    constexpr float kEntryH = 24.0f;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const float ry = archive_list_rect_.y + 4.0f +
                         static_cast<float>(i) * kEntryH - scroll_y_;
        if (ry + kEntryH < archive_list_rect_.y ||
            ry > archive_list_rect_.Bottom()) {
            continue;
        }
        if (i % 2 == 1) {
            r.FillRect({archive_list_rect_.x + 4.0f, ry,
                        archive_list_rect_.width - 8.0f, kEntryH},
                       tokens.surface, 4.0f);
        }
        const auto& e = entries[i];
        const Layout::Rect tag_r{archive_list_rect_.x + 8.0f, ry + 4.0f,
                                 38.0f, 16.0f};
        r.FillRect(tag_r, e.is_dir ? tokens.secondary : tokens.accent, 8.0f);
        const Rendering::TextStyle tag_st{
            e.is_dir ? tokens.muted : tokens.accent_foreground, 9.5f, 600};
        const char* tag_lbl = e.is_dir ? "DIR" : "FILE";
        r.DrawText(tag_lbl,
                   Layout::CenteredTextRect(r, tag_lbl, tag_st, tag_r, 2.0f, true),
                   tag_st);

        const Rendering::TextStyle name_st{tokens.foreground, 11.5f, 450};
        r.DrawText(e.path,
                   Layout::CenteredTextRect(
                       r, e.path, name_st,
                       {archive_list_rect_.x + 54.0f, ry,
                        archive_list_rect_.width - 138.0f, kEntryH}, 0.0f, false),
                   name_st);

        if (!e.is_dir) {
            const std::string sz_str =
                FormatByteSize(e.size > 0 ? static_cast<uint64_t>(e.size) : 0);
            const Rendering::TextStyle sz_st{tokens.muted, 10.5f, 500};
            const float szw = r.MeasureText(sz_str, sz_st).width;
            r.DrawText(sz_str,
                       Layout::CenteredTextRect(
                           r, sz_str, sz_st,
                           {archive_list_rect_.Right() - szw - 12.0f, ry,
                            szw + 4.0f, kEntryH}, 0.0f, false),
                       sz_st);
        }
    }
    r.PopClip();
}

Layout::Size DownloadModelDialogContent::GetMinSize() const {
    return {400.0f, 340.0f};
}
Layout::Size DownloadModelDialogContent::GetPreferredSize() const {
    return {560.0f, 370.0f};
}
Layout::Size DownloadModelDialogContent::GetMaxSize() const {
    return {2000.0f, 2000.0f};
}

// ===========================================================================
// ImagePreviewDialogContent
// ===========================================================================

ImagePreviewDialogContent::ImagePreviewDialogContent(
    std::string model_name,
    std::string primary_file_name,
    std::vector<content::civitai::CivitaiModelImage> images,
    int initial_index,
    std::function<void(const std::string&)> on_status)
    : model_name_(std::move(model_name))
    , primary_file_name_(std::move(primary_file_name))
    , images_(std::move(images))
    , on_status_(std::move(on_status)) {
    if (!images_.empty()) {
        index_ = std::clamp(initial_index, 0,
                            static_cast<int>(images_.size()) - 1);
    }

    prev_btn_ = std::make_shared<Button>("< Prev", ButtonStyle::Secondary);
    next_btn_ = std::make_shared<Button>("Next >", ButtonStyle::Secondary);
    copy_prompt_btn_ = std::make_shared<Button>("Copy Prompt", ButtonStyle::Primary);
    copy_json_btn_ = std::make_shared<Button>("Copy sd.exe JSON", ButtonStyle::Secondary);

    static_cast<Button*>(prev_btn_.get())->SetOnClick(
        [this]() { StepImage(-1); });
    static_cast<Button*>(next_btn_.get())->SetOnClick(
        [this]() { StepImage(1); });
    static_cast<Button*>(copy_prompt_btn_.get())->SetOnClick([this]() {
        if (images_.empty()) return;
        const auto& img = images_[static_cast<std::size_t>(index_)];
        const std::string txt = img.prompt.empty() ? model_name_ : img.prompt;
        XER::Content::Clipboard::SetText(txt);
        if (on_status_) on_status_("Copied generation prompt to clipboard.");
    });
    static_cast<Button*>(copy_json_btn_.get())->SetOnClick([this]() {
        if (images_.empty()) return;
        const auto& img = images_[static_cast<std::size_t>(index_)];
        XER::Content::Clipboard::SetText(
            BuildSdGenerateJson(model_name_, primary_file_name_, img));
        if (on_status_) on_status_("Copied sd.exe generate.json config to clipboard.");
    });

    AddChild(prev_btn_);
    AddChild(next_btn_);
    AddChild(copy_prompt_btn_);
    AddChild(copy_json_btn_);
}

std::shared_ptr<Dialog> ImagePreviewDialogContent::CreateDialog(
    std::string model_name,
    std::string primary_file_name,
    std::vector<content::civitai::CivitaiModelImage> images,
    int initial_index,
    std::function<void(const std::string&)> on_status) {
    const std::string title = model_name + " — Generated Artwork Preview";
    auto content = std::make_shared<ImagePreviewDialogContent>(
        std::move(model_name), std::move(primary_file_name),
        std::move(images), initial_index, std::move(on_status));
    auto dlg = Dialog::Create(title, content, "Close", "");
    dlg->SetPanelWidth(700.0f);
    return dlg;
}

void ImagePreviewDialogContent::StepImage(int delta) {
    if (images_.empty()) return;
    const int n = static_cast<int>(images_.size());
    index_ = (index_ + delta + n) % n;
    Invalidate();
}

void ImagePreviewDialogContent::Tick(double) {
    if (HubPreviewCache::Instance().ConsumeCompleted()) {
        Invalidate();
    }
}

void ImagePreviewDialogContent::Layout(const Layout::Rect& bounds) {
    Layout(bounds, CachedLayoutTokens());
}

void ImagePreviewDialogContent::Layout(const Layout::Rect& bounds,
                                       const Theme::Tokens& tokens) {
    CacheLayoutTokens(tokens);
    SetBounds(bounds);

    const float x = bounds.x;
    const float w = bounds.width;
    stage_rect_ = {x, bounds.y + 4.0f, w, 286.0f};

    const float nav_y = stage_rect_.Bottom() + 8.0f;
    prev_btn_->Layout({x, nav_y, 78.0f, 30.0f}, tokens);
    next_btn_->Layout({x + 86.0f, nav_y, 78.0f, 30.0f}, tokens);
    copy_json_btn_->Layout({bounds.Right() - 142.0f, nav_y, 142.0f, 30.0f}, tokens);
    copy_prompt_btn_->Layout({bounds.Right() - 266.0f, nav_y, 116.0f, 30.0f}, tokens);

    strip_rect_ = {x, nav_y + 38.0f, w, 46.0f};
    thumb_rects_.clear();
    const std::size_t n = std::min<std::size_t>(images_.size(), 12);
    float tx = strip_rect_.x;
    for (std::size_t i = 0; i < n; ++i) {
        thumb_rects_.push_back({tx, strip_rect_.y + 2.0f, 44.0f, 42.0f});
        tx += 50.0f;
    }

    meta_rect_ = {x, strip_rect_.Bottom() + 8.0f, w,
                  std::max(72.0f, bounds.Bottom() - (strip_rect_.Bottom() + 8.0f))};
}

bool ImagePreviewDialogContent::HandleEvent(const Event& event) {
    if (event.type == Event::Type::Click &&
        event.mouse_button == MouseButton::Left) {
        for (std::size_t i = 0; i < thumb_rects_.size(); ++i) {
            if (thumb_rects_[i].Contains(event.position)) {
                index_ = static_cast<int>(i);
                Invalidate();
                return true;
            }
        }
        if (stage_rect_.Contains(event.position)) {
            StepImage(event.position.x > stage_rect_.x + stage_rect_.width * 0.5f ? 1 : -1);
            return true;
        }
    }
    return false;
}

void ImagePreviewDialogContent::Render(Rendering::Renderer& r,
                                       const Theme::Tokens& tokens) {
    r.FillRect(stage_rect_, tokens.background, tokens.radius_medium);
    r.StrokeRect(stage_rect_, tokens.border, 1.0f, tokens.radius_medium);

    if (images_.empty()) {
        const Rendering::TextStyle empty_st{tokens.muted, 13.0f, 450};
        r.DrawText("No example images attached to this model version.",
                   Layout::CenteredTextRect(r, "No example images attached to this model version.",
                                            empty_st, stage_rect_, 16.0f, true),
                   empty_st);
        return;
    }

    const auto& cur = images_[static_cast<std::size_t>(index_)];
    const std::string hi_path =
        HubPreviewCache::Instance().GetOrRequest(cur.url, 1024, true);
    const std::string fallback_path = hi_path.empty()
        ? HubPreviewCache::Instance().GetOrRequest(cur.url, 512, false)
        : hi_path;

    const Layout::Rect inner_stage{stage_rect_.x + 6.0f, stage_rect_.y + 6.0f,
                                   stage_rect_.width - 12.0f,
                                   stage_rect_.height - 12.0f};
    const Layout::Rect fit =
        HubPreviewCache::ContainRect(inner_stage, cur.width, cur.height);
    if (fallback_path.empty() || !r.DrawImage(fallback_path, fit, 1.0f, 6.0f)) {
        const Rendering::TextStyle loading_st{tokens.muted, 12.5f, 500};
        r.DrawText("Loading generated artwork from Civitai CDN...",
                   Layout::CenteredTextRect(
                       r, "Loading generated artwork from Civitai CDN...",
                       loading_st, stage_rect_, 16.0f, true),
                   loading_st);
    }

    // Image counter + resolution between nav buttons
    std::string info = std::to_string(index_ + 1) + " / " +
                       std::to_string(images_.size());
    if (cur.width > 0 && cur.height > 0) {
        info += "   •   " + std::to_string(cur.width) + " × " +
                std::to_string(cur.height);
    }
    const Rendering::TextStyle info_st{tokens.muted, 11.5f, 600};
    r.DrawText(info,
               Layout::CenteredTextRect(
                   r, info, info_st,
                   {stage_rect_.x + 174.0f, stage_rect_.Bottom() + 8.0f,
                    200.0f, 30.0f}, 0.0f, false),
               info_st);

    // Thumbnail filmstrip
    for (std::size_t i = 0; i < thumb_rects_.size(); ++i) {
        const auto& tr = thumb_rects_[i];
        const bool sel = static_cast<int>(i) == index_;
        r.FillRect(tr, tokens.secondary, 6.0f);
        const std::string tp =
            HubPreviewCache::Instance().GetOrRequest(images_[i].url, 512, true);
        if (!tp.empty()) {
            r.DrawImage(tp, tr, sel ? 1.0f : 0.72f, 6.0f);
        }
        r.StrokeRect(tr, sel ? tokens.accent : tokens.border,
                     sel ? 2.0f : 1.0f, 6.0f);
    }

    // Generation metadata card
    r.FillRect(meta_rect_, tokens.secondary, tokens.radius_medium);
    r.StrokeRect(meta_rect_, tokens.border, 1.0f, tokens.radius_medium);

    const Rendering::TextStyle p_lbl{tokens.accent, 10.5f, 700};
    const Rendering::TextStyle p_txt{tokens.foreground, 11.5f, 450};
    const Rendering::TextStyle m_txt{tokens.muted, 11.0f, 500};

    std::string prompt_line = cur.prompt.empty()
        ? "(No prompt metadata supplied by creator)"
        : cur.prompt;
    if (prompt_line.size() > 170) {
        prompt_line = prompt_line.substr(0, 167) + "...";
    }
    r.DrawText("PROMPT",
               Layout::CenteredTextRect(
                   r, "PROMPT", p_lbl,
                   {meta_rect_.x + 10.0f, meta_rect_.y + 6.0f, 80.0f, 14.0f},
                   0.0f, false),
               p_lbl);
    r.DrawText(prompt_line,
               {meta_rect_.x + 10.0f, meta_rect_.y + 22.0f,
                meta_rect_.width - 20.0f, 34.0f},
               p_txt);

    std::ostringstream params;
    if (!cur.sampler.empty()) params << "Sampler: " << cur.sampler << "   ";
    if (cur.steps > 0) params << "Steps: " << cur.steps << "   ";
    if (cur.cfg_scale > 0.0) {
        char cb[32];
        std::snprintf(cb, sizeof(cb), "CFG: %.1f   ", cur.cfg_scale);
        params << cb;
    }
    if (!cur.seed.empty()) params << "Seed: " << cur.seed;
    const std::string param_str = params.str().empty()
        ? "Click 'Copy sd.exe JSON' to generate a ready config for sd.exe"
        : params.str();
    r.DrawText(param_str,
               Layout::CenteredTextRect(
                   r, param_str, m_txt,
                   {meta_rect_.x + 10.0f, meta_rect_.Bottom() - 22.0f,
                    meta_rect_.width - 20.0f, 16.0f}, 0.0f, false),
               m_txt);
}

Layout::Size ImagePreviewDialogContent::GetMinSize() const {
    return {480.0f, 420.0f};
}
Layout::Size ImagePreviewDialogContent::GetPreferredSize() const {
    return {660.0f, 476.0f};
}
Layout::Size ImagePreviewDialogContent::GetMaxSize() const {
    return {2400.0f, 2400.0f};
}

}  // namespace XER::Tools::HubBrowser
