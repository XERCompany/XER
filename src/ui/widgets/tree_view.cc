#include "tree_view.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

constexpr float kRowInset = 4.0f;      // leading padding before chevrons
constexpr float kChevronZone = 18.0f;  // clickable chevron slot width
constexpr float kCheckboxZone = 20.0f; // checkbox slot width (multiple mode)
constexpr float kIconSize = 16.0f;
constexpr float kLabelGap = 6.0f;
constexpr double kTypeaheadWindowSeconds = 0.9;

std::string ToLower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return out;
}

} // namespace

std::shared_ptr<TreeView> TreeView::Create() {
    return std::shared_ptr<TreeView>(new TreeView());
}

TreeView::TreeView() : list_(VirtualList::Create()) {
    SetClipsChildren(true);
    // The list is a permanent child covering the content area; WidgetTree
    // hit-testing routes pointer/keyboard input to it directly.
    AddChild(list_);
    list_->SetItemHeight(row_height_);
    list_->SetItemRenderer(
        [this](Rendering::Renderer& renderer, Layout::Rect row,
               int index, bool /*selected*/) {
            RenderRow(renderer, row, index);
        });
    list_->SetOnItemClick(
        [this](int index, Layout::Point position) {
            return RowClick(index, position);
        });
    list_->SetOnItemActivated([this](int index) { ActivateRow(index); });
    list_->SetOnSelectionChanged(
        [this] { PullSelectionFromList(); });
    list_->SetKeyFallback(
        [this](const Event& event) { return FallbackKey(event); });
}

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------

void TreeView::SetItems(std::vector<TreeNode> roots) {
    roots_ = std::move(roots);
    // Prune selection to ids that still exist; expansion stays (a missing
    // id in the expanded set is harmless).
    std::set<std::string> live;
    std::function<void(const std::vector<TreeNode>&)> collect =
        [&](const std::vector<TreeNode>& nodes) {
            for (const auto& n : nodes) {
                live.insert(n.id);
                collect(n.children);
            }
        };
    collect(roots_);
    for (auto it = selected_ids_.begin(); it != selected_ids_.end();) {
        if (!live.count(*it)) {
            it = selected_ids_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = load_state_.begin(); it != load_state_.end();) {
        if (!live.count(it->first)) {
            it = load_state_.erase(it);
        } else {
            ++it;
        }
    }
    load_errors_.clear();
    Reflatten();
    FireLazyLoads();
}

void TreeView::SetNodeChildren(const std::string& id,
                               std::vector<TreeNode> children) {
    TreeNode* node = FindNode(id);
    if (node == nullptr) return;
    node->children = std::move(children);
    node->is_branch = true;
    load_state_[id] = LoadState::Loaded;
    load_errors_.erase(id);
    // Drift semantics: a checked branch checks children loaded later.
    if (multiple_ && selected_ids_.count(id)) {
        for (auto& child : node->children) {
            SetCheckedCascade(child, true);
        }
    }
    Reflatten();
}

void TreeView::SetNodeLoadFailed(const std::string& id,
                                 std::string error) {
    if (FindNode(id) == nullptr) return;
    load_state_[id] = LoadState::Failed;
    load_errors_[id] = std::move(error);
    Reflatten();
}

TreeNode* TreeView::FindNode(const std::string& id) {
    TreeNode* hit = nullptr;
    std::function<void(std::vector<TreeNode>&)> dfs =
        [&](std::vector<TreeNode>& nodes) {
            for (auto& n : nodes) {
                if (hit != nullptr) return;
                if (n.id == id) {
                    hit = &n;
                    return;
                }
                dfs(n.children);
            }
        };
    dfs(roots_);
    return hit;
}

const TreeNode* TreeView::FindNode(const std::string& id) const {
    return const_cast<TreeView*>(this)->FindNode(id);
}

int TreeView::FlatIndexOf(const std::string& id) const {
    for (size_t i = 0; i < flat_.size(); ++i) {
        if (flat_[i].kind == RowKind::Node &&
            flat_[i].node != nullptr && flat_[i].node->id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void TreeView::CollectIds(TreeNode& node,
                          std::vector<std::string>* out) const {
    out->push_back(node.id);
    for (auto& child : node.children) {
        CollectIds(child, out);
    }
}

// ---------------------------------------------------------------------------
// Expansion
// ---------------------------------------------------------------------------

void TreeView::SetExpanded(std::set<std::string> ids) {
    if (ids == expanded_) return;
    expanded_ = std::move(ids);
    Reflatten();
    FireLazyLoads();
}

void TreeView::SetNodeExpanded(const std::string& id, bool expanded) {
    const bool was = expanded_.count(id) != 0;
    if (was == expanded) return;
    if (expanded) {
        expanded_.insert(id);
    } else {
        expanded_.erase(id);
    }
    Reflatten();
    FireLazyLoads();
}

void TreeView::ExpandAll() {
    std::function<void(const std::vector<TreeNode>&)> dfs =
        [&](const std::vector<TreeNode>& nodes) {
            for (const auto& n : nodes) {
                if (n.is_branch || !n.children.empty()) {
                    expanded_.insert(n.id);
                }
                dfs(n.children);
            }
        };
    dfs(roots_);
    Reflatten();
    FireLazyLoads();
}

void TreeView::CollapseAll() {
    if (expanded_.empty()) return;
    expanded_.clear();
    Reflatten();
}

void TreeView::SetOnExpandChanged(
    std::function<void(const std::string&, bool)> cb) {
    on_expand_changed_ = std::move(cb);
}

void TreeView::ToggleExpanded(TreeNode& node) {
    const bool now = expanded_.count(node.id) == 0;
    if (now) {
        expanded_.insert(node.id);
    } else {
        expanded_.erase(node.id);
    }
    Reflatten();
    FireLazyLoads();
    if (on_expand_changed_) {
        on_expand_changed_(node.id, now);
    }
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

void TreeView::SetMultiple(bool multiple) {
    if (multiple_ == multiple) return;
    multiple_ = multiple;
    list_->SetSelectionMode(multiple
                                ? VirtualList::SelectionMode::Multiple
                                : VirtualList::SelectionMode::Single);
    if (!multiple && selected_ids_.size() > 1) {
        const std::string keep = *selected_ids_.begin();
        selected_ids_.clear();
        selected_ids_.insert(keep);
    }
    Reflatten();
}

void TreeView::SetSelectedId(std::string id) {
    selected_ids_.clear();
    if (!id.empty() && FindNode(id) != nullptr) {
        selected_ids_.insert(std::move(id));
    }
    PushSelectionToList();
    Invalidate();
}

std::string TreeView::GetSelectedId() const {
    return selected_ids_.empty() ? std::string() : *selected_ids_.begin();
}

void TreeView::SetSelectedIds(std::set<std::string> ids) {
    for (auto it = ids.begin(); it != ids.end();) {
        if (FindNode(*it) == nullptr) {
            it = ids.erase(it);
        } else {
            ++it;
        }
    }
    if (!multiple_ && ids.size() > 1) {
        const std::string keep = *ids.begin();
        ids.clear();
        ids.insert(keep);
    }
    selected_ids_ = std::move(ids);
    PushSelectionToList();
    Invalidate();
}

void TreeView::SetOnSelectionChanged(std::function<void()> cb) {
    on_selection_changed_ = std::move(cb);
}

void TreeView::SetCheckedCascade(TreeNode& node, bool checked) {
    std::vector<std::string> ids;
    CollectIds(node, &ids);
    for (const auto& id : ids) {
        if (checked) {
            selected_ids_.insert(id);
        } else {
            selected_ids_.erase(id);
        }
    }
}

void TreeView::ToggleCheck(TreeNode& node) {
    const CheckState state = NodeCheckState(node);
    // Unchecked OR partial both resolve to fully checked — the standard
    // "click a mixed box to select everything" behavior.
    const bool check = state != CheckState::Checked;
    SetCheckedCascade(node, check);
    PushSelectionToList();
    Reflatten();  // tri-state map refresh
    if (on_selection_changed_) on_selection_changed_();
}

TreeView::CheckState TreeView::NodeCheckState(
    const TreeNode& node) const {
    const auto it = check_state_.find(node.id);
    if (it != check_state_.end()) {
        return it->second;
    }
    return selected_ids_.count(node.id) ? CheckState::Checked
                                        : CheckState::Unchecked;
}

void TreeView::ComputeCheckStates(const std::vector<TreeNode>& nodes) {
    for (const auto& node : nodes) {
        ComputeCheckStates(node.children);
        const bool self = selected_ids_.count(node.id) != 0;
        if (node.children.empty()) {
            check_state_[node.id] =
                self ? CheckState::Checked : CheckState::Unchecked;
            continue;
        }
        // Branch state is descendant-derived (aria-checked="mixed"
        // semantics): the branch's own id in the set doesn't decide the
        // box — cascade ops keep both consistent anyway.
        bool all_checked = true;
        bool any_checked = false;
        for (const auto& child : node.children) {
            const CheckState cs = check_state_[child.id];
            if (cs != CheckState::Checked) all_checked = false;
            if (cs != CheckState::Unchecked) any_checked = true;
        }
        check_state_[node.id] =
            all_checked ? CheckState::Checked
            : any_checked ? CheckState::Partial
                          : CheckState::Unchecked;
    }
}

// ---------------------------------------------------------------------------
// Lazy loading
// ---------------------------------------------------------------------------

void TreeView::SetLoadChildren(
    std::function<void(TreeView&, const std::string&)> cb) {
    load_children_ = std::move(cb);
}

void TreeView::FireLazyLoads() {
    if (!load_children_) return;
    // Collect first, fire second: a synchronous callback that calls
    // SetNodeChildren triggers a nested Reflatten + FireLazyLoads — by
    // marking Loading before collecting we keep the inner pass from
    // re-collecting, and re-checking at fire time keeps exactly-once
    // even when ids were resolved by an earlier nested pass.
    std::vector<std::string> pending;
    std::function<void(std::vector<TreeNode>&)> dfs =
        [&](std::vector<TreeNode>& nodes) {
            for (auto& n : nodes) {
                if (n.is_branch && n.children.empty() &&
                    expanded_.count(n.id) &&
                    load_state_.count(n.id) == 0) {
                    load_state_[n.id] = LoadState::Loading;
                    pending.push_back(n.id);
                }
                dfs(n.children);
            }
        };
    dfs(roots_);
    if (!pending.empty()) Reflatten();  // Loading rows appear
    for (const std::string& id : pending) {
        if (load_state_.count(id) &&
            load_state_[id] == LoadState::Loading) {
            load_children_(*this, id);
        }
    }
}

void TreeView::RetryLoad(TreeNode& parent) {
    load_state_[parent.id] = LoadState::Loading;
    load_errors_.erase(parent.id);
    Reflatten();
    if (load_children_) {
        load_children_(*this, parent.id);
    }
}

// ---------------------------------------------------------------------------
// Flatten + selection sync
// ---------------------------------------------------------------------------

void TreeView::Reflatten() {
    flat_.clear();
    check_state_.clear();
    ComputeCheckStates(roots_);
    FlattenInto(roots_, 0);
    list_->SetItemCount(static_cast<int>(flat_.size()));
    PushSelectionToList();
    RequestLayout();
    Invalidate();
}

void TreeView::FlattenInto(std::vector<TreeNode>& nodes, int depth) {
    for (auto& node : nodes) {
        flat_.push_back({RowKind::Node, &node, nullptr, depth});
        const bool open = expanded_.count(node.id) != 0;
        if (!open) continue;
        if (!node.children.empty()) {
            FlattenInto(node.children, depth + 1);
            continue;
        }
        // Expanded branch with no children: a lazy-load placeholder row
        // (Loading or Retry) — unless it already loaded empty.
        if (node.is_branch) {
            const auto it = load_state_.find(node.id);
            const LoadState state =
                it == load_state_.end() ? LoadState::Loading : it->second;
            if (state == LoadState::Loaded) continue;
            flat_.push_back({state == LoadState::Failed
                                 ? RowKind::Retry
                                 : RowKind::Loading,
                             nullptr, &node, depth + 1});
        }
    }
}

void TreeView::PullSelectionFromList() {
    std::set<std::string> ids;
    for (const int index : list_->GetSelectedIndices()) {
        if (index >= 0 && index < static_cast<int>(flat_.size())) {
            const FlatRow& row = flat_[index];
            if (row.kind == RowKind::Node && row.node != nullptr &&
                !row.node->disabled) {
                ids.insert(row.node->id);
            }
        }
    }
    if (ids == selected_ids_) {
        // Still normalize the list's index set (it may hold placeholder
        // or disabled rows the id model rejected).
        PushSelectionToList();
        return;
    }
    selected_ids_ = std::move(ids);
    PushSelectionToList();
    Reflatten();  // tri-state refresh in multiple mode
    if (on_selection_changed_) on_selection_changed_();
}

void TreeView::PushSelectionToList() {
    std::set<int> indices;
    for (size_t i = 0; i < flat_.size(); ++i) {
        const FlatRow& row = flat_[i];
        if (row.kind == RowKind::Node && row.node != nullptr &&
            selected_ids_.count(row.node->id)) {
            indices.insert(static_cast<int>(i));
        }
    }
    list_->SetSelectedIndices(std::move(indices));
}

// ---------------------------------------------------------------------------
// VirtualList callbacks — tree semantics
// ---------------------------------------------------------------------------

Layout::Rect TreeView::RowContentRect(const FlatRow& row,
                                      const Layout::Rect& rect) const {
    const float x = rect.x + kRowInset +
                    indent_size_ * static_cast<float>(row.depth);
    return {x, rect.y, std::max(0.0f, rect.Right() - x), rect.height};
}

bool TreeView::RowClick(int index, Layout::Point position) {
    if (index < 0 || index >= static_cast<int>(flat_.size())) {
        return true;  // stale index during a reflatten — swallow it
    }
    const FlatRow& row = flat_[index];
    if (row.kind == RowKind::Loading) {
        return true;  // inert placeholder
    }
    if (row.kind == RowKind::Retry) {
        RetryLoad(*row.parent);
        return true;
    }
    TreeNode& node = *row.node;
    const float x = position.x - list_->GetBounds().x;
    const float content_x =
        kRowInset + indent_size_ * static_cast<float>(row.depth);
    if (node.is_branch &&
        x >= content_x && x < content_x + kChevronZone) {
        ToggleExpanded(node);
        return true;
    }
    if (multiple_) {
        // Checkbox mode: the WHOLE row acts as the toggle (checkbox zone
        // included). Letting the click fall through to the list's
        // select-one semantics would silently wipe every other check.
        if (!node.disabled) ToggleCheck(node);
        return true;
    }
    // Disabled rows are consumed rather than selected.
    return node.disabled;
}

void TreeView::ActivateRow(int index) {
    if (index < 0 || index >= static_cast<int>(flat_.size())) return;
    const FlatRow& row = flat_[index];
    if (row.kind == RowKind::Retry) {
        RetryLoad(*row.parent);
        return;
    }
    if (row.kind != RowKind::Node || row.node == nullptr) return;
    // Enter/Space on a branch toggles expansion; on a leaf the click /
    // cursor selection already did the work.
    if (row.node->is_branch) {
        ToggleExpanded(*row.node);
    }
}

void TreeView::MoveCursorToParent(int flat_index) {
    if (flat_index <= 0 ||
        flat_index >= static_cast<int>(flat_.size())) {
        return;
    }
    const int depth = flat_[flat_index].depth;
    for (int i = flat_index - 1; i >= 0; --i) {
        if (flat_[i].kind == RowKind::Node && flat_[i].depth < depth) {
            list_->SetCursorIndex(i);
            if (!multiple_ && flat_[i].node != nullptr &&
                !flat_[i].node->disabled) {
                selected_ids_ = {flat_[i].node->id};
                PushSelectionToList();
                if (on_selection_changed_) on_selection_changed_();
            }
            return;
        }
    }
}

bool TreeView::FallbackKey(const Event& event) {
    const int index = list_->GetCursorIndex();
    const FlatRow* row = (index >= 0 &&
                          index < static_cast<int>(flat_.size()))
                             ? &flat_[index]
                             : nullptr;
    if (event.type == Event::Type::TextInput) {
        for (const char ch : event.text) {
            Typeahead(ch);
        }
        return true;
    }
    if (event.type != Event::Type::KeyDown || row == nullptr) {
        return false;
    }
    switch (event.key) {
        case KeyCode::ArrowLeft:
            if (row->kind == RowKind::Node && row->node != nullptr &&
                row->node->is_branch && expanded_.count(row->node->id)) {
                ToggleExpanded(*row->node);
            } else if (row->kind == RowKind::Node) {
                // Collapsed branch or leaf: hop to the parent row.
                MoveCursorToParent(index);
            } else if (index > 0) {
                // Placeholder rows sit directly under their parent —
                // the parent IS the previous flat row.
                list_->SetCursorIndex(index - 1);
            }
            return true;
        case KeyCode::ArrowRight:
            if (row->kind == RowKind::Retry) {
                RetryLoad(*row->parent);
                return true;
            }
            if (row->kind == RowKind::Node && row->node != nullptr &&
                row->node->is_branch) {
                if (!expanded_.count(row->node->id)) {
                    ToggleExpanded(*row->node);
                } else if (index + 1 < static_cast<int>(flat_.size()) &&
                           flat_[index + 1].depth > row->depth) {
                    // Expanded branch: descend to the first child row.
                    list_->SetCursorIndex(index + 1);
                }
                return true;
            }
            return false;
        default:
            return false;
    }
}

void TreeView::Typeahead(char ch) {
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(
        now - last_typeahead_).count();
    typeahead_ = (elapsed > kTypeaheadWindowSeconds)
                     ? std::string(1, ch)
                     : typeahead_ + ch;
    last_typeahead_ = now;
    const std::string prefix = ToLower(typeahead_);
    if (prefix.empty() || flat_.empty()) return;

    const int start = std::max(0, list_->GetCursorIndex());
    const int count = static_cast<int>(flat_.size());
    for (int step = 1; step <= count; ++step) {
        const int i = (start + step) % count;
        const FlatRow& row = flat_[i];
        if (row.kind != RowKind::Node || row.node == nullptr ||
            row.node->disabled) {
            continue;
        }
        if (ToLower(row.node->label).compare(0, prefix.size(), prefix) ==
            0) {
            list_->SetCursorIndex(i);
            if (!multiple_ &&
                selected_ids_.count(row.node->id) == 0) {
                selected_ids_ = {row.node->id};
                PushSelectionToList();
                if (on_selection_changed_) on_selection_changed_();
            }
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void TreeView::RenderRow(Rendering::Renderer& renderer,
                         Layout::Rect rect, int index) {
    if (index < 0 || index >= static_cast<int>(flat_.size())) return;
    const FlatRow& row = flat_[index];
    const Layout::Rect content = RowContentRect(row, rect);
    float x = content.x;
    const Theme::Tokens& tokens = CachedLayoutTokens();

    const auto draw_text = [&](std::string_view text,
                               Theme::Color color, int weight) {
        const Rendering::TextStyle style{color, tokens.font_small,
                                         weight};
        const float th =
            renderer.MeasureText(text, style).height;
        Layout::Rect tr{x,
                        rect.y + std::max(
                            0.0f, (rect.height - th) * 0.5f),
                        std::max(0.0f, rect.Right() - x - 4.0f),
                        th + 1.0f};
        if (tr.width > 0.0f) {
            renderer.DrawText(text, tr, style);
        }
    };

    if (row.kind == RowKind::Loading) {
        draw_text("Loading…", tokens.muted, 400);
        return;
    }
    if (row.kind == RowKind::Retry) {
        x += kChevronZone;
        draw_text("Retry", tokens.accent, 600);
        // Rough offset past the word — fixed pitch guess is fine for a
        // placeholder row.
        x += 5.0f * 7.0f;
        auto it = load_errors_.find(row.parent->id);
        if (it != load_errors_.end() && !it->second.empty()) {
            draw_text(it->second, tokens.muted, 400);
        }
        return;
    }

    const TreeNode& node = *row.node;
    const bool selected = selected_ids_.count(node.id) != 0;

    // Chevron (branches only): '>' collapsed / 'v' expanded, same
    // two-stroke style as Accordion.
    if (node.is_branch) {
        const float cx = x + kChevronZone * 0.5f;
        const float cy = rect.y + rect.height * 0.5f;
        const Theme::Color chevron_color =
            node.disabled ? tokens.disabled : tokens.muted;
        if (expanded_.count(node.id)) {
            renderer.DrawLine({cx - 4.0f, cy - 2.0f},
                              {cx, cy + 2.0f}, chevron_color, 1.5f);
            renderer.DrawLine({cx, cy + 2.0f},
                              {cx + 4.0f, cy - 2.0f}, chevron_color, 1.5f);
        } else {
            renderer.DrawLine({cx - 2.0f, cy - 4.0f},
                              {cx + 2.0f, cy}, chevron_color, 1.5f);
            renderer.DrawLine({cx + 2.0f, cy},
                              {cx - 2.0f, cy + 4.0f}, chevron_color, 1.5f);
        }
        x += kChevronZone;
    }

    // Checkbox (multiple mode): 12px box, tri-state fill.
    if (multiple_) {
        const float box = 12.0f;
        const Layout::Rect cb{x + (kCheckboxZone - box) * 0.5f,
                              rect.y + (rect.height - box) * 0.5f,
                              box, box};
        const CheckState cs = NodeCheckState(node);
        const Theme::Color frame =
            node.disabled ? tokens.disabled : tokens.border;
        if (cs == CheckState::Checked) {
            renderer.FillRect(cb, tokens.accent, tokens.radius_small);
            // Check mark.
            const float cx = cb.x + box * 0.25f;
            const float cy = cb.y + box * 0.55f;
            renderer.DrawLine({cx, cy},
                              {cb.x + box * 0.45f, cb.y + box * 0.75f},
                              tokens.accent_foreground, 1.6f);
            renderer.DrawLine({cb.x + box * 0.45f, cb.y + box * 0.75f},
                              {cb.x + box * 0.78f, cb.y + box * 0.28f},
                              tokens.accent_foreground, 1.6f);
        } else if (cs == CheckState::Partial) {
            renderer.StrokeRect(cb, frame, 1.0f, tokens.radius_small);
            renderer.FillRect({cb.x + 2.5f, cb.y + box * 0.5f - 1.0f,
                               box - 5.0f, 2.0f},
                              tokens.accent, 1.0f);
        } else {
            renderer.StrokeRect(cb, frame, 1.0f, tokens.radius_small);
        }
        x += kCheckboxZone;
    }

    if (!node.icon.empty()) {
        const Layout::Rect ir{x, rect.y + (rect.height - kIconSize) * 0.5f,
                              kIconSize, kIconSize};
        if (renderer.DrawImage(node.icon, ir, 1.0f, 0.0f)) {
            x += kIconSize + kLabelGap;
        }
        // Undecodable/missing icons collapse quietly — no gap, no chip.
    }

    draw_text(node.label,
              node.disabled ? tokens.disabled : tokens.foreground,
              selected ? 600 : 400);
}

void TreeView::Render(Rendering::Renderer& renderer,
                      const Theme::Tokens& tokens) {
    CacheLayoutTokens(tokens);
    const auto b = GetBounds();
    if (b.width <= 0.0f || b.height <= 0.0f) return;
    // Same chrome as Table: surface fill + hairline border. The list
    // child (inset 1px) paints the rows.
    renderer.FillRect(b, tokens.surface, tokens.radius_medium);
    renderer.StrokeRect(b, tokens.border, 1.0f, tokens.radius_medium);
}

void TreeView::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    const auto b = GetBounds();
    if (list_ != nullptr) {
        list_->Layout({b.x + 1.0f, b.y + 1.0f,
                       std::max(0.0f, b.width - 2.0f),
                       std::max(0.0f, b.height - 2.0f)});
    }
}

bool TreeView::HandleEvent(const Event& event) {
    // The list child covers the content area and is the event target for
    // anything inside it; clicks on the 1px frame (or any event that
    // bubbles up unhandled) forward into the list so wheel scrolling and
    // clicks near the edge still work. Bubbles stop at the first 'true',
    // so an already-handled event never reaches here twice.
    return list_ != nullptr && list_->HandleEvent(event);
}

void TreeView::SetRowHeight(float px) {
    row_height_ = std::max(18.0f, px);
    list_->SetItemHeight(px);
    RequestLayout();
}

void TreeView::SetIndentSize(float px) {
    indent_size_ = std::clamp(px, 4.0f, 64.0f);
    Invalidate();
}

void TreeView::EnsureVisible(const std::string& id) {
    const int index = FlatIndexOf(id);
    if (index >= 0) list_->EnsureVisible(index);
}

void TreeView::ScrollToNode(const std::string& id) {
    const int index = FlatIndexOf(id);
    if (index >= 0) list_->ScrollToIndex(index);
}

Layout::Size TreeView::GetMinSize() const {
    return {120.0f, row_height_};
}

Layout::Size TreeView::GetPreferredSize() const {
    return {260.0f, row_height_ * 10.0f};
}

Layout::Size TreeView::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
