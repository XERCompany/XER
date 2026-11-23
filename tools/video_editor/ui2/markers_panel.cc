// MarkersPanel — see markers_panel.h for the contract. The list is
// a VirtualList (one drawn row per marker); the toolbar is composed
// widgets (search TextInput, color Select, add Button).
//
// Double-click note: the kit's Event enum carries a single Click
// type (no double-click variant — surface_hooks.h §6 leaves
// synthesis to the surface), so SeekToMarker fires when two clicks
// land on the same row inside 450 ms; Enter on the cursor row and
// the context "Go to" item share the same path.
//
// Annotation note: MarkerRow.duration_seconds/comment are
// presentation fields the model lacks — OpenEditDialog/SetMarker
// Annotation persist them in annotations_ (keyed by marker time so
// they survive SetMarkers re-feeds). They are session data only;
// when TimelineModel grows marker duration/comment columns the shell
// push simply fills them instead.
#include "markers_panel.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <utility>

#include "helper/string_utils.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "timecode.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/context_menu.h"
#include "widgets/dialog.h"
#include "widgets/label.h"
#include "widgets/select.h"
#include "widgets/text_input.h"
#include "widgets/virtual_list.h"

// windows.h may have re-defined DrawText before we got here.
#ifdef DrawText
#undef DrawText
#endif

namespace XER::VideoEditor::Ui2 {
namespace {

namespace W = XER::UI::Widgets;
namespace Layout = XER::UI::Layout;
using XER::UI::Theme::Color;

constexpr float kToolbarH = 30.0f;
constexpr float kToolbarGap = 6.0f;
constexpr float kStatusH = 18.0f;
constexpr float kRowH = 30.0f;
constexpr auto kDoubleClickWindow = std::chrono::milliseconds(450);

// The named palette the color Select offers — Premiere's marker
// color set trimmed to the eight canonical swatches.
struct Swatch {
    const char* name;
    std::array<std::uint8_t, 4> rgba;
};
constexpr Swatch kPalette[] = {
    {"Yellow", {255, 214, 0, 255}},   // the model default
    {"Green", {0, 200, 83, 255}},
    {"Cyan", {0, 188, 212, 255}},
    {"Blue", {41, 121, 255, 255}},
    {"Purple", {156, 39, 176, 255}},
    {"Red", {244, 67, 54, 255}},
    {"Orange", {255, 138, 0, 255}},
    {"White", {238, 238, 238, 255}},
};
constexpr int kPaletteCount =
    static_cast<int>(std::size(kPalette));

Color MarkerColor(const std::array<std::uint8_t, 4>& rgba) {
    return {rgba[0] / 255.0f, rgba[1] / 255.0f, rgba[2] / 255.0f,
            rgba[3] / 255.0f};
}

int PaletteIndexFor(const std::array<std::uint8_t, 4>& rgba) {
    for (int i = 0; i < kPaletteCount; ++i) {
        if (kPalette[i].rgba == rgba) return i;
    }
    return 0;
}

std::string Lower(const std::string& text) {
    return XER::Helper::String::ToLowerAscii(text);
}

// The dialog content: name / comment / duration fields + a color
// Select stacked vertically. A plain drawn-labeled composite — the
// Dialog wants one content widget.
class EditForm final : public W::Widget {
public:
    EditForm(std::shared_ptr<W::TextInput> name,
             std::shared_ptr<W::TextInput> comment,
             std::shared_ptr<W::TextInput> duration,
             std::shared_ptr<W::Select> color)
        : name_(std::move(name)), comment_(std::move(comment)),
          duration_(std::move(duration)), color_(std::move(color)) {
        AddChild(name_);
        AddChild(comment_);
        AddChild(duration_);
        AddChild(color_);
    }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        const auto b = GetBounds();
        const auto label = [&](float y, const char* text) {
            renderer.DrawText(
                text, {b.x, y, 70.0f, 22.0f},
                {tokens.muted, Theme::kFontCaption, 500});
        };
        float y = b.y + 6.0f;
        label(y + 6.0f, "Name");
        y += 36.0f;
        label(y + 6.0f, "Color");
        y += 36.0f;
        label(y + 6.0f, "Dur (s)");
        y += 36.0f;
        label(y + 6.0f, "Comment");
    }
    void Layout(const Layout::Rect& bounds) override {
        SetBounds(bounds);
        const auto& tokens = CachedLayoutTokens();
        float y = bounds.y + 6.0f;
        const float field_x = bounds.x + 74.0f;
        const float field_w = bounds.x + bounds.width - field_x;
        name_->Layout({field_x, y, field_w, 28.0f}, tokens);
        y += 36.0f;
        color_->Layout({field_x, y, field_w, 28.0f}, tokens);
        y += 36.0f;
        duration_->Layout({field_x, y, field_w, 28.0f}, tokens);
        y += 36.0f;
        comment_->Layout({field_x, y, field_w, 28.0f}, tokens);
    }
    using Widget::Layout;
    bool HandleEvent(const W::Event&) override { return false; }
    Layout::Size GetMinSize() const override { return {240.0f, 150.0f}; }
    Layout::Size GetPreferredSize() const override {
        return {320.0f, 150.0f};
    }
    Layout::Size GetMaxSize() const override {
        return {100000.0f, 160.0f};
    }

private:
    std::shared_ptr<W::TextInput> name_, comment_, duration_;
    std::shared_ptr<W::Select> color_;
};

}  // namespace

std::shared_ptr<MarkersPanel> MarkersPanel::Create() {
    return std::shared_ptr<MarkersPanel>(new MarkersPanel());
}

MarkersPanel::MarkersPanel() {
    SetFocusable(true);
    SetClipsChildren(true);
}

void MarkersPanel::SetHooks(Hooks hooks) { hooks_ = std::move(hooks); }

void MarkersPanel::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;

    search_input_ = std::make_shared<W::TextInput>("");
    search_input_->SetPlaceholder("Search markers…");
    search_input_->SetTooltip("Filter by name, comment or scope");
    search_input_->SetOnValueChanged(
        [this](const std::string& value) { SetSearchText(value); });

    std::vector<std::string> names;
    names.reserve(kPaletteCount);
    for (const Swatch& s : kPalette) names.emplace_back(s.name);
    color_select_ = std::make_shared<W::Select>(names, color_choice_);
    color_select_->SetTooltip("Color for the next added marker");
    color_select_->SetOnChanged(
        [this](int index) { SetColorChoice(index); });

    add_btn_ = std::make_shared<W::Button>(
        "+ Marker", W::ButtonStyle::Primary);
    add_btn_->SetSize(W::ButtonSize::Small);
    add_btn_->SetTooltip("Add a marker at the playhead (M)");
    add_btn_->SetOnClick([this] { AddAtPlayhead(); });

    list_ = W::VirtualList::Create();
    list_->SetItemHeight(kRowH);
    list_->SetSelectionMode(W::VirtualList::SelectionMode::Single);
    list_->SetItemRenderer(
        [this](XER::UI::Rendering::Renderer& renderer,
               Layout::Rect row, int index, bool /*selected*/) {
            const MarkerRow* mr = RowAt(index);
            if (mr == nullptr) return;
            const auto& tokens = CachedLayoutTokens();
            const float pad = Theme::kSpaceXs;
            float x = row.x + pad;
            // Timecode column — mono digits in the NLE timecode color.
            const float tc_w = 92.0f;
            renderer.DrawText(
                TimecodeFor(mr->marker.time),
                {x, row.y + 4.0f, tc_w, row.height - 8.0f},
                {tokens.timecode, Theme::kFontCaption, 500, true});
            x += tc_w + pad;
            // Color dot.
            const float dot = 9.0f;
            const Layout::Rect dot_r{
                x, row.y + (row.height - dot) * 0.5f, dot, dot};
            renderer.FillRect(dot_r, MarkerColor(mr->marker.color),
                              dot * 0.5f);
            renderer.StrokeRect(dot_r, tokens.border, 1.0f,
                                dot * 0.5f);
            x += dot + pad;
            // Scope chip — "Proj" vs a clip label, muted.
            const bool project_scope =
                mr->scope.empty() || mr->scope == "Project";
            const std::string scope =
                project_scope ? "Seq" : mr->scope;
            const float scope_w = project_scope ? 30.0f : 56.0f;
            const Layout::Rect scope_r{
                x, row.y + 6.0f, scope_w, row.height - 12.0f};
            renderer.FillRect(scope_r, tokens.secondary,
                              tokens.radius_small);
            const XER::UI::Rendering::TextStyle scope_style{
                tokens.muted, Theme::kFontCaption, 500};
            renderer.DrawText(
                W::EllipsizeText(renderer, scope, scope_style,
                                 scope_r.width - 4.0f),
                scope_r, scope_style);
            x += scope_w + pad;
            // Playhead proximity highlight on the name column —
            // within ~half a frame reads as "the marker under the
            // playhead" (the legacy panel's SetPlayhead parity).
            const bool current =
                std::fabs(mr->marker.time - playhead_) < 1e-3;
            const float name_w = row.Right() - x - pad;
            if (current) {
                renderer.FillRect(
                    {x - 2.0f, row.y + 3.0f, name_w + 4.0f,
                     row.height - 6.0f},
                    Theme::WithAlpha(tokens.accent, 0.18f),
                    tokens.radius_small);
            }
            const std::string name =
                mr->marker.name.empty() ? "Marker" : mr->marker.name;
            // Duration tail on the name line ("· 4.0s" when the
            // annotation carries one).
            std::string name_line = name;
            double dur = 0.0;
            std::string comment;
            if (mr->duration_seconds > 0.0) {
                dur = mr->duration_seconds;
                comment = mr->comment;
            } else if (AnnotationFor(mr->marker.time, &dur,
                                     &comment)) {
                // annotation already loaded below
            }
            char dbuf[16];
            if (dur > 0.0) {
                std::snprintf(dbuf, sizeof(dbuf), " · %.1fs", dur);
                name_line += dbuf;
            }
            const float comment_w =
                comment.empty() ? 0.0f : 120.0f;
            const float nl_w =
                std::max(0.0f, name_w - comment_w);
            const XER::UI::Rendering::TextStyle name_style{
                current ? tokens.accent : tokens.foreground,
                Theme::kFontBody, current ? 600 : 400};
            renderer.DrawText(
                W::EllipsizeText(renderer, name_line, name_style,
                                 nl_w),
                {x, row.y + 4.0f, nl_w, row.height - 8.0f},
                name_style);
            if (!comment.empty()) {
                const XER::UI::Rendering::TextStyle comment_style{
                    tokens.muted, Theme::kFontCaption, 400};
                renderer.DrawText(
                    W::EllipsizeText(renderer, comment, comment_style,
                                     comment_w),
                    {row.Right() - comment_w - pad, row.y + 4.0f,
                     comment_w, row.height - 8.0f},
                    comment_style);
            }
        });
    list_->SetOnItemClick(
        [this](int index, Layout::Point) -> bool {
            // Double-click synthesis — two clicks on the same row
            // inside the window seek through the same path the
            // context menu's "Go to" item uses.
            const auto now = std::chrono::steady_clock::now();
            const bool dbl =
                index == last_click_row_ &&
                now - last_click_time_ < kDoubleClickWindow;
            last_click_row_ = index;
            last_click_time_ = now;
            if (dbl) {
                last_click_row_ = -1;
                SeekToMarker(index);
            }
            return false;   // let the selection update run
        });
    list_->SetOnItemActivated(
        [this](int index) { SeekToMarker(index); });
    list_->SetTooltip(
        "Double-click / Enter seeks; right-click edits");

    status_ = std::make_shared<W::Label>("No markers", true);

    AddChild(search_input_);
    AddChild(color_select_);
    AddChild(add_btn_);
    AddChild(list_);
    AddChild(status_);
    RebuildVisible();
}

// ---- data -----------------------------------------------------------

void MarkersPanel::SetMarkers(std::vector<MarkerRow> rows) {
    rows_ = std::move(rows);
    // Fold the session annotations in (keyed by time — a re-feed
    // preserves what the edit dialog wrote even though the model
    // push itself carries no duration/comment).
    for (MarkerRow& r : rows_) {
        if (r.duration_seconds > 0.0 || !r.comment.empty()) {
            annotations_[r.marker.time] =
                Annotation{r.duration_seconds, r.comment};
        } else if (const auto it = annotations_.find(r.marker.time);
                   it != annotations_.end()) {
            r.duration_seconds = it->second.duration;
            r.comment = it->second.comment;
        }
    }
    last_click_row_ = -1;
    RebuildVisible();
}

void MarkersPanel::SetMarkers(std::vector<Marker> markers) {
    std::vector<MarkerRow> rows;
    rows.reserve(markers.size());
    for (Marker& m : markers) {
        MarkerRow row;
        row.marker = std::move(m);
        rows.push_back(std::move(row));
    }
    SetMarkers(std::move(rows));
}

const MarkerRow* MarkersPanel::RowAt(int visible_index) const {
    if (visible_index < 0 ||
        visible_index >= static_cast<int>(visible_.size())) {
        return nullptr;
    }
    return &rows_[static_cast<std::size_t>(
        visible_[static_cast<std::size_t>(visible_index)])];
}

std::vector<double> MarkersPanel::VisibleTimes() const {
    std::vector<double> out;
    out.reserve(visible_.size());
    for (const int index : visible_) {
        out.push_back(
            rows_[static_cast<std::size_t>(index)].marker.time);
    }
    return out;
}

// ---- view state -------------------------------------------------------

void MarkersPanel::SetSearchText(std::string text) {
    if (search_input_ != nullptr && search_input_->GetValue() != text) {
        search_input_->SetValue(text);
    }
    if (search_ == text) return;
    search_ = std::move(text);
    RebuildVisible();
}

void MarkersPanel::SetColorChoice(int index) {
    if (index < 0 || index >= kPaletteCount) return;
    color_choice_ = index;
    if (color_select_ != nullptr) {
        color_select_->SetSelectedIndex(index);
    }
}

bool MarkersPanel::AnnotationFor(double time, double* duration,
                                 std::string* comment) const {
    const auto it = annotations_.find(time);
    if (it == annotations_.end()) return false;
    if (duration != nullptr) *duration = it->second.duration;
    if (comment != nullptr) *comment = it->second.comment;
    return true;
}

// ---- verbs -------------------------------------------------------------

void MarkersPanel::AddAtPlayhead() {
    if (hooks_.on_add_marker == nullptr) return;
    Marker m;
    m.time = hooks_.playhead_seconds ? hooks_.playhead_seconds()
                                     : playhead_;
    m.name = "Marker";
    m.color = kPalette[color_choice_].rgba;
    hooks_.on_add_marker(m);
}

void MarkersPanel::SeekToMarker(int visible_index) {
    const MarkerRow* row = RowAt(visible_index);
    if (row != nullptr && hooks_.on_seek) {
        hooks_.on_seek(row->marker.time);
    }
}

void MarkersPanel::DeleteMarkerAt(int visible_index) {
    const MarkerRow* row = RowAt(visible_index);
    if (row != nullptr && hooks_.on_remove_marker) {
        hooks_.on_remove_marker(row->marker.time);
    }
}

void MarkersPanel::RenameMarkerAt(int visible_index,
                                  const std::string& name) {
    const MarkerRow* row = RowAt(visible_index);
    if (row == nullptr || name.empty() ||
        name == row->marker.name || hooks_.on_update_marker == nullptr) {
        return;
    }
    Marker updated = row->marker;
    updated.name = name;
    hooks_.on_update_marker(row->marker.time, updated);
}

void MarkersPanel::SetMarkerColorAt(
    int visible_index, const std::array<std::uint8_t, 4>& color) {
    const MarkerRow* row = RowAt(visible_index);
    if (row == nullptr || color == row->marker.color ||
        hooks_.on_update_marker == nullptr) {
        return;
    }
    Marker updated = row->marker;
    updated.color = color;
    hooks_.on_update_marker(row->marker.time, updated);
}

void MarkersPanel::SetMarkerAnnotation(int visible_index,
                                       double duration,
                                       const std::string& comment) {
    const MarkerRow* row = RowAt(visible_index);
    if (row == nullptr) return;
    annotations_[row->marker.time] = Annotation{duration, comment};
    RebuildVisible();
}

// ---- filter -------------------------------------------------------------

void MarkersPanel::RebuildVisible() {
    visible_.clear();
    visible_.reserve(rows_.size());
    const std::string needle = Lower(search_);
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
        const MarkerRow& row = rows_[static_cast<std::size_t>(i)];
        if (!needle.empty()) {
            const bool hit =
                Lower(row.marker.name).find(needle) !=
                    std::string::npos ||
                Lower(row.comment).find(needle) !=
                    std::string::npos ||
                Lower(row.scope).find(needle) != std::string::npos;
            if (!hit) continue;
        }
        visible_.push_back(i);
    }
    if (list_ != nullptr) {
        list_->SetItemCount(static_cast<int>(visible_.size()));
    }
    SyncStatusText();
    Invalidate();
}

void MarkersPanel::SyncStatusText() {
    if (status_ == nullptr) return;
    if (rows_.empty()) {
        status_->SetText("No markers — M adds one at the playhead");
    } else if (visible_.size() == rows_.size()) {
        status_->SetText(std::to_string(rows_.size()) + " marker(s)");
    } else {
        status_->SetText(std::to_string(visible_.size()) + " of " +
                         std::to_string(rows_.size()) + " markers");
    }
}

std::string MarkersPanel::TimecodeFor(double seconds) const {
    if (hooks_.format_timecode) {
        return hooks_.format_timecode(seconds);
    }
    // 30 fps non-drop fallback (headless tests + unwired panels).
    return FormatTimecode(seconds, 30, 1);
}

// ---- context menu + edit dialog --------------------------------------

int MarkersPanel::ListIndexAt(Layout::Point point) const {
    if (list_ == nullptr) return -1;
    const Layout::Rect b = list_->GetBounds();
    if (!b.Contains(point)) return -1;
    const int row = static_cast<int>(
        (point.y - b.y + list_->GetScrollY()) / kRowH);
    return (row >= 0 && row < static_cast<int>(visible_.size()))
               ? row
               : -1;
}

bool MarkersPanel::ShowContextMenuAt(Layout::Point point) {
    EnsureChildren();
    if (list_ == nullptr || !list_->GetBounds().Contains(point)) {
        return false;
    }
    const int index = ListIndexAt(point);
    std::vector<W::ContextMenu::Item> items;
    if (const MarkerRow* row = RowAt(index); row != nullptr) {
        W::ContextMenu::Item go;
        go.label = "Go to marker";
        go.action = [this, index] { SeekToMarker(index); };
        items.push_back(std::move(go));

        W::ContextMenu::Item edit;
        edit.label = "Edit marker…";
        edit.separator_before = true;
        edit.action = [this, index] { OpenEditDialog(index); };
        items.push_back(std::move(edit));

        // Color choices as checked items — one row per palette
        // swatch (ContextMenu has no submenu support).
        for (int i = 0; i < kPaletteCount; ++i) {
            W::ContextMenu::Item color;
            color.label = std::string("Color: ") + kPalette[i].name;
            color.checked =
                row->marker.color == kPalette[i].rgba;
            const auto rgba = kPalette[i].rgba;
            color.action = [this, index, rgba] {
                SetMarkerColorAt(index, rgba);
            };
            items.push_back(std::move(color));
        }

        W::ContextMenu::Item del;
        del.label = "Delete marker";
        del.danger = true;
        del.separator_before = true;
        del.action = [this, index] { DeleteMarkerAt(index); };
        items.push_back(std::move(del));
    }
    W::ContextMenu::Item add;
    add.label = "Add marker @ playhead";
    add.separator_before = !items.empty();
    add.action = [this] { AddAtPlayhead(); };
    items.push_back(std::move(add));

    auto menu = W::ContextMenu::Create(std::move(items));
    menu->SetAnchor(point);
    ShowOverlay(menu);
    return true;
}

void MarkersPanel::OpenEditDialog(int visible_index) {
    EnsureChildren();
    const MarkerRow* row = RowAt(visible_index);
    if (row == nullptr) return;
    edit_row_ = visible_index;
    const double time = row->marker.time;

    edit_name_ = std::make_shared<W::TextInput>(row->marker.name);
    edit_name_->SetPlaceholder("Marker name");
    double dur = row->duration_seconds;
    std::string comment = row->comment;
    AnnotationFor(time, &dur, &comment);
    edit_comment_ = std::make_shared<W::TextInput>(comment);
    edit_comment_->SetPlaceholder("Note (session)");
    char dbuf[24] = "";
    if (dur > 0.0) std::snprintf(dbuf, sizeof(dbuf), "%.3f", dur);
    edit_duration_ = std::make_shared<W::TextInput>(dbuf);
    edit_duration_->SetPlaceholder("0 = point marker");

    std::vector<std::string> names;
    for (const Swatch& s : kPalette) names.emplace_back(s.name);
    edit_color_ = std::make_shared<W::Select>(
        names, PaletteIndexFor(row->marker.color));

    auto form = std::make_shared<EditForm>(edit_name_, edit_comment_,
                                           edit_duration_, edit_color_);
    edit_dialog_ =
        W::Dialog::Create("Edit marker", form, "Apply", "Cancel");
    edit_dialog_->SetOnResult([this](W::Dialog::Result r) {
        if (r == W::Dialog::Result::Accepted) CommitEditDialog();
    });
    edit_dialog_->SetOnClosed([this] { HideOverlay(); });
    ShowOverlay(edit_dialog_);
}

void MarkersPanel::CommitEditDialog() {
    if (edit_row_ < 0) return;
    const MarkerRow* row = RowAt(edit_row_);
    edit_row_ = -1;
    if (row == nullptr) return;
    const double time = row->marker.time;

    Marker updated = row->marker;
    if (edit_name_ != nullptr && !edit_name_->GetValue().empty()) {
        updated.name = edit_name_->GetValue();
    }
    if (edit_color_ != nullptr &&
        edit_color_->GetSelectedIndex() >= 0 &&
        edit_color_->GetSelectedIndex() < kPaletteCount) {
        updated.color =
            kPalette[edit_color_->GetSelectedIndex()].rgba;
    }
    if (updated.name != row->marker.name ||
        updated.color != row->marker.color) {
        if (hooks_.on_update_marker) {
            hooks_.on_update_marker(time, updated);
        }
    }
    // Annotation fields — session-side (see the header comment).
    double dur = 0.0;
    if (edit_duration_ != nullptr) {
        dur = std::atof(edit_duration_->GetValue().c_str());
        if (dur < 0.0) dur = 0.0;
    }
    const std::string comment =
        edit_comment_ != nullptr ? edit_comment_->GetValue()
                                 : std::string();
    annotations_[time] = Annotation{dur, comment};
    RebuildVisible();
}

// ---- widget contract -----------------------------------------------------

void MarkersPanel::Render(XER::UI::Rendering::Renderer& renderer,
                          const XER::UI::Theme::Tokens& tokens) {
    renderer.FillRect(GetBounds(), tokens.panel, 0.0f);
}

void MarkersPanel::Layout(const Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const float x = bounds.x + Theme::kSpaceSm;
    float y = bounds.y + Theme::kSpaceSm;
    const float w = bounds.width - Theme::kSpaceSm * 2.0f;
    const auto& tokens = CachedLayoutTokens();

    // Row 1: search (flex) + color select + add button.
    const float add_w = 76.0f;
    const float color_w = 84.0f;
    search_input_->Layout(
        {x, y, w - add_w - color_w - kToolbarGap * 2.0f, kToolbarH},
        tokens);
    color_select_->Layout(
        {x + w - add_w - color_w - kToolbarGap, y, color_w, kToolbarH},
        tokens);
    add_btn_->Layout({x + w - add_w, y, add_w, kToolbarH}, tokens);
    y += kToolbarH + kToolbarGap;

    const float content_h =
        bounds.y + bounds.height - Theme::kSpaceSm - kStatusH - y;
    const Layout::Rect content{x, y, w, std::max(0.0f, content_h)};
    list_->Layout(content, tokens);
    status_->Layout(
        {x, y + std::max(0.0f, content_h) + 2.0f, w, kStatusH},
        tokens);
}

void MarkersPanel::Tick(double /*dt_seconds*/) {}

}  // namespace XER::VideoEditor::Ui2
