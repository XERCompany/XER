// project_manager.cc — see project_manager.h for the contract.
// Layout: a two-column home screen (brand + actions + new-project
// form left, scrollable recents grid right), all chrome drawn against
// the VeTheme tokens and all interactive controls real design-system
// widgets.
#include "project_manager.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "helper/path_utils.h"
#include "icon_helpers.h"
#include "rendering/renderer.h"
#include "sequence_preset.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/field.h"
#include "widgets/item.h"
#include "widgets/label.h"
#include "widgets/scroll_area.h"
#include "widgets/select.h"
#include "widgets/text_input.h"

namespace XER::VideoEditor::Ui {
namespace {

namespace Layout = XER::UI::Layout;
namespace Path = XER::Helper::Path;
namespace Rendering = XER::UI::Rendering;
namespace Widgets = XER::UI::Widgets;
using XER::UI::Theme::Color;

// Grid metrics (media-bin convention: 16:9 thumbs on a 216px target
// card, 16px gutters).
constexpr float kTileTargetW = 216.0f;
constexpr float kTileGap = VeTheme::kSpaceLg;
constexpr float kTileTextH = 48.0f;   // title + meta strip under thumb

std::int64_t NowUnix() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// First glyph of the tile title, uppercase — the placeholder poster's
// monogram. ASCII-uppercases; a leading non-ASCII byte is drawn
// verbatim (the font covers the app's UTF-8 UI set).
std::string TileInitial(const std::string& title) {
    for (const unsigned char c : title) {
        if (!std::isspace(c)) {
            return std::string(
                1, static_cast<char>(std::toupper(c)));
        }
    }
    return "P";
}

}  // namespace

// -------------------------------------------------------------------
// RecentsGrid — drawn tile grid inside the ScrollArea. Owns the tile
// view-models (built from Records::RecentProject rows) plus the hit
// map its Render()/HandleEvent() share. Missing project files are
// statted once per SetTiles — never per frame.
// -------------------------------------------------------------------
class ProjectManagerScreen::RecentsGrid final : public Widgets::Widget {
public:
    struct Hooks {
        std::function<void(const std::string&)> on_open;
        std::function<void(const std::string&)> on_remove;
    };

    explicit RecentsGrid(Hooks hooks) : hooks_(std::move(hooks)) {}

    void SetTiles(std::vector<Records::RecentTile> tiles) {
        tiles_ = std::move(tiles);
        hits_.clear();
        hover_tile_ = -1;
        hover_close_ = false;
        missing_.assign(tiles_.size(), false);
        for (std::size_t i = 0; i < tiles_.size(); ++i) {
            std::error_code ec;
            missing_[i] = tiles_[i].path.empty() ||
                !std::filesystem::exists(Path::FromUtf8(tiles_[i].path),
                                         ec);
        }
        Invalidate();
    }

    std::size_t TileCount() const { return tiles_.size(); }

    void Render(Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        const auto bounds = GetBounds();
        if (tiles_.empty()) {
            renderer.DrawText(
                "No recent projects yet — create one or open a file.",
                {bounds.x, bounds.y + 12.0f, bounds.width, 22.0f},
                {tokens.muted, tokens.font_body, 400});
            return;
        }
        for (std::size_t i = 0; i < hits_.size() && i < tiles_.size();
             ++i) {
            const Records::RecentTile& tile = tiles_[i];
            const auto& card = hits_[i].card;
            const auto thumb = Layout::Rect{card.x + 1.0f, card.y + 1.0f,
                                            card.width - 2.0f, thumb_h_};
            const bool hovered = static_cast<int>(i) == hover_tile_;

            renderer.FillRect(card, tokens.surface,
                              VeTheme::kRadiusMd);
            bool drew = false;
            if (!tile.poster_path.empty()) {
                drew = renderer.DrawImage(tile.poster_path, thumb, 1.0f,
                                          VeTheme::kRadiusMd);
            }
            if (!drew) {
                // Styled placeholder: accent-tinted panel + the
                // project monogram (no ad-hoc bitmap work needed).
                renderer.FillRect(
                    thumb, VeTheme::Mix(tokens.panel, tokens.accent, 0.10f),
                    VeTheme::kRadiusMd);
                renderer.DrawText(
                    TileInitial(tile.title),
                    {thumb.x, thumb.y + thumb_h_ * 0.5f - 18.0f,
                     thumb.width, 36.0f},
                    {tokens.accent, VeTheme::kFontTitle * 1.6f, 600});
                VeIcons::DrawIcon(renderer, "bin.kind.video", 16,
                                  tokens.muted,
                                  {thumb.x + thumb.width * 0.5f - 8.0f,
                                   thumb.y + thumb_h_ * 0.5f + 14.0f,
                                   16.0f, 16.0f});
            }
            if (hovered) {
                renderer.FillRect(
                    thumb, VeTheme::WithAlpha(tokens.accent, 0.10f),
                    VeTheme::kRadiusMd);
            }
            renderer.StrokeRect(
                card, hovered ? tokens.accent : tokens.border,
                hovered ? 1.5f : 1.0f, VeTheme::kRadiusMd);

            // Title + meta (time-ago · duration); missing files dim.
            const Color title_color =
                missing_[i] ? tokens.muted : tokens.foreground;
            renderer.DrawText(
                tile.title,
                {card.x + 10.0f, card.y + thumb_h_ + 7.0f,
                 card.width - 20.0f, 18.0f},
                {title_color, tokens.font_body, 600});
            std::string meta = tile.meta_line;
            if (missing_[i]) {
                meta += meta.empty() ? "missing"
                                     : " \xc2\xb7 missing";
            }
            renderer.DrawText(
                meta,
                {card.x + 10.0f, card.y + thumb_h_ + 27.0f,
                 card.width - 20.0f, 16.0f},
                {tokens.muted, tokens.font_small, 400});

            // Hover × chip (top-right of the thumb).
            if (hovered) {
                const auto& chip = hits_[i].close;
                renderer.FillRect(
                    chip,
                    hover_close_
                        ? VeTheme::WithAlpha(tokens.danger, 0.85f)
                        : VeTheme::kOverlayChip,
                    VeTheme::kRadiusSm);
                if (!VeIcons::DrawIcon(
                        renderer, "action.close", 13,
                        hover_close_ ? tokens.danger_foreground
                                     : tokens.foreground,
                        chip)) {
                    renderer.DrawText(
                        "\xc3\x97", chip,
                        {tokens.foreground, tokens.font_body, 600});
                }
            }
        }
    }

    void Layout(const Layout::Rect& bounds) override {
        SetBounds(bounds);
        const float width = std::max(bounds.width, 1.0f);
        cols_ = std::max(
            1, static_cast<int>(
                   (width + kTileGap) / (kTileTargetW + kTileGap)));
        card_w_ = (width - (cols_ - 1) * kTileGap) /
                  static_cast<float>(cols_);
        thumb_h_ = card_w_ * 0.5625f;
        card_h_ = thumb_h_ + kTileTextH;

        hits_.assign(tiles_.size(), {});
        for (std::size_t i = 0; i < tiles_.size(); ++i) {
            const int row = static_cast<int>(i) / cols_;
            const int col = static_cast<int>(i) % cols_;
            const float x = bounds.x +
                            col * (card_w_ + kTileGap);
            const float y = bounds.y +
                            row * (card_h_ + kTileGap);
            hits_[i].card = {x, y, card_w_, card_h_};
            hits_[i].close = {x + card_w_ - 28.0f, y + 6.0f,
                              22.0f, 22.0f};
        }
    }

    bool HandleEvent(const Widgets::Event& event) override {
        using Widgets::Event;
        using Widgets::MouseButton;
        if (event.type == Event::Type::MouseMove) {
            int tile = -1;
            bool close = false;
            for (std::size_t i = 0; i < hits_.size(); ++i) {
                if (hits_[i].card.Contains(event.position)) {
                    tile = static_cast<int>(i);
                    close = hits_[i].close.Contains(event.position);
                    break;
                }
            }
            if (tile != hover_tile_ || close != hover_close_) {
                hover_tile_ = tile;
                hover_close_ = close;
                Invalidate();
            }
            return false;
        }
        if (event.type == Event::Type::MouseDown &&
            event.mouse_button == MouseButton::Right) {
            // Context remove: right-click drops the row without an
            // extra menu (mirrors the × affordance — documented on the
            // class; the records store owns the real delete).
            for (std::size_t i = 0; i < hits_.size(); ++i) {
                if (hits_[i].card.Contains(event.position)) {
                    if (hooks_.on_remove) {
                        hooks_.on_remove(tiles_[i].path);
                    }
                    return true;
                }
            }
            return false;
        }
        if (event.type == Event::Type::Click &&
            event.mouse_button == MouseButton::Left) {
            for (std::size_t i = 0; i < hits_.size(); ++i) {
                if (hits_[i].close.Contains(event.position)) {
                    if (hooks_.on_remove) {
                        hooks_.on_remove(tiles_[i].path);
                    }
                    return true;
                }
                if (hits_[i].card.Contains(event.position)) {
                    if (hooks_.on_open) {
                        hooks_.on_open(tiles_[i].path);
                    }
                    return true;
                }
            }
            return true;   // dead click inside the grid — swallow
        }
        return false;
    }

    Layout::Size GetMinSize() const override { return {0.0f, 0.0f}; }
    Layout::Size GetPreferredSize() const override {
        return {kTileTargetW, card_h_};
    }
    Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }
    float GetContentHeight() const override {
        if (tiles_.empty()) {
            return 40.0f;
        }
        const int rows =
            (static_cast<int>(tiles_.size()) + cols_ - 1) / cols_;
        return rows * (card_h_ + kTileGap) - kTileGap + 1.0f;
    }

private:
    struct TileHit {
        Layout::Rect card;
        Layout::Rect close;
    };

    Hooks hooks_;
    std::vector<Records::RecentTile> tiles_;
    std::vector<bool> missing_;
    std::vector<TileHit> hits_;
    int cols_ = 1;
    float card_w_ = kTileTargetW;
    float card_h_ = kTileTargetW * 0.5625f + kTileTextH;
    float thumb_h_ = kTileTargetW * 0.5625f;
    int hover_tile_ = -1;
    bool hover_close_ = false;
};

// -------------------------------------------------------------------
// ProjectManagerScreen
// -------------------------------------------------------------------

ProjectManagerScreen::ProjectManagerScreen(Hooks hooks)
    : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetVisible(false);
    EnsureChildren();
}

void ProjectManagerScreen::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    // Icon-capable buttons resolve through the pack (idempotent).
    VeIcons::InstallButtonIconResolver();

    open_btn_ = std::make_shared<Widgets::Button>(
        "Open Project\xe2\x80\xa6", Widgets::ButtonStyle::Secondary);
    open_btn_->SetIcon("action.file_open");
    open_btn_->SetOnClick([this] {
        if (hooks_.on_open_browse) hooks_.on_open_browse();
    });

    name_input_ = std::make_shared<Widgets::TextInput>("Untitled");
    name_input_->SetPlaceholder("Project name");
    name_input_->SetOnSubmit([this] { SubmitCreate(); });
    name_field_ = std::make_shared<Widgets::Field>();
    name_field_->SetLabel("Project name");
    name_field_->AddChild(name_input_);

    preset_select_ = std::make_shared<Widgets::Select>();
    {
        std::vector<std::string> options;
        for (const SequencePreset& preset : BuiltinSequencePresets()) {
            std::string label = preset.name;
            if (preset.w > 0 && preset.h > 0) {
                label += "  \xc2\xb7  " + std::to_string(preset.w) +
                         "\xc3\x97" + std::to_string(preset.h);
                const double fps =
                    static_cast<double>(preset.fps_num) /
                    std::max(1, preset.fps_den);
                char buf[16];
                std::snprintf(buf, sizeof(buf), " @ %gfps", fps);
                label += buf;
            }
            options.push_back(std::move(label));
        }
        if (options.empty()) {
            options.push_back("HD 1080p 30");
        }
        const int count = static_cast<int>(options.size());
        preset_select_->SetOptions(std::move(options));
        // Default to the 1080p30-ish entry (index 4 in the builtin
        // ordering — clamps to 0 on tiny lists).
        preset_select_->SetSelectedIndex(count > 4 ? 4 : 0);
    }
    preset_field_ = std::make_shared<Widgets::Field>();
    preset_field_->SetLabel("Sequence preset");
    preset_field_->AddChild(preset_select_);

    location_input_ = std::make_shared<Widgets::TextInput>();
    location_input_->SetPlaceholder("Location");
    location_input_->SetOnSubmit([this] { SubmitCreate(); });
    browse_btn_ = std::make_shared<Widgets::Button>(
        "\xe2\x80\xa6", Widgets::ButtonStyle::Secondary);
    browse_btn_->SetTooltip("Choose project folder");
    browse_btn_->SetOnClick([this] {
        if (hooks_.pick_location == nullptr) return;
        const std::string picked = hooks_.pick_location();
        if (!picked.empty()) {
            location_input_->SetValue(picked);
        }
    });
    location_field_ = std::make_shared<Widgets::Field>();
    location_field_->SetLabel("Location");
    location_field_->AddChild(location_input_);

    create_btn_ = std::make_shared<Widgets::Button>(
        "Create Project", Widgets::ButtonStyle::Primary);
    create_btn_->SetIcon("action.add");
    create_btn_->SetOnClick([this] { SubmitCreate(); });

    demo_item_ = std::make_shared<Widgets::Item>();
    demo_item_->SetTitle("Demo sequence");
    demo_item_->SetDescription(
        "Generated bars + title — explore without importing media");
    demo_item_->SetOnClick([this] {
        if (hooks_.on_open_demo) hooks_.on_open_demo();
    });

    RecentsGrid::Hooks grid_hooks;
    grid_hooks.on_open = [this](const std::string& path) {
        if (hooks_.on_open_recent) hooks_.on_open_recent(path);
    };
    grid_hooks.on_remove = [this](const std::string& path) {
        if (hooks_.on_remove_recent) hooks_.on_remove_recent(path);
        Refresh();
    };
    recents_grid_ = std::make_shared<RecentsGrid>(grid_hooks);
    recents_scroll_ =
        std::make_shared<Widgets::ScrollArea>(recents_grid_);

    AddChild(open_btn_);
    AddChild(name_field_);
    AddChild(preset_field_);
    AddChild(location_field_);
    AddChild(browse_btn_);
    AddChild(create_btn_);
    AddChild(demo_item_);
    AddChild(recents_scroll_);

    if (hooks_.default_location != nullptr) {
        location_input_->SetValue(hooks_.default_location());
    }
}

void ProjectManagerScreen::Open() {
    EnsureChildren();
    Refresh();
    SetVisible(true);
    RequestLayout();
}

void ProjectManagerScreen::Dismiss() { SetVisible(false); }

void ProjectManagerScreen::Refresh() {
    std::vector<Records::RecentProject> recents;
    if (hooks_.list_recents) {
        recents = hooks_.list_recents();
    }
    recents_count_ = static_cast<int>(recents.size());
    std::vector<Records::RecentTile> tiles;
    tiles.reserve(recents.size());
    const std::int64_t now = NowUnix();
    for (const Records::RecentProject& rec : recents) {
        tiles.push_back(Records::MakeRecentTile(rec, now));
    }
    recents_grid_->SetTiles(std::move(tiles));
    Invalidate();
}

void ProjectManagerScreen::SetStatus(const std::string& text,
                                     bool is_error) {
    status_text_ = text;
    status_is_error_ = is_error;
    if (hooks_.on_status) hooks_.on_status(text, is_error);
    Invalidate();
}

void ProjectManagerScreen::SubmitCreate() {
    if (hooks_.on_create_project == nullptr) return;
    NewProjectRequest req;
    req.name = name_input_ != nullptr ? name_input_->GetValue()
                                      : std::string{};
    req.preset_index =
        preset_select_ != nullptr ? preset_select_->GetSelectedIndex()
                                  : 0;
    req.directory = location_input_ != nullptr
                        ? location_input_->GetValue()
                        : std::string{};
    hooks_.on_create_project(req);
}

Layout::Rect ProjectManagerScreen::LeftColumnRect() const {
    const auto bounds = GetBounds();
    const float pad_x = std::clamp(bounds.width * 0.055f, 28.0f, 72.0f);
    const float left_w =
        std::clamp(bounds.width * 0.26f, 280.0f, 340.0f);
    return {bounds.x + pad_x, bounds.y + 36.0f, left_w,
            std::max(0.0f, bounds.height - 36.0f - 52.0f)};
}

Layout::Rect ProjectManagerScreen::RightColumnRect() const {
    const auto bounds = GetBounds();
    const auto left = LeftColumnRect();
    const float pad_x = std::clamp(bounds.width * 0.055f, 28.0f, 72.0f);
    const float x = left.x + left.width + 56.0f;
    return {x, bounds.y + 36.0f,
            std::max(0.0f, bounds.x + bounds.width - pad_x - x),
            std::max(0.0f, bounds.height - 36.0f - 52.0f)};
}

void ProjectManagerScreen::Render(
    Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    // Opaque over the workspace — this is a replacement surface, not
    // a scrim (the title bar above stays visible/interactive).
    renderer.FillRect(bounds, tokens.background, 0.0f);

    const auto left = LeftColumnRect();
    const auto right = RightColumnRect();

    // Brand block.
    renderer.DrawText("XER STUDIO", {left.x, left.y, left.width, 18.0f},
                      {tokens.accent, tokens.font_small, 700});
    renderer.DrawText("Video Editor",
                      {left.x, left.y + 20.0f, left.width, 30.0f},
                      {tokens.foreground, VeTheme::kFontTitle * 1.3f,
                       600});
    renderer.DrawText("New, open, or pick up where you left off.",
                      {left.x, left.y + 52.0f, left.width, 18.0f},
                      {tokens.muted, tokens.font_small, 400});

    // Section captions — the drawn-header convention (muted, small,
    // weight 600), same rects Layout() gives the controls below.
    const Rendering::TextStyle caption{tokens.muted, tokens.font_small,
                                       600};
    renderer.DrawText("NEW PROJECT",
                      {left.x, left.y + 122.0f, left.width, 16.0f},
                      caption);
    renderer.DrawText("TEMPLATES",
                      {left.x, left.y + 372.0f, left.width, 16.0f},
                      caption);
    renderer.DrawText("RECENT PROJECTS",
                      {right.x, right.y, right.width, 16.0f}, caption);
    renderer.DrawText(
        std::to_string(recents_count_) +
            (recents_count_ == 1 ? " project" : " projects"),
        {right.x + right.width - 120.0f, right.y, 120.0f, 16.0f},
        {tokens.muted, tokens.font_small, 400});

    // Column divider hairline.
    const float div_x = left.x + left.width + 28.0f;
    renderer.DrawLine({div_x, left.y}, {div_x, bounds.y + bounds.height - 52.0f},
                      tokens.border, 1.0f);

    // Status line + the Esc affordance sit on the bottom edge.
    if (!status_text_.empty()) {
        renderer.DrawText(
            status_text_,
            {bounds.x + 28.0f, bounds.y + bounds.height - 32.0f,
             bounds.width * 0.6f, 20.0f},
            {status_is_error_ ? tokens.danger : tokens.muted,
             tokens.font_small, 400});
    }
    renderer.DrawText("Esc — skip to editor",
                      {bounds.x + bounds.width - 170.0f,
                       bounds.y + bounds.height - 32.0f, 142.0f, 20.0f},
                      {tokens.muted, tokens.font_small, 400});
}

void ProjectManagerScreen::Layout(const Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const auto left = LeftColumnRect();
    const auto right = RightColumnRect();

    // Field = caption band + the 32px control (Field::Layout math).
    const float caption_h =
        tokens.font_small * 1.4f + tokens.spacing_small * 0.5f;
    const float field_h = caption_h + 32.0f;

    open_btn_->Layout({left.x, left.y + 78.0f, left.width, 36.0f},
                      tokens);

    const float name_y = left.y + 144.0f;
    name_field_->Layout({left.x, name_y, left.width, field_h}, tokens);
    preset_field_->Layout({left.x, name_y + 60.0f, left.width, field_h},
                          tokens);
    const float loc_y = name_y + 120.0f;
    location_field_->Layout(
        {left.x, loc_y, left.width - 44.0f, field_h}, tokens);
    browse_btn_->Layout(
        {left.x + left.width - 36.0f, loc_y + caption_h, 36.0f, 30.0f},
        tokens);

    create_btn_->Layout({left.x, loc_y + 62.0f, left.width, 36.0f},
                        tokens);
    demo_item_->Layout({left.x, left.y + 392.0f, left.width, 52.0f},
                       tokens);

    recents_scroll_->Layout(
        {right.x, right.y + 26.0f, right.width,
         std::max(40.0f, right.height - 26.0f)},
        tokens);
}

bool ProjectManagerScreen::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    using Widgets::Event;
    using Widgets::KeyCode;
    if (event.type == Event::Type::KeyDown &&
        event.key == KeyCode::Escape) {
        if (hooks_.on_dismiss) hooks_.on_dismiss();
        return true;
    }
    // Non-modal but covering: pointer events inside the screen's
    // bounds stay here — the workspace below must not see them.
    return event.type == Event::Type::MouseDown ||
           event.type == Event::Type::MouseUp ||
           event.type == Event::Type::Click;
}

}  // namespace XER::VideoEditor::Ui
