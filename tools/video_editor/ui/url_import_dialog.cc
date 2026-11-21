#include "url_import_dialog.h"

#include <algorithm>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "url_import.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/field.h"
#include "widgets/label.h"
#include "widgets/text_input.h"

namespace XER::VideoEditor::Ui {
namespace {

namespace Layout = XER::UI::Layout;

constexpr float kPanelWidth = 520.0f;
constexpr float kPanelHeight = 190.0f;
constexpr float kPanelPad = 22.0f;

}  // namespace

UrlImportDialog::UrlImportDialog(Hooks hooks)
    : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetClipsChildren(false);
}

void UrlImportDialog::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;

    title_ = std::make_shared<Widgets::Label>("Import from URL");
    url_input_ = std::make_shared<Widgets::TextInput>();
    url_input_->SetPlaceholder("https://…");
    url_input_->SetOnValueChanged(
        [this](const std::string&) { RefreshValidation(); });
    url_input_->SetOnSubmit([this] { Apply(); });

    field_ = std::make_shared<Widgets::Field>();
    field_->SetLabel("Media URL");
    field_->SetDescription(
        "Direct link or YouTube/Vimeo/TikTok/… (yt-dlp)");
    field_->AddChild(url_input_);

    ok_button_ = std::make_shared<Widgets::Button>(
        "Import", Widgets::ButtonStyle::Primary);
    ok_button_->SetOnClick([this] { Apply(); });
    cancel_button_ = std::make_shared<Widgets::Button>(
        "Cancel", Widgets::ButtonStyle::Secondary);
    cancel_button_->SetOnClick([this] { Cancel(); });

    AddChild(title_);
    AddChild(field_);
    AddChild(ok_button_);
    AddChild(cancel_button_);
    RefreshValidation();
}

void UrlImportDialog::Reset() {
    EnsureChildren();
    url_input_->SetValue("");
    RefreshValidation();
}

void UrlImportDialog::Dismiss() {
    if (hooks_.on_cancel) hooks_.on_cancel();
}

void UrlImportDialog::RefreshValidation() {
    if (!children_attached_) return;
    const std::string url = url_input_->GetValue();
    const auto first = url.find_first_not_of(" \t");
    const bool empty = first == std::string::npos;
    // The dialog only gates on "something was typed" — scheme/host
    // classification and the platform-vs-direct decision belong to
    // UrlImport::ClassifyUrl (smoke-checked), so an odd-but-typed URL
    // still reaches the importer and reports its own error.
    ok_button_->SetEnabled(!empty);
    field_->SetError(empty ? "" :
        UrlImport::ClassifyUrl(url) == UrlImport::UrlKind::kInvalid
            ? "Not an http(s) URL — the importer will reject it"
            : "");
}

void UrlImportDialog::Apply() {
    EnsureChildren();
    std::string url = url_input_->GetValue();
    const auto first = url.find_first_not_of(" \t");
    if (first == std::string::npos) return;
    const auto last = url.find_last_not_of(" \t");
    url = url.substr(first, last - first + 1);
    if (hooks_.on_apply) hooks_.on_apply(url);
}

void UrlImportDialog::Cancel() {
    if (hooks_.on_cancel) hooks_.on_cancel();
}

Layout::Rect UrlImportDialog::PanelRect() const {
    const auto bounds = GetBounds();
    const float width = std::min(kPanelWidth, bounds.width - 40.0f);
    return {bounds.x + (bounds.width - width) * 0.5f,
            bounds.y + (bounds.height - kPanelHeight) * 0.5f,
            width, kPanelHeight};
}

void UrlImportDialog::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, VeTheme::kScrim, 0.0f);
    const auto panel = PanelRect();
    renderer.FillRect(panel, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(panel, tokens.border, 1.0f, tokens.radius_large);
    renderer.FillRect({panel.x, panel.y, 4.0f, panel.height},
                      tokens.accent, tokens.radius_large);
}

void UrlImportDialog::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const auto panel = PanelRect();
    const float px = panel.x + kPanelPad;
    const float pw = panel.width - kPanelPad * 2.0f;

    title_->Layout({px, panel.y + 14.0f, pw, 26.0f}, tokens);

    // Field = caption band + 32px input + footer line (reserved whether
    // or not the hint is showing so nothing below shifts).
    const float caption_h =
        tokens.font_small * 1.4f + tokens.spacing_small * 0.5f;
    const float footer_h = tokens.font_small * 1.5f + 6.0f;
    field_->Layout({px, panel.y + 52.0f, pw,
                    caption_h + 32.0f + footer_h},
                   tokens);

    const float button_y = panel.y + panel.height - 44.0f;
    cancel_button_->Layout({panel.x + panel.width - kPanelPad - 196.0f,
                            button_y, 94.0f, 34.0f},
                           tokens);
    ok_button_->Layout({panel.x + panel.width - kPanelPad - 94.0f,
                        button_y, 94.0f, 34.0f},
                       tokens);
}

bool UrlImportDialog::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    using XER::UI::Widgets::Event;
    using XER::UI::Widgets::MouseButton;
    if (event.type == Event::Type::KeyDown) {
        if (event.key == XER::UI::Widgets::KeyCode::Escape) {
            Cancel();
            return true;
        }
        if (event.key == XER::UI::Widgets::KeyCode::Enter) {
            Apply();
            return true;
        }
    }
    if (event.type == Event::Type::Click &&
        event.mouse_button == MouseButton::Left &&
        !PanelRect().Contains(event.position)) {
        Cancel();   // backdrop click — modal convention
        return true;
    }
    return event.type == Event::Type::MouseDown ||
           event.type == Event::Type::MouseUp ||
           event.type == Event::Type::Click;
}

void UrlImportDialog::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
