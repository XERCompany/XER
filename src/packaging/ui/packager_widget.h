/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include "packaging/package_spec.h"

#include "ui/widgets/widget.h"

#include <atomic>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
using HWND = void*;
#endif

namespace XER::UI::Widgets {
class AppTitleBar;
class Button;
class Checkbox;
class Label;
class MessageScroller;
class Progress;
class Table;
class TextInput;
}  // namespace XER::UI::Widgets

namespace XER::Packaging::Ui {

// PackagerWidget — the xer_packager app UI, composed entirely of src/ui
// widgets (no custom-painted surfaces):
//
//   [ AppTitleBar ]
//   Target executable  [ input                    ] [Browse...]
//   Output folder      [ input                    ] [Browse...]
//   Package name       [ input         ]  Version [ input  ]
//   Extra files/dirs   [ input (; separated)      ] [Add...]
//   Icon (.ico)        [ input                    ] [Browse...]
//   [x] Write .zip archive          [Scan dependencies] [Pack]
//   [ dependency table (name / size / source) ]
//   [ progress bar ]  status label
//   [ log pane (MessageScroller, newest at bottom) ]
//
// The dep scan and the pack itself run on a worker thread; progress/log
// callbacks marshal through a mutex-guarded queue drained in Tick —
// the UI thread never blocks on file I/O.
class PackagerWidget final : public XER::UI::Widgets::Widget {
public:
    // `owner` parents the native file/folder pickers; nullptr is fine
    // (headless screenshot/tests) — the browse buttons then no-op.
    explicit PackagerWidget(HWND owner = nullptr);
    ~PackagerWidget() override;

    XER::UI::Widgets::AppTitleBar* TitleBar() const {
        return title_bar_.get();
    }

    // Prefills the exe field (and name/output when they are still
    // empty). Used by the CLI to seed the GUI, and by the headless
    // screenshot to show real data.
    void SetExePath(const std::string& utf8_path);

    // Synchronous scan + table fill on the calling thread — the
    // headless screenshot seam (the async worker has no window to
    // drain into when there is no frame pump).
    bool ScanNow(std::string* error = nullptr);

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
    enum class JobKind { Scan, Pack };

    // Shared state between the worker thread and Tick/DrainWorker.
    struct Job {
        std::mutex mu;
        std::deque<std::string> log;
        float progress = -1.0f;
        std::string step;
        bool finished = false;
        bool ok = false;
        std::string error;
        DependencyScanResult scan;
        PackageResult result;
    };

    void BuildChildren();
    void LayoutChildren(const XER::UI::Layout::Rect& b,
                        const XER::UI::Theme::Tokens& tokens);
    void StartJob(JobKind kind);
    void DrainJob();
    void ApplyScanResult(Job& job);
    void SetBusy(bool busy);
    void AppendLog(std::string line);
    void SetStatus(std::string text);

    // Reads the form into a spec; fills `error` (and marks the bad
    // field) when a required input is missing/invalid.
    bool CollectSpec(PackageSpec* spec, std::string* error) const;
    std::vector<std::string> SplitPaths(const std::string& text) const;

    // Win32 pickers; no-op when owner_ is null.
    std::string PickFile(const wchar_t* title, const wchar_t* filter);
    std::string PickFolder(const wchar_t* title);
    std::string WideToUtf8(const wchar_t* wide) const;
    std::wstring Utf8ToWide(const std::string& utf8) const;

    static std::string FmtBytes(std::uint64_t bytes);

    HWND owner_;

    // ---- children ----
    std::shared_ptr<XER::UI::Widgets::AppTitleBar> title_bar_;
    std::shared_ptr<XER::UI::Widgets::Label> exe_label_;
    std::shared_ptr<XER::UI::Widgets::TextInput> exe_input_;
    std::shared_ptr<XER::UI::Widgets::Button> exe_browse_;
    std::shared_ptr<XER::UI::Widgets::Label> out_label_;
    std::shared_ptr<XER::UI::Widgets::TextInput> out_input_;
    std::shared_ptr<XER::UI::Widgets::Button> out_browse_;
    std::shared_ptr<XER::UI::Widgets::Label> name_label_;
    std::shared_ptr<XER::UI::Widgets::TextInput> name_input_;
    std::shared_ptr<XER::UI::Widgets::Label> version_label_;
    std::shared_ptr<XER::UI::Widgets::TextInput> version_input_;
    std::shared_ptr<XER::UI::Widgets::Label> extras_label_;
    std::shared_ptr<XER::UI::Widgets::TextInput> extras_input_;
    std::shared_ptr<XER::UI::Widgets::Button> extras_browse_;
    std::shared_ptr<XER::UI::Widgets::Label> icon_label_;
    std::shared_ptr<XER::UI::Widgets::TextInput> icon_input_;
    std::shared_ptr<XER::UI::Widgets::Button> icon_browse_;
    std::shared_ptr<XER::UI::Widgets::Checkbox> zip_check_;
    std::shared_ptr<XER::UI::Widgets::Label> zip_label_;
    std::shared_ptr<XER::UI::Widgets::Button> scan_button_;
    std::shared_ptr<XER::UI::Widgets::Button> pack_button_;
    std::shared_ptr<XER::UI::Widgets::Table> dep_table_;
    std::shared_ptr<XER::UI::Widgets::Progress> progress_;
    std::shared_ptr<XER::UI::Widgets::Label> status_label_;
    std::shared_ptr<XER::UI::Widgets::Label> log_label_;
    std::shared_ptr<XER::UI::Widgets::MessageScroller> log_view_;

    // ---- worker ----
    std::shared_ptr<Job> job_;
    std::future<void> job_future_;

    int log_lines_ = 0;
    static constexpr int kMaxLogLines = 250;
};

}  // namespace XER::Packaging::Ui
