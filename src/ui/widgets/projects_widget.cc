#include "projects_widget.h"
#include "helper/string_utils.h"

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

#include <algorithm>
#include <filesystem>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <cwctype>
#include <string>
#include <vector>

#include "cache/cache_manager.h"
#include "node_graph/node_graph_project_store.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/button.h"
#include "widgets/dialog.h"
#include "widgets/label.h"
#include "widgets/text_input.h"

namespace XER::UI::Widgets {

namespace {

constexpr float kPadX = 18.0f;
constexpr float kSideGap = 14.0f;
constexpr float kTopPad = 10.0f;
constexpr float kBottomPad = 18.0f;
constexpr float kCardPad = 14.0f;
constexpr float kSectionGap = 16.0f;
constexpr float kRowH = 60.0f;
constexpr float kRowGap = 8.0f;
constexpr float kBtnH = 34.0f;
constexpr float kBtnW = 64.0f;
constexpr float kInputH = 34.0f;
constexpr float kHeaderGap = 8.0f;
constexpr std::int64_t kArmedDeleteMs = 3000;

// Content widget for the "Configure project" dialog: rename field plus the
// project's graph list. Each graph row offers Open / Rename (inline) /
// Delete (two-click arm).
// Native open-file dialog (images only) — mirrors the helper used by the
// node-graph editor for project files.
#ifdef _WIN32
std::wstring PickImageFile(HWND owner) {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp;*.ico\0"
                     L"All files\0*.*\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"png";
    ofn.lpstrTitle = L"Choose project icon";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return {};
    return std::wstring(file);
}
#endif

// Project avatar storage: the picked image is transcoded by FFmpeg into
// <persistent>/node_graph/icons/<project_id>/icon.jpg. The DB `icon`
// column only stores the "icon.jpg" marker — the file path is always
// derived from the project id.
std::string ProjectIconDir(std::int64_t project_id) {
    const auto dir = XER::Engine::Cache::CacheManager::Instance()
                         .Persistent()
                         .root() /
                     "node_graph" / "icons" / std::to_string(project_id);
    return XER::Engine::Helper::String::WideToUtf8(dir.wstring());
}

std::string ProjectIconJpgPath(std::int64_t project_id) {
    return ProjectIconDir(project_id) + "/icon.jpg";
}

bool HasProjectIconMarker(const std::string& icon) {
    return !icon.empty() && icon.front() != '#';
}

// Converts the picked source image to icons/<id>/icon.jpg via the FFmpeg
// bridge (max 256px on the longest side — plenty for a 36px avatar).
bool ConvertProjectAvatar(const std::string& src_utf8,
                          std::int64_t project_id) {
    const std::string dir = ProjectIconDir(project_id);
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(XER::Engine::Helper::String::Utf8ToWide(dir)), ec);
    if (ec) return false;
    std::string err;
    return XER::engine::bridge::ffmpeg::ImageToJpeg(
        src_utf8, ProjectIconJpgPath(project_id), 256, &err);
}

void RemoveProjectAvatar(std::int64_t project_id) {
    std::error_code ec;
    std::filesystem::remove(
        std::filesystem::path(
            XER::Engine::Helper::String::Utf8ToWide(ProjectIconJpgPath(project_id))),
        ec);
}

// Photo avatar picker: circular preview (picked source image, the stored
// icon.jpg, or a "+" placeholder) + "Choose image…" / "Remove" controls.
// The picked file is only previewed here — the owner converts it through
// FFmpeg into <cache>/node_graph/icons/<project_id>/icon.jpg on confirm,
// so the DB `icon` column just stores the "icon.jpg" marker.
class AvatarPickerWidget final : public Widget {
public:
    // preview_path: absolute path of the already-stored icon (may be empty).
    // has_icon: whether the project row should keep the icon marker.
    AvatarPickerWidget(void* owner_hwnd, std::string preview_path,
                       bool has_icon)
        : owner_hwnd_(owner_hwnd),
          preview_path_(std::move(preview_path)), has_icon_(has_icon) {}

    // True when the project should store the icon marker ("icon.jpg").
    bool HasIcon() const { return has_icon_; }
    // Absolute path of a newly picked source image ("" when unchanged).
    const std::string& TakeIconSource() const { return picked_src_; }

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override {
        const auto b = GetBounds();
        const Layout::Rect pv{b.x, b.y + (b.height - 30.0f) * 0.5f,
                              30.0f, 30.0f};
        const bool drawn = !preview_path_.empty() &&
            renderer.DrawImage(preview_path_, pv, 1.0f, 15.0f);
        if (!drawn) {
            Theme::Color fill = tokens.secondary;
            fill.a *= 0.45f;
            renderer.FillRect(pv, fill, 15.0f);
            const Rendering::TextStyle plus{tokens.muted,
                                            tokens.font_body, 400};
            renderer.DrawText(
                "+",
                Layout::Rect{pv.x + pv.width * 0.5f - 4.0f,
                             pv.y + pv.height * 0.5f - 9.0f, 10.0f, 18.0f},
                plus);
        }
        renderer.StrokeRect(pv, tokens.border, 1.0f, 15.0f);

        const Layout::Rect choose{
            b.x + 40.0f, b.y + (b.height - 26.0f) * 0.5f, 116.0f, 26.0f};
        Theme::Color cbg = tokens.secondary;
        cbg.a *= 0.85f;
        renderer.FillRect(choose, cbg, tokens.radius_small);
        renderer.StrokeRect(choose, tokens.border, 1.0f,
                            tokens.radius_small);
        const Rendering::TextStyle cs{tokens.foreground,
                                      tokens.font_small, 500};
        const auto m = renderer.MeasureText("Choose image…", cs);
        renderer.DrawText(
            "Choose image…",
            Layout::Rect{choose.x + (choose.width - m.width) * 0.5f,
                         choose.y + (choose.height - cs.size) * 0.5f,
                         m.width + 4.0f, cs.size + 4.0f},
            cs);
        choose_rect_ = choose;

        remove_rect_ = {};
        if (has_icon_) {
            const Layout::Rect rem{choose.x + choose.width + 10.0f,
                                   choose.y, 52.0f, 26.0f};
            const Rendering::TextStyle rs{tokens.muted,
                                          tokens.font_small, 500};
            const auto rm = renderer.MeasureText("Remove", rs);
            renderer.DrawText(
                "Remove",
                Layout::Rect{rem.x + (rem.width - rm.width) * 0.5f,
                             rem.y + (rem.height - rs.size) * 0.5f,
                             rm.width + 4.0f, rs.size + 4.0f},
                rs);
            remove_rect_ = rem;
        }
    }

    void Layout(const Layout::Rect& bounds) override {
        SetBounds(bounds);
        bounds_ = bounds;
    }
    void Layout(const Layout::Rect& bounds, const Theme::Tokens&) override {
        Layout(bounds);
    }

    bool HandleEvent(const Event& event) override {
        if (event.type != Event::Type::Click ||
            event.mouse_button != MouseButton::Left) {
            return false;
        }
        if (choose_rect_.Contains(event.position)) {
            PickAvatar();
            Invalidate();
            return true;
        }
        if (remove_rect_.width > 0.0f &&
            remove_rect_.Contains(event.position)) {
            has_icon_ = false;
            picked_src_.clear();
            preview_path_.clear();
            Invalidate();
            return true;
        }
        return false;
    }

    Layout::Size GetMinSize() const override { return {200.0f, 30.0f}; }
    Layout::Size GetPreferredSize() const override {
        return {320.0f, 34.0f};
    }
    Layout::Size GetMaxSize() const override { return {600.0f, 60.0f}; }

private:
    void PickAvatar() {
#ifdef _WIN32
        const std::wstring src = PickImageFile(
            static_cast<HWND>(owner_hwnd_));
        if (src.empty()) return;
        picked_src_ = XER::Engine::Helper::String::WideToUtf8(src);
        preview_path_ = picked_src_;
        has_icon_ = true;
#endif
    }

    void* owner_hwnd_ = nullptr;
    std::string preview_path_;
    std::string picked_src_;
    bool has_icon_ = false;
    Layout::Rect bounds_{};
    Layout::Rect choose_rect_{};
    Layout::Rect remove_rect_{};
};

class ProjectConfigContent final : public Widget {
public:
    ProjectConfigContent(
        NodeGraph::ProjectStore* store, std::int64_t pid,
        const std::string& name, void* owner_hwnd,
        std::string icon_preview_path, bool has_icon,
        std::function<void(const std::string&, bool)> on_notify,
        std::function<void(const std::string&)> on_open_graph)
        : store_(store), pid_(pid), on_notify_(std::move(on_notify)),
          on_open_graph_(std::move(on_open_graph)) {
        name_input_ = std::make_shared<TextInput>(name);
        name_input_->SetPlaceholder("Project name");
        AddChild(name_input_);
        // Prefill version / cache path / nodes data from the store row.
        NodeGraph::ProjectStore::ProjectInfo info;
        const bool have_info =
            store_ != nullptr && store_->GetProject(pid_, &info, nullptr);
        if (have_info) {
            project_id_text_ = "ID: " + std::to_string(info.id);
            version_input_ = std::make_shared<TextInput>(
                std::to_string(info.version));
            cache_path_input_ =
                std::make_shared<TextInput>(info.cache_path);
            nodes_data_input_ =
                std::make_shared<TextInput>(info.nodes_data);
        } else {
            project_id_text_ = "ID: " + std::to_string(pid_);
            version_input_ = std::make_shared<TextInput>("1");
            cache_path_input_ = std::make_shared<TextInput>("");
            nodes_data_input_ = std::make_shared<TextInput>("");
        }
        version_input_->SetPlaceholder("Version");
        cache_path_input_->SetPlaceholder("Project cache path");
        nodes_data_input_->SetPlaceholder("Nodes data (JSON)");
        icon_picker_ = std::make_shared<AvatarPickerWidget>(
            owner_hwnd, std::move(icon_preview_path), has_icon);
        AddChild(version_input_);
        AddChild(cache_path_input_);
        AddChild(nodes_data_input_);
        AddChild(icon_picker_);
        RebuildGraphs();
    }

    std::string TakeName() const { return name_input_->GetValue(); }
    int TakeVersion() const {
        try { return std::stoi(version_input_->GetValue()); }
        catch (...) { return 1; }
    }
    std::string TakeCachePath() const { return cache_path_input_->GetValue(); }
    std::string TakeNodesData() const { return nodes_data_input_->GetValue(); }
    bool HasIcon() const { return icon_picker_->HasIcon(); }
    const std::string& TakeIconSource() const {
        return icon_picker_->TakeIconSource();
    }

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override {
        // Field captions above each config input (positions mirror Layout).
        Rendering::TextStyle cap{tokens.muted, tokens.font_small, 400};
        renderer.DrawText("Name", Layout::Rect{bounds_.x, bounds_.y - 15.0f, 200.0f, 12.0f}, cap);
        if (!project_id_text_.empty()) {
            renderer.DrawText(project_id_text_,
                              Layout::Rect{bounds_.x + bounds_.width - 160.0f,
                                           bounds_.y - 15.0f, 160.0f, 12.0f}, cap);
        }
        renderer.DrawText("Version", Layout::Rect{bounds_.x, bounds_.y + 19.0f, 200.0f, 12.0f}, cap);
        renderer.DrawText("Cache path", Layout::Rect{bounds_.x + 136.0f, bounds_.y + 19.0f, 200.0f, 12.0f}, cap);
        renderer.DrawText("Nodes data", Layout::Rect{bounds_.x, bounds_.y + 79.0f, 200.0f, 12.0f}, cap);
        renderer.DrawText("Icon", Layout::Rect{bounds_.x, bounds_.y + 113.0f, 200.0f, 12.0f}, cap);
        // "Graphs" section label above the list.
        Rendering::TextStyle style{tokens.muted, tokens.font_small, 600};
        renderer.DrawText("Graphs in this project",
                          Layout::Rect{bounds_.x, graphs_label_y_,
                                       bounds_.width, 14.0f},
                          style);
    }

    void Layout(const Layout::Rect& bounds) override {
        bounds_ = bounds;
        SetBounds(bounds);
        name_input_->Layout(
            Layout::Rect{bounds.x, bounds.y, bounds.width, 30.0f});
        version_input_->Layout(
            Layout::Rect{bounds.x, bounds.y + 34.0f, 120.0f, 26.0f});
        cache_path_input_->Layout(
            Layout::Rect{bounds.x + 136.0f, bounds.y + 34.0f,
                         bounds.width - 136.0f, 26.0f});
        nodes_data_input_->Layout(
            Layout::Rect{bounds.x, bounds.y + 96.0f, bounds.width, 26.0f});
        icon_picker_->Layout(
            Layout::Rect{bounds.x, bounds.y + 128.0f, bounds.width, 32.0f});
        graphs_label_y_ = bounds.y + 170.0f;
        float y = graphs_label_y_ + 20.0f;
        for (auto& g : graph_rows_) {
            if (g.renaming) {
                // Inline rename row: input + OK + Cancel.
                g.rename_input->Layout(Layout::Rect{
                    bounds.x, y + 3.0f, bounds.width - 130.0f, 24.0f});
                g.rename_ok->Layout(Layout::Rect{
                    bounds.x + bounds.width - 124.0f, y + 3.0f, 52.0f, 24.0f});
                g.rename_cancel->Layout(Layout::Rect{
                    bounds.x + bounds.width - 66.0f, y + 3.0f, 60.0f, 24.0f});
            } else {
                // Normal row: label + Open / Rename / Delete.
                g.label->Layout(Layout::Rect{
                    bounds.x, y + 4.0f, bounds.width - 196.0f, 16.0f});
                float bx = bounds.x + bounds.width;
                g.del->Layout(Layout::Rect{bx - 56.0f, y + 3.0f, 56.0f, 24.0f});
                bx -= 62.0f;
                g.rename_btn->Layout(
                    Layout::Rect{bx - 62.0f, y + 3.0f, 62.0f, 24.0f});
                bx -= 68.0f;
                g.open_btn->Layout(
                    Layout::Rect{bx - 54.0f, y + 3.0f, 54.0f, 24.0f});
            }
            y += 30.0f;
        }
        if (empty_label_ != nullptr) {
            empty_label_->Layout(
                Layout::Rect{bounds.x, y + 2.0f, bounds.width, 18.0f});
            y += 22.0f;
        }
        content_h_ = y - bounds.y + 8.0f;
    }

    void Layout(const Layout::Rect& bounds, const Theme::Tokens&) override {
        Layout(bounds);
    }

    bool HandleEvent(const Event&) override { return false; }

    Layout::Size GetMinSize() const override { return {280.0f, 160.0f}; }
    Layout::Size GetPreferredSize() const override {
        return {380.0f, desired_h_ > 0.0f ? desired_h_ : 220.0f};
    }
    Layout::Size GetMaxSize() const override { return {640.0f, 480.0f}; }
    float GetContentHeight() const override {
        return desired_h_ > 0.0f ? desired_h_ : 220.0f;
    }

private:
    struct GraphRow {
        std::string name;
        std::shared_ptr<Label> label;
        std::shared_ptr<Button> open_btn;
        std::shared_ptr<Button> rename_btn;
        std::shared_ptr<Button> del;
        // Inline-rename state (swaps label+buttons for input+OK/Cancel).
        bool renaming = false;
        std::shared_ptr<TextInput> rename_input;
        std::shared_ptr<Button> rename_ok;
        std::shared_ptr<Button> rename_cancel;
    };

    void DetachRowWidgets(GraphRow& g) {
        if (g.label) RemoveChild(g.label);
        if (g.open_btn) RemoveChild(g.open_btn);
        if (g.rename_btn) RemoveChild(g.rename_btn);
        if (g.del) RemoveChild(g.del);
        if (g.rename_input) RemoveChild(g.rename_input);
        if (g.rename_ok) RemoveChild(g.rename_ok);
        if (g.rename_cancel) RemoveChild(g.rename_cancel);
    }

    void RebuildGraphs() {
        for (auto& g : graph_rows_) DetachRowWidgets(g);
        graph_rows_.clear();
        empty_label_.reset();

        if (store_ != nullptr && store_->IsOpen()) {
            const auto graphs = store_->ListGraphs(pid_);
            if (graphs.empty()) {
                empty_label_ = std::make_shared<Label>("No graphs yet", true);
                AddChild(empty_label_);
            }
            for (const auto& gi : graphs) {
                GraphRow row;
                row.name = gi.name;
                const std::string gname = gi.name;
                row.renaming = (renaming_graph_ == gname);

                if (!row.renaming) {
                    row.label = std::make_shared<Label>(
                        gi.name + "  ·  updated " +
                            FormatGraphTime(gi.updated_at),
                        true);
                    row.open_btn = std::make_shared<Button>("Open",
                                                            ButtonStyle::Outline);
                    row.open_btn->SetTooltip("Open this graph in the editor");
                    row.open_btn->SetOnClick([this, gname] {
                        if (on_open_graph_) on_open_graph_(gname);
                    });
                    row.rename_btn = std::make_shared<Button>("Rename",
                                                              ButtonStyle::Ghost);
                    row.rename_btn->SetTooltip("Rename this graph");
                    row.rename_btn->SetOnClick([this, gname] {
                        renaming_graph_ = gname;
                        RebuildGraphs();
                        RequestLayout();
                    });
                    const bool armed = (armed_graph_ == gname);
                    row.del = std::make_shared<Button>(
                        armed ? "Sure?" : "Delete",
                        armed ? ButtonStyle::Destructive : ButtonStyle::Ghost);
                    row.del->SetOnClick([this, gname] {
                        if (armed_graph_ != gname) {
                            armed_graph_ = gname;
                            RebuildGraphs();
                            RequestLayout();
                            Invalidate();
                            return;
                        }
                        armed_graph_.clear();
                        std::string err;
                        if (store_->DeleteGraph(pid_, gname, &err)) {
                            if (on_notify_)
                                on_notify_("Deleted graph '" + gname + "'",
                                           false);
                        } else if (on_notify_) {
                            on_notify_("Delete graph failed — " + err, true);
                        }
                        RebuildGraphs();
                        RequestLayout();
                        Invalidate();
                    });
                    AddChild(row.label);
                    AddChild(row.open_btn);
                    AddChild(row.rename_btn);
                    AddChild(row.del);
                } else {
                    // Inline rename: input + OK / Cancel.
                    row.rename_input = std::make_shared<TextInput>(gname);
                    row.rename_input->SetPlaceholder("Graph name");
                    row.rename_ok =
                        std::make_shared<Button>("OK", ButtonStyle::Primary);
                    row.rename_ok->SetOnClick([this, gname] {
                        CommitGraphRename(gname);
                    });
                    row.rename_cancel =
                        std::make_shared<Button>("Cancel", ButtonStyle::Ghost);
                    row.rename_cancel->SetOnClick([this] {
                        renaming_graph_.clear();
                        RebuildGraphs();
                        RequestLayout();
                    });
                    AddChild(row.rename_input);
                    AddChild(row.rename_ok);
                    AddChild(row.rename_cancel);
                }
                graph_rows_.push_back(std::move(row));
            }
        } else if (on_notify_) {
            on_notify_("Project database is not available", true);
        }
        // Eager height for Dialog::PanelRect (queried before first Layout):
        // name input + "Graphs" label + rows*30 + padding.
        desired_h_ = 216.0f +
                     static_cast<float>(std::max<std::size_t>(
                         graph_rows_.size() + (empty_label_ ? 1 : 0), 1)) *
                         30.0f;
        RequestLayout();
    }

    void CommitGraphRename(const std::string& old_name) {
        renaming_graph_.clear();
        // Find the row to read the edited value.
        for (auto& g : graph_rows_) {
            if (g.name != old_name || !g.rename_input) continue;
            std::string renamed = XER::Engine::Helper::String::TrimAsciiWhitespaceCopy(
                g.rename_input->GetValue());
            if (!renamed.empty() && renamed != old_name && store_ != nullptr) {
                std::string err;
                if (store_->RenameGraph(pid_, old_name, renamed, &err)) {
                    if (on_notify_)
                        on_notify_("Renamed graph '" + old_name + "' → '" +
                                       renamed + "'",
                                   false);
                } else if (on_notify_) {
                    on_notify_("Rename failed — " + err, true);
                }
            }
            break;
        }
        RebuildGraphs();
        RequestLayout();
        Invalidate();
    }

    static std::string FormatGraphTime(double epoch) {
        if (epoch <= 0.0) return "never";
        const std::time_t t = static_cast<std::time_t>(epoch);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
        return buf;
    }

    NodeGraph::ProjectStore* store_ = nullptr;
    std::int64_t pid_ = 0;
    std::function<void(const std::string&, bool)> on_notify_;
    std::function<void(const std::string&)> on_open_graph_;
    std::shared_ptr<TextInput> name_input_;
    std::shared_ptr<TextInput> version_input_;
    std::shared_ptr<TextInput> cache_path_input_;
    std::shared_ptr<TextInput> nodes_data_input_;
    std::shared_ptr<AvatarPickerWidget> icon_picker_;
    std::string project_id_text_;
    std::vector<GraphRow> graph_rows_;
    std::shared_ptr<Label> empty_label_;
    std::string armed_graph_;
    std::string renaming_graph_;
    Layout::Rect bounds_{};
    float graphs_label_y_ = 0.0f;
    float content_h_ = 0.0f;
    float desired_h_ = 0.0f;
};


// Body of the "New project" dialog: name + version + cache path +
// nodes data + icon picker. Fixed content height so Dialog::PanelRect
// sizes the panel before first Layout.
class ProjectCreateContent final : public Widget {
public:
    explicit ProjectCreateContent(void* owner_hwnd)
        : owner_hwnd_(owner_hwnd) {
        name_input_ = std::make_shared<TextInput>("");
        name_input_->SetPlaceholder("Project name");
        version_input_ = std::make_shared<TextInput>("1");
        version_input_->SetPlaceholder("Version");
        cache_path_input_ = std::make_shared<TextInput>("");
        cache_path_input_->SetPlaceholder("Project cache path");
        nodes_data_input_ = std::make_shared<TextInput>("");
        nodes_data_input_->SetPlaceholder("Nodes data (JSON)");
        icon_picker_ = std::make_shared<AvatarPickerWidget>(
            owner_hwnd_, "", false);
        for (auto* w : {&name_input_, &version_input_, &cache_path_input_,
                        &nodes_data_input_}) {
            AddChild(*w);
        }
        AddChild(icon_picker_);
    }

    std::string TakeName() const { return name_input_->GetValue(); }
    int TakeVersion() const {
        try { return std::stoi(version_input_->GetValue()); }
        catch (...) { return 1; }
    }
    std::string TakeCachePath() const { return cache_path_input_->GetValue(); }
    std::string TakeNodesData() const { return nodes_data_input_->GetValue(); }
    bool HasIcon() const { return icon_picker_->HasIcon(); }
    const std::string& TakeIconSource() const {
        return icon_picker_->TakeIconSource();
    }

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override {
        const Rendering::TextStyle cap{tokens.muted, tokens.font_small, 400};
        renderer.DrawText("Name", Layout::Rect{bounds_.x, bounds_.y - 15.0f, 200.0f, 12.0f}, cap);
        renderer.DrawText("Version", Layout::Rect{bounds_.x, bounds_.y + 19.0f, 200.0f, 12.0f}, cap);
        renderer.DrawText("Cache path", Layout::Rect{bounds_.x + 136.0f, bounds_.y + 19.0f, 200.0f, 12.0f}, cap);
        renderer.DrawText("Nodes data", Layout::Rect{bounds_.x, bounds_.y + 79.0f, 200.0f, 12.0f}, cap);
        renderer.DrawText("Icon", Layout::Rect{bounds_.x, bounds_.y + 113.0f, 200.0f, 12.0f}, cap);
    }

    void Layout(const Layout::Rect& bounds) override {
        bounds_ = bounds;
        SetBounds(bounds);
        name_input_->Layout(Layout::Rect{bounds.x, bounds.y, bounds.width, 30.0f});
        version_input_->Layout(Layout::Rect{bounds.x, bounds.y + 34.0f, 120.0f, 26.0f});
        cache_path_input_->Layout(Layout::Rect{bounds.x + 136.0f, bounds.y + 34.0f, bounds.width - 136.0f, 26.0f});
        nodes_data_input_->Layout(Layout::Rect{bounds.x, bounds.y + 96.0f, bounds.width, 26.0f});
        icon_picker_->Layout(Layout::Rect{bounds.x, bounds.y + 128.0f, bounds.width, 32.0f});
    }

    void Layout(const Layout::Rect& bounds, const Theme::Tokens&) override {
        Layout(bounds);
    }
    bool HandleEvent(const Event&) override { return false; }

    Layout::Size GetMinSize() const override { return {280.0f, 200.0f}; }
    Layout::Size GetPreferredSize() const override { return {420.0f, 240.0f}; }
    Layout::Size GetMaxSize() const override { return {640.0f, 400.0f}; }
    float GetContentHeight() const override { return 176.0f; }

private:
    std::shared_ptr<TextInput> name_input_;
    std::shared_ptr<TextInput> version_input_;
    std::shared_ptr<TextInput> cache_path_input_;
    std::shared_ptr<TextInput> nodes_data_input_;
    std::shared_ptr<AvatarPickerWidget> icon_picker_;
    void* owner_hwnd_ = nullptr;
    Layout::Rect bounds_{};
};

// Headless UI-style name prompt body: muted description line, a small
// field caption and the text input. Reports a fixed content height so
// Dialog::PanelRect sizes the panel before first Layout.
class NamePromptContent final : public Widget {
public:
    NamePromptContent(std::string description, std::string caption,
                      std::string initial)
        : description_(std::move(description)), caption_(std::move(caption)) {
        input_ = std::make_shared<TextInput>(initial);
        input_->SetPlaceholder(caption_);
        AddChild(input_);
    }

    const std::string& TakeName() const { return input_->GetValue(); }

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override {
        const auto b = GetBounds();
        renderer.DrawText(
            description_, Layout::Rect{b.x, b.y, b.width, 16.0f},
            Rendering::TextStyle{tokens.muted, tokens.font_small, 400});
        renderer.DrawText(
            caption_, Layout::Rect{b.x, b.y + 22.0f, b.width, 14.0f},
            Rendering::TextStyle{tokens.muted, tokens.font_small, 600});
    }

    void Layout(const Layout::Rect& bounds) override {
        SetBounds(bounds);
        input_->Layout(Layout::Rect{bounds.x, bounds.y + 42.0f,
                                    bounds.width, 34.0f});
    }

    void Layout(const Layout::Rect& bounds, const Theme::Tokens&) override {
        Layout(bounds);
    }

    bool HandleEvent(const Event&) override { return false; }

    Layout::Size GetMinSize() const override { return {240.0f, 80.0f}; }
    Layout::Size GetPreferredSize() const override { return {360.0f, 88.0f}; }
    Layout::Size GetMaxSize() const override { return {640.0f, 200.0f}; }
    float GetContentHeight() const override { return 88.0f; }

private:
    std::string description_;
    std::string caption_;
    std::shared_ptr<TextInput> input_;
};

// First UTF-8 code point of `name`, ASCII-uppercased when possible.
// Used for the project avatar letter (Cyrillic passes through unchanged).
std::string AvatarLetter(const std::string& name) {
    if (name.empty()) return "?";
    const auto* s = reinterpret_cast<const unsigned char*>(name.data());
    std::size_t len = 1;
    if (s[0] < 0x80) {
        len = 1;
    } else if ((s[0] & 0xE0) == 0xC0) {
        len = 2;
    } else if ((s[0] & 0xF0) == 0xE0) {
        len = 3;
    } else if ((s[0] & 0xF8) == 0xF0) {
        len = 4;
    }
    len = std::min(len, name.size());
    std::string out = name.substr(0, len);
    if (out.size() == 1 && out[0] >= 'a' && out[0] <= 'z') {
        out[0] = static_cast<char>(out[0] - 'a' + 'A');
    }
    return out;
}

// Trim ASCII whitespace from both ends of a project/graph name.
std::string TrimName(std::string s) {
    return XER::Engine::Helper::String::TrimAsciiWhitespaceCopy(s);
}

// "autosave" is the editor's internal autosave bucket — users must not
// create or rename projects to it.
bool IsReservedProjectName(const std::string& name) {
    return name == "autosave";
}

// A single project row: hover-highlighted rounded surface, letter avatar,
// name + meta text, and action buttons pinned to the right edge. Clicking
// the row body (not a button) triggers the primary action.

class ProjectRowWidget final : public Widget {
public:
    ProjectRowWidget(std::string name, std::string meta,
                     std::vector<std::shared_ptr<Button>> buttons,
                     std::string icon_path,
                     std::function<void()> on_activate)
        : name_(std::move(name)), meta_(std::move(meta)),
          buttons_(std::move(buttons)), icon_path_(std::move(icon_path)),
          on_activate_(std::move(on_activate)) {
        for (auto& b : buttons_) AddChild(b);
    }

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override {
        const auto b = GetBounds();
        if (RowHot()) {
            Theme::Color bg = tokens.secondary;
            bg.a *= 0.6f;
            renderer.FillRect(b, bg, tokens.radius_medium);
            // Accent spine on the leading edge — studio list highlight.
            Theme::Color spine = tokens.accent;
            spine.a *= 0.85f;
            renderer.FillRect(
                Layout::Rect{b.x + 3.0f, b.y + 8.0f, 3.0f,
                             std::max(0.0f, b.height - 16.0f)},
                spine, 1.5f);
        }

        // Avatar: project photo (icons/<id>/icon.jpg) clipped to a circle;
        // falls back to the accent-tinted initial disc when absent.
        constexpr float kAvatar = 36.0f;
        const Layout::Rect avatar{b.x + 14.0f,
                                  b.y + (b.height - kAvatar) * 0.5f,
                                  kAvatar, kAvatar};
        const Theme::Color accent = tokens.accent;
        const bool has_photo = !icon_path_.empty() &&
            renderer.DrawImage(icon_path_, avatar, 1.0f, kAvatar * 0.5f);
        if (!has_photo) {
            Theme::Color abg = accent;
            abg.a *= 0.18f;
            renderer.FillRect(avatar, abg, kAvatar * 0.5f);
            const Rendering::TextStyle ls{accent, tokens.font_body, 700};
            renderer.DrawText(AvatarLetter(name_), avatar, ls);
        }
        Theme::Color aring = accent;
        aring.a *= 0.45f;
        renderer.StrokeRect(avatar, aring, 1.0f, kAvatar * 0.5f);

        // Name + meta, left-aligned next to the avatar, right edge stops
        // before the button cluster.
        const float tx = avatar.x + avatar.width + 12.0f;
        const float tw = std::max(40.0f, buttons_x_ - tx - 8.0f);
        const Rendering::TextStyle name_style{tokens.foreground,
                                              tokens.font_body, 500};
        renderer.DrawText(name_,
                          Layout::Rect{tx, b.y + 11.0f, tw, 18.0f},
                          name_style);
        const Rendering::TextStyle meta_style{tokens.muted,
                                              tokens.font_small, 400};
        renderer.DrawText(meta_,
                          Layout::Rect{tx, b.y + 31.0f, tw, 14.0f},
                          meta_style);
    }

    void Layout(const Layout::Rect& bounds) override {
        SetBounds(bounds);
        bounds_ = bounds;
        // Buttons: right-aligned cluster, vertically centered, 8px gaps.
        constexpr float kRowBtnH = 30.0f, kGap = 8.0f, kPad = 12.0f;
        float bx = bounds.x + bounds.width - kPad;
        for (auto it = buttons_.rbegin(); it != buttons_.rend(); ++it) {
            const float w = std::max(56.0f, (*it)->GetPreferredSize().width);
            bx -= w;
            (*it)->Layout(Layout::Rect{bx, bounds.y + (bounds.height - kRowBtnH) * 0.5f,
                                       w, kRowBtnH});
            bx -= kGap;
        }
        buttons_x_ = bx + kGap;
    }

    void Layout(const Layout::Rect& bounds, const Theme::Tokens&) override {
        Layout(bounds);
    }

    bool HandleEvent(const Event& event) override {
        // Row-body click = primary action, but only left of the button
        // cluster — gaps between buttons must not open the project.
        if (event.type == Event::Type::Click &&
            event.mouse_button == MouseButton::Left && on_activate_ &&
            buttons_x_ > 0.0f && event.position.x < buttons_x_) {
            on_activate_();
            return true;
        }
        return false;
    }

    Layout::Size GetMinSize() const override { return {300.0f, 56.0f}; }
    Layout::Size GetPreferredSize() const override {
        return {GetMinSize().width, 60.0f};
    }
    Layout::Size GetMaxSize() const override { return {100000.0f, 72.0f}; }
    float GetContentHeight() const override { return GetPreferredSize().height; }

private:
    bool RowHot() const {
        if (IsHovered()) return true;
        for (const auto& b : buttons_) {
            if (b->IsHovered()) return true;
        }
        return false;
    }

    std::string name_;
    std::string meta_;
    std::vector<std::shared_ptr<Button>> buttons_;
    std::string icon_path_;
    std::function<void()> on_activate_;
    Layout::Rect bounds_{};
    float buttons_x_ = 0.0f;
};

} // namespace

ProjectsWidget::ProjectsWidget(void* parent_hwnd, Hooks hooks)
    : parent_hwnd_(parent_hwnd)
    , hooks_(std::move(hooks)) {}

ProjectsWidget::~ProjectsWidget() = default;

void ProjectsWidget::SetMode(Mode mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    DetachRows();
    Refresh();
}

void ProjectsWidget::SetStore(NodeGraph::ProjectStore* store) {
    store_ = store;
    owned_store_.reset();
}

NodeGraph::ProjectStore* ProjectsWidget::ActiveStore() {
    if (store_ != nullptr) return store_;
    if (mode_ != Mode::kBrowser) return nullptr;
    if (owned_store_ == nullptr || !owned_store_->IsOpen()) {
        const auto db_path =
            XER::Engine::Cache::CacheManager::Instance().Persistent().root() /
            "node_graph" / "projects.db";
        owned_store_ = std::make_unique<NodeGraph::ProjectStore>(db_path.string());
        std::string err;
        if (!owned_store_->Open(&err)) {
            owned_store_.reset();
            Notify("Project database is not available — " + err, true);
            return nullptr;
        }
    }
    store_ = owned_store_.get();
    return store_;
}

bool ProjectsWidget::PassesFilter(const std::string& name) const {
    if (mode_ != Mode::kBrowser || search_input_ == nullptr) return true;
    const std::string needle = XER::Engine::Helper::String::ToLowerAscii(search_input_->GetValue());
    if (needle.empty()) return true;
    return XER::Engine::Helper::String::ToLowerAscii(name).find(needle) != std::string::npos;
}

void ProjectsWidget::Notify(const std::string& text, bool is_error) {
    if (hooks_.on_notify) hooks_.on_notify(text, is_error);
}

std::string ProjectsWidget::FormatUpdated(double epoch_seconds) const {
    if (epoch_seconds <= 0.0) return "never";
    const std::time_t now = std::time(nullptr);
    const std::time_t t = static_cast<std::time_t>(epoch_seconds);
    const double diff = std::difftime(now, t);
    if (diff >= 0.0 && diff < 90.0) return "just now";
    if (diff >= 0.0 && diff < 3600.0)
        return std::to_string(static_cast<int>(diff / 60.0)) + "m ago";
    if (diff >= 0.0 && diff < 86400.0)
        return std::to_string(static_cast<int>(diff / 3600.0)) + "h ago";
    if (diff >= 0.0 && diff < 7.0 * 86400.0)
        return std::to_string(static_cast<int>(diff / 86400.0)) + "d ago";
    char buf[32];
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

// ── Build / refresh ────────────────────────────────────────────────────────

void ProjectsWidget::DetachRows() {
    for (auto& w : row_widgets_) {
        RemoveChild(w);
    }
    row_widgets_.clear();
    rows_.clear();
    rows_built_ = false;
}

void ProjectsWidget::Refresh() {
    DetachRows();
    armed_delete_id_ = -1;
    RebuildRows();
    RequestLayout();
    Invalidate();
}

void ProjectsWidget::RebuildRows() {
    if (rows_built_) return;
    rows_built_ = true;

    NodeGraph::ProjectStore* store = ActiveStore();
    if (store == nullptr || !store->IsOpen()) {
        if (mode_ == Mode::kPage) {
            Notify("Project database is not available", true);
        }
        return;
    }

    std::vector<NodeGraph::ProjectStore::ProjectInfo> projects =
        store->ListProjects();
    // "autosave" is an internal implementation detail — hide it.
    projects.erase(std::remove_if(projects.begin(), projects.end(),
                                  [](const auto& p) {
                                      return p.name == "autosave";
                                  }),
                   projects.end());
    // Newest first — the browser is a launcher, recency matters most.
    std::sort(projects.begin(), projects.end(),
              [](const auto& a, const auto& b) {
                  return a.updated_at > b.updated_at;
              });

    for (const auto& p : projects) {
        if (!PassesFilter(p.name)) continue;

        Row row;
        row.id = p.id;
        row.name = p.name;

        const std::string gcount =
            std::to_string(p.graph_count) +
            (p.graph_count == 1 ? " graph" : " graphs");
        row.meta = gcount + " · updated " + FormatUpdated(p.updated_at);

        const std::int64_t id = p.id;
        const std::string name = p.name;

        std::vector<std::shared_ptr<Button>> buttons;

        auto primary_btn = std::make_shared<Button>(
            mode_ == Mode::kBrowser ? "Select" : "Open",
            ButtonStyle::Primary);
        primary_btn->SetTooltip(mode_ == Mode::kBrowser
                                    ? "Open this project in the editor"
                                    : "Open this project in the graph editor");
        const auto on_open = [this, id] {
            if (hooks_.on_open_project) hooks_.on_open_project(id);
        };
        primary_btn->SetOnClick(on_open);
        buttons.push_back(primary_btn);

        auto secondary_btn = std::make_shared<Button>(
            mode_ == Mode::kBrowser ? "Configure" : "Rename",
            ButtonStyle::Outline);
        secondary_btn->SetOnClick([this, id, name] {
            if (mode_ == Mode::kBrowser) {
                PromptProjectConfig(id, name);
            } else {
                PromptName("Rename project", name,
                           [this, id, name](std::string new_name) {
                               if (new_name.empty() || new_name == name)
                                   return;
                               if (IsReservedProjectName(new_name)) {
                                   Notify("'" + new_name +
                                              "' is reserved for autosave",
                                          true);
                                   return;
                               }
                               NodeGraph::ProjectStore* s = ActiveStore();
                               if (s == nullptr) return;
                               std::string err;
                               if (s->RenameProject(id, new_name, &err)) {
                                   Notify("Renamed to '" + new_name + "'");
                               } else {
                                   Notify("Rename failed — " + err, true);
                               }
                               Refresh();
                           },
                           "Rename",
                           "Enter a new name for this project.",
                           "Project name");
            }
        });
        buttons.push_back(secondary_btn);

        if (mode_ == Mode::kBrowser) {
            auto extra_btn = std::make_shared<Button>("Duplicate",
                                                      ButtonStyle::Secondary);
            extra_btn->SetTooltip("Copy all graphs into a new project");
            extra_btn->SetOnClick([this, id, name] {
                DuplicateProject(id, name);
            });
            buttons.push_back(extra_btn);
        }

        const bool armed = (armed_delete_id_ == id);
        auto delete_btn = std::make_shared<Button>(
            armed ? "Sure?" : "Delete",
            armed ? ButtonStyle::Destructive : ButtonStyle::Ghost);
        delete_btn->SetOnClick([this, id, name] {
            if (armed_delete_id_ != id) {
                // First click arms; second click within the window deletes.
                armed_delete_id_ = id;
                armed_until_ = std::chrono::steady_clock::now() +
                               std::chrono::milliseconds(kArmedDeleteMs);
                DetachRows();
                RebuildRows();
                RequestLayout();
                return;
            }
            armed_delete_id_ = -1;
            NodeGraph::ProjectStore* s = ActiveStore();
            if (s == nullptr) return;
            std::string err;
            if (s->DeleteProject(id, &err)) {
                Notify("Deleted project '" + name + "'");
            } else {
                Notify("Delete failed — " + err, true);
            }
            Refresh();
        });
        buttons.push_back(delete_btn);

        row.widget = std::make_shared<ProjectRowWidget>(
            p.name, row.meta, std::move(buttons),
            HasProjectIconMarker(p.icon) ? ProjectIconJpgPath(p.id)
                                       : std::string(),
            on_open);
        rows_.push_back(row);
        row_widgets_.push_back(row.widget);
    }

    // Row widgets are detached by DetachRows() before each rebuild.
    for (auto& w : row_widgets_) {
        AddChild(w);
    }
}

void ProjectsWidget::PromptCreateProject() {
    auto content = std::make_shared<ProjectCreateContent>(parent_hwnd_);
    auto dialog = Dialog::Create("New project", content, "Create", "Cancel");
    dialog->SetOnClosed([dlg = dialog.get(), content, this] {
        if (!dlg->WasConfirmed()) return;
        const std::string name = TrimName(content->TakeName());
        if (name.empty()) {
            Notify("Project name cannot be empty", true);
            return;
        }
        if (IsReservedProjectName(name)) {
            Notify("'" + name + "' is reserved for autosave", true);
            return;
        }
        NodeGraph::ProjectStore* store = ActiveStore();
        if (store == nullptr || !store->IsOpen()) {
            Notify("Project database is not available", true);
            return;
        }
        std::string err;
        const std::int64_t id = store->CreateProject(name, &err);
        if (id <= 0) {
            Notify("Create failed — " + err, true);
            return;
        }
        // Avatar: transcode the picked source through FFmpeg into
        // icons/<id>/icon.jpg, then store the marker in the project row.
        std::string icon_value;
        const std::string& avatar_src = content->TakeIconSource();
        if (!avatar_src.empty()) {
            if (ConvertProjectAvatar(avatar_src, id)) {
                icon_value = "icon.jpg";
            } else {
                Notify("Avatar conversion failed — icon not set", true);
            }
        }
        // Persist the extra config fields chosen in the dialog.
        store->SaveProjectConfig(
            id, name, content->TakeVersion(),
            XER::Engine::Helper::String::TrimAsciiWhitespaceCopy(
                content->TakeCachePath()),
            content->TakeNodesData(), icon_value, nullptr);
        Notify("Created project '" + name + "'");
        Refresh();
        if (mode_ == Mode::kBrowser && hooks_.on_open_project) {
            hooks_.on_open_project(id);
        }
    });
    ShowOverlay(dialog);
}

void ProjectsWidget::PromptName(
    const std::string& title, const std::string& initial,
    std::function<void(std::string)> on_submit,
    std::string confirm_label, std::string description,
    std::string field_caption) {
    auto content = std::make_shared<NamePromptContent>(
        description.empty() ? "Enter a name." : description,
        field_caption.empty() ? "Name" : field_caption, initial);
    auto dialog = Dialog::Create(title, content, confirm_label, "Cancel");
    dialog->SetOnClosed([dlg = dialog.get(), content, cb = std::move(on_submit)] {
        // Only submit on explicit confirm (button / Enter); Esc, scrim and
        // Cancel dismiss without touching the callback.
        if (!dlg->WasConfirmed()) return;
        cb(TrimName(content->TakeName()));
    });
    ShowOverlay(dialog);
}

void ProjectsWidget::CreateNamedProject(const std::string& raw_name) {
    const std::string name = TrimName(raw_name);
    NodeGraph::ProjectStore* store = ActiveStore();
    if (store == nullptr || !store->IsOpen()) {
        Notify("Project database is not available", true);
        return;
    }
    if (name.empty()) {
        Notify("Project name cannot be empty", true);
        return;
    }
    if (IsReservedProjectName(name)) {
        Notify("'" + name + "' is reserved for autosave", true);
        return;
    }
    std::string err;
    const std::int64_t id = store->CreateProject(name, &err);
    if (id <= 0) {
        Notify("Create failed — " + err, true);
        return;
    }
    Notify("Created project '" + name + "'");
    Refresh();
    // Browser flow: creating a project means the user wants to work in it.
    if (mode_ == Mode::kBrowser && hooks_.on_open_project) {
        hooks_.on_open_project(id);
    }
}

void ProjectsWidget::DuplicateProject(std::int64_t id,
                                      const std::string& name) {
    NodeGraph::ProjectStore* store = ActiveStore();
    if (store == nullptr || !store->IsOpen()) {
        Notify("Project database is not available", true);
        return;
    }
    std::string copy_name = name + " copy";
    std::string err;
    std::int64_t copy_id = store->CreateProject(copy_name, &err);
    if (copy_id < 0) {
        Notify("Duplicate failed — " + err, true);
        return;
    }
    std::size_t copied = 0;
    for (const auto& g : store->ListGraphs(id)) {
        std::string json;
        if (store->LoadGraph(id, g.name, &json, nullptr) && !json.empty()) {
            if (store->SaveGraph(copy_id, g.name, json, nullptr)) {
                ++copied;
            }
        }
    }
    Notify("Duplicated '" + name + "' → '" + copy_name + "' (" +
               std::to_string(copied) + " graph(s))");
    Refresh();
}

void ProjectsWidget::PromptProjectConfig(std::int64_t id,
                                         const std::string& name) {
    NodeGraph::ProjectStore* store = ActiveStore();
    if (store == nullptr || !store->IsOpen()) {
        Notify("Project database is not available", true);
        return;
    }
    NodeGraph::ProjectStore::ProjectInfo icon_info;
    const bool have_icon_info = store->GetProject(id, &icon_info, nullptr);
    const bool has_icon =
        have_icon_info && HasProjectIconMarker(icon_info.icon);
    auto content = std::make_shared<ProjectConfigContent>(
        store, id, name, parent_hwnd_,
        has_icon ? ProjectIconJpgPath(id) : std::string(), has_icon,
        [this](const std::string& text, bool is_error) {
            Notify(text, is_error);
        },
        [this, id](const std::string& graph_name) {
            if (hooks_.on_open_project_graph) {
                hooks_.on_open_project_graph(id, graph_name);
            } else if (hooks_.on_open_project) {
                hooks_.on_open_project(id);
            }
        });
    auto dialog = Dialog::Create("Configure project — " + name, content,
                                 "Close");
    dialog->SetOnClosed([this, content, id, name, store]() {
        const std::string trimmed = TrimName(content->TakeName());
        if (trimmed.empty() || IsReservedProjectName(trimmed)) return;
        // Avatar: transcode a newly picked source; a cleared marker also
        // removes the cached icon.jpg so no orphan file is left behind.
        const std::string& avatar_src = content->TakeIconSource();
        if (!avatar_src.empty() &&
            !ConvertProjectAvatar(avatar_src, id)) {
            Notify("Avatar conversion failed — keeping previous icon",
                   true);
        }
        const std::string icon_value =
            content->HasIcon() ? "icon.jpg" : "";
        if (icon_value.empty()) {
            RemoveProjectAvatar(id);
        }
        std::string err;
        if (store->SaveProjectConfig(id, trimmed, content->TakeVersion(),
                                     XER::Engine::Helper::String::TrimAsciiWhitespaceCopy(content->TakeCachePath()),
                                     content->TakeNodesData(), icon_value,
                                     &err)) {
            if (trimmed != name) Notify("Saved project '" + trimmed + "'");
            Refresh();
        } else {
            Notify("Save failed — " + err, true);
        }
    });
    ShowOverlay(dialog);
}

void ProjectsWidget::Tick(double dt_seconds) {
    (void)dt_seconds;
    // Expire the armed delete state.
    if (armed_delete_id_ >= 0 &&
        std::chrono::steady_clock::now() > armed_until_) {
        armed_delete_id_ = -1;
        DetachRows();
        RebuildRows();
        RequestLayout();
    }
}

// ── Layout / render ────────────────────────────────────────────────────────

void ProjectsWidget::Layout(const Layout::Rect& bounds) {
    Layout(bounds, last_tokens_);
}

void ProjectsWidget::Layout(const Layout::Rect& bounds,
                            const Theme::Tokens& tokens) {
    last_tokens_ = tokens;

    // Build toolbar widgets once.
    if (!name_input_) {
        name_input_ = std::make_shared<TextInput>("");
        name_input_->SetPlaceholder("Project name");
        name_input_->SetTooltip("Project name");

        search_input_ = std::make_shared<TextInput>("");
        search_input_->SetPlaceholder(mode_ == Mode::kBrowser
                                          ? "Search projects…"
                                          : "Filter by name…");
        search_input_->SetTooltip("Filter projects by name");
        search_input_->SetOnValueChanged([this](const std::string&) {
            DetachRows();
            RebuildRows();
            RequestLayout();
        });

        new_btn_ = std::make_shared<Button>("New Project", ButtonStyle::Primary);
        new_btn_->SetTooltip("Create an empty project in the database");
        new_btn_->SetOnClick([this] {
            PromptCreateProject();
        });

        save_as_btn_ =
            std::make_shared<Button>("Save Current As…", ButtonStyle::Secondary);
        save_as_btn_->SetTooltip("Save the current graph into a project");
        save_as_btn_->SetOnClick([this] {
            PromptName("Save current graph as", "",
                       [this](std::string name) {
                           if (name.empty()) return;
                           if (hooks_.on_save_current_as) {
                               hooks_.on_save_current_as(name);
                           }
                           Refresh();
                       },
                       "Save",
                       "Pick a project to store the current graph under.",
                       "Project name");
        });

        refresh_btn_ = std::make_shared<Button>("Refresh", ButtonStyle::Outline);
        refresh_btn_->SetTooltip("Reload the project list");
        refresh_btn_->SetOnClick([this] { Refresh(); });

        // Attach toolbar widgets once, here — NOT in RebuildRows: Refresh()
        // can run (via SetMode/hooks) before the first Layout creates them,
        // which would latch children_attached_ with nothing attached.
        AddChild(name_input_);
        AddChild(search_input_);
        AddChild(new_btn_);
        AddChild(save_as_btn_);
        AddChild(refresh_btn_);
        children_attached_ = true;
    }
    RebuildRows();

    SetBounds(bounds);
    const float content_w =
        std::max(80.0f, bounds.width - kPadX * 2.0f - kSideGap);
    const float card_x = bounds.x + kPadX;
    const float card_w = content_w;
    const float inner_x = card_x + kCardPad;
    const float inner_w = card_w - 2.0f * kCardPad;

    // Content starts at the top — the nav tabs already name the panel.
    // (The old "Projects" hero header was removed: it duplicated the nav
    // label and pushed the toolbar down.)
    float y = bounds.y + kTopPad;
    browser_title_rect_ = {};
    browser_sub_rect_ = {};

    // ── Toolbar card: mode-dependent actions ─────────────────────────────
    {
        const float card_top = y;
        const float card_h = kCardPad * 2.0f + kInputH;
        toolbar_card_ = Layout::Rect{card_x, card_top, card_w, card_h};
        const float input_w = std::max(160.0f, inner_w * 0.45f);
        const float btn_gap = 8.0f;
        const float by = card_top + kCardPad;  // controls share one height
        float bx = inner_x + input_w + btn_gap;

        if (mode_ == Mode::kBrowser) {
            search_input_->Layout(
                Layout::Rect{inner_x, card_top + kCardPad, input_w, kInputH});
            new_btn_->Layout(Layout::Rect{bx, by, 110.0f, kBtnH});
            bx += 110.0f + btn_gap;
            refresh_btn_->Layout(Layout::Rect{bx, by, 90.0f, kBtnH});
        } else {
            name_input_->Layout(
                Layout::Rect{inner_x, card_top + kCardPad, input_w, kInputH});
            new_btn_->Layout(Layout::Rect{bx, by, 110.0f, kBtnH});
            bx += 110.0f + btn_gap;
            save_as_btn_->Layout(Layout::Rect{bx, by, 140.0f, kBtnH});
        }
        // Unused widgets park offscreen (zero-size) so hit-tests can't reach.
        const Layout::Rect park{bounds.x - 5000.0f, bounds.y - 5000.0f,
                                1.0f, 1.0f};
        name_input_->SetVisible(mode_ == Mode::kPage);
        search_input_->SetVisible(mode_ == Mode::kBrowser);
        save_as_btn_->SetVisible(mode_ == Mode::kPage);
        refresh_btn_->SetVisible(mode_ == Mode::kBrowser);
        if (mode_ == Mode::kBrowser) {
            name_input_->Layout(park);
            save_as_btn_->Layout(park);
        } else {
            search_input_->Layout(park);
            refresh_btn_->Layout(park);
        }
        y = card_top + card_h + kSectionGap;
    }

    // ── Projects card ────────────────────────────────────────────────────
    {
        const float card_top = y;
        const float card_title_h = 17.0f + kHeaderGap;
        float ry = card_top + kCardPad + card_title_h;

        projects_card_.x = card_x;
        projects_card_.y = card_top;
        projects_card_.width = card_w;
        projects_card_.height = 0.0f; // filled below

        if (rows_.empty()) {
            empty_rect_ = Layout::Rect{inner_x + 8.0f, ry + 6.0f,
                                       inner_w - 16.0f, 44.0f};
            ry += 56.0f;
        } else {
            for (auto& r : rows_) {
                r.top = ry;
                r.height = kRowH;
                ry += kRowH + kRowGap;
            }
            ry -= kRowGap;
        }

        const float card_bottom = ry + kCardPad;
        projects_card_.height = card_bottom - card_top;

        // Each row widget owns its buttons (right-aligned cluster).
        for (auto& r : rows_) {
            if (r.widget) {
                r.widget->Layout(
                    Layout::Rect{inner_x + 4.0f, r.top, inner_w - 8.0f,
                                 r.height});
            }
        }

        y = card_bottom + kSectionGap;
    }

    content_height_ = y - bounds.y - kSectionGap + kBottomPad;
}

void ProjectsWidget::Render(Rendering::Renderer& renderer,
                            const Theme::Tokens& tokens) {
    // Toolbar card.
    if (toolbar_card_.width > 0.0f && toolbar_card_.height > 0.0f) {
        renderer.FillRect(toolbar_card_, tokens.surface, tokens.radius_large);
        renderer.StrokeRect(toolbar_card_, tokens.border, 1.0f,
                            tokens.radius_large);
    }

    // Projects card.
    if (projects_card_.width > 0.0f && projects_card_.height > 0.0f) {
        renderer.FillRect(projects_card_, tokens.surface,
                          tokens.radius_large);
        renderer.StrokeRect(projects_card_, tokens.border, 1.0f,
                            tokens.radius_large);

        // Card title + count badge (shadcn-style chip).
        const float title_y = projects_card_.y + kCardPad;
        const Rendering::TextStyle title{tokens.foreground,
                                         tokens.font_body, 600};
        renderer.DrawText("Projects",
                          Layout::Rect{projects_card_.x + kCardPad, title_y,
                                       projects_card_.width - 2.0f * kCardPad,
                                       17.0f},
                          title);
        if (!rows_.empty()) {
            const std::string count = std::to_string(rows_.size());
            const float bw = 26.0f + count.size() * 7.0f;
            const Layout::Rect badge{
                projects_card_.x + projects_card_.width - kCardPad - bw,
                title_y + 1.0f, bw, 18.0f};
            Theme::Color bbg = tokens.accent;
            bbg.a *= 0.14f;
            renderer.FillRect(badge, bbg, 9.0f);
            const Rendering::TextStyle bs{tokens.accent, tokens.font_small, 600};
            renderer.DrawText(count, badge, bs);
        }

        // Empty state: centered hint inside a dashed-feel frame.
        if (rows_.empty() && empty_rect_.width > 0.0f) {
            Theme::Color frame = tokens.border;
            frame.a *= 0.8f;
            renderer.StrokeRect(empty_rect_, frame, 1.0f,
                                tokens.radius_medium);
            const bool filtered =
                mode_ == Mode::kBrowser && search_input_ != nullptr &&
                !search_input_->GetValue().empty();
            const Rendering::TextStyle empty{tokens.muted,
                                             tokens.font_small, 400};
            renderer.DrawText(
                filtered ? "No projects match the filter."
                         : "No projects yet — create one above.",
                Layout::Rect{empty_rect_.x, empty_rect_.y + 8.0f,
                             empty_rect_.width, 14.0f},
                empty);
            const Rendering::TextStyle hint{tokens.muted,
                                            tokens.font_small, 400};
            renderer.DrawText(
                filtered ? "Try a different search term."
                         : "Use “New Project” to start.",
                Layout::Rect{empty_rect_.x, empty_rect_.y + 24.0f,
                             empty_rect_.width, 14.0f},
                hint);
        }
    }
}

} // namespace XER::UI::Widgets
