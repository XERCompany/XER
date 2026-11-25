/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "packaging/ui/packager_widget.h"

#include "packaging/dependency_scanner.h"
#include "packaging/packager.h"

#include "ui/rendering/renderer.h"
#include "ui/widgets/app_title_bar.h"
#include "ui/widgets/button.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/label.h"
#include "ui/widgets/message_scroller.h"
#include "ui/widgets/progress.h"
#include "ui/widgets/table.h"
#include "ui/widgets/text_input.h"

#include <algorithm>
#include <chrono>
#include <filesystem>

#ifdef _WIN32
#include <commdlg.h>
#include <shlobj.h>
#endif

namespace XER::Packaging::Ui {

namespace fs = std::filesystem;
namespace W = XER::UI::Widgets;
using XER::UI::Layout::Rect;

namespace {

constexpr float kPad = 16.0f;
constexpr float kGap = 8.0f;
constexpr float kLabelW = 128.0f;
constexpr float kRowH = 28.0f;
constexpr float kBrowseW = 88.0f;
constexpr float kTitleH = 38.0f;
constexpr float kActionsH = 32.0f;
constexpr float kProgressH = 10.0f;
constexpr float kStatusH = 18.0f;
constexpr float kLogLabelH = 16.0f;
constexpr float kLogH = 110.0f;

}  // namespace

PackagerWidget::PackagerWidget(HWND owner) : owner_(owner) {
    SetFocusable(false);
    BuildChildren();
}

PackagerWidget::~PackagerWidget() {
    // A still-running worker only writes into the shared Job — safe to
    // abandon; detach the future so its destruction does not block.
    if (job_future_.valid()) {
        (void)job_future_.wait_for(std::chrono::seconds(2));
    }
}

void PackagerWidget::BuildChildren() {
    title_bar_ = std::make_shared<W::AppTitleBar>("XER Packager");
    title_bar_->SetWorkspaceLabel("Packager");
    title_bar_->SetActionPill("Pack", [this] { StartJob(JobKind::Pack); });
    AddChild(title_bar_);

    auto make_label = [](const char* text) {
        auto l = std::make_shared<W::Label>(text, /*muted=*/true);
        return l;
    };

    exe_label_ = make_label("Target executable");
    exe_input_ = std::make_shared<W::TextInput>();
    exe_input_->SetPlaceholder("path\\to\\app.exe");
    exe_input_->SetOnValueChanged([this](const std::string& v) {
        // Keep name/output guesses in sync until the user edits them.
        if (v.empty()) return;
        const fs::path p = fs::path(v);
        if (name_input_ != nullptr && name_input_->GetValue().empty())
            name_input_->SetValue(p.stem().string());
        if (out_input_ != nullptr && out_input_->GetValue().empty())
            out_input_->SetValue(
                (p.parent_path() / "dist").generic_string());
    });
    exe_browse_ = std::make_shared<W::Button>(
        "Browse…", W::ButtonStyle::Outline);
    exe_browse_->SetOnClick([this] {
        const std::string p = PickFile(
            L"Select target executable",
            L"Executables\0*.exe\0All files\0*.*\0\0");
        if (!p.empty()) SetExePath(p);
    });
    AddChild(exe_label_);
    AddChild(exe_input_);
    AddChild(exe_browse_);

    out_label_ = make_label("Output folder");
    out_input_ = std::make_shared<W::TextInput>();
    out_input_->SetPlaceholder("staging parent dir");
    out_browse_ = std::make_shared<W::Button>(
        "Browse…", W::ButtonStyle::Outline);
    out_browse_->SetOnClick([this] {
        const std::string p = PickFolder(L"Select output folder");
        if (!p.empty()) out_input_->SetValue(p);
    });
    AddChild(out_label_);
    AddChild(out_input_);
    AddChild(out_browse_);

    name_label_ = make_label("Package name");
    name_input_ = std::make_shared<W::TextInput>();
    name_input_->SetPlaceholder("my-app");
    version_label_ = make_label("Version");
    version_input_ = std::make_shared<W::TextInput>("0.0.0");
    AddChild(name_label_);
    AddChild(name_input_);
    AddChild(version_label_);
    AddChild(version_input_);

    extras_label_ = make_label("Extra files/dirs");
    extras_input_ = std::make_shared<W::TextInput>();
    extras_input_->SetPlaceholder("separate paths with ';' — optional");
    extras_browse_ = std::make_shared<W::Button>(
        "Add…", W::ButtonStyle::Outline);
    extras_browse_->SetOnClick([this] {
        const std::string p = PickFile(
            L"Add extra file", L"All files\0*.*\0\0");
        if (p.empty()) return;
        std::string v = extras_input_->GetValue();
        if (!v.empty() && v.back() != ';') v += ';';
        extras_input_->SetValue(v + p);
    });
    AddChild(extras_label_);
    AddChild(extras_input_);
    AddChild(extras_browse_);

    icon_label_ = make_label("Icon (.ico)");
    icon_input_ = std::make_shared<W::TextInput>();
    icon_input_->SetPlaceholder("optional");
    icon_browse_ = std::make_shared<W::Button>(
        "Browse…", W::ButtonStyle::Outline);
    icon_browse_->SetOnClick([this] {
        const std::string p = PickFile(
            L"Select icon", L"Icons\0*.ico\0All files\0*.*\0\0");
        if (!p.empty()) icon_input_->SetValue(p);
    });
    AddChild(icon_label_);
    AddChild(icon_input_);
    AddChild(icon_browse_);

    zip_check_ = std::make_shared<W::Checkbox>(false);
    zip_label_ = make_label("also write .zip archive");
    scan_button_ = std::make_shared<W::Button>(
        "Scan dependencies", W::ButtonStyle::Secondary);
    scan_button_->SetOnClick([this] { StartJob(JobKind::Scan); });
    pack_button_ = std::make_shared<W::Button>(
        "Pack", W::ButtonStyle::Primary);
    pack_button_->SetOnClick([this] { StartJob(JobKind::Pack); });
    AddChild(zip_check_);
    AddChild(zip_label_);
    AddChild(scan_button_);
    AddChild(pack_button_);

    dep_table_ = W::Table::Create();
    dep_table_->SetColumns({{"File", 0.0f},
                            {"Size", 110.0f},
                            {"Origin / role", 160.0f}});
    dep_table_->SetHeader(true);
    dep_table_->SetRowHeight(24.0f);
    AddChild(dep_table_);

    progress_ = std::make_shared<W::Progress>(0.0f);
    status_label_ = make_label("Ready.");
    log_label_ = make_label("Log");
    log_view_ = std::make_shared<W::MessageScroller>();
    AddChild(progress_);
    AddChild(status_label_);
    AddChild(log_label_);
    AddChild(log_view_);
}

void PackagerWidget::SetExePath(const std::string& utf8_path) {
    exe_input_->SetValue(utf8_path);  // fires the name/output guesses
}

// ---- layout --------------------------------------------------------------

void PackagerWidget::Layout(const Rect& bounds) {
    SetBounds(bounds);
    LayoutChildren(GetBounds(), CachedLayoutTokens());
}

void PackagerWidget::Layout(const Rect& bounds,
                            const XER::UI::Theme::Tokens& tokens) {
    CacheLayoutTokens(tokens);
    Layout(bounds);
}

void PackagerWidget::LayoutChildren(const Rect& b,
                                    const XER::UI::Theme::Tokens&) {
    title_bar_->Layout({b.x, b.y, b.width, kTitleH}, CachedLayoutTokens());

    float y = b.y + kTitleH + kPad;
    const float left = b.x + kPad;
    const float right = b.x + b.width - kPad;
    const float field_x = left + kLabelW + kGap;
    const float field_w = right - kBrowseW - kGap - field_x;

    auto row = [&](std::shared_ptr<W::Label> label,
                   std::shared_ptr<W::Widget> input,
                   std::shared_ptr<W::Widget> browse) {
        label->Layout({left, y + 4.0f, kLabelW, kRowH},
                      CachedLayoutTokens());
        input->Layout({field_x, y, field_w, kRowH}, CachedLayoutTokens());
        browse->Layout({field_x + field_w + kGap, y, kBrowseW, kRowH},
                       CachedLayoutTokens());
        y += kRowH + kGap;
    };

    row(exe_label_, exe_input_, exe_browse_);
    row(out_label_, out_input_, out_browse_);

    // Name + version share one row.
    name_label_->Layout({left, y + 4.0f, kLabelW, kRowH},
                        CachedLayoutTokens());
    const float ver_w = 110.0f;
    const float ver_label_w = 56.0f;
    name_input_->Layout(
        {field_x, y, right - field_x - ver_label_w - ver_w - kGap * 2.0f,
         kRowH},
        CachedLayoutTokens());
    version_label_->Layout(
        {right - ver_w - ver_label_w - kGap, y + 4.0f, ver_label_w,
         kRowH},
        CachedLayoutTokens());
    version_input_->Layout({right - ver_w, y, ver_w, kRowH},
                           CachedLayoutTokens());
    y += kRowH + kGap;

    row(extras_label_, extras_input_, extras_browse_);
    row(icon_label_, icon_input_, icon_browse_);

    // Action row: zip toggle left, Scan/Pack right.
    zip_check_->Layout({left, y + 3.0f, 22.0f, 22.0f},
                       CachedLayoutTokens());
    zip_label_->Layout({left + 30.0f, y + 6.0f, 200.0f, 20.0f},
                       CachedLayoutTokens());
    const float pack_w = 110.0f, scan_w = 150.0f;
    pack_button_->Layout(
        {right - pack_w, y, pack_w, kActionsH}, CachedLayoutTokens());
    scan_button_->Layout(
        {right - pack_w - kGap - scan_w, y, scan_w, kActionsH},
        CachedLayoutTokens());
    y += kActionsH + kPad;

    // Bottom-anchored block: log pane, progress, status.
    const float bottom_h = kLogLabelH + kLogH + kGap + kProgressH +
                           kGap + kStatusH;
    const float table_bottom = b.y + b.height - kPad - bottom_h;
    dep_table_->Layout({left, y, right - left,
                        std::max(60.0f, table_bottom - y)},
                       CachedLayoutTokens());

    float by = table_bottom + kGap;
    log_label_->Layout({left, by, 200.0f, kLogLabelH},
                       CachedLayoutTokens());
    by += kLogLabelH;
    log_view_->Layout({left, by, right - left, kLogH},
                      CachedLayoutTokens());
    by += kLogH + kGap;
    progress_->Layout({left, by, right - left, kProgressH},
                      CachedLayoutTokens());
    by += kProgressH + kGap;
    status_label_->Layout({left, by, right - left, kStatusH},
                          CachedLayoutTokens());
}

// ---- render / events -----------------------------------------------------

void PackagerWidget::Render(XER::UI::Rendering::Renderer& renderer,
                            const XER::UI::Theme::Tokens& tokens) {
    renderer.FillRect(GetBounds(), tokens.background, 0.0f);
}

bool PackagerWidget::HandleEvent(const W::Event&) { return false; }

void PackagerWidget::Tick(double) { DrainJob(); }

XER::UI::Layout::Size PackagerWidget::GetMinSize() const {
    return {760.0f, 520.0f};
}
XER::UI::Layout::Size PackagerWidget::GetPreferredSize() const {
    return {980.0f, 680.0f};
}
XER::UI::Layout::Size PackagerWidget::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

// ---- jobs -----------------------------------------------------------------

void PackagerWidget::StartJob(JobKind kind) {
    if (job_future_.valid()) {
        SetStatus("a job is already running");
        return;
    }
    PackageSpec spec;
    std::string error;
    if (!CollectSpec(&spec, &error)) {
        SetStatus(error);
        AppendLog("error: " + error);
        return;
    }

    auto job = std::make_shared<Job>();
    job_ = job;
    auto push_log = [job](const std::string& line) {
        std::lock_guard g(job->mu);
        if (job->log.size() >= 512) job->log.pop_front();
        job->log.push_back(line);
    };
    auto push_progress = [job](float f, const std::string& step) {
        std::lock_guard g(job->mu);
        job->progress = f;
        job->step = step;
    };

    SetBusy(true);
    if (kind == JobKind::Scan) {
        SetStatus("scanning dependencies…");
        push_log("scan: " + spec.exe_path.string());
        job_future_ = std::async(std::launch::async,
                                 [job, spec, push_log] {
            std::vector<fs::path> dirs = spec.search_dirs;
            if (dirs.empty())
                dirs = Packager::DefaultSearchDirs(spec.exe_path);
            std::string err;
            job->ok = ScanDependencies(spec.exe_path, dirs, &job->scan,
                                       &err);
            {
                std::lock_guard g(job->mu);
                job->error = err;
                job->finished = true;
            }
            (void)push_log;
        });
    } else {
        SetStatus("packing…");
        job_future_ = std::async(std::launch::async,
                                 [job, spec, push_log, push_progress] {
            std::string err;
            job->ok = XER::Packaging::Packager::Pack(
                spec, &job->result, &err, push_progress, push_log);
            {
                std::lock_guard g(job->mu);
                job->error = err;
                job->finished = true;
            }
        });
    }
}

void PackagerWidget::DrainJob() {
    if (job_ == nullptr) return;

    std::deque<std::string> lines;
    float progress = -1.0f;
    std::string step;
    bool finished = false;
    {
        std::lock_guard g(job_->mu);
        lines.swap(job_->log);
        progress = job_->progress;
        step = job_->step;
        finished = job_->finished;
    }
    for (auto& l : lines) AppendLog(std::move(l));
    if (progress >= 0.0f) {
        progress_->SetValue(progress);
        if (!step.empty()) SetStatus(step);
    }

    if (!finished || !job_future_.valid() ||
        job_future_.wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready) {
        return;
    }
    job_future_.get();

    const bool ok = job_->ok;
    const std::string err = job_->error;
    if (ok) ApplyScanResult(*job_);
    if (ok) {
        progress_->SetValue(1.0f);
        if (!job_->result.manifest_path.empty()) {
            SetStatus("packed: " + job_->result.staging_dir.string());
        } else {
            SetStatus("scan done: " +
                      std::to_string(job_->scan.dlls.size()) +
                      " DLL(s) resolved");
        }
    } else {
        SetStatus("failed: " + err);
        AppendLog("error: " + err);
    }
    job_.reset();
    SetBusy(false);
    Invalidate();
}

void PackagerWidget::ApplyScanResult(Job& job) {
    std::vector<std::vector<std::string>> rows;
    std::error_code ec;
    const DependencyScanResult* scan = &job.scan;
    if (scan->dlls.empty() && !job.result.files.empty()) {
        // Pack result → show manifest entries instead.
        for (const auto& e : job.result.files) {
            rows.push_back(
                {e.path, FmtBytes(e.size), e.role});
        }
        dep_table_->SetRows(rows);
        return;
    }
    for (const auto& dep : scan->dlls) {
        const std::uint64_t size = fs::file_size(dep.path, ec);
        std::string src =
            dep.origin == DepOrigin::ExeDir ? "exe dir" : "search dir";
        if (dep.delay_loaded) src += ", delay-load";
        rows.push_back({dep.path.filename().string(), FmtBytes(size),
                        src});
    }
    dep_table_->SetRows(rows);
    if (!scan->unresolved.empty()) {
        std::string line = "OS/unresolved imports (" +
                           std::to_string(scan->unresolved.size()) +
                           "):";
        const std::size_t show =
            std::min<std::size_t>(scan->unresolved.size(), 10);
        for (std::size_t i = 0; i < show; ++i)
            line += " " + scan->unresolved[i];
        if (scan->unresolved.size() > show)
            line += " +" +
                    std::to_string(scan->unresolved.size() - show) +
                    " more";
        AppendLog(line);
    }
}

bool PackagerWidget::ScanNow(std::string* error) {
    PackageSpec spec;
    std::string err;
    if (!CollectSpec(&spec, &err)) {
        if (error != nullptr) *error = err;
        return false;
    }
    std::vector<fs::path> dirs = spec.search_dirs;
    if (dirs.empty()) dirs = Packager::DefaultSearchDirs(spec.exe_path);
    auto job = std::make_shared<Job>();
    if (!ScanDependencies(spec.exe_path, dirs, &job->scan, &err)) {
        if (error != nullptr) *error = err;
        return false;
    }
    ApplyScanResult(*job);
    SetStatus("scan done: " +
              std::to_string(job->scan.dlls.size()) +
              " DLL(s) resolved");
    return true;
}

void PackagerWidget::SetBusy(bool busy) {
    scan_button_->SetEnabled(!busy);
    pack_button_->SetEnabled(!busy);
    if (busy) pack_button_->SetLoading(true);
    else pack_button_->SetLoading(false);
}

void PackagerWidget::AppendLog(std::string line) {
    if (log_lines_ >= kMaxLogLines) {
        log_view_->Clear();
        log_lines_ = 0;
        log_view_->AddMessage(std::make_shared<W::Label>(
            "… log trimmed …", /*muted=*/true));
        ++log_lines_;
    }
    log_view_->AddMessage(
        std::make_shared<W::Label>(std::move(line), /*muted=*/true));
    log_view_->ScrollToBottom();
    ++log_lines_;
}

void PackagerWidget::SetStatus(std::string text) {
    status_label_->SetText(std::move(text));
}

// ---- spec ----------------------------------------------------------------

std::vector<std::string> PackagerWidget::SplitPaths(
    const std::string& text) const {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (c == ';') {
            const std::size_t a = cur.find_first_not_of(" \t");
            const std::size_t b = cur.find_last_not_of(" \t");
            if (a != std::string::npos)
                out.push_back(cur.substr(a, b - a + 1));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    const std::size_t a = cur.find_first_not_of(" \t");
    const std::size_t b = cur.find_last_not_of(" \t");
    if (a != std::string::npos) out.push_back(cur.substr(a, b - a + 1));
    return out;
}

bool PackagerWidget::CollectSpec(PackageSpec* spec,
                                 std::string* error) const {
    *spec = PackageSpec{};
    std::error_code ec;

    spec->exe_path = fs::path(exe_input_->GetValue());
    if (spec->exe_path.empty() || !fs::exists(spec->exe_path, ec)) {
        if (error != nullptr)
            *error = "target executable not found: " +
                     exe_input_->GetValue();
        return false;
    }
    spec->output_dir = fs::path(out_input_->GetValue());
    if (out_input_->GetValue().empty()) {
        if (error != nullptr) *error = "output folder is required";
        return false;
    }
    spec->name = name_input_->GetValue();
    if (spec->name.empty())
        spec->name = spec->exe_path.stem().string();
    spec->version = version_input_->GetValue().empty()
                        ? "0.0.0"
                        : version_input_->GetValue();
    for (const std::string& p : SplitPaths(extras_input_->GetValue())) {
        const fs::path f(p);
        if (!fs::exists(f, ec)) {
            if (error != nullptr)
                *error = "extra path not found: " + p;
            return false;
        }
        if (fs::is_directory(f, ec)) spec->extra_dirs.push_back(f);
        else spec->extra_files.push_back(f);
    }
    if (!icon_input_->GetValue().empty()) {
        spec->icon_path = fs::path(icon_input_->GetValue());
        if (!fs::exists(spec->icon_path, ec)) {
            if (error != nullptr)
                *error = "icon not found: " + icon_input_->GetValue();
            return false;
        }
    }
    spec->format = zip_check_->IsChecked() ? PackageFormat::Zip
                                         : PackageFormat::Staging;
    return true;
}

// ---- native pickers --------------------------------------------------------

std::wstring PackagerWidget::Utf8ToWide(const std::string& utf8) const {
#ifdef _WIN32
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1,
                                      nullptr, 0);
    if (n <= 0) return {};
    std::wstring wide(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), n);
    wide.pop_back();
    return wide;
#else
    (void)utf8;
    return {};
#endif
}

std::string PackagerWidget::WideToUtf8(const wchar_t* wide) const {
#ifdef _WIN32
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0,
                                      nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), n,
                        nullptr, nullptr);
    out.pop_back();
    return out;
#else
    (void)wide;
    return {};
#endif
}

std::string PackagerWidget::PickFile(const wchar_t* title,
                                     const wchar_t* filter) {
#ifdef _WIN32
    if (owner_ == nullptr) return {};
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner_;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return {};
    return WideToUtf8(file);
#else
    (void)title;
    (void)filter;
    return {};
#endif
}

std::string PackagerWidget::PickFolder(const wchar_t* title) {
#ifdef _WIN32
    if (owner_ == nullptr) return {};
    static const bool coinit = [] {
        return SUCCEEDED(CoInitializeEx(
            nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    }();
    (void)coinit;
    wchar_t display[MAX_PATH]{};
    BROWSEINFOW bi{};
    bi.hwndOwner = owner_;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.pszDisplayName = display;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl == nullptr) return {};
    wchar_t path[MAX_PATH]{};
    const BOOL ok = SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    if (!ok) return {};
    return WideToUtf8(path);
#else
    (void)title;
    return {};
#endif
}

std::string PackagerWidget::FmtBytes(std::uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 3) {
        v /= 1024.0;
        ++u;
    }
    char buf[32];
    if (u == 0) std::snprintf(buf, sizeof(buf), "%llu B",
                              static_cast<unsigned long long>(bytes));
    else std::snprintf(buf, sizeof(buf), "%.1f %s", v, units[u]);
    return buf;
}

}  // namespace XER::Packaging::Ui
