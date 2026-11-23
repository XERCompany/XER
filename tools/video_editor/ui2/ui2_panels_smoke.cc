// ui2_panels smoke — see ui2_panels_smoke.h. All geometry helpers and
// public verbs are exercised directly; rendering is never invoked
// (the paint contract is structural — virtualized ranges, not
// pixels).
#include "ui2_panels_smoke.h"

#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "bin_surface.h"
#include "color_panel.h"
#include "effects_panel.h"
#include "extensions_panel.h"
#include "history_panel.h"
#include "inspector_panel.h"
#include "markers_panel.h"
#include "media_panel.h"
#include "mixer_panel.h"
#include "monitor_panel.h"
#include "queue_panel.h"
#include "scopes_panel.h"
#include "timeline_strip.h"
#include "transcript_panel.h"
#include "timeline_history.h"
#include "timeline_model.h"
#include "widgets/widget.h"

namespace XER::VideoEditor::Ui2 {
namespace {

namespace W = XER::UI::Widgets;
using XER::UI::Layout::Point;
using XER::UI::Layout::Rect;

bool NearlyEqual(float a, float b, float eps = 0.01f) {
    return std::fabs(a - b) <= eps;
}

// ---- fixture rows --------------------------------------------------

CatalogEntry MakeEntry(std::int64_t id, const std::string& path,
                       double duration, std::int64_t size, int w,
                       int h, bool audio, std::int64_t added = 0) {
    CatalogEntry e;
    e.id = id;
    e.path = path;
    e.duration_seconds = duration;
    e.file_size_bytes = size;
    e.width = w;
    e.height = h;
    e.has_audio = audio;
    e.added_at_unix = added;
    return e;
}

std::vector<CatalogEntry> FixtureEntries() {
    // Two video, two audio, one still image (w/h>0 + .png ext).
    return {
        MakeEntry(1, "D:/media/alpha.mp4", 10.0, 100, 1920, 1080, true,
                  100),
        MakeEntry(2, "D:/media/beta.wav", 20.0, 50, 0, 0, true, 200),
        MakeEntry(3, "D:/media/gamma.mp4", 30.0, 200, 1920, 1080,
                  true, 300),
        MakeEntry(4, "D:/media/delta.mp3", 5.0, 25, 0, 0, true, 400),
        MakeEntry(5, "D:/media/still.png", 0.0, 10, 800, 600, false,
                  500),
    };
}

W::Event Mouse(W::Event::Type type, float x, float y,
               W::MouseButton button = W::MouseButton::Left,
               bool ctrl = false) {
    W::Event e;
    e.type = type;
    e.position = {x, y};
    e.mouse_button = button;
    e.is_ctrl = ctrl;
    return e;
}

// ---- 1. BinGridSurface virtualization + rect math -------------------

std::string CheckGridGeometry() {
    BinGridSurface grid;
    std::vector<BinItem> items;
    items.reserve(20);
    for (int i = 0; i < 20; ++i) {
        BinItem item;
        item.path = "D:/media/clip_" + std::to_string(i) + ".mp4";
        item.name = "clip_" + std::to_string(i);
        item.duration_seconds = 10.0 + i;
        item.has_video = true;
        item.has_audio = true;
        items.push_back(std::move(item));
    }
    grid.SetItems(std::move(items));
    grid.Layout(Rect{0.0f, 0.0f, 540.0f, 300.0f});

    // usable = 540 - 2*8 + 8 = 532 -> floor(532/132) = 4 columns.
    if (grid.Columns() != 4) {
        return "columns: expected 4, got " +
               std::to_string(grid.Columns());
    }
    if (grid.Rows() != 5) {
        return "rows: expected 5, got " + std::to_string(grid.Rows());
    }
    const auto size = grid.CardSize();
    // (524 - 3*8)/4 = 125 wide; height = 125*9/16 + 24 = 94.31.
    if (!NearlyEqual(size.width, 125.0f, 0.5f)) {
        return "card width off: " + std::to_string(size.width);
    }
    // Card 0 rect top-left at inset; card 1 one pitch right.
    const Rect c0 = grid.CardRect(0);
    const Rect c1 = grid.CardRect(1);
    if (!NearlyEqual(c0.x, 8.0f) || !NearlyEqual(c0.y, 8.0f)) {
        return "card0 origin off";
    }
    if (!NearlyEqual(c1.x - c0.x, size.width + 8.0f)) {
        return "card pitch off";
    }
    // ItemAtPoint round trip: center of card 7 -> 7; the 8px gutter
    // between cards hits nothing.
    const Rect c7 = grid.CardRect(7);
    if (grid.ItemAtPoint({c7.x + 2.0f, c7.y + 2.0f}) != 7) {
        return "ItemAtPoint(card7) != 7";
    }
    if (grid.ItemAtPoint({c0.x + c0.width + 4.0f,
                          c0.y + 2.0f}) != -1) {
        return "gutter should hit -1";
    }
    // Virtualization: at scroll 0 the visible range covers only the
    // rows intersecting the 300px viewport, not all 20.
    const auto [first, last] = grid.VisibleIndexRange();
    if (first != 0 || last >= 20 || last < 8) {
        return "visible range off: [" + std::to_string(first) + "," +
               std::to_string(last) + ")";
    }
    // Scroll to bottom -> last row's cards become the range tail.
    grid.ScrollTo(grid.MaxScrollY());
    const auto [sfirst, slast] = grid.VisibleIndexRange();
    if (slast != 20 || sfirst <= 0) {
        return "scrolled range off: [" + std::to_string(sfirst) +
               "," + std::to_string(slast) + ")";
    }
    grid.ScrollTo(0.0f);
    return "";
}

std::string CheckGridEvents() {
    BinGridSurface grid;
    std::vector<BinItem> items;
    for (int i = 0; i < 8; ++i) {
        BinItem item;
        item.path = "D:/media/c" + std::to_string(i) + ".mp4";
        item.has_video = true;
        items.push_back(std::move(item));
    }
    grid.SetItems(std::move(items));
    grid.Layout(Rect{0.0f, 0.0f, 540.0f, 300.0f});

    int sel_changes = 0;
    grid.SetOnSelectionChanged([&] { ++sel_changes; });
    int activated = -1;
    grid.SetOnActivateItem([&](int i) { activated = i; });
    std::vector<std::string> drag_paths;
    int begins = 0, ends = 0;
    grid.SetOnBeginDrag(
        [&](const std::vector<std::string>& paths, Point) {
            ++begins;
            drag_paths = paths;
        });
    grid.SetOnEndDrag([&](Point) { ++ends; });

    // Click card 0 -> selected.
    const Rect c0 = grid.CardRect(0);
    const Point p0{c0.x + 4.0f, c0.y + 4.0f};
    grid.HandleEvent(Mouse(W::Event::Type::MouseDown, p0.x, p0.y));
    grid.HandleEvent(Mouse(W::Event::Type::MouseUp, p0.x, p0.y));
    if (!grid.SelectedIndices().count(0) || sel_changes == 0) {
        return "click did not select card 0";
    }
    // Ctrl+click card 1 -> both selected.
    const Rect c1 = grid.CardRect(1);
    grid.HandleEvent(
        Mouse(W::Event::Type::MouseDown, c1.x + 4.0f, c1.y + 4.0f,
              W::MouseButton::Left, true));
    grid.HandleEvent(
        Mouse(W::Event::Type::MouseUp, c1.x + 4.0f, c1.y + 4.0f,
              W::MouseButton::Left, true));
    if (!grid.SelectedIndices().count(0) ||
        !grid.SelectedIndices().count(1)) {
        return "ctrl+click did not extend selection";
    }
    // Double-click card 0 -> activate.
    grid.HandleEvent(Mouse(W::Event::Type::MouseDown, p0.x, p0.y));
    grid.HandleEvent(Mouse(W::Event::Type::MouseUp, p0.x, p0.y));
    if (activated != 0) {
        return "double-click did not activate card 0 (got " +
               std::to_string(activated) + ")";
    }
    // Press card 1, drag past the threshold -> begin fires with the
    // selection payload; release ends the session.
    grid.HandleEvent(
        Mouse(W::Event::Type::MouseDown, c1.x + 4.0f, c1.y + 4.0f));
    grid.HandleEvent(
        Mouse(W::Event::Type::MouseMove, c1.x + 30.0f, c1.y + 30.0f));
    if (begins != 1 || drag_paths.empty()) {
        return "drag did not promote past threshold";
    }
    grid.HandleEvent(Mouse(W::Event::Type::MouseUp, 900.0f, 700.0f));
    if (ends != 1) {
        return "drag end did not fire";
    }
    return "";
}

// ---- 2. MediaPanel search + sort ------------------------------------

std::string CheckMediaFilterSort() {
    auto panel = MediaPanel::Create();
    panel->SetEntries(FixtureEntries());
    if (panel->EntryCount() != 5 || panel->VisibleCount() != 5) {
        return "entries not fed";
    }
    // Name sort (default): alpha, beta, delta, gamma, still.
    auto paths = panel->VisiblePaths();
    if (paths.size() != 5 || paths[0].find("alpha") == std::string::npos ||
        paths[1].find("beta") == std::string::npos) {
        return "default name sort wrong";
    }
    // Search "mp4" -> alpha, gamma.
    panel->SetSearchText("mp4");
    paths = panel->VisiblePaths();
    if (paths.size() != 2 ||
        paths[0].find("alpha") == std::string::npos ||
        paths[1].find("gamma") == std::string::npos) {
        return "mp4 search wrong";
    }
    panel->SetSearchText("GAMMA");
    if (panel->VisibleCount() != 1) {
        return "case-insensitive search failed";
    }
    panel->SetSearchText("");
    // Kind filter -> audio only: beta, delta.
    panel->SetKindFilter(MediaPanel::KindFilter::Audio);
    paths = panel->VisiblePaths();
    if (paths.size() != 2 ||
        paths[0].find("beta") == std::string::npos ||
        paths[1].find("delta") == std::string::npos) {
        return "audio filter wrong";
    }
    panel->SetKindFilter(MediaPanel::KindFilter::All);
    // Duration sort desc: gamma(30), beta(20), alpha(10), delta(5),
    // still(0).
    panel->SetSortMode(MediaPanel::SortMode::Duration);
    paths = panel->VisiblePaths();
    if (paths.size() != 5 ||
        paths[0].find("gamma") == std::string::npos ||
        paths[1].find("beta") == std::string::npos ||
        paths[4].find("still") == std::string::npos) {
        return "duration sort wrong";
    }
    // Size sort desc: gamma(200), alpha(100), beta(50), delta(25),
    // still(10).
    panel->SetSortMode(MediaPanel::SortMode::Size);
    paths = panel->VisiblePaths();
    if (paths[0].find("gamma") == std::string::npos ||
        paths[4].find("still") == std::string::npos) {
        return "size sort wrong";
    }
    // Imported sort desc: still(500), delta(400), gamma(300),
    // beta(200), alpha(100).
    panel->SetSortMode(MediaPanel::SortMode::Imported);
    paths = panel->VisiblePaths();
    if (paths[0].find("still") == std::string::npos ||
        paths[4].find("alpha") == std::string::npos) {
        return "imported sort wrong";
    }
    // Kind sort: video first (alpha, gamma), still (image rank 1),
    // then audio (beta, delta).
    panel->SetSortMode(MediaPanel::SortMode::Kind);
    paths = panel->VisiblePaths();
    if (paths.size() != 5 ||
        paths[0].find("alpha") == std::string::npos ||
        paths[1].find("gamma") == std::string::npos ||
        paths[2].find("still") == std::string::npos ||
        paths[3].find("beta") == std::string::npos ||
        paths[4].find("delta") == std::string::npos) {
        return "kind sort wrong";
    }
    return "";
}

// ---- 3. EffectsPanel registry + search ---------------------------------

std::string CheckFxTree() {
    const std::vector<FxEntry> entries = BuildFxRegistryEntries();
    // Core table (~22) + builtin registry (~51) — assert the floor,
    // not the exact count (plugins/scripts may add more).
    if (entries.size() < 60) {
        return "catalog too small: " + std::to_string(entries.size());
    }
    const FxEntry* mosaic = nullptr;
    const FxEntry* reframe = nullptr;
    const FxEntry* voice = nullptr;
    const FxEntry* sepia = nullptr;
    for (const FxEntry& e : entries) {
        if (e.type == "builtin:xer.fx.track_mosaic") mosaic = &e;
        if (e.type == "builtin:xer.fx.smart_reframe") reframe = &e;
        if (e.type == "builtin:xer.af.voice_isolate") voice = &e;
        if (e.type == "builtin:xer.fx.sepia") sepia = &e;
    }
    if (mosaic == nullptr || reframe == nullptr ||
        voice == nullptr || sepia == nullptr) {
        return "registry entries missing (mosaic/reframe/voice/sepia)";
    }
    if (mosaic->badge != FxEntry::Badge::Track ||
        reframe->badge != FxEntry::Badge::Track) {
        return "track-driven badge wrong";
    }
    if (voice->badge != FxEntry::Badge::Audio) {
        return "audio badge wrong";
    }
    if (mosaic->default_params.size() != 3 ||
        sepia->default_params.empty()) {
        return "registry param defaults missing";
    }

    auto panel = EffectsPanel::Create();
    panel->SetEntries(entries);
    if (panel->VisibleEntryCount() !=
        static_cast<int>(entries.size())) {
        return "visible count != entry count";
    }
    panel->SetSearchText("mosaic");
    const int filtered = panel->VisibleEntryCount();
    if (filtered < 1 || filtered > 4) {
        return "mosaic search gave " + std::to_string(filtered);
    }
    panel->SetSearchText("");
    if (panel->VisibleEntryCount() !=
        static_cast<int>(entries.size())) {
        return "search clear did not restore";
    }
    // Apply path: provider -> on_apply_fx(clip_ids, type).
    panel->SetClipSelectionProvider(
        [] { return std::vector<std::string>{"clip_a"}; });
    std::string applied_type;
    std::vector<std::string> applied_ids;
    panel->SetOnApplyFx(
        [&](const std::vector<std::string>& ids,
            const std::string& type) {
            applied_ids = ids;
            applied_type = type;
        });
    // Drive the apply directly with a known type (the same funnel
    // double-click/menu take — ApplyType is the single path).
    panel->ApplySelected();   // no selection -> no fire
    if (!applied_type.empty()) {
        return "apply fired with no selection";
    }
    // Favorites toggling shows the group.
    panel->SetFavorites({"builtin:xer.fx.sepia"});
    if (!panel->IsFavorite("builtin:xer.fx.sepia")) {
        return "favorite not recorded";
    }
    return "";
}

// ---- 4/5. Inspector provider round trip + history undo ---------------

Clip MakeClip() {
    Clip c;
    c.id = "clip_a";
    c.source_path = "D:/media/alpha.mp4";
    c.track = 0;
    c.timeline_start = 1.0;
    c.duration = 4.0;
    c.playback_rate = 1.0;
    c.transform.opacity = 1.0f;
    c.transform.scale = 1.0f;
    c.effects.push_back({"gaussian_blur", {2.0f}});
    return c;
}

std::string CheckInspectorRoundTrip() {
    auto panel = InspectorPanel::Create();
    const Clip clip = MakeClip();
    // Provider returns copies — the VeApp seam shape.
    panel->SetSelectionProvider(
        [&clip] { return std::vector<Clip>{clip}; });
    if (panel->SelectedClipCount() != 1) {
        return "provider did not feed the panel";
    }
    const Clip shown = panel->SelectedClips().front();
    if (shown.id != "clip_a" ||
        !NearlyEqual(shown.transform.opacity, 1.0f)) {
        return "provider round trip mismatch";
    }
    // Commit seam: mutated copies leave through on_commit_clips.
    std::vector<Clip> committed;
    InspectorHooks hooks;
    hooks.on_commit_clips =
        [&committed](const std::vector<Clip>& cs) { committed = cs; };
    panel->SetHooks(hooks);
    panel->ApplyTransformField(
        InspectorPanel::TransformField::Opacity, 0.42f);
    if (committed.size() != 1 ||
        !NearlyEqual(committed[0].transform.opacity, 0.42f)) {
        return "opacity commit missing/wrong";
    }
    panel->SetEffectParam(0, 0, 9.5f);
    if (committed.empty() ||
        !NearlyEqual(committed.back().effects[0].params[0], 9.5f)) {
        return "fx param commit missing/wrong";
    }
    return "";
}

std::string CheckInspectorHistory() {
    // Real document + history — the shell's commit mapping.
    Project project;
    Track v1;
    v1.kind = TrackKind::Video;
    v1.name = "V1";
    project.tracks = {v1};
    project.width = 1920;
    project.height = 1080;
    Clip clip = MakeClip();
    project.clips = {clip};
    TimelineHistory history(project);

    auto panel = InspectorPanel::Create();
    panel->SetSelectionProvider(
        [&project] { return std::vector<Clip>{project.clips.front()}; });

    InspectorHooks hooks;
    hooks.on_commit_clips =
        [&history](const std::vector<Clip>& cs) {
            for (const Clip& c : cs) {
                MediaBounds bounds;
                bounds.duration_seconds = c.duration;
                bounds.has_video = true;
                std::string error;
                history.ReplaceClip(c.id, c, bounds, &error);
            }
        };
    panel->SetHooks(hooks);

    panel->ApplyTransformField(
        InspectorPanel::TransformField::PosX, 0.25f);
    if (!NearlyEqual(project.clips[0].transform.offset_x, 0.25f)) {
        return "commit did not land on the project";
    }
    panel->SetEffectParam(0, 0, 7.0f);
    if (!NearlyEqual(project.clips[0].effects[0].params[0], 7.0f)) {
        return "fx param commit did not land";
    }
    // One undo restores BOTH commits? No — each ReplaceClip is its own
    // step (they were separate verbs here); undo twice -> original.
    if (!history.CanUndo()) {
        return "history has nothing to undo";
    }
    history.Undo();   // back out the fx-param commit
    if (!NearlyEqual(project.clips[0].effects[0].params[0], 2.0f)) {
        return "undo did not restore fx param";
    }
    history.Undo();   // back out the transform commit
    if (!NearlyEqual(project.clips[0].transform.offset_x, 0.0f)) {
        return "undo did not restore transform";
    }
    return "";
}

// Size-contract audit — every dockable panel must report an honest
// min <= preferred <= max so the shell's split floors can trust them.
std::string CheckPanelMinSizes() {
    const std::vector<std::pair<const char*,
                                std::shared_ptr<W::Widget>>> panels{
        {"media", MediaPanel::Create()},
        {"effects", EffectsPanel::Create()},
        {"inspector", InspectorPanel::Create()},
        {"history", HistoryPanel::Create()},
        {"queue", QueuePanel::Create()},
        {"markers", MarkersPanel::Create()},
        {"extensions", ExtensionsPanel::Create()},
        {"mixer", MixerPanel::Create()},
        {"color", ColorPanel::Create()},
        {"scopes", ScopesPanel::Create()},
        {"transcript", TranscriptPanel::Create()},
        {"monitor(src)", MonitorPanel::Create(MonitorPanel::Kind::Source)},
        {"monitor(pgm)", MonitorPanel::Create(MonitorPanel::Kind::Program)},
        {"timeline", TimelineStrip::Create()},
        {"bin grid", std::make_shared<BinGridSurface>()},
    };
    for (const auto& [name, panel] : panels) {
        const auto mn = panel->GetMinSize();
        const auto pf = panel->GetPreferredSize();
        const auto mx = panel->GetMaxSize();
        if (mn.width < 0.0f || mn.height < 0.0f) {
            return std::string(name) + ": negative min size";
        }
        if (mn.width > pf.width || mn.height > pf.height) {
            return std::string(name) + ": min exceeds preferred";
        }
        if (pf.width > mx.width || pf.height > mx.height) {
            return std::string(name) + ": preferred exceeds max";
        }
    }
    return "";
}

}  // namespace

std::string Ui2PanelsSmoke() {
    if (const std::string err = CheckGridGeometry(); !err.empty()) {
        return "grid: " + err;
    }
    if (const std::string err = CheckGridEvents(); !err.empty()) {
        return "grid events: " + err;
    }
    if (const std::string err = CheckMediaFilterSort(); !err.empty()) {
        return "media: " + err;
    }
    if (const std::string err = CheckFxTree(); !err.empty()) {
        return "effects: " + err;
    }
    if (const std::string err = CheckInspectorRoundTrip();
        !err.empty()) {
        return "inspector: " + err;
    }
    if (const std::string err = CheckInspectorHistory(); !err.empty()) {
        return "inspector history: " + err;
    }
    if (const std::string err = CheckPanelMinSizes(); !err.empty()) {
        return "min sizes: " + err;
    }
    return "";
}

}  // namespace XER::VideoEditor::Ui2
