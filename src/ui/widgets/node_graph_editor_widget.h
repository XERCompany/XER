#ifndef XER_UI_WIDGETS_NODE_GRAPH_EDITOR_WIDGET_H
#define XER_UI_WIDGETS_NODE_GRAPH_EDITOR_WIDGET_H

#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "canvas/canvas.h"
#include "motion/motion.h"
#include "node_graph/node_graph_executor.h"
#include "node_graph/node_graph_history.h"
#include "node_graph/node_graph_project_store.h"
#include "theme/theme.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

class Button;
class Label;
class Select;
class Switch;
class TextInput;

// Full node-graph editor built on XER::NodeGraph. The graph surface is a
// Skia Canvas child HWND (pan/zoom, nodes, typed port links, box select,
// minimap); chrome (toolbar, inspector, status bar) is regular widgets.
// Supports undo/redo, add-node palette, context menus, inline rename,
// subgraph drill-down + collapse, Executor runs and JSON save/load with
// cache autosave.
class SettingsWidget;
class ProjectsWidget;
class Tabs;
class ScrollArea;
class VideoPreviewWidget;
class NodeParamHost;

class NodeGraphEditorWidget final : public Widget {
public:
    explicit NodeGraphEditorWidget(
        void* parent_hwnd,
        std::function<void(Theme::Variant)> on_theme_changed = {},
        std::int64_t initial_project_id = -1);
    ~NodeGraphEditorWidget() override;

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    void Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) override;
    bool HandleEvent(const Event&) override { return false; }

    Layout::Size GetMinSize() const override { return {400.0f, 300.0f}; }
    Layout::Size GetPreferredSize() const override { return {1100.0f, 700.0f}; }
    Layout::Size GetMaxSize() const override { return {100000.0f, 100000.0f}; }

    void Tick();
    void SetView(int index);

    // Optional host hook for plugin nodes (e.g. VST3). When set and the
    // context menu is opened on a "plugin.*" node, an "Open Plugin Editor"
    // entry is offered; the handler receives the canvas rect in window
    // client DIP coordinates.
    void SetPluginEditorHandler(
        std::function<void(NodeGraph::NodeId node_id, float canvas_x,
                           float canvas_y, float canvas_w, float canvas_h)>
            handler);

    // Optional host hook: when set, a "‹ Projects" button appears in the nav
    // row and invokes this callback (the host swaps back to the project
    // browser). Used by the app-level Projects-first flow.
    void SetBrowserBackHook(std::function<void()> hook);

    // Opens a project from the ProjectStore (loads "main" graph, attaches
    // autosave, switches to the graph view). Public for the app shell.
    void OpenProjectById(std::int64_t project_id);
    // Opens a specific graph inside a project; empty graph_name auto-picks
    // "main" (or the first graph). Autosave then writes back to that graph.
    void OpenProjectGraph(std::int64_t project_id,
                          const std::string& graph_name);

    // Root graph access (tests / embedding).
    NodeGraph::Graph& RootGraph();
    const NodeGraph::Graph& RootGraph() const;

    // Hide/show the raw HWND chrome (graph canvas + param overlay). The
    // widget tree does not manage HWND children — the app calls this when
    // swapping this widget out of / back into the window content.
    void SetHostVisible(bool visible);

private:
    struct Level {
        std::unique_ptr<NodeGraph::Graph> owned;  // set only for the root level
        NodeGraph::Graph* graph = nullptr;
        std::unique_ptr<NodeGraph::History> history;
        std::unique_ptr<NodeGraph::Executor> executor;
        double pan_x = 0.0;
        double pan_y = 0.0;
        double zoom = 1.0;
        std::string crumb;
        // For subgraph levels: the node in the parent graph that owns `graph`.
        NodeGraph::NodeId owner_node = NodeGraph::kInvalidNode;
        std::set<NodeGraph::NodeId> selected_nodes;
        // Visual-only collapse: hides param rows, keeps ports/links.
        std::set<NodeGraph::NodeId> collapsed_nodes;
        std::optional<NodeGraph::LinkId> selected_link;
        uint64_t seen_revision = 0;
    };

    struct PortGeom {
        std::string name;
        bool is_output = false;
        double y = 0.0;  // world-space port center
    };

    // Interactive parameter row drawn inside the node card.
    // kind: 0=number (drag-adjust) 1=bool (click toggle)
    //       2=options (click cycles) 3=text (read-only, edit in inspector)
    //       4=file path (text + "…" browse button opens a file dialog)
    struct ParamGeom {
        std::string name;
        int kind = 3;
        double y = 0.0;    // world-space row top
        double h = 0.0;    // world-space row height
        bool browse = false;  // "…" button zone at the row's right edge
    };

    struct NodeGeom {
        double x = 0.0, y = 0.0, w = 190.0, h = 60.0;
        std::vector<PortGeom> ports;
        std::vector<ParamGeom> params;
        // Registry descriptor cached at geometry build — saves a map
        // lookup per node per painted frame.
        const NodeGraph::NodeTypeDescriptor* desc = nullptr;
    };

    // Screen-space link curve, computed once per paint in
    // RebuildGeometries and shared by DrawLinks / HitLink /
    // HitLinkEndpoint — avoids per-link PortScreenPos lookups and gives
    // hit-testing a cheap bbox reject.
    struct LinkGeom {
        float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        float cx1 = 0, cy1 = 0, cx2 = 0, cy2 = 0;
        float minx = 0, miny = 0, maxx = 0, maxy = 0;
    };

    // Canvas-drawn context menu entry (shadcn popover item).
    struct MenuEntry {
        std::string label;
        std::string shortcut;   // right-aligned kbd hint, may be empty
        int action = -1;        // dispatch id; -1 for separators
        bool disabled = false;
        bool danger = false;    // destructive action (red label)
        bool separator = false;
    };

    enum class DragMode { None, Pan, NodeDrag, LinkDrag, BoxSelect, ParamDrag };

    Level& Current() { return levels_.back(); }
    const Level& Current() const { return levels_.back(); }

    // Layout / render
    void PaintCanvas(Canvas::Canvas& c);
    void RebuildGeometries();
    void DrawGrid(Canvas::Canvas& c);
    void DrawGroups(Canvas::Canvas& c);
    void DrawLinks(Canvas::Canvas& c);
    void DrawTempLink(Canvas::Canvas& c);
    void DrawNodes(Canvas::Canvas& c);
    void DrawBoxSelect(Canvas::Canvas& c);
    void DrawSelectionFrame(Canvas::Canvas& c);
    void DrawMinimap(Canvas::Canvas& c);
    void DrawPalette(Canvas::Canvas& c);
    void DrawMenu(Canvas::Canvas& c);
    void DrawPopoverChrome(Canvas::Canvas& c, float x, float y, float w,
                           float h, float alpha = 1.0f);
    void DrawRenameField(Canvas::Canvas& c);
    void DrawToast(Canvas::Canvas& c);
    void DrawChromeOverlay(Canvas::Canvas& c);

    // Theme-derived canvas colors (shadcn palette, works in light+dark).
    const Theme::Tokens& CT() const;
    Canvas::Canvas::Color TC(const Theme::Color& c) const;
    Canvas::Canvas::Color WithAlpha(const Theme::Color& c, float a) const;
    Canvas::Canvas::Color AccentC(float a = 1.0f) const;
    Canvas::Canvas::Color DangerC(float a = 1.0f) const;
    Canvas::Canvas::Color WarnC(float a = 1.0f) const;
    Canvas::Canvas::Color PopoverBg() const;
    Theme::Color Mix(const Theme::Color& a, const Theme::Color& b,
                     float t) const;

    // Context-menu geometry in canvas-local pixels.
    struct MenuBox {
        float x = 0, y = 0, w = 0, h = 0;
    };
    MenuBox ComputeMenuBox() const;
    // Index of the menu_entries_ row under canvas-y, or -1.
    int MenuIndexAtY(double y) const;
    // Row rect (canvas-local) for entry i; sep rows return h=9.
    void MenuEntryRect(int i, float& top, float& h) const;
    // Scroll metrics for the canvas-drawn popovers (menu + palette).
    float MenuContentHeight() const;
    float MenuScrollMax() const;
    float PaletteScrollMax() const;
    void EnsureMenuItemVisible(int index);
    void EnsurePaletteItemVisible(int index);

    // Coordinate helpers (canvas-local DIP space).
    double ToScreenX(double wx) const;
    double ToScreenY(double wy) const;
    double ToWorldX(double sx) const;
    double ToWorldY(double sy) const;

    // Hit testing (canvas-local coords).
    NodeGraph::NodeId HitNode(double sx, double sy) const;
    const PortGeom* HitPort(NodeGraph::NodeId node, double sx, double sy,
                            bool want_output) const;
    NodeGraph::LinkId HitLink(double sx, double sy) const;
    // Link endpoint grab test: returns the link and which end was grabbed
    // (true = output/from end, false = input/to end) when (sx,sy) is within
    // the port grab radius of either endpoint dot.
    NodeGraph::LinkId HitLinkEndpoint(double sx, double sy,
                                      bool* grabbed_output) const;
    const ParamGeom* HitParam(NodeGraph::NodeId node, double sx,
                              double sy) const;
    NodeGraph::PortRef ResolveDropPort(double sx, double sy,
                                       bool want_output) const;
    bool PortScreenPos(NodeGraph::NodeId nid, const std::string& port,
                       bool is_output, double& sx, double& sy) const;

    // Input
    void OnCanvasInput(unsigned msg, int x, int y, int button, int wheel,
                       int key, int mods);
    void HandleMouseDown(int x, int y, int button, int mods, bool dblclk);
    void HandleMouseMove(int x, int y, int button, int mods);
    void HandleMouseUp(int x, int y, int button, int mods);
    void HandleWheel(int x, int y, int wheel);
    void HandleKey(unsigned msg, int key, int mods);
    void HandleChar(int ch);

    // Editing operations (all through History when undoable).
    void DeleteSelection();
    void SelectAll();
    void Undo();
    void Redo();
    // System-clipboard graph editing (JSON payload via XER::Content::Clipboard).
    void CopySelection(bool cut);
    void PasteClipboard();
    void PasteClipboardText(const std::string& text);
    void DuplicateSelection();
    std::string SerializeSelection();
    // Align/distribute for 2+ selected nodes (align: 0=left 1=centerX
    // 2=right 3=top 4=middleY 5=bottom; distribute: horizontal/vertical).
    void AlignSelection(int mode);
    void DistributeSelection(bool horizontal);
    void RunGraph();
    void StepGraph();
    void ResetDebug();
    // Static graph validation (Graph::Validate): cached per graph
    // revision, drawn as badges on node headers, listed in a problems
    // menu opened from the canvas context menu.
    const std::vector<NodeGraph::ValidationIssue>& Issues() const;
    void OpenProblemsMenu(double x, double y);
    void ActivateProblem(int index);
    void FocusNode(NodeGraph::NodeId id);
    void GroupSelection();
    void UngroupSelection();
    void ToggleNodeCollapse(NodeGraph::NodeId id);
    void NewGraph();
    void SaveGraph();
    void LoadGraph();
    void FitView();
    void EnterSubgraph(NodeGraph::NodeId id);
    void ExitSubgraph();
    void CollapseSelection();
    void OpenPalette(double sx, double sy);
    void ClosePalette();
    void RebuildPaletteItems();
    void AddNodeAtPalette(int index);
    void OpenNodeMenu(NodeGraph::NodeId id, double sx, double sy);
    void OpenPluginEditorFor(NodeGraph::NodeId id);
    void OpenLinkMenu(NodeGraph::LinkId id, double sx, double sy);
    void OpenCanvasMenu(double sx, double sy);
    void CloseMenu();
    void ActivateMenuEntry(int idx);
    // Auto-connect free ports: with a node selection only those nodes'
    // ports participate (both directions); without selection the whole
    // graph is wired. Matching is by port type plus name/category
    // affinity; every link goes through History so it is one undo step.
    int Autoconnect();
    // File dialog for io.file_open's `path` param — stores the picked
    // path via History::SetParam.
    void OpenParamFileBrowse(NodeGraph::NodeId node,
                             const std::string& param_name);
    int NextMenuSelectable(int from, int dir) const;
    bool OnChromeContextMenu(const Event& event);
    void StartRename(NodeGraph::NodeId id);
    void CommitRename();
    void CommitRenameCommand();

    // Inspector (param editing widgets).
    void RebuildInspector();
    void ClearInspector();
    void AddParamControl(NodeGraph::Node& node, const NodeGraph::ParamSpec& spec,
                         float x, float y, float w);

    // Persistence.
    void ScheduleAutosave();
    void FlushAutosave();
    NodeGraph::ProjectStore* EnsureProjectStore();
    std::int64_t AutosaveProjectId();
    // Projects page actions (wired to ProjectsWidget hooks).
    void SaveCurrentGraphAsProject(const std::string& name);

    void SetStatus(const std::string& text, bool is_error = false);
    void Toast(const std::string& text, bool is_error = false);

    std::function<void(NodeGraph::NodeId, float, float, float, float)>
        plugin_editor_handler_;

    // HWND-backed param widgets overlaid on node cards (EDIT/COMBO/
    // CHECKBOX children of a click-through host window).
    void SyncParamHost();
    void OnParamHostCommit(std::uint64_t node, const std::string& param,
                           const std::string& text);
    void OnParamHostBrowse(std::uint64_t node, const std::string& param);

    Canvas::Canvas canvas_;
    std::unique_ptr<NodeParamHost> param_host_;
    void* parent_hwnd_ = nullptr;

    std::deque<Level> levels_;

    // Top navigation (TabsMenu) + projects/settings views.
    std::shared_ptr<Tabs> nav_tabs_;
    std::shared_ptr<Button> back_btn_;  // "‹ Projects" — visible with back hook
    std::function<void()> browser_back_hook_;
    std::shared_ptr<ScrollArea> settings_scroll_;
    std::shared_ptr<SettingsWidget> settings_widget_;
    std::shared_ptr<ScrollArea> projects_scroll_;
    std::shared_ptr<ProjectsWidget> projects_widget_;
    // Video preview view (view_ == 3): decode/playback lives in
    // XER::Content::Video::VideoEngine inside the widget.
    std::shared_ptr<VideoPreviewWidget> video_widget_;
    int view_ = 0;  // 0 = graph, 1 = projects, 2 = settings, 3 = video
    // Project the editor is attached to (-1 = detached/autosave project).
    // Autosave writes into this project's "main" graph when set.
    std::int64_t project_id_ = -1;
    bool chrome_attached_ = false;
    bool ctx_handler_set_ = false;

    // Bottom status bar.
    std::shared_ptr<Label> status_label_;

    // Inspector widgets (rebuilt on selection/revision change).
    std::vector<std::shared_ptr<Widget>> inspector_widgets_;
    std::string inspector_key_;
    float inspector_width_ = 0.0f;

    // Geometry cache, rebuilt each paint (world coords for nodes, screen
    // coords for links).
    std::map<NodeGraph::NodeId, NodeGeom> geoms_;
    std::map<NodeGraph::LinkId, LinkGeom> link_geoms_;
    double world_min_x_ = 0.0, world_min_y_ = 0.0;
    double world_max_x_ = 0.0, world_max_y_ = 0.0;
    int canvas_w_ = 0, canvas_h_ = 0;

    // Interaction state.
    DragMode drag_mode_ = DragMode::None;
    double drag_start_sx_ = 0.0, drag_start_sy_ = 0.0;
    double drag_last_sx_ = 0.0, drag_last_sy_ = 0.0;
    double mouse_sx_ = 0.0, mouse_sy_ = 0.0;
    double press_wx_ = 0.0, press_wy_ = 0.0;
    double drag_dx_ = 0.0, drag_dy_ = 0.0;
    std::map<NodeGraph::NodeId, std::pair<double, double>> drag_orig_;
    std::map<NodeGraph::NodeId, std::pair<double, double>> drag_offset_;
    NodeGraph::PortRef link_from_;
    bool link_from_is_output_ = true;
    bool link_dragging_ = false;
    // Drop-port under the cursor for the in-flight link drag, resolved
    // once per mousemove — DrawTempLink reuses it instead of scanning
    // every port on every painted frame.
    NodeGraph::PortRef drop_port_;
    bool drop_valid_ = false;
    // Link whose endpoint is being re-routed by a LinkDrag; disconnected
    // after the drop succeeds on a different port.
    NodeGraph::LinkId reroute_link_ = NodeGraph::kInvalidLink;
    // Link under the cursor — highlighted in DrawLinks.
    NodeGraph::LinkId hovered_link_ = NodeGraph::kInvalidLink;
    // Param drag state (number rows).
    NodeGraph::NodeId param_node_ = NodeGraph::kInvalidNode;
    std::string param_name_;
    int param_kind_ = -1;
    double param_start_x_ = 0.0;
    double param_start_value_ = 0.0;
    Layout::Rect box_rect_{};
    bool box_active_ = false;

    // Add-node palette (canvas-drawn).
    bool palette_open_ = false;
    std::string palette_filter_;
    int palette_hover_ = -1;
    double palette_anchor_x_ = 0.0, palette_anchor_y_ = 0.0;
    std::vector<std::string> palette_items_;
    std::vector<std::string> palette_titles_;
    std::vector<std::string> palette_cats_;
    std::vector<std::string> palette_types_;
    float palette_scroll_ = 0.0f; // px offset into the item list

    // Context menu (canvas-drawn shadcn popover).
    bool menu_open_ = false;
    bool menu_is_link_ = false;
    NodeGraph::NodeId menu_node_ = NodeGraph::kInvalidNode;
    NodeGraph::LinkId menu_link_ = NodeGraph::kInvalidLink;
    std::vector<MenuEntry> menu_entries_;
    int menu_hover_ = -1;
    double menu_x_ = 0.0, menu_y_ = 0.0;
    float menu_scroll_ = 0.0f; // px offset into menu_entries_
    Motion::Tween menu_anim_;    // 0->1 open fade+rise
    Motion::Tween palette_anim_; // 0->1 open fade+rise
    Motion::Tween toast_anim_;   // 0->1 entrance
    NodeGraph::NodeId hovered_node_ = NodeGraph::kInvalidNode;

    // Inline rename.
    bool renaming_ = false;
    NodeGraph::NodeId rename_node_ = NodeGraph::kInvalidNode;
    std::string rename_buffer_;
    // Pending UTF-16 high surrogate for WM_CHAR text input (palette/rename).
    std::uint32_t pending_high_surrogate_ = 0;

    // Run results / status.
    NodeGraph::ExecutionResult last_result_;
    bool has_run_ = false;
    int paste_offset_count_ = 0;  // cascade offset for repeated pastes
    std::set<NodeGraph::NodeId> error_nodes_;
    // Nodes evaluated by the most recent StepGraph() call — highlighted.
    std::set<NodeGraph::NodeId> debug_nodes_;
    // Validation-issue cache — rebuilt lazily when the graph revision
    // changes (Issues() is called from paint, hence mutable).
    mutable const NodeGraph::Graph* issues_graph_ = nullptr;
    mutable std::uint64_t issues_rev_ = 0;
    mutable std::vector<NodeGraph::ValidationIssue> issues_;
    mutable std::unordered_map<NodeGraph::NodeId,
                               NodeGraph::ValidationIssue::Severity>
        node_issue_sev_;
    std::map<NodeGraph::NodeId, std::string> node_error_text_;
    uint64_t last_run_revision_ = 0;
    std::string status_text_ = "Ready";
    bool status_is_error_ = false;
    std::string toast_text_;
    bool toast_is_error_ = false;
    std::chrono::steady_clock::time_point toast_until_{};
    std::chrono::steady_clock::time_point last_tick_{};

    // Autosave (SQLite project store + cache-text fallback).
    bool autosave_enabled_ = true;
    bool autosave_dirty_ = false;
    std::chrono::steady_clock::time_point last_autosave_{};
    std::unique_ptr<NodeGraph::ProjectStore> project_store_;
    std::int64_t autosave_project_id_ = -1;
    // Graph name autosave writes into the attached project ("main" default,
    // or the graph opened via OpenProjectGraph).
    std::string autosave_graph_name_ = "main";

    // Theme tokens captured at Layout time for canvas painting.
    Theme::Tokens canvas_tokens_{};
    bool canvas_tokens_valid_ = false;

    std::string pending_file_path_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_NODE_GRAPH_EDITOR_WIDGET_H
