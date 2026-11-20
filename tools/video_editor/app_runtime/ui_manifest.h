// UI manifest loaders — the declarative half of the widget tree.
// Several file formats coexist on purpose (per the asset-pack style):
//
//   assets/ui/toolbar.json   (jsoncpp)  — toolbar buttons + actions
//   assets/ui/strings.ini    (ini)      — label/i18n overrides
//   assets/ui/theme.toml     (toml)     — color token overrides
//   assets/ui/layout.yaml    (yaml-cpp) — panel/nav/workspace layout
//
// All loaders are additive: missing files leave caller defaults.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace XER::VideoEditor::AppRuntime {

// ---- toolbar.json ------------------------------------------------------
// {
//   "Toolbar": {
//     "buttons": [
//       {"id": "import",    "label": "Import", "icon": "bin.import",
//        "action": "file.import"},
//       {"id": "separator", "type": "separator"},
//       {"id": "undo", "label": "Undo", "icon": "action.undo",
//        "action": "edit.undo"}
//     ]
//   }
// }
struct ToolbarButton {
    std::string id;
    std::string label;
    std::string icon;      // logical icon name (icons.yaml)
    std::string action;    // action id dispatched to the app
    bool separator = false;
};

bool LoadToolbarJson(const std::filesystem::path& path,
                     std::vector<ToolbarButton>* buttons,
                     std::string* error);

// ---- strings.ini -------------------------------------------------------
// [labels]
// media_bin_title = MEDIA LIBRARY
// transport_play  = Play
struct UiStrings {
    std::unordered_map<std::string, std::string> labels;
    // "key" → localized label, or `fallback` when unset.
    std::string Get(const std::string& key,
                    const std::string& fallback) const;
};

bool LoadStringsIni(const std::filesystem::path& path, UiStrings* out,
                    std::string* error);

// Process-wide strings, filled by Boot(). Empty → compiled defaults.
UiStrings& SharedUiStrings();

// ---- theme.toml --------------------------------------------------------
// [colors]
// accent       = "#8B5CF6"
// panel_bg     = "#16161A"
// [metrics]
// radius       = 8
struct ThemeOverride {
    std::unordered_map<std::string, std::uint32_t> colors;  // ARGB
    std::unordered_map<std::string, double> metrics;
};

bool LoadThemeToml(const std::filesystem::path& path,
                   ThemeOverride* out, std::string* error);

ThemeOverride& SharedThemeOverride();

// ---- layout.yaml -------------------------------------------------------
// Layout:
//   NavRail:
//     items: [media, audio, text, fx, markers, settings]
//   Panels:
//     left: {visible: true, width: 320}
//     right: {visible: true, width: 340, tab: 0}
//   Monitor: {transport_center: true}
struct LayoutConfig {
    std::vector<std::string> nav_items;   // empty = all compiled items
    bool left_visible = true;
    bool right_visible = true;
    int left_width = 0;                   // 0 = default
    int right_width = 0;
    int right_tab = -1;                   // -1 = keep
    bool transport_center = false;
};

bool LoadLayoutYaml(const std::filesystem::path& path,
                    LayoutConfig* out, std::string* error);

LayoutConfig& SharedLayoutConfig();

// ---- menus.yaml ----------------------------------------------------------
// Context-menu templates; each menu id matches a widget-owned menu.
//   Menus:
//     - id: media_bin.item
//       items:
//         - {action: bin.delete, label: "Delete Clip", danger: true}
//         - {separator: true}
struct MenuItemDef {
    bool separator = false;
    bool danger = false;       // tint destructive rows
    std::string action;        // action id dispatched to the app
    std::string label;
    std::string icon;          // logical icon name (icons.yaml)
    std::string shortcut;      // display-only combo ("Ctrl+X")
};

struct MenuDef {
    std::string id;
    std::vector<MenuItemDef> items;
};

bool LoadMenusYaml(const std::filesystem::path& path,
                   std::vector<MenuDef>* out, std::string* error);

std::vector<MenuDef>& SharedMenusConfig();
const MenuDef* FindMenuDef(const std::string& id);

// ---- effects.yaml --------------------------------------------------------
// Effect-library catalog. `kind` selects which param fields matter:
// "slider" uses min/max/default/unit, "choice" uses choices.
//   Effects:
//     - id: blur.gaussian
//       label: Gaussian Blur
//       params:
//         - {id: radius, kind: slider, min: 0, max: 100, default: 8}
struct EffectParamDef {
    std::string id;
    std::string label;
    std::string kind;            // slider | choice | toggle | ...
    double min_v = 0.0;
    double max_v = 0.0;
    double default_v = 0.0;
    std::string unit;            // display suffix (px, dB, %)
    std::vector<std::string> choices;
};

struct EffectDef {
    std::string id;
    std::string label;
    std::string category;        // bin grouping in the FX panel
    std::string icon;
    std::vector<EffectParamDef> params;
};

bool LoadEffectsYaml(const std::filesystem::path& path,
                     std::vector<EffectDef>* out, std::string* error);

std::vector<EffectDef>& SharedEffectsCatalog();
const EffectDef* FindEffectDef(const std::string& id);

// ---- inspector.yaml ------------------------------------------------------
// Per-selection inspector schemas: which sections/fields a selection
// kind ("clip.video", "track.audio", ...) shows.
//   Inspector:
//     - selection: clip.video
//       sections:
//         - {id: transform, title: Transform,
//            fields: [{id: position, kind: vec2}]}
struct InspectorFieldDef {
    std::string id;
    std::string label;
    std::string kind;            // vec2 | slider | text | ...
};

struct InspectorSectionDef {
    std::string id;
    std::string title;
    std::vector<InspectorFieldDef> fields;
};

struct InspectorSchemaDef {
    std::string selection;
    std::vector<InspectorSectionDef> sections;
};

bool LoadInspectorYaml(const std::filesystem::path& path,
                       std::vector<InspectorSchemaDef>* out,
                       std::string* error);

std::vector<InspectorSchemaDef>& SharedInspectorSchemas();
const InspectorSchemaDef* FindInspectorSchema(
    const std::string& selection);

// ---- workspaces.yaml (layout presets) ------------------------------------
// Column-level presets mirroring Ui::WorkspaceLayout — kept for the
// file copy of the built-ins. The panel-level view of the same file
// is WorkspaceSpec/LoadWorkspacesConfig below.
//   Workspaces:
//     - {name: Editing, left_visible: true, right_visible: true,
//        right_tab: 0, timeline_fraction: 0.32}
struct WorkspaceDef {
    std::string name;
    bool left_visible = true;
    bool right_visible = true;
    int right_tab = 0;              // right-column tab index (0..9)
    double timeline_fraction = 0.32;
};

bool LoadWorkspacesYaml(const std::filesystem::path& path,
                        std::vector<WorkspaceDef>* out,
                        std::string* error);

std::vector<WorkspaceDef>& SharedWorkspaces();

// ---- monitor.yaml --------------------------------------------------------
// Program-monitor chrome: transport button order, overlay toggles,
// preview quality.
//   Monitor:
//     Transport: {buttons: [play, prev_frame, next_frame]}
//     Overlays: {safe_margins: false, grid: false}
//     default_quality: full
struct MonitorConfig {
    std::vector<std::string> transport_buttons;  // empty = compiled set
    bool safe_margins = false;
    bool grid = false;
    std::string default_quality = "full";  // full | half | quarter
};

bool LoadMonitorYaml(const std::filesystem::path& path,
                     MonitorConfig* out, std::string* error);

MonitorConfig& SharedMonitorConfig();

// ---- panels.yaml ---------------------------------------------------------
// The dockable panel set — the declarative half of the widget tree the
// workspaces below rearrange.
//   Panels:
//     dock_size_unit: px            # px | fraction
//     splitter_thickness: 6
//     workspace_order: [Editing, Color, Audio, Effects]
//     panels:
//       - {id: media_bin, title: Media Library, dock: left,
//          size: 320, min_size: 230, visible: true}
struct PanelSpec {
    std::string id;
    std::string title;
    std::string dock = "right";  // left | right | bottom | center | top
    float size = 0.0f;           // dock_size_unit units; 0 = fill/auto
    bool visible = true;
    std::vector<std::string> tabs;  // sibling ids in the same tab group
    int min_size = 0;            // clamp bound, same unit as size
};

struct PanelsConfig {
    std::vector<PanelSpec> panels;
    std::string dock_size_unit = "px";   // "px" or "fraction"
    int splitter_thickness = 6;
    std::vector<std::string> workspace_order;
};

// Returns true on a usable file. Soft problems (an unknown dock value,
// a skipped entry) do not fail the load — they are appended to `error`
// as notes, so a non-empty error with a true return means "loaded with
// warnings".
bool LoadPanelsConfig(const std::filesystem::path& path,
                      PanelsConfig* out, std::string* error);

PanelsConfig& SharedPanelsConfig();

// ---- workspaces.yaml (panel presets) -------------------------------------
// The panel-level view of the same Workspaces: sequence — per-panel
// visibility/size overrides plus the tab to raise. Coexists with
// WorkspaceDef above: old column-level fields stay readable by
// LoadWorkspacesYaml.
//   Workspaces:
//     - name: Editing
//       active_tab: inspector
//       panel_visibility: {media_bin: true, mixer: false}
//       panel_sizes: {media_bin: 320, timeline: 240}
struct WorkspaceSpec {
    std::string name;
    std::map<std::string, bool> panel_visibility;
    std::map<std::string, float> panel_sizes;  // PanelsConfig units
    std::string active_tab;                     // panel id to raise
};

bool LoadWorkspacesConfig(const std::filesystem::path& path,
                          std::vector<WorkspaceSpec>* out,
                          std::string* error);

std::vector<WorkspaceSpec>& SharedWorkspacesConfig();

}  // namespace XER::VideoEditor::AppRuntime
