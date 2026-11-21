// Modal "Import from URL" overlay — the smallest of the editor
// dialogs: one Field captioned TextInput for the http(s) URL plus
// OK/Cancel. Follows the SpeedDialog conventions (scrim + centered
// card + accent edge, Escape/backdrop cancel, Enter submits).
//
// The dialog owns no state: the shell reads the URL out of on_apply
// and runs the download through UrlImport::Download on a worker —
// a platform link (YouTube & friends) routes to yt-dlp, everything
// else is a direct GET into the media cache dir.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "layout/geometry.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Field;
class Label;
class TextInput;
}

namespace XER::VideoEditor::Ui {

class UrlImportDialog final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        // Fired by OK/Enter with the trimmed URL — validation is the
        // importer's job (ClassifyUrl); the dialog only blocks empty.
        std::function<void(const std::string& url)> on_apply;
        // Fired by Cancel / Escape / backdrop click.
        std::function<void()> on_cancel;
    };

    explicit UrlImportDialog(Hooks hooks = {});

    // Clears the field for a fresh prompt — call before ShowOverlay.
    void Reset();
    // Fires on_cancel — e.g. so the shell can close the dialog when a
    // download completes.
    void Dismiss();

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {100000.0f, 100000.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {100000.0f, 100000.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    void EnsureChildren();
    void Apply();
    void Cancel();
    void RefreshValidation();
    XER::UI::Layout::Rect PanelRect() const;

    Hooks hooks_;
    bool children_attached_ = false;

    std::shared_ptr<XER::UI::Widgets::Label> title_;
    std::shared_ptr<XER::UI::Widgets::Field> field_;
    std::shared_ptr<XER::UI::Widgets::TextInput> url_input_;
    std::shared_ptr<XER::UI::Widgets::Button> ok_button_;
    std::shared_ptr<XER::UI::Widgets::Button> cancel_button_;
};

}  // namespace XER::VideoEditor::Ui
