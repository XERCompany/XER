// ide_app.cc — see ide_app.h for the layout/contract.
#include "ide_app.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "ide_format.h"
#include "ide_preview_widget.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/widget_tree.h"
#include "widgets/app_title_bar.h"
#include "widgets/button.h"
#include "widgets/dialog.h"
#include "widgets/label.h"
#include "widgets/text_input.h"
#include "widgets/textarea.h"

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
// windows.h re-defines DrawText as DrawTextA/W, which would hijack the
// Renderer::DrawText calls below (renderer.h undefs it only for itself).
#undef DrawText
#endif

// The ve.core v3 service table lives in XER_VideoEditor_Core
// (project_store.cc) — declared here to keep the IDE TU free of the
// project model headers. Same signature as project_store.h.
namespace XER::VideoEditor {
const std::vector<std::string>& VeServiceNames();
}

namespace XER::IdeXejso {
namespace {

namespace fs = std::filesystem;
using XER::UI::Layout::Point;
using XER::UI::Layout::Rect;
using XER::UI::Widgets::AppTitleBar;
using XER::UI::Widgets::Button;
using XER::UI::Widgets::ButtonStyle;
using XER::UI::Widgets::Dialog;
using XER::UI::Widgets::Event;
using XER::UI::Widgets::KeyCode;
using XER::UI::Widgets::Label;
using XER::UI::Widgets::MouseButton;
using XER::UI::Widgets::Textarea;
using XER::UI::Widgets::TextInput;

constexpr float kTitleBarH = 38.0f;
constexpr float kToolbarH = 38.0f;
constexpr float kTabsH = 26.0f;
constexpr float kFindH = 30.0f;
constexpr float kLogH = 150.0f;
constexpr float kStatusH = 24.0f;
constexpr float kTreeW = 250.0f;
constexpr float kPreviewW = 430.0f;
constexpr std::size_t kLogCapLines = 4000;
constexpr std::size_t kLogCapChars = 200000;  // log buffer cap

fs::path U8Path(const std::string& utf8) {
    return fs::path(reinterpret_cast<const char8_t*>(utf8.c_str()));
}

std::string LowerCopy(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(
            static_cast<unsigned char>(c)));
    }
    return s;
}

std::string LeafName(const std::string& rel) {
    const auto slash = rel.find_last_of('/');
    return slash == std::string::npos ? rel : rel.substr(slash + 1);
}

}  // namespace

IdeAppWidget::IdeAppWidget(void* parent_hwnd, std::string project_dir)
    : hwnd_(parent_hwnd), project_dir_(std::move(project_dir)) {
    EnsureChildren();
    if (!project_dir_.empty()) {
        OpenProject(project_dir_);
    } else {
        SetStatus("no project — File > Open Folder or New Extension");
    }
}

// ---- construction -------------------------------------------------------

void IdeAppWidget::EnsureChildren() {
    if (children_ready_) return;
    children_ready_ = true;

    title_bar_ = std::make_shared<AppTitleBar>("IDE");
    AddChild(title_bar_);
    BuildTitleBarMenus();

    const auto make_btn = [this](const char* text, ButtonStyle style,
                                 std::function<void()> fn,
                                 const char* tip) {
        auto b = std::make_shared<Button>(text, style);
        b->SetOnClick(std::move(fn));
        if (tip != nullptr) b->SetTooltip(tip);
        AddChild(b);
        return b;
    };
    btn_new_ext_ = make_btn("New Ext", ButtonStyle::Secondary,
        [this] { NewExtension(); }, "Scaffold a new .xexrj project");
    btn_open_ = make_btn("Open…", ButtonStyle::Secondary,
        [this] { OpenFolder(); }, "Open an extension source folder");
    btn_new_file_ = make_btn("+File", ButtonStyle::Ghost,
        [this] { NewFile(); }, "New file in the project");
    btn_new_dir_ = make_btn("+Dir", ButtonStyle::Ghost,
        [this] { NewFolder(); }, "New folder in the project");
    btn_save_ = make_btn("Save", ButtonStyle::Secondary,
        [this] { SaveCurrent(); }, "Save buffer (Ctrl+S)");
    btn_format_ = make_btn("Format", ButtonStyle::Ghost,
        [this] { FormatCurrent(); },
        "Format the current file (Ctrl+Shift+F)");
    btn_build_ = make_btn("Build", ButtonStyle::Secondary,
        [this] { DoBuild(); }, "Pack the project into a .xexrj (ztzmake)");
    btn_test_ = make_btn("Test", ButtonStyle::Secondary,
        [this] { DoTest(); }, "ext_load + probe in VE.ExtensionsRuntime");
    btn_next_err_ = make_btn("Err⤳", ButtonStyle::Ghost,
        [this] { JumpToNextError(); },
        "Jump to the next build error in the log");
    btn_editor_ = make_btn("Run in Editor", ButtonStyle::Primary,
        [this] { DoOpenInEditor(); },
        "Deploy to assets/extensions and launch video_editor.exe");
    btn_preview_ = make_btn("Preview", ButtonStyle::Ghost,
        [this] { TogglePreview(); },
        "Live CEF preview (auto-reloads on save)");

    preview_ = std::shared_ptr<IdePreviewPanel>(
        IdePreviewPanel::Create());
    preview_->SetParentHwnd(hwnd_);
    preview_->SetVisible(false);
    AddChild(preview_);

    file_tree_ = std::make_shared<FileTreeWidget>();
    file_tree_->SetOnActivate([this](const std::string& rel) {
        OpenFile(rel);
    });
    AddChild(file_tree_);

    editor_ = std::make_shared<CodeEditorWidget>();
    editor_->SetPlaceholder(
        "open a project, then click a file in the tree");
    editor_->SetOnChanged([this](const std::string&) {
        if (tabs_.active >= 0) {
            tabs_.SetDirty(tabs_.active, true);
        }
        UpdateTitle();
        UpdateStatusMeta();
        if (find_open_) UpdateFindMatches();
    });
    editor_->SetOnCaretChanged([this](int, int) {
        UpdateStatusMeta();
    });
    AddChild(editor_);

    // Find bar — hidden until Ctrl+F.
    find_input_ = std::make_shared<TextInput>();
    find_input_->SetPlaceholder("find in file (Enter=next, Esc=close)");
    find_input_->SetOnValueChanged(
        [this](const std::string&) { UpdateFindMatches(); });
    find_input_->SetOnSubmit([this] { FindStep(1); });
    find_input_->SetVisible(false);
    AddChild(find_input_);
    const auto find_btn = [this](const char* text,
                                 std::function<void()> fn) {
        auto b = std::make_shared<Button>(text, ButtonStyle::Ghost);
        b->SetOnClick(std::move(fn));
        b->SetVisible(false);
        AddChild(b);
        return b;
    };
    find_prev_ = find_btn("‹", [this] { FindStep(-1); });
    find_next_ = find_btn("›", [this] { FindStep(1); });
    find_close_ = find_btn("×", [this] { CloseFindBar(); });
    find_count_ = std::make_shared<Label>("", true);
    find_count_->SetVisible(false);
    AddChild(find_count_);

    log_view_ = std::make_shared<Textarea>();
    log_view_->SetReadOnly(true);
    log_view_->SetPlaceholder("build / test output lands here");
    AddChild(log_view_);

    status_ = std::make_shared<Label>("", true);
    AddChild(status_);
    meta_label_ = std::make_shared<Label>("", true);
    AddChild(meta_label_);
    path_label_ = std::make_shared<Label>("", true);
    AddChild(path_label_);
}

void IdeAppWidget::InstallShortcuts() {
    if (shortcuts_installed_ || GetTree() == nullptr) return;
    shortcuts_installed_ = true;
    GetTree()->SetKeyHandler([this](const Event& e) {
        if (e.type == Event::Type::KeyDown && e.is_ctrl) {
            switch (e.key) {
                case KeyCode::S:
                    SaveCurrent();
                    return true;
                case KeyCode::F:
                    if (e.is_shift) {
                        FormatCurrent();
                    } else {
                        ToggleFindBar();
                    }
                    return true;
                case KeyCode::O:
                    OpenFolder();
                    return true;
                default:
                    break;
            }
        }
        if (e.type == Event::Type::KeyDown &&
            e.key == KeyCode::Escape && find_open_) {
            CloseFindBar();
            return true;
        }
        return false;
    });
}

void IdeAppWidget::BuildTitleBarMenus() {
    using Menu = AppTitleBar::Menu;
    using Item = AppTitleBar::MenuItem;
    std::vector<Menu> menus;

    Menu file;
    file.label = "File";
    file.mnemonic = 'F';
    file.items = {
        Item{"New Extension…", "", [this] { NewExtension(); }},
        Item{"Open Folder…", "Ctrl+O", [this] { OpenFolder(); }},
        Item{"New File…", "", [this] { NewFile(); }, true, false,
             false, true},
        Item{"New Folder…", "", [this] { NewFolder(); }},
        Item{"Save", "Ctrl+S", [this] { SaveCurrent(); }, true, false,
             false, true},
        Item{"Exit", "", [this] {
                 if (on_quit_) on_quit_();
             }, true, false, false, true},
    };
    menus.push_back(std::move(file));

    Menu edit;
    edit.label = "Edit";
    edit.mnemonic = 'E';
    edit.items = {
        Item{"Find…", "Ctrl+F", [this] { ToggleFindBar(); }},
        Item{"Find Next", "", [this] { FindStep(1); }},
        Item{"Format File", "Ctrl+Shift+F",
             [this] { FormatCurrent(); }, true, false, false, true},
        Item{"Jump to Next Error", "",
             [this] { JumpToNextError(); }},
        Item{"ve.* API Reference…", "",
             [this] { ShowApiReference(); }, true, false, false, true},
        Item{"Insert ext.ve() Snippet…", "",
             [this] { InsertVeSnippet(); }},
    };
    menus.push_back(std::move(edit));

    Menu ext;
    ext.label = "Extension";
    ext.mnemonic = 'x';
    ext.items = {
        Item{"Build .xexrj", "", [this] { DoBuild(); }},
        Item{"Test in Runtime", "", [this] { DoTest(); }},
        Item{"Run in VideoEditor", "", [this] { DoOpenInEditor(); },
             true, false, false, true},
        Item{"Toggle HTML Preview", "", [this] { TogglePreview(); }},
    };
    menus.push_back(std::move(ext));

    Menu help;
    help.label = "Help";
    help.mnemonic = 'H';
    help.items = {
        Item{"About IDEXEJSO", "", [this] { ShowAbout(); }},
    };
    menus.push_back(std::move(help));

    title_bar_->SetMenus(std::move(menus));
    title_bar_->SetCenterCommand(
        "extension id / command…",
        [this] { NewExtension(); }, false);
}

// ---- layout / render ------------------------------------------------------

void IdeAppWidget::Layout(const Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const float w = bounds.width;

    title_bar_->Layout({bounds.x, bounds.y, w, kTitleBarH});

    const float tb_y = bounds.y + kTitleBarH;
    const std::vector<std::pair<std::shared_ptr<Button>, float>> row = {
        {btn_new_ext_, 78.0f}, {btn_open_, 72.0f},
        {btn_new_file_, 56.0f}, {btn_new_dir_, 52.0f},
        {btn_save_, 56.0f},     {btn_format_, 64.0f},
        {btn_build_, 60.0f},    {btn_test_, 56.0f},
        {btn_next_err_, 56.0f}, {btn_editor_, 108.0f},
        {btn_preview_, 68.0f},
    };
    float x = bounds.x + 8.0f;
    for (const auto& [btn, bw] : row) {
        btn->Layout({x, tb_y + 5.0f, bw, kToolbarH - 10.0f});
        x += bw + 6.0f;
    }
    path_label_->Layout(
        {x + 6.0f, tb_y + 9.0f, bounds.Right() - x - 14.0f, 20.0f});

    // Tab strip across the top of the editor area (right of the tree).
    float body_y = tb_y + kToolbarH;
    const bool has_tabs = tabs_.active >= 0;
    const float tabs_y = body_y;

    // Fixed-width tabs: ~7px/char capped, + close affordance. Kept in
    // sync with Render, which draws what Layout measured.
    tab_rects_.assign(tabs_.tabs.size(), {});
    tab_close_rects_.assign(tabs_.tabs.size(), {});
    if (has_tabs) {
        float tx = bounds.x + kTreeW;
        for (std::size_t i = 0; i < tabs_.tabs.size(); ++i) {
            const std::string leaf =
                LeafName(tabs_.tabs[i].rel_path);
            const float tw = std::min(
                190.0f,
                34.0f + static_cast<float>(leaf.size()) * 7.0f);
            const Rect tr{tx, tabs_y, tw, kTabsH};
            tab_rects_[i] = tr;
            tab_close_rects_[i] =
                {tr.Right() - 20.0f, tr.y + 5.0f, 16.0f,
                 kTabsH - 10.0f};
            tx += tw;
            if (tx > bounds.Right()) break;
        }
        body_y += kTabsH;
    }

    // Find bar under the tab strip.
    if (find_open_) {
        const float fy = body_y;
        find_input_->Layout({bounds.x + kTreeW + 4.0f, fy + 3.0f,
                             260.0f, kFindH - 6.0f});
        float fx = bounds.x + kTreeW + 270.0f;
        find_prev_->Layout({fx, fy + 3.0f, 28.0f, kFindH - 6.0f});
        fx += 30.0f;
        find_next_->Layout({fx, fy + 3.0f, 28.0f, kFindH - 6.0f});
        fx += 30.0f;
        find_close_->Layout({fx, fy + 3.0f, 28.0f, kFindH - 6.0f});
        fx += 34.0f;
        find_count_->Layout(
            {fx, fy + 5.0f, bounds.Right() - fx - 8.0f, 20.0f});
        body_y += kFindH;
    }

    const float body_h =
        bounds.y + bounds.height - kLogH - kStatusH - body_y;
    file_tree_->Layout(
        {bounds.x, tb_y + kToolbarH, kTreeW,
         bounds.height - kTitleBarH - kToolbarH - kLogH - kStatusH});
    const float preview_w =
        (preview_ && preview_->IsVisible()) ? kPreviewW : 0.0f;
    editor_->Layout(
        {bounds.x + kTreeW, body_y, w - kTreeW - preview_w, body_h});
    if (preview_w > 0.0f) {
        preview_->Layout(
            {bounds.x + w - preview_w, body_y, preview_w, body_h});
    }
    log_view_->Layout(
        {bounds.x, body_y + body_h, w, kLogH});
    status_->Layout(
        {bounds.x + 10.0f, bounds.y + bounds.height - kStatusH + 2.0f,
         w - 260.0f, kStatusH - 4.0f});
    meta_label_->Layout(
        {bounds.Right() - 248.0f,
         bounds.y + bounds.height - kStatusH + 2.0f, 240.0f,
         kStatusH - 4.0f});
}

void IdeAppWidget::Render(XER::UI::Rendering::Renderer& renderer,
                          const XER::UI::Theme::Tokens& tokens) {
    InstallShortcuts();
    const Rect b = GetBounds();
    renderer.FillRect(b, tokens.background, 0.0f);

    // Mirror the log view's line metrics for click hit-testing (its
    // Textarea pads by spacing_small and rows are font_body * 1.4).
    log_pad_ = tokens.spacing_small;
    log_line_h_ = tokens.font_body * 1.4f;

    // Toolbar separator + log/status separators.
    const float tb = b.y + kTitleBarH + kToolbarH;
    renderer.FillRect({b.x, tb - 1.0f, b.width, 1.0f}, tokens.border,
                      0.0f);
    const float log_top = b.y + b.height - kLogH - kStatusH;
    renderer.FillRect({b.x, log_top, b.width, 1.0f}, tokens.border,
                      0.0f);
    renderer.FillRect({b.x, b.y + b.height - kStatusH, b.width, 1.0f},
                      tokens.border, 0.0f);

    // ---- tab strip -----------------------------------------------------
    if (!tab_rects_.empty()) {
        const Rect strip{b.x + kTreeW, tb, b.width - kTreeW, kTabsH};
        renderer.FillRect(strip, tokens.panel, 0.0f);
        renderer.FillRect({strip.x, strip.Bottom() - 1.0f, strip.width,
                           1.0f},
                          tokens.border, 0.0f);
        for (std::size_t i = 0; i < tab_rects_.size(); ++i) {
            const Rect& tr = tab_rects_[i];
            const bool act = static_cast<int>(i) == tabs_.active;
            const bool hov = static_cast<int>(i) == hovered_tab_;
            renderer.FillRect(
                tr, act ? tokens.surface
                        : (hov ? tokens.hover : tokens.panel),
                0.0f);
            if (act) {
                renderer.FillRect(
                    {tr.x, tr.Bottom() - 2.0f, tr.width, 2.0f},
                    tokens.accent, 0.0f);
            }
            const std::string leaf =
                LeafName(tabs_.tabs[i].rel_path);
            renderer.DrawText(
                leaf,
                {tr.x + 10.0f, tr.y + 5.0f, tr.width - 44.0f,
                 tr.height - 8.0f},
                {act ? tokens.foreground : tokens.muted,
                 tokens.font_small, act ? 600 : 400});
            // Dirty dot.
            if (tabs_.tabs[i].dirty) {
                renderer.FillRect(
                    {tr.Right() - 30.0f,
                     tr.y + tr.height * 0.5f - 3.0f, 6.0f, 6.0f},
                    tokens.accent, 3.0f);
            }
            // Close "×" — shown on the active tab and on hover, the
            // same understated chrome the Tabs widget uses.
            if (act || hov) {
                const Rect& cr = tab_close_rects_[i];
                renderer.DrawText(
                    "×",
                    {cr.x, cr.y - 1.0f, cr.width, cr.height},
                    {hov ? tokens.foreground : tokens.muted,
                     tokens.font_body, 400});
            }
            // Hairline between tabs (VS Code-style separators).
            if (i + 1 < tab_rects_.size()) {
                XER::UI::Theme::Color sep = tokens.border;
                sep.a *= 0.6f;
                renderer.FillRect(
                    {tr.Right() - 0.5f, tr.y + 6.0f, 1.0f,
                     tr.height - 12.0f},
                    sep, 0.0f);
            }
        }
    }

}

bool IdeAppWidget::HandleEvent(const Event& event) {
    switch (event.type) {
        case Event::Type::MouseMove:
            if (!tab_rects_.empty() &&
                event.position.y <=
                    GetBounds().y + kTitleBarH + kToolbarH + kTabsH) {
                int hov = -1;
                for (std::size_t i = 0; i < tab_rects_.size(); ++i) {
                    if (tab_rects_[i].Contains(event.position)) {
                        hov = static_cast<int>(i);
                        break;
                    }
                }
                if (hov != hovered_tab_) {
                    hovered_tab_ = hov;
                    Invalidate();
                }
            } else if (hovered_tab_ != -1) {
                hovered_tab_ = -1;
                Invalidate();
            }
            return false;
        case Event::Type::MouseDown: {
            // Middle-click closes a tab.
            if (event.mouse_button == MouseButton::Middle) {
                for (std::size_t i = 0; i < tab_rects_.size(); ++i) {
                    if (tab_rects_[i].Contains(event.position)) {
                        CloseTab(static_cast<int>(i));
                        return true;
                    }
                }
            }
            return false;
        }
        case Event::Type::Click: {
            if (event.mouse_button != MouseButton::Left) return false;
            // Tab strip: close button or activate.
            for (std::size_t i = 0; i < tab_rects_.size(); ++i) {
                if (i < tab_close_rects_.size() &&
                    tab_close_rects_[i].Contains(event.position)) {
                    CloseTab(static_cast<int>(i));
                    return true;
                }
                if (tab_rects_[i].Contains(event.position)) {
                    ActivateTab(static_cast<int>(i));
                    return true;
                }
            }
            // Log view: diagnostic jump.
            if (TryLogJump(event.position)) return true;
            return false;
        }
        default:
            return false;
    }
}

void IdeAppWidget::Tick(double dt_seconds) {
    (void)dt_seconds;
    InstallShortcuts();
}

// ---- dialogs / pickers ------------------------------------------------------

void IdeAppWidget::PromptText(
    const std::string& title, const std::string& placeholder,
    std::function<void(const std::string&)> on_submit) {
    auto input = std::make_shared<TextInput>();
    input->SetPlaceholder(placeholder);
    auto dialog = Dialog::Create(title, input, "Create", "Cancel");
    dialog->SetPanelWidth(460.0f);
    std::weak_ptr<TextInput> weak = input;
    std::weak_ptr<Dialog> weak_dlg = dialog;
    dialog->SetOnClosed(
        [weak_dlg, weak, on_submit = std::move(on_submit)]() mutable {
            const auto dialog = weak_dlg.lock();
            if (dialog == nullptr || !dialog->WasConfirmed()) return;
            if (const auto in = weak.lock()) {
                on_submit(in->GetValue());
            }
        });
    ShowOverlay(dialog);
    input->SetFocused(true);
}

std::string IdeAppWidget::PickFolder() {
#ifndef _WIN32
    return {};
#else
    // SHBrowseForFolder wants COM init on this thread for some shell
    // folders; COINIT_APARTMENTTHREADED is idempotent.
    static const bool coinit = [] {
        return SUCCEEDED(CoInitializeEx(
            nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
    }();
    (void)coinit;
    wchar_t display[MAX_PATH]{};
    BROWSEINFOW bi{};
    bi.hwndOwner = static_cast<HWND>(hwnd_);
    bi.lpszTitle = L"Select extension source folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.pszDisplayName = display;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl == nullptr) return {};
    wchar_t path[MAX_PATH]{};
    const BOOL ok = SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    if (!ok) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0,
                                      nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, path, -1, out.data(), n,
                        nullptr, nullptr);
    out.pop_back();
    return out;
#endif
}

// ---- project state -----------------------------------------------------------

void IdeAppWidget::OpenProject(const std::string& dir) {
    project_dir_ = dir;
    last_xexrj_.clear();
    tabs_ = EditorTabs{};
    log_lines_.clear();
    diag_cursor_ = 0;
    file_tree_->SetRoot(dir);
    file_tree_->SetSelected("");
    // Open the entry point up front — the file everyone edits first.
    std::string manifest;
    ManifestInfo mi;
    if (ReadTextFile(dir + "/manifest.toml", &manifest, nullptr) &&
        ParseManifestLite(manifest, &mi) && !mi.entry.empty()) {
        OpenFile(mi.entry);
    } else {
        editor_->SetText("");
    }
    UpdateTitle();
    UpdateStatusMeta();
    SetStatus("project: " + dir);
    Log("opened " + dir);
}

void IdeAppWidget::ReloadTree() {
    file_tree_->SetRoot(project_dir_);
}

// ---- buffers / tabs -------------------------------------------------------------

std::string IdeAppWidget::ActivePath() const {
    if (tabs_.active < 0 ||
        tabs_.active >= static_cast<int>(tabs_.tabs.size())) {
        return {};
    }
    return tabs_.tabs[static_cast<std::size_t>(tabs_.active)].rel_path;
}

bool IdeAppWidget::ActiveDirty() const {
    return tabs_.active >= 0 &&
           tabs_.active < static_cast<int>(tabs_.tabs.size()) &&
           tabs_.tabs[static_cast<std::size_t>(tabs_.active)].dirty;
}

void IdeAppWidget::SyncEditorToTab() {
    if (tabs_.active < 0 ||
        tabs_.active >= static_cast<int>(tabs_.tabs.size())) {
        return;
    }
    auto& tab = tabs_.tabs[static_cast<std::size_t>(tabs_.active)];
    tab.text = editor_->GetText();
    tab.caret_line = editor_->CaretLine();
    tab.caret_col = editor_->CaretCol();
}

void IdeAppWidget::LoadTabIntoEditor(int index) {
    if (index < 0 || index >= static_cast<int>(tabs_.tabs.size())) {
        editor_->SetText("");
        editor_->SetLanguage(EditorLanguage::Plain);
        return;
    }
    const auto& tab = tabs_.tabs[static_cast<std::size_t>(index)];
    editor_->SetText(tab.text);
    editor_->SetLanguage(LanguageForPath(tab.rel_path));
    editor_->SetCaret(tab.caret_line, tab.caret_col);
    file_tree_->SetSelected(tab.rel_path);
    if (find_open_) UpdateFindMatches();
    UpdateTitle();
    UpdateStatusMeta();
}

void IdeAppWidget::OpenFile(const std::string& rel_path) {
    if (project_dir_.empty()) return;
    SyncEditorToTab();
    const int idx = tabs_.Open(rel_path);
    auto& tab = tabs_.tabs[static_cast<std::size_t>(idx)];
    if (tab.text.empty() && !tab.dirty) {
        std::string text, error;
        if (!ReadTextFile(project_dir_ + "/" + rel_path, &text,
                          &error)) {
            SetStatus(error, true);
            tabs_.Close(idx);
            return;
        }
        tab.text = std::move(text);
    }
    LoadTabIntoEditor(idx);
    UpdatePreviewFor(rel_path);
    Invalidate();
}

void IdeAppWidget::TogglePreview() {
    if (preview_ == nullptr) return;
    const bool show = !preview_->IsVisible();
    preview_->SetVisible(show);
    if (show) {
        if (!preview_->Available()) {
            SetStatus("preview unavailable (CEF not initialized)",
                      true);
        }
        UpdatePreviewFor(ActivePath());
    }
    Invalidate();
}

void IdeAppWidget::UpdatePreviewFor(const std::string& rel_path) {
    if (preview_ == nullptr) return;
    if (rel_path.empty()) return;
    const std::string low = LowerCopy(rel_path);
    const bool html = low.size() >= 5 &&
                      (low.ends_with(".html") ||
                       low.ends_with(".htm"));
    if (!html) return;
    // SetFile also (re)arms the directory watcher for live reload.
    preview_->SetVisible(true);
    preview_->SetFile(project_dir_ + "/" + rel_path);
}

void IdeAppWidget::ActivateTab(int index) {
    if (index == tabs_.active) return;
    SyncEditorToTab();
    if (!tabs_.Activate(index)) return;
    LoadTabIntoEditor(tabs_.active);
    Invalidate();
}

void IdeAppWidget::CloseTab(int index) {
    if (index < 0 || index >= static_cast<int>(tabs_.tabs.size())) {
        return;
    }
    // A dirty tab is saved rather than dropped — same "never lose
    // edits" contract the v1 auto-save-on-switch had.
    if (tabs_.tabs[static_cast<std::size_t>(index)].dirty) {
        if (index == tabs_.active) {
            SyncEditorToTab();
        }
        const auto& tab = tabs_.tabs[static_cast<std::size_t>(index)];
        std::string error;
        if (WriteTextFile(project_dir_ + "/" + tab.rel_path, tab.text,
                          &error)) {
            Log("saved " + tab.rel_path + " on tab close");
        } else {
            SetStatus(error, true);
            return;   // keep the tab rather than lose the buffer
        }
    }
    const bool was_active = index == tabs_.active;
    tabs_.Close(index);   // fixes `active` itself
    if (was_active) {
        LoadTabIntoEditor(tabs_.active);
    }
    UpdateTitle();
    UpdateStatusMeta();
    Invalidate();
}

bool IdeAppWidget::SaveCurrent() {
    const std::string rel = ActivePath();
    if (rel.empty() || project_dir_.empty()) {
        SetStatus("nothing to save");
        return false;
    }
    SyncEditorToTab();
    auto& tab = tabs_.tabs[static_cast<std::size_t>(tabs_.active)];
    std::string error;
    if (!WriteTextFile(project_dir_ + "/" + rel, tab.text, &error)) {
        SetStatus(error, true);
        return false;
    }
    tab.dirty = false;
    UpdateTitle();
    UpdateStatusMeta();
    SetStatus("saved " + rel);
    return true;
}

void IdeAppWidget::UpdateTitle() {
    // Path strip on the toolbar: project dir + buffer + dirty marker.
    const std::string rel = ActivePath();
    path_label_->SetText(
        project_dir_.empty()
            ? std::string()
            : project_dir_ +
                  (rel.empty()
                       ? ""
                       : "  —  " + rel +
                             (ActiveDirty() ? " *" : "")));
}

void IdeAppWidget::UpdateStatusMeta() {
    if (meta_label_ == nullptr) return;
    if (tabs_.active < 0) {
        meta_label_->SetText("");
        return;
    }
    const EditorLanguage lang = editor_->Language();
    meta_label_->SetText(
        "Ln " + std::to_string(editor_->CaretLine() + 1) +
        ", Col " + std::to_string(editor_->CaretCol() + 1) +
        "   " + LanguageName(lang) +
        (ActiveDirty() ? "   •modified" : ""));
}

// ---- find bar ------------------------------------------------------------------

void IdeAppWidget::ToggleFindBar() {
    find_open_ = !find_open_;
    find_input_->SetVisible(find_open_);
    find_prev_->SetVisible(find_open_);
    find_next_->SetVisible(find_open_);
    find_close_->SetVisible(find_open_);
    find_count_->SetVisible(find_open_);
    if (find_open_) {
        find_input_->SetFocused(true);
        UpdateFindMatches();
    } else {
        editor_->SetSearchMatches({});
        editor_->SetFocused(true);
    }
    Invalidate();
}

void IdeAppWidget::CloseFindBar() {
    if (!find_open_) return;
    find_open_ = false;
    find_input_->SetVisible(false);
    find_prev_->SetVisible(false);
    find_next_->SetVisible(false);
    find_close_->SetVisible(false);
    find_count_->SetVisible(false);
    editor_->SetSearchMatches({});
    editor_->SetFocused(true);
    Invalidate();
}

void IdeAppWidget::UpdateFindMatches() {
    find_matches_.clear();
    find_index_ = -1;
    const std::string query = find_input_->GetValue();
    if (!query.empty()) {
        const std::string needle = LowerCopy(query);
        const auto& lines = editor_->Lines();
        for (std::size_t li = 0; li < lines.size(); ++li) {
            const std::string hay = LowerCopy(lines[li]);
            std::size_t pos = 0;
            while ((pos = hay.find(needle, pos)) !=
                   std::string::npos) {
                find_matches_.push_back(
                    {static_cast<int>(li), static_cast<int>(pos),
                     static_cast<int>(query.size())});
                pos += std::max<std::size_t>(1, needle.size());
            }
        }
        if (!find_matches_.empty()) find_index_ = 0;
    }
    editor_->SetSearchMatches(find_matches_, find_index_);
    if (find_matches_.empty()) {
        find_count_->SetText(query.empty() ? "" : "no matches");
    } else {
        find_count_->SetText(
            std::to_string(find_index_ + 1) + "/" +
            std::to_string(find_matches_.size()));
    }
    if (find_index_ >= 0) {
        const SearchMatch& m =
            find_matches_[static_cast<std::size_t>(find_index_)];
        editor_->SelectRange(m.line, m.col, m.len);
    }
}

void IdeAppWidget::FindStep(int dir) {
    if (find_matches_.empty()) {
        UpdateFindMatches();
        if (find_matches_.empty()) return;
    }
    const int n = static_cast<int>(find_matches_.size());
    find_index_ = ((find_index_ < 0 ? 0 : find_index_) + dir + n) % n;
    editor_->SetSearchMatches(find_matches_, find_index_);
    const SearchMatch& m =
        find_matches_[static_cast<std::size_t>(find_index_)];
    editor_->SelectRange(m.line, m.col, m.len);
    find_count_->SetText(std::to_string(find_index_ + 1) + "/" +
                         std::to_string(n));
}

// ---- format / navigate ------------------------------------------------------------

void IdeAppWidget::FormatCurrent() {
    const std::string rel = ActivePath();
    if (rel.empty()) {
        SetStatus("no buffer to format");
        return;
    }
    const int caret_line = editor_->CaretLine();
    std::string out, engine, error;
    if (!FormatSource(rel, editor_->GetText(), &out, &engine,
                      &error)) {
        if (error.empty()) error = "format failed";
        SetStatus(error, true);
        Log("format: " + error);
        return;
    }
    if (!error.empty()) {
        // Partial result (incomplete format) — still applied, but the
        // note must surface instead of failing silently.
        Log("format: " + error);
    }
    if (out == editor_->GetText()) {
        SetStatus("already formatted (" + engine + ")");
        return;
    }
    editor_->SetText(std::move(out));
    editor_->GotoLine(caret_line);
    if (tabs_.active >= 0) {
        tabs_.SetDirty(tabs_.active, true);
    }
    UpdateTitle();
    UpdateStatusMeta();
    SetStatus("formatted (" + engine + ")");
    Log("format: " + rel + " formatted (" + engine + ")");
}

std::string IdeAppWidget::ResolveDiagPath(const std::string& path) const {
    if (project_dir_.empty() || path.empty()) return {};
    std::string norm = path;
    std::replace(norm.begin(), norm.end(), '\\', '/');
    std::string root = project_dir_;
    std::replace(root.begin(), root.end(), '\\', '/');
    // Absolute inside the project?
    if (norm.size() > root.size() &&
        norm.compare(0, root.size(), root) == 0 &&
        norm[root.size()] == '/') {
        return norm.substr(root.size() + 1);
    }
    // Project-relative as written?
    std::error_code ec;
    if (fs::is_regular_file(U8Path(project_dir_) / U8Path(norm),
                            ec)) {
        return norm;
    }
    // Fall back to matching by suffix/filename in the tree listing.
    const auto items = ListProjectFiles(project_dir_);
    const std::string leaf = LeafName(norm);
    for (const auto& it : items) {
        if (it.is_dir) continue;
        if (it.rel_path == norm ||
            (it.rel_path.size() >= norm.size() &&
             it.rel_path.compare(it.rel_path.size() - norm.size(),
                                 norm.size(), norm) == 0 &&
             it.rel_path[it.rel_path.size() - norm.size()] == '/') ||
            LeafName(it.rel_path) == leaf) {
            return it.rel_path;
        }
    }
    return {};
}

void IdeAppWidget::JumpTo(const std::string& rel, int line, int col) {
    if (rel.empty()) return;
    OpenFile(rel);
    editor_->GotoLine(std::max(0, line - 1), std::max(0, col - 1));
    editor_->SetFocused(true);
    SetStatus("jumped to " + rel + ":" + std::to_string(line));
}

void IdeAppWidget::JumpToNextError() {
    if (log_lines_.empty()) {
        SetStatus("no build output yet");
        return;
    }
    // Walk newest-first, wrapping; errors preferred but any file:line
    // reference counts as a jumpable diagnostic.
    const int n = static_cast<int>(log_lines_.size());
    for (int step = 0; step < n; ++step) {
        const int idx = (diag_cursor_ + step) % n;
        DiagnosticRef d;
        if (!ParseDiagnostic(log_lines_[static_cast<std::size_t>(idx)],
                             &d)) {
            continue;
        }
        diag_cursor_ = (idx + 1) % n;
        const std::string rel = ResolveDiagPath(d.path);
        if (rel.empty()) {
            SetStatus("error points outside the project: " + d.path,
                      true);
            return;
        }
        JumpTo(rel, d.line, d.col);
        return;
    }
    SetStatus("no file:line diagnostics in the log");
}

bool IdeAppWidget::TryLogJump(Point position) {
    const Rect lb = log_view_->GetBounds();
    if (!lb.Contains(position)) return false;
    const int row = static_cast<int>(
        (position.y - lb.y - log_pad_) / log_line_h_);
    if (row < 0 || row >= static_cast<int>(log_lines_.size())) {
        return false;
    }
    DiagnosticRef d;
    if (!ParseDiagnostic(log_lines_[static_cast<std::size_t>(row)],
                         &d)) {
        return false;
    }
    const std::string rel = ResolveDiagPath(d.path);
    if (rel.empty()) {
        SetStatus("no such file in project: " + d.path, true);
        return true;
    }
    JumpTo(rel, d.line, d.col);
    return true;
}

// ---- actions -------------------------------------------------------------------

void IdeAppWidget::NewExtension() {
    PromptText("New Extension — id (e.g. my_fx)", "extension id",
        [this](const std::string& name) {
            if (name.empty()) {
                SetStatus("new extension cancelled — empty id");
                return;
            }
            const std::string parent = PickFolder();
            if (parent.empty()) {
                SetStatus("new extension cancelled — no folder");
                return;
            }
            std::string error;
            const std::string full = parent + "/" + name;
            if (!ScaffoldExtension(full, name, name, true, &error)) {
                SetStatus(error, true);
                Log("scaffold failed: " + error);
                return;
            }
            Log("scaffolded " + full);
            OpenProject(full);
        });
}

void IdeAppWidget::OpenFolder() {
    const std::string dir = PickFolder();
    if (dir.empty()) return;
    std::error_code ec;
    if (!fs::is_regular_file(U8Path(dir) / "manifest.toml", ec)) {
        SetStatus("no manifest.toml in " + dir +
                  " — not an extension project", true);
    }
    OpenProject(dir);
}

void IdeAppWidget::NewFile() {
    if (project_dir_.empty()) {
        SetStatus("open a project first", true);
        return;
    }
    PromptText("New File — path relative to project root",
        "scripts/new.js",
        [this](const std::string& rel) {
            if (rel.empty()) return;
            std::string error;
            if (!WriteTextFile(project_dir_ + "/" + rel, "", &error)) {
                SetStatus(error, true);
                return;
            }
            ReloadTree();
            OpenFile(rel);
        });
}

void IdeAppWidget::NewFolder() {
    if (project_dir_.empty()) {
        SetStatus("open a project first", true);
        return;
    }
    PromptText("New Folder — path relative to project root",
        "assets",
        [this](const std::string& rel) {
            if (rel.empty()) return;
            std::error_code ec;
            fs::create_directories(U8Path(project_dir_) / U8Path(rel),
                                   ec);
            if (ec) {
                SetStatus("cannot create " + rel, true);
                return;
            }
            ReloadTree();
        });
}

void IdeAppWidget::DoBuild() {
    if (project_dir_.empty()) {
        SetStatus("open a project first", true);
        return;
    }
    if (ActiveDirty()) SaveCurrent();
    diag_cursor_ = 0;
    SetStatus("building…");
    std::string out;
    if (BuildExtension(project_dir_, &out,
                       [this](const std::string& l) { Log(l); })) {
        last_xexrj_ = out;
        SetStatus("built " + out +
                  " — click a file:line log line to jump");
    } else {
        SetStatus("build failed — see log (click a diagnostic)",
                  true);
    }
}

void IdeAppWidget::DoTest() {
    if (project_dir_.empty()) {
        SetStatus("open a project first", true);
        return;
    }
    if (ActiveDirty()) SaveCurrent();
    // Always rebuild so the test reflects the buffer on disk.
    std::string out;
    SetStatus("building + testing…");
    if (!BuildExtension(project_dir_, &out,
                        [this](const std::string& l) { Log(l); })) {
        SetStatus("test: build failed — see log", true);
        return;
    }
    last_xexrj_ = out;
    // Probe symbol: manifest [pack] probe, else the conventional name.
    std::string probe = "ve_ext_probe";
    std::string manifest;
    ManifestInfo mi;
    if (ReadTextFile(project_dir_ + "/manifest.toml", &manifest,
                     nullptr) &&
        ParseManifestLite(manifest, &mi) && !mi.probe.empty()) {
        probe = mi.probe;
    }
    if (TestExtension(out, probe,
                      [this](const std::string& l) { Log(l); })) {
        SetStatus("test passed");
    } else {
        SetStatus("test failed — see log", true);
    }
}

void IdeAppWidget::DoOpenInEditor() {
    if (last_xexrj_.empty()) {
        if (project_dir_.empty()) {
            SetStatus("open a project first", true);
            return;
        }
        DoBuild();
        if (last_xexrj_.empty()) return;   // build failed
    }
    if (OpenInEditor(last_xexrj_,
                     [this](const std::string& l) { Log(l); })) {
        SetStatus("launched video_editor.exe");
    } else {
        SetStatus("could not launch the editor — see log", true);
    }
}

void IdeAppWidget::ShowAbout() {
    auto text = std::make_shared<Label>(
        "IDEXEJSO — scaffold, edit, pack and test "
        "VideoEditor .xexrj extensions.", true);
    auto dialog = Dialog::Create("About", text, "OK", "");
    ShowOverlay(dialog);
}

void IdeAppWidget::ShowApiReference() {
    // The v3 service table comes straight out of the VE core lib, so
    // the IDE can never drift from what DispatchExtHostCall serves.
    const auto& names = XER::VideoEditor::VeServiceNames();
    std::string text =
        "xer_ext JS API (main.js):  const ext = "
        "NativeImport(\"xer_ext\");\n"
        "  ext.ve(name, args?) -> JSON result string   (alias: "
        "veCall)\n"
        "  ext.nativeCall(id, fn, args?) — fire-and-forget ext_call\n"
        "  ext.log / setStatus / id / listFiles / readFile / openUi\n\n"
        "ve.core v3 services (" + std::to_string(names.size()) + "):\n";
    std::string prefix;
    for (const auto& s : names) {
        const auto dot = s.find('.');
        const std::string head =
            dot == std::string::npos ? s : s.substr(0, dot);
        if (head != prefix) {
            prefix = head;
            text += "\n  " + head + ".*\n";
        }
        text += "    ext.ve(\"" + s + "\", {})\n";
    }
    auto view = std::make_shared<Textarea>(text);
    view->SetReadOnly(true);
    auto dialog =
        Dialog::Create("ve.* API Reference", view, "OK", "");
    dialog->SetPanelWidth(560.0f);
    ShowOverlay(dialog);
}

void IdeAppWidget::InsertVeSnippet() {
    if (tabs_.active < 0) {
        SetStatus("open a file first", true);
        return;
    }
    PromptText("Insert ext.ve() — service name", "timeline.info",
        [this](const std::string& name) {
            if (name.empty()) return;
            editor_->InsertTextAtCaret("ext.ve(\"" + name + "\", {})");
            editor_->SetFocused(true);
        });
}

// ---- log / status -----------------------------------------------------------------

void IdeAppWidget::Log(const std::string& line) {
    // Newest-first: the read-only Textarea can't scroll without a
    // caret, so the freshest line must live at the top.
    log_text_ = line + "\n" + log_text_;
    if (log_text_.size() > kLogCapChars) {
        log_text_.resize(kLogCapChars);
    }
    log_view_->SetText(log_text_);
    log_lines_.push_front(line);
    if (log_lines_.size() > kLogCapLines) {
        log_lines_.pop_back();
    }
}

void IdeAppWidget::SetStatus(const std::string& text, bool is_error) {
    status_->SetText(text);
    status_->SetMuted(!is_error);
    UpdateTitle();
}

}  // namespace XER::IdeXejso
