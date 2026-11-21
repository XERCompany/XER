// ide_app.h — IDEXEJSO root widget: the extension IDE workspace.
//
// Same window/app framework the video editor uses (XER::UI::Core::
// Window + AppTitleBar frameless chrome + WidgetTree dispatch), just
// shaped for .xexrj authoring:
//
//   +-------------------------------------------------------+
//   | AppTitleBar  (File / Extension menus + window ctl)    |
//   +-------------------------------------------------------+
//   | toolbar: New Ext | Open | +File | +Dir | Save | Build |
//   |          Test | Run in Editor                         |
//   +----------+--------------------------------------------+
//   | file     |  editor (Textarea, dirty-tracked, Ctrl+S)  |
//   | tree     |                                            |
//   +----------+--------------------------------------------+
//   | log (Textarea read-only, newest-first)                |
//   +-------------------------------------------------------+
//   | status strip                                          |
//   +-------------------------------------------------------+
#pragma once

#include <functional>
#include <memory>
#include <string>

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
    void OpenFile(const std::string& rel_path);
    bool SaveCurrent();

    void NewExtension();
    void OpenFolder();
    void NewFile();
    void NewFolder();
    void DoBuild();
    void DoTest();
    void DoOpenInEditor();
    void ShowAbout();

    void Log(const std::string& line);
    void SetStatus(const std::string& text, bool is_error = false);
    void UpdateTitle();

    void* hwnd_ = nullptr;
    std::function<void()> on_quit_;

    std::shared_ptr<XER::UI::Widgets::AppTitleBar> title_bar_;
    std::shared_ptr<FileTreeWidget> file_tree_;
    std::shared_ptr<XER::UI::Widgets::Textarea> editor_;
    std::shared_ptr<XER::UI::Widgets::Textarea> log_view_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;
    std::shared_ptr<XER::UI::Widgets::Label> path_label_;
    // Toolbar buttons kept alive as members.
    std::shared_ptr<XER::UI::Widgets::Button> btn_new_ext_;
    std::shared_ptr<XER::UI::Widgets::Button> btn_open_;
    std::shared_ptr<XER::UI::Widgets::Button> btn_new_file_;
    std::shared_ptr<XER::UI::Widgets::Button> btn_new_dir_;
    std::shared_ptr<XER::UI::Widgets::Button> btn_save_;
    std::shared_ptr<XER::UI::Widgets::Button> btn_build_;
    std::shared_ptr<XER::UI::Widgets::Button> btn_test_;
    std::shared_ptr<XER::UI::Widgets::Button> btn_editor_;

    std::string project_dir_;
    std::string current_file_;      // rel path, "" = no buffer
    bool dirty_ = false;
    std::string last_xexrj_;
    std::string log_text_;
    bool shortcuts_installed_ = false;
    bool children_ready_ = false;
};

}  // namespace XER::IdeXejso
