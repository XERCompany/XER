#include "app_ui.h"

#include <cstdint>
#include <functional>
#include <memory>

#include "app/crash_handler.h"
#include "node_graph_app_nodes.h"
#include "ui/core/window.h"
#include "ui/core/application.h"
#include "ui/widgets/dialog.h"
#include "ui/widgets/label.h"
#include "ui/widgets/node_graph_editor_widget.h"
#include "ui/widgets/projects_widget.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/screen_loading_widget.h"

namespace XER::App {

class AppUI::Impl {
public:
    Impl()
        : ui_initialized_(false)
        , window_created_(false) {
    }

    ~Impl() {
        if (ui_initialized_) {
            XER::UI::Core::Application::Instance().Shutdown();
        }
    }

    bool Initialize() {
        if (ui_initialized_) {
            return true;
        }

        auto& ui_app = XER::UI::Core::Application::Instance();
        if (!ui_app.Initialize()) {
            return false;
        }

        ui_initialized_ = true;
        return true;
    }

    void Shutdown() {
        XER::App::NodeGraphApp::CloseAllPluginEditors();
        if (ui_initialized_) {
            XER::UI::Core::Application::Instance().Shutdown();
            ui_initialized_ = false;
        }
    }

    bool CreateMainWindow(const std::string& title, int width, int height) {
        if (!ui_initialized_) {
            return false;
        }

        window_ = std::make_shared<XER::UI::Core::Window>();
        if (!window_->Create(title, width, height)) {
            window_.reset();
            return false;
        }

        window_->SetTheme(XER::UI::Theme::Variant::Dark);

        window_created_ = true;
        window_->SetSizeLimits(560, 400, 2560, 1600);
        return true;
    }

    void ShowWindow() {
        if (window_) {
            window_->Show();
        }
    }

    void HideWindow() {
        if (window_) {
            window_->Hide();
        }
    }

    void CloseWindow() {
        if (window_) {
            window_->Close();
        }
    }

    void SetupNodeGraphEditor() {
        if (!window_) {
            return;
        }

        // Application-level node types (audio file I/O, Lua scripting,
        // VST3 plugin processing) into the shared registry.
        XER::App::NodeGraphApp::RegisterAppNodes();

        // Splash first: boot checklist (project DB, projects, cache files,
        // drivers) runs on a worker thread; the project browser is shown
        // when it completes. Content swaps are deferred by one frame:
        // assigning std::function members or swapping the widget tree from
        // inside the running frame/event callback is unsafe, so handlers
        // only stash pending_action_, executed at the top of the next
        // frame callback before tick_fn_ is invoked.
        auto loading = std::make_shared<XER::UI::Widgets::ScreenLoadingWidget>(
            [this]() {
                pending_action_ = [this]() { ShowProjectsBrowser(); };
            });
        tick_fn_ = [loading]() { loading->Tick(); };
        window_->SetContent(loading);
        window_->SetFrameCallback([this]() {
            if (pending_action_) {
                auto action = std::move(pending_action_);
                pending_action_ = nullptr;
                action();
            }
            if (tick_fn_) {
                tick_fn_();
            }
        }, 33);
    }

    // Landing screen: project browser (Select / Configure / Duplicate /
    // Delete + search). Selecting a project lazily creates the editor,
    // loads the project and swaps it in as window content.
    void ShowProjectsBrowser() {
        if (!window_) return;

        XER::UI::Widgets::ProjectsWidget::Hooks hooks;
        hooks.on_open_project = [this](std::int64_t id) {
            pending_action_ = [this, id]() { OpenEditorForProject(id, ""); };
        };
        hooks.on_open_project_graph =
            [this](std::int64_t id, const std::string& graph_name) {
                pending_action_ = [this, id, graph_name]() {
                    OpenEditorForProject(id, graph_name);
                };
            };
        hooks.on_notify = [](const std::string& text, bool is_error) {
            OutputDebugStringA(
                (std::string(is_error ? "[error] " : "") + text + "\n").c_str());
        };

        auto projects = std::make_shared<XER::UI::Widgets::ProjectsWidget>(
            window_->GetHWND(), std::move(hooks));
        projects->SetMode(XER::UI::Widgets::ProjectsWidget::Mode::kBrowser);
        browser_ = std::make_shared<XER::UI::Widgets::ScrollArea>(projects);
        browser_projects_ = projects;

        // Detach VST3/CLAP editor HWNDs — they are children of the frame
        // and would otherwise linger over the project browser.
        XER::App::NodeGraphApp::CloseAllPluginEditors();
        // The editor's canvas/param-overlay are raw HWND children of the
        // frame — the widget tree does not hide them on content swap.
        if (editor_) editor_->SetHostVisible(false);
        window_->SetContent(browser_);
        tick_fn_ = [browser = browser_]() { browser->TickTree(0.033); };
        MaybeShowCrashReportDialog();
    }

    // One-shot dialog after a crash-handler auto-restart: the pending
    // marker is consumed here so it shows exactly once per report.
    void MaybeShowCrashReportDialog() {
        if (!window_) return;
        const auto report = Crash::ConsumePendingReport();
        if (!report.has_value()) return;
        auto content = std::make_shared<XER::UI::Widgets::Label>(
            "The application was restarted after a crash.\n"
            "Crash report saved to:\n" + report->string(), true);
        auto dialog = XER::UI::Widgets::Dialog::Create(
            "Crash report", content, "Open folder", "Close");
        dialog->SetOnClosed([dlg = dialog.get(), dir = *report]() {
            if (dlg->WasConfirmed()) {
                Crash::OpenReportFolder(dir);
            }
        });
        browser_->ShowOverlay(dialog);
    }

    void OpenEditorForProject(std::int64_t project_id,
                              const std::string& graph_name = "") {
        if (!window_) return;
        Crash::SetSessionProject(project_id, graph_name);
        XER::App::NodeGraphApp::CloseAllPluginEditors();

        if (!editor_) {
            editor_ = std::make_shared<XER::UI::Widgets::NodeGraphEditorWidget>(
                window_->GetHWND(),
                [window = window_](XER::UI::Theme::Variant v) {
                    window->SetTheme(v);
                },
                project_id);
            if (!graph_name.empty()) {
                // Constructor opens the project's default graph; reload the
                // explicitly requested one.
                editor_->OpenProjectGraph(project_id, graph_name);
            }
            editor_->SetPluginEditorHandler(
                [hwnd = window_->GetHWND()](std::uint64_t id, float x,
                                            float y, float w, float h) {
                    std::string err;
                    if (!XER::App::NodeGraphApp::OpenPluginEditor(id, hwnd, x, y, w, h,
                                                             &err)) {
                        OutputDebugStringA(
                            ("Plugin editor: " + err + "\n").c_str());
                    }
                });
            editor_->SetBrowserBackHook([this]() {
                pending_action_ = [this]() { ShowProjectsBrowser(); };
            });
        } else {
            editor_->OpenProjectGraph(project_id, graph_name);
        }

        window_->SetContent(editor_);
        // Restore the editor's HWND chrome (canvas + param overlay); the
        // following Layout pass also re-asserts per-view visibility.
        if (editor_) editor_->SetHostVisible(true);
        tick_fn_ = [editor = editor_]() { editor->Tick(); };
    }

    int Run() {
        if (!ui_initialized_) {
            return 1;
        }

        return XER::UI::Core::Application::Instance().Run();
    }

    bool IsWindowVisible() const {
        return window_ && window_->IsVisible();
    }

private:
    bool ui_initialized_;
    bool window_created_;
    std::shared_ptr<XER::UI::Core::Window> window_;
    std::shared_ptr<XER::UI::Widgets::NodeGraphEditorWidget> editor_;
    std::shared_ptr<XER::UI::Widgets::ScrollArea> browser_;
    std::shared_ptr<XER::UI::Widgets::ProjectsWidget> browser_projects_;
    // Per-frame tick target + deferred content swap (see SetupNodeGraphEditor).
    std::function<void()> tick_fn_;
    std::function<void()> pending_action_;
};

AppUI::AppUI()
    : impl_(std::make_unique<Impl>())
    , initialized_(false) {
}

AppUI::~AppUI() {
    if (initialized_) {
        Shutdown();
    }
}

bool AppUI::Initialize() {
    if (initialized_) {
        return true;
    }

    if (!impl_->Initialize()) {
        return false;
    }

    initialized_ = true;
    return true;
}

void AppUI::Shutdown() {
    if (initialized_) {
        impl_->Shutdown();
        initialized_ = false;
    }
}

bool AppUI::CreateMainWindow(const std::string& title, int width, int height) {
    if (!initialized_) {
        return false;
    }
    return impl_->CreateMainWindow(title, width, height);
}

void AppUI::ShowWindow() {
    impl_->ShowWindow();
}

void AppUI::HideWindow() {
    impl_->HideWindow();
}

void AppUI::CloseWindow() {
    impl_->CloseWindow();
}

void AppUI::SetupNodeGraphEditor() {
    impl_->SetupNodeGraphEditor();
}

int AppUI::Run() {
    return impl_->Run();
}

bool AppUI::IsWindowVisible() const {
    return impl_->IsWindowVisible();
}

} // namespace XER::App