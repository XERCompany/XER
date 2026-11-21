// ide_app.cc — see ide_app.h for the layout/contract.
#include "ide_app.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

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
#endif

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
constexpr float kLogH = 150.0f;
constexpr float kStatusH = 24.0f;
constexpr float kTreeW = 250.0f;
constexpr std::size_t kLogCapChars = 200000;  // log buffer cap

fs::path U8Path(const std::string& utf8) {
    return fs::path(reinterpret_cast<const char8_t*>(utf8.c_str()));
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
    btn_build_ = make_btn("Build", ButtonStyle::Secondary,
        [this] { DoBuild(); }, "Pack the project into a .xexrj (ztzmake)");
    btn_test_ = make_btn("Test", ButtonStyle::Secondary,
        [this] { DoTest(); }, "ext_load + probe in VE.ExtensionsRuntime");
    btn_editor_ = make_btn("Run in Editor", ButtonStyle::Primary,
        [this] { DoOpenInEditor(); },
        "Deploy to assets/extensions and launch video_editor.exe");

    file_tree_ = std::make_shared<FileTreeWidget>();
    file_tree_->SetOnActivate([this](const std::string& rel) {
        OpenFile(rel);
    });
    AddChild(file_tree_);

    editor_ = std::make_shared<Textarea>();
    editor_->SetPlaceholder(
        "open a project, then click a file in the tree");
    editor_->SetOnChanged([this](const std::string&) {
        dirty_ = true;
        UpdateTitle();
    });
    AddChild(editor_);

    log_view_ = std::make_shared<Textarea>();
    log_view_->SetReadOnly(true);
    log_view_->SetPlaceholder("build / test output lands here");
    AddChild(log_view_);

    status_ = std::make_shared<Label>("", true);
    AddChild(status_);
    path_label_ = std::make_shared<Label>("", true);
    AddChild(path_label_);
}

void IdeAppWidget::InstallShortcuts() {
    if (shortcuts_installed_ || GetTree() == nullptr) return;
    shortcuts_installed_ = true;
    GetTree()->SetKeyHandler([this](const Event& e) {
        if (e.type == Event::Type::KeyDown && e.is_ctrl &&
            e.key == KeyCode::S) {
            SaveCurrent();
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

    Menu ext;
    ext.label = "Extension";
    ext.mnemonic = 'E';
    ext.items = {
        Item{"Build .xexrj", "", [this] { DoBuild(); }},
        Item{"Test in Runtime", "", [this] { DoTest(); }},
        Item{"Run in VideoEditor", "", [this] { DoOpenInEditor(); },
             true, false, false, true},
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
        {btn_save_, 56.0f},     {btn_build_, 60.0f},
        {btn_test_, 56.0f},     {btn_editor_, 108.0f},
    };
    float x = bounds.x + 8.0f;
    for (const auto& [btn, bw] : row) {
        btn->Layout({x, tb_y + 5.0f, bw, kToolbarH - 10.0f});
        x += bw + 6.0f;
    }
    path_label_->Layout(
        {x + 6.0f, tb_y + 9.0f, bounds.Right() - x - 14.0f, 20.0f});

    const float body_y = tb_y + kToolbarH;
    const float body_h =
        bounds.height - kTitleBarH - kToolbarH - kLogH - kStatusH;
    file_tree_->Layout({bounds.x, body_y, kTreeW, body_h});
    editor_->Layout(
        {bounds.x + kTreeW, body_y, w - kTreeW, body_h});
    log_view_->Layout(
        {bounds.x, body_y + body_h, w, kLogH});
    status_->Layout(
        {bounds.x + 10.0f, bounds.y + bounds.height - kStatusH + 2.0f,
         w - 20.0f, kStatusH - 4.0f});
}

void IdeAppWidget::Render(XER::UI::Rendering::Renderer& renderer,
                          const XER::UI::Theme::Tokens& tokens) {
    InstallShortcuts();
    const Rect b = GetBounds();
    renderer.FillRect(b, tokens.background, 0.0f);
    // Toolbar separator + log/status separators.
    const float tb = b.y + kTitleBarH + kToolbarH;
    renderer.FillRect({b.x, tb - 1.0f, b.width, 1.0f}, tokens.border,
                      0.0f);
    const float log_top = b.y + b.height - kLogH - kStatusH;
    renderer.FillRect({b.x, log_top, b.width, 1.0f}, tokens.border,
                      0.0f);
    renderer.FillRect({b.x, b.y + b.height - kStatusH, b.width, 1.0f},
                      tokens.border, 0.0f);
}

bool IdeAppWidget::HandleEvent(const Event& event) {
    // Bubbled clicks land here when no child consumed them — nothing
    // to do; keyboard shortcuts ride the tree key handler.
    (void)event;
    return false;
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
    current_file_.clear();
    dirty_ = false;
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
    SetStatus("project: " + dir);
    Log("opened " + dir);
}

void IdeAppWidget::ReloadTree() {
    file_tree_->SetRoot(project_dir_);
}

void IdeAppWidget::OpenFile(const std::string& rel_path) {
    if (project_dir_.empty()) return;
    // v1: auto-save the outgoing buffer (documented behavior — the
    // alternative needs a 3-way save/discard/cancel dialog).
    if (dirty_ && !current_file_.empty() &&
        current_file_ != rel_path) {
        SaveCurrent();
    }
    std::string text, error;
    if (!ReadTextFile(project_dir_ + "/" + rel_path, &text, &error)) {
        SetStatus(error, true);
        return;
    }
    current_file_ = rel_path;
    dirty_ = false;
    file_tree_->SetSelected(rel_path);
    editor_->SetText(std::move(text));
    UpdateTitle();
}

bool IdeAppWidget::SaveCurrent() {
    if (current_file_.empty() || project_dir_.empty()) {
        SetStatus("nothing to save");
        return false;
    }
    std::string error;
    if (!WriteTextFile(project_dir_ + "/" + current_file_,
                       editor_->GetText(), &error)) {
        SetStatus(error, true);
        return false;
    }
    dirty_ = false;
    UpdateTitle();
    SetStatus("saved " + current_file_);
    return true;
}

void IdeAppWidget::UpdateTitle() {
    // Path strip on the toolbar: project dir + buffer + dirty marker.
    path_label_->SetText(
        project_dir_.empty()
            ? std::string()
            : project_dir_ +
                  (current_file_.empty()
                       ? ""
                       : "  —  " + current_file_ +
                             (dirty_ ? " *" : "")));
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
    if (dirty_) SaveCurrent();
    SetStatus("building…");
    std::string out;
    if (BuildExtension(project_dir_, &out,
                       [this](const std::string& l) { Log(l); })) {
        last_xexrj_ = out;
        SetStatus("built " + out);
    } else {
        SetStatus("build failed — see log", true);
    }
}

void IdeAppWidget::DoTest() {
    if (project_dir_.empty()) {
        SetStatus("open a project first", true);
        return;
    }
    if (dirty_) SaveCurrent();
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

// ---- log / status -----------------------------------------------------------------

void IdeAppWidget::Log(const std::string& line) {
    // Newest-first: the read-only Textarea can't scroll without a
    // caret, so the freshest line must live at the top.
    log_text_ = line + "\n" + log_text_;
    if (log_text_.size() > static_cast<std::size_t>(kLogCapChars)) {
        log_text_.resize(static_cast<std::size_t>(kLogCapChars));
    }
    log_view_->SetText(log_text_);
}

void IdeAppWidget::SetStatus(const std::string& text, bool is_error) {
    status_->SetText(text);
    status_->SetMuted(!is_error);
    UpdateTitle();
}

}  // namespace XER::IdeXejso
