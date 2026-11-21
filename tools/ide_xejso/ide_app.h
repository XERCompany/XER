// ide_app.h — IDEXEJSO root widget: the extension IDE workspace.
//
// Same window/app framework the video editor uses (XER::UI::Core::
// Window + AppTitleBar frameless chrome + WidgetTree dispatch), just
// shaped for .xexrj authoring:
//
//   +-------------------------------------------------------+
//   | AppTitleBar  (File / Edit / Extension menus + ctl)    |
//   +-------------------------------------------------------+
//   | toolbar: New Ext | Open | +File | +Dir | Save | Format|
//   |          Build | Test | NextErr | Run in Editor       |
//   +----------+--------------------------------------------+
//   | file     |  tab strip (dirty dot, close x)            |
//   | tree     |  [find bar when Ctrl+F]                    |
//   |          |  CodeEditorWidget (syntax + caret)         |
//   +----------+--------------------------------------------+
//   | log (Textarea read-only — diagnostics are clickable)  |
//   +-------------------------------------------------------+
//   | status: message ..............  Ln x, Col y | lang |* |
//   +-------------------------------------------------------+
#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "code_editor_widget.h"
#include "file_tree_widget.h"
#include "ide_project.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class AppTitleBar;
class Button;
class Label;
class Textarea;
class TextInput;
}

namespace XER::IdeXejso {

class IdePreviewPanel;

class IdeAppWidget final : public XER::UI::Widgets::Widget {
public:
    // `parent_hwnd` owners the Win32 pickers (SHBrowseForFolder);
    // `project_dir` may be empty (-> "no project" tree).
    IdeAppWidget(void* parent_hwnd, std::string project_dir);
    ~IdeAppWidget() override = default;

    XER::UI::Widgets::AppTitleBar* TitleBar() const {
        return title_bar_.get();
    }
    void SetOnQuit(std::function<void()> callback) {
        on_quit_ = std::move(callback);
    }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {860.0f, 560.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {1280.0f, 820.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    void EnsureChildren();
    void InstallShortcuts();
    void BuildTitleBarMenus();

    // Dialog + single-line TextInput prompt in the overlay layer.
    void PromptText(const std::string& title,
                    const std::string& placeholder,
                    std::function<void(const std::string&)> on_submit);
    // Win32 folder picker (SHBrowseForFolderW); "" when cancelled.
    std::string PickFolder();

    void OpenProject(const std::string& dir);
    void ReloadTree();

    // ---- buffers / tabs ---------------------------------------------------
    // Pushes the editor's text+caret into the active tab record.
    void SyncEditorToTab();
    // Loads tab `index` into the editor (text, language, caret).
    void LoadTabIntoEditor(int index);
    void OpenFile(const std::string& rel_path);
    void ActivateTab(int index);
    void CloseTab(int index);
    bool SaveCurrent();
    std::string ActivePath() const;
    bool ActiveDirty() const;

    // ---- find bar ---------------------------------------------------------
    void ToggleFindBar();
    void CloseFindBar();
    void UpdateFindMatches();
    void FindStep(int dir);   // +1 next, -1 prev

    // ---- format / navigate --------------------------------------------------
    void FormatCurrent();
    // Right-side live preview (CEF): toggled by toolbar/menu; also
    // auto-shows when an .html file opens.
    void TogglePreview();
    void UpdatePreviewFor(const std::string& rel_path);
    // Resolves a diagnostic path to a project-relative path; "" when
    // the file can't be found inside the project.
    std::string ResolveDiagPath(const std::string& path) const;
    void JumpTo(const std::string& rel, int line, int col);
    void JumpToNextError();
    bool TryLogJump(XER::UI::Layout::Point position);

    void NewExtension();
    void OpenFolder();
    void NewFile();
    void NewFolder();
    void DoBuild();
    void DoTest();
    void DoOpenInEditor();
    void ShowAbout();
    void ShowApiReference();
    void InsertVeSnippet();

    void Log(const std::string& line);
    void SetStatus(const std::string& text, bool is_error = false);
    void UpdateTitle();
    void UpdateStatusMeta();

    void* hwnd_ = nullptr;
    std::function<void()> on_quit_;

    std::shared_ptr<XER::UI::Widgets::AppTitleBar> title_bar_;
    std::shared_ptr<FileTreeWidget> file_tree_;
    std::shared_ptr<CodeEditorWidget> editor_;
    std::shared_ptr<XER::UI::Widgets::Textarea> log_view_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;
    std::shared_ptr<XER::UI::Widgets::Label> meta_label_;
    std::shared_ptr<XER::UI::Widgets::Label> path_label_;
    // Find bar (shown/hidden via SetVisible).
    std::shared_ptr<XER::UI::Widgets::TextInput> find_input_;
    std::shared_ptr<XER::UI::Widgets::Button> find_prev_;
    std::shared_ptr<XER::UI::Widgets::Button> find_next_;
    std::shared_ptr<XER::UI::Widgets::Button> find_close_;
    std::shared_ptr<XER::UI::Widgets::Label> find_count_;
    // Toolbar buttons kept alive as members.
    std::shared_ptr<XER::UI::Widgets::Button> btn_new_ext_;
    std::shared_ptr<XER::UI::Widgets::Button> btn_open_;
    std::shared_ptr<XER::UI::Widgets::Button> btn_new_file_;
    std::shared_ptr<XER::UI::Widgets::Button> btn_new_dir_;
    std::shared_ptr<XER::UI::Widgets::Button> btn_save_;
    std::shared_ptr<XER::UI::Widgets::Button> btn_format_;
    std::shared_ptr<XER::UI::Widgets::Button> btn_build_;
    std::shared_ptr<XER::UI::Widgets::Button> btn_test_;
    std::shared_ptr<XER::UI::Widgets::Button> btn_next_err_;
    std::shared_ptr<XER::UI::Widgets::Button> btn_editor_;
    std::shared_ptr<XER::UI::Widgets::Button> btn_preview_;
    std::shared_ptr<IdePreviewPanel> preview_;

    std::string project_dir_;
    EditorTabs tabs_;
    std::vector<XER::UI::Layout::Rect> tab_rects_;
    std::vector<XER::UI::Layout::Rect> tab_close_rects_;
    int hovered_tab_ = -1;
    std::string last_xexrj_;
    std::string log_text_;
    std::deque<std::string> log_lines_;   // newest-first, capped
    int diag_cursor_ = 0;                 // for JumpToNextError
    bool find_open_ = false;
    std::vector<SearchMatch> find_matches_;
    int find_index_ = -1;
    // Log-view metrics mirrored from the last Render's tokens.
    float log_line_h_ = 19.6f;
    float log_pad_ = 8.0f;
    bool shortcuts_installed_ = false;
    bool children_ready_ = false;
};

}  // namespace XER::IdeXejso
