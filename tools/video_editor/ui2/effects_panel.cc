// EffectsPanel — see effects_panel.h for the contract. The catalog
// merges three sources behind one FxEntry list:
//   1. the compositor's named core effects (the same table the legacy
//      effects panel shipped, verbatim);
//   2. BuiltinFx::Effects() — the ~51 first-party kernels with real
//      registry names/params (type "builtin:<id>");
//   3. SharedPluginHost() third-party DLLs (non-builtin) + any
//      JsFxRuntime script effects (type "plugin:<id>"/"script:<id>").
#include "effects_panel.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "builtin_fx.h"
#include "helper/string_utils.h"
#include "js_fx.h"
#include "plugin_host.h"
#include "rendering/renderer.h"
#include "sdk/xer_plugin.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/context_menu.h"
#include "widgets/label.h"
#include "widgets/text_input.h"
#include "widgets/tree_view.h"
#include "widgets/virtual_list.h"

// windows.h may have re-defined DrawText before we got here.
#ifdef DrawText
#undef DrawText
#endif

namespace XER::VideoEditor::Ui2 {
namespace {

namespace W = XER::UI::Widgets;
namespace Layout = XER::UI::Layout;

constexpr float kToolbarH = 30.0f;
constexpr float kToolbarGap = 6.0f;
constexpr float kStatusH = 20.0f;
// Replicated from tree_view.cc — the leading row chrome zones my
// wrapped item-click honors for branch rows.
constexpr float kRowInset = 4.0f;
constexpr float kChevronZone = 18.0f;
constexpr double kDoubleClickMs = 400.0;

std::string Lower(const std::string& s) {
    return XER::Helper::String::ToLowerAscii(s);
}

// Category buckets — the registry has no category field, so group by
// the source packs builtin_fx.cc documents (adjust/color/blur/
// distort/overlay/style packs + the AI track-driven pair).
std::string CategoryForBuiltin(const std::string& id) {
    static const std::unordered_set<std::string> kGrading = {
        "xer.fx.exposure",       "xer.fx.gamma",
        "xer.fx.levels",         "xer.fx.auto_contrast",
        "xer.fx.temperature",    "xer.fx.tint",
        "xer.fx.vibrance",       "xer.fx.lift_gamma_gain",
        "xer.fx.hue_rotate",     "xer.fx.sepia",
        "xer.fx.duotone",        "xer.fx.channel_mixer",
        "xer.fx.invert_lum",     "xer.fx.color_isolate",
        "xer.fx.rgb_swap",       "xer.fx.saturation_boost",
    };
    static const std::unordered_set<std::string> kBlur = {
        "xer.fx.directional_blur", "xer.fx.radial_blur",
        "xer.fx.zoom_blur",        "xer.fx.fast_gaussian",
        "xer.fx.unsharp_mask",     "xer.fx.median3",
    };
    static const std::unordered_set<std::string> kDistort = {
        "xer.fx.wave_h", "xer.fx.wave_v", "xer.fx.ripple",
        "xer.fx.mirror", "xer.fx.tile",   "xer.fx.twirl",
    };
    static const std::unordered_set<std::string> kStylize = {
        "xer.fx.film_grain",  "xer.fx.letterbox",
        "xer.fx.vignette_soft", "xer.fx.grid_lines",
        "xer.fx.border_frame", "xer.fx.corner_marks",
        "xer.fx.solarize",    "xer.fx.threshold_bw",
        "xer.fx.scanlines",   "xer.fx.neon_edges",
        "xer.fx.halftone",    "xer.fx.emboss_color",
        "xer.fx.edge_glow",   "xer.fx.tone_split",
    };
    if (id.rfind("xer.af.", 0) == 0) return "Audio Effects";
    if (id == "xer.fx.smart_reframe" || id == "xer.fx.track_mosaic" ||
        id == "xer.fx.face_blur") {
        return "AI & Tracking";
    }
    if (kGrading.count(id) || kBlur.count(id) || kDistort.count(id) ||
        kStylize.count(id)) {
        if (kGrading.count(id)) return "Color & Grading";
        if (kBlur.count(id)) return "Blur & Sharpen";
        if (kDistort.count(id)) return "Distort";
        return "Stylize & Overlay";
    }
    return "Video Effects";
}

// Fixed folder order (Favorites is prepended dynamically).
const char* kCategoryOrder[] = {
    "Color & Grading", "Blur & Sharpen", "Distort",
    "Stylize & Overlay", "Transform", "Keying", "Depth & Lens",
    "AI & Tracking", "Video Effects", "Audio Effects",
    "Plugins", "Scripts",
};

std::string BadgeSuffix(FxEntry::Badge badge) {
    switch (badge) {
        case FxEntry::Badge::Audio: return "   [A]";
        case FxEntry::Badge::Track: return "   [T]";
        default: return "";
    }
}

// Positional param defaults + descriptions for the builtin kernels
// whose PluginDesc declares no params (the Desc() helper leaves
// desc.params empty — the kernels read positional floats with their
// own fallbacks). This mirrors the legacy effects panel's hand table
// (ui/effects_panel.cc Catalog) so the same ids get the same
// defaults; entries that DO declare params (smart_reframe,
// track_mosaic, voice_isolate) keep their PluginDesc values.
struct BuiltinDefaults {
    std::vector<float> params;
    const char* description;
};

const BuiltinDefaults* LegacyBuiltinDefaults(const std::string& id) {
    static const std::unordered_map<std::string, BuiltinDefaults>
        kTable = {
            {"xer.fx.exposure", {{0.0f}, "exposure · stops -4..4"}},
            {"xer.fx.gamma", {{1.0f}, "gamma correction · gamma 0.2..4"}},
            {"xer.fx.levels",
             {{0.0f, 255.0f, 1.0f},
              "levels · black 0-128, white 128-255, gamma 0.2..4"}},
            {"xer.fx.auto_contrast",
             {{0.5f}, "auto levels · clip percent 0..10"}},
            {"xer.fx.temperature", {{0.0f}, "warm/cool · -100..100"}},
            {"xer.fx.tint", {{0.0f}, "green/magenta · -100..100"}},
            {"xer.fx.vibrance", {{0.0f}, "vibrance · -100..100"}},
            {"xer.fx.lift_gamma_gain",
             {{0.0f, 1.0f, 1.0f},
              "lift -1..1 · gamma 0.2..4 · gain 0..4"}},
            {"xer.fx.directional_blur",
             {{12.0f, 0.0f},
              "directional smear · radius px, angle deg"}},
            {"xer.fx.radial_blur",
             {{0.3f, 0.5f, 0.5f},
              "radial smear · strength 0..1, center x,y 0..1"}},
            {"xer.fx.zoom_blur",
             {{0.3f}, "zoom smear · strength 0..1"}},
            {"xer.fx.fast_gaussian",
             {{6.0f}, "fast gaussian · radius px 1..32"}},
            {"xer.fx.unsharp_mask",
             {{1.0f, 3.0f},
              "unsharp mask · amount 0..4, radius 1..16"}},
            {"xer.fx.median3", {{1.0f}, "median 3x3 · passes 1..4"}},
            {"xer.fx.hue_rotate", {{0.0f}, "hue rotate · degrees"}},
            {"xer.fx.sepia", {{1.0f}, "sepia · amount 0..1"}},
            {"xer.fx.duotone",
             {{220.0f, 40.0f},
              "duotone · shadow hue, light hue deg"}},
            {"xer.fx.channel_mixer",
             {{1.0f, 1.0f, 1.0f},
              "channel mixer · r,g,b gains 0..2"}},
            {"xer.fx.invert_lum",
             {{1.0f}, "invert luminance · amount 0..1"}},
            {"xer.fx.color_isolate",
             {{0.0f, 20.0f},
              "keep hue · target deg, tolerance deg"}},
            {"xer.fx.rgb_swap", {{0.0f}, "channel order · 0-5"}},
            {"xer.fx.saturation_boost",
             {{30.0f}, "saturation · -100..100"}},
            {"xer.fx.wave_h",
             {{12.0f, 120.0f},
              "wave horizontal · amp px, wavelength px"}},
            {"xer.fx.wave_v",
             {{12.0f, 120.0f},
              "wave vertical · amp px, wavelength px"}},
            {"xer.fx.ripple",
             {{10.0f, 60.0f},
              "ripple · amp px, wavelength px"}},
            {"xer.fx.mirror", {{0.0f}, "mirror · mode 0-3"}},
            {"xer.fx.tile", {{3.0f}, "tile · count 2..8"}},
            {"xer.fx.twirl", {{90.0f}, "twirl · angle deg"}},
            {"xer.fx.film_grain",
             {{20.0f, 1.0f}, "film grain · amount 0..100, mono 0/1"}},
            {"xer.fx.letterbox",
             {{0.12f}, "letterbox · bar fraction 0..0.45"}},
            {"xer.fx.vignette_soft",
             {{0.5f, 0.7f},
              "vignette · strength 0..1, radius 0..1"}},
            {"xer.fx.grid_lines",
             {{4.0f, 0.3f}, "grid · cells 2..16, alpha 0..1"}},
            {"xer.fx.border_frame",
             {{8.0f}, "border frame · thickness px"}},
            {"xer.fx.corner_marks",
             {{24.0f}, "corner marks · length px"}},
            {"xer.fx.solarize",
             {{128.0f}, "solarize · threshold 0..255"}},
            {"xer.fx.threshold_bw",
             {{128.0f}, "threshold b/w · level 0..255"}},
            {"xer.fx.scanlines",
             {{4.0f, 0.5f},
              "scanlines · spacing px, strength 0..1"}},
            {"xer.fx.neon_edges",
             {{40.0f, 300.0f}, "neon edges · threshold, hue deg"}},
            {"xer.fx.halftone",
             {{8.0f}, "halftone · cell px 4..32"}},
            {"xer.fx.emboss_color",
             {{135.0f, 1.0f},
              "emboss color · angle deg, strength 0..2"}},
            {"xer.fx.edge_glow",
             {{40.0f, 1.5f},
              "edge glow · threshold, intensity 0..4"}},
            {"xer.fx.tone_split",
             {{220.0f, 40.0f},
              "tone split · shadow hue, highlight hue"}},
        };
    const auto it = kTable.find(id);
    return it == kTable.end() ? nullptr : &it->second;
}

}  // namespace

// ---- catalog --------------------------------------------------------------

std::vector<FxEntry> BuildFxRegistryEntries() {
    std::vector<FxEntry> list;

    // 1. The compositor's named core effects — the same dispatch-table
    //    defaults + descriptions the legacy effects panel shipped
    //    (ui/effects_panel.cc), so project clip effects keep identical
    //    semantics.
    const auto core = [](const char* type,
                         std::vector<float> params,
                         const char* description,
                         const char* category) {
        FxEntry e;
        e.type = type;
        e.name = type;   // core types have no separate display name
        e.description = description;
        e.category = category;
        e.default_params = std::move(params);
        e.badge = FxEntry::Badge::Video;
        return e;
    };
    list.push_back(core("brightness_contrast", {0.0f, 1.0f},
                        "brightness -1..1 · contrast 0..2",
                        "Color & Grading"));
    list.push_back(core("grayscale", {}, "remove color",
                        "Color & Grading"));
    list.push_back(core("color_balance",
                        {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f},
                        "shadow/mid/high r,g,b shifts + gains",
                        "Color & Grading"));
    list.push_back(core("posterize", {4.0f},
                        "band colors · levels per channel 2-256",
                        "Color & Grading"));
    list.push_back(core("box_blur", {4.0f}, "cheap blur · radius px",
                        "Blur & Sharpen"));
    list.push_back(core("gaussian_blur", {2.0f}, "soft blur · sigma",
                        "Blur & Sharpen"));
    list.push_back(core("sharpen", {1.5f}, "unsharp mask · amount",
                        "Blur & Sharpen"));
    list.push_back(core("edge_detect", {},
                        "edge highlight (Skia)", "Blur & Sharpen"));
    list.push_back(core("emboss", {}, "relief emboss (Skia)",
                        "Stylize & Overlay"));
    list.push_back(core("motion_blur", {0.0f, 10.0f},
                        "directional · angle deg, samples",
                        "Blur & Sharpen"));
    list.push_back(core("bokeh_dof",
                        {1.0f, 0.5f, 0.5f, 0.12f, 0.3f, 18.0f, 3.0f},
                        "depth of field · focus, range, boost, radius",
                        "Depth & Lens"));
    list.push_back(core("bokeh_blur", {10.0f, 2.5f},
                        "bokeh blur · radius, boost", "Depth & Lens"));
    list.push_back(core("flip_h", {}, "mirror horizontally",
                        "Transform"));
    list.push_back(core("flip_v", {}, "mirror vertically",
                        "Transform"));
    list.push_back(core("vignette", {0.5f, 0.7f},
                        "darken edges · strength, size (Skia)",
                        "Stylize & Overlay"));
    list.push_back(core("chromatic_aberration", {2.0f},
                        "RGB fringe · shift px (Skia)",
                        "Stylize & Overlay"));
    list.push_back(core("pixelate", {8.0f},
                        "mosaic · block px (Skia)",
                        "Stylize & Overlay"));
    list.push_back(core("oil_paint", {4.0f},
                        "painterly · radius (Skia)",
                        "Stylize & Overlay"));
    list.push_back(core("chroma_key", {0.0f, 255.0f, 0.0f, 120.0f, 40.0f},
                        "green screen · key r,g,b, tolerance, softness",
                        "Keying"));
    list.push_back(core("lens_sphere", {0.45f, 1.4f, 3.0f, 0.35f, 0.6f},
                        "fisheye · radius, strength, zoom, cx, cy",
                        "Depth & Lens"));
    list.push_back(core("magnifier", {0.3f, 1.8f, 2.0f},
                        "lens zoom · radius, strength, power",
                        "Depth & Lens"));
    list.push_back(core("ribbed_glass", {24.0f, 6.0f, 0.0f, 0.5f},
                        "glass ribs · spacing, amplitude, angle, alpha",
                        "Depth & Lens"));

    // 2. The builtin registry — real names + positional param defaults
    //    straight from PluginDesc (no duplicated tables). Audio kinds
    //    get the Audio badge; the track-driven pair gets Track.
    for (const PluginDesc& desc : BuiltinFx::Effects()) {
        FxEntry e;
        e.type = "builtin:" + desc.id;
        e.name = desc.name;
        e.category = CategoryForBuiltin(desc.id);
        std::string description;
        for (const XER_ParamDesc& p : desc.params) {
            if (!description.empty()) description += ", ";
            description += p.name != nullptr ? p.name : "param";
            e.default_params.push_back(p.default_value);
        }
        e.description = description.empty()
                            ? desc.name + " · no parameters"
                            : desc.name + " · " + description;
        if (desc.params.empty()) {
            // Undeclared positional params — the kernels still take
            // them. Port the legacy panel's defaults so apply +
            // inspector editing get the documented values.
            if (const BuiltinDefaults* d =
                    LegacyBuiltinDefaults(desc.id)) {
                e.default_params = d->params;
                e.description = d->description;
            }
        }
        e.badge = desc.kind == XER_PLUGIN_AUDIO_FX
                      ? FxEntry::Badge::Audio
                  : e.category == "AI & Tracking"
                      ? FxEntry::Badge::Track
                      : FxEntry::Badge::Video;
        list.push_back(std::move(e));
    }

    // 3. Third-party plugin DLLs — the non-builtin host effects
    //    (PluginHost registers builtins too; skip them as duplicates
    //    of section 2, which carries the "builtin:" spelling).
    for (const PluginDesc& desc : SharedPluginHost().Effects()) {
        if (desc.builtin) continue;
        FxEntry e;
        e.type = "plugin:" + desc.id;
        e.name = desc.name;
        e.category = "Plugins";
        for (const XER_ParamDesc& p : desc.params) {
            e.default_params.push_back(p.default_value);
        }
        e.description = desc.name + " · " + desc.vendor + " (plugin)";
        e.badge = desc.kind == XER_PLUGIN_AUDIO_FX
                      ? FxEntry::Badge::Audio
                      : FxEntry::Badge::Video;
        list.push_back(std::move(e));
    }

    // 4. JS script effects — the runtime catalog is empty until the
    //    shell loads it, so this section no-ops headless.
    for (const auto& d : JsFxRuntime::VideoEffects()) {
        FxEntry e;
        e.type = "script:" + d.id;
        e.name = d.title.empty() ? d.id : d.title;
        e.category = "Scripts";
        for (const auto& pd : d.params) {
            e.default_params.push_back(pd.def);
        }
        e.description = e.name + " · script (JS)";
        e.badge = FxEntry::Badge::Video;
        list.push_back(std::move(e));
    }
    for (const auto& d : JsFxRuntime::AudioEffects()) {
        FxEntry e;
        e.type = "script:" + d.id;
        e.name = d.title.empty() ? d.id : d.title;
        e.category = "Scripts";
        for (const auto& pd : d.params) {
            e.default_params.push_back(pd.def);
        }
        e.description = e.name + " · script (JS)";
        e.badge = FxEntry::Badge::Audio;
        list.push_back(std::move(e));
    }
    return list;
}

// ---- panel ---------------------------------------------------------------

std::shared_ptr<EffectsPanel> EffectsPanel::Create() {
    return std::shared_ptr<EffectsPanel>(new EffectsPanel());
}

EffectsPanel::EffectsPanel() {
    SetFocusable(true);
    SetClipsChildren(true);
}

void EffectsPanel::SetOnApplyFx(
    std::function<void(const std::vector<std::string>&,
                       const std::string&)>
        cb) {
    on_apply_fx_ = std::move(cb);
}

void EffectsPanel::SetClipSelectionProvider(
    std::function<std::vector<std::string>()> provider) {
    clip_provider_ = std::move(provider);
    SyncApplyState();
}

void EffectsPanel::SetOnStatus(
    std::function<void(const std::string&, bool)> cb) {
    on_status_ = std::move(cb);
}

void EffectsPanel::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;

    search_input_ = std::make_shared<W::TextInput>("");
    search_input_->SetPlaceholder("Search effects…");
    search_input_->SetTooltip("Filter by name, type or description");
    search_input_->SetOnValueChanged([this](const std::string& value) {
        if (search_ == value) return;
        const bool was_searching = !search_.empty();
        search_ = value;
        const bool searching = !search_.empty();
        if (!was_searching && searching && tree_ != nullptr) {
            // Freeze the user's expansion set; the search rebuild
            // opens every surviving folder.
            saved_expanded_ = tree_->GetExpanded();
        }
        if (was_searching && !searching && tree_ != nullptr) {
            tree_->SetExpanded(saved_expanded_);
        }
        RebuildTree();
    });

    tree_ = W::TreeView::Create();
    tree_->SetRowHeight(24.0f);
    tree_->SetOnSelectionChanged([this] {
        if (!syncing_selection_) SyncApplyState();
    });
    tree_->SetOnExpandChanged([this](const std::string&, bool) {
        // Keep the saved pre-search set honest when the user folds a
        // folder under an active search — clearing the search restores
        // the latest manual state, not the stale snapshot.
        if (!search_.empty()) saved_expanded_ = tree_->GetExpanded();
    });

    // Wrap the internal list's item-click to add leaf double-click
    // apply while preserving the tree's chevron-zone behavior
    // (RowClick is private — replicate its branch contract here).
    if (auto list = tree_->GetList()) {
        list->SetOnItemClick(
            [this](int index, Layout::Point position) -> bool {
                const auto flat = FlatRows();
                if (index < 0 ||
                    index >= static_cast<int>(flat.size())) {
                    return false;
                }
                const FlatRef& ref =
                    flat[static_cast<std::size_t>(index)];
                const bool is_branch =
                    ref.node->is_branch || !ref.node->children.empty();
                const float row_x = tree_->GetList()->GetBounds().x +
                                    kRowInset +
                                    tree_->GetIndentSize() *
                                        static_cast<float>(ref.depth);
                if (is_branch) {
                    if (position.x >= row_x &&
                        position.x < row_x + kChevronZone) {
                        tree_->SetNodeExpanded(
                            ref.node->id,
                            !tree_->IsExpanded(ref.node->id));
                        return true;
                    }
                    return false;
                }
                // Leaf: synthesize double-click apply.
                const auto now = std::chrono::steady_clock::now();
                const double dt_ms =
                    std::chrono::duration<double, std::milli>(
                        now - last_click_time_)
                        .count();
                const bool same_row = last_click_id_ == ref.node->id;
                const bool near =
                    std::abs(position.x - last_click_point_.x) < 6.0f &&
                    std::abs(position.y - last_click_point_.y) < 6.0f;
                if (same_row && near && dt_ms <= kDoubleClickMs) {
                    last_click_id_.clear();
                    if (const FxEntry* e = EntryForTreeId(ref.node->id)) {
                        ApplyType(e->type);
                    }
                    return true;
                }
                last_click_id_ = ref.node->id;
                last_click_time_ = now;
                last_click_point_ = position;
                return false;   // let the tree select the leaf
            });
    }

    apply_btn_ = std::make_shared<W::Button>("Apply",
                                             W::ButtonStyle::Primary);
    apply_btn_->SetSize(W::ButtonSize::Small);
    apply_btn_->SetTooltip("Apply the selected effect to the "
                           "timeline selection");
    apply_btn_->SetOnClick([this] { ApplySelected(); });
    apply_btn_->SetEnabled(false);

    status_ = std::make_shared<W::Label>("Select an effect", true);

    AddChild(search_input_);
    AddChild(tree_);
    AddChild(apply_btn_);
    AddChild(status_);
}

// ---- model -----------------------------------------------------------------

void EffectsPanel::SetEntries(std::vector<FxEntry> entries) {
    entries_ = std::move(entries);
    RebuildTree();
}

const FxEntry* EffectsPanel::FindEntry(const std::string& type) const {
    for (const FxEntry& e : entries_) {
        if (e.type == type) return &e;
    }
    return nullptr;
}

std::vector<float> EffectsPanel::DefaultParams(
    const std::string& type) const {
    if (const FxEntry* e = FindEntry(type)) return e->default_params;
    return {};
}

void EffectsPanel::SetSearchText(std::string text) {
    if (search_input_ != nullptr) search_input_->SetValue(text);
    const bool was_searching = !search_.empty();
    if (search_ == text) return;
    search_ = std::move(text);
    const bool searching = !search_.empty();
    if (tree_ != nullptr) {
        if (!was_searching && searching) {
            saved_expanded_ = tree_->GetExpanded();
        } else if (was_searching && !searching) {
            tree_->SetExpanded(saved_expanded_);
        }
    }
    RebuildTree();
}

int EffectsPanel::VisibleEntryCount() const {
    int count = 0;
    const std::function<void(const std::vector<W::TreeNode>&)> walk =
        [&](const std::vector<W::TreeNode>& nodes) {
            for (const auto& n : nodes) {
                if (n.id.rfind("fx:", 0) == 0 ||
                    n.id.rfind("fav:", 0) == 0) {
                    ++count;
                }
                walk(n.children);
            }
        };
    walk(roots_);
    return count;
}

void EffectsPanel::SetFavorites(std::set<std::string> types) {
    favorites_ = std::move(types);
    RebuildTree();
}

bool EffectsPanel::IsFavorite(const std::string& type) const {
    return favorites_.count(type) != 0;
}

const FxEntry* EffectsPanel::EntryForTreeId(
    const std::string& id) const {
    std::string type;
    if (id.rfind("fx:", 0) == 0) {
        type = id.substr(3);
    } else if (id.rfind("fav:", 0) == 0) {
        type = id.substr(4);
    } else {
        return nullptr;
    }
    return FindEntry(type);
}

std::string EffectsPanel::SelectedType() const {
    if (tree_ == nullptr) return {};
    const FxEntry* e = EntryForTreeId(tree_->GetSelectedId());
    return e != nullptr ? e->type : std::string();
}

void EffectsPanel::RebuildTree() {
    EnsureChildren();
    const std::string needle = Lower(search_);
    const auto matches = [&](const FxEntry& e) {
        if (needle.empty()) return true;
        return Lower(e.name).find(needle) != std::string::npos ||
               Lower(e.type).find(needle) != std::string::npos ||
               Lower(e.description).find(needle) != std::string::npos;
    };

    std::vector<W::TreeNode> roots;

    // Favorites group — distinct "fav:" ids so a duplicated leaf keeps
    // a unique selection key (tree ids are identity).
    if (!favorites_.empty()) {
        W::TreeNode fav_root;
        fav_root.id = "cat:Favorites";
        fav_root.label = "Favorites";
        fav_root.is_branch = true;
        for (const FxEntry& e : entries_) {
            if (!favorites_.count(e.type) || !matches(e)) continue;
            W::TreeNode leaf;
            leaf.id = "fav:" + e.type;
            leaf.label = e.name + BadgeSuffix(e.badge);
            fav_root.children.push_back(std::move(leaf));
        }
        if (!fav_root.children.empty()) {
            roots.push_back(std::move(fav_root));
        }
    }

    // Category folders in the fixed order, then leftovers alphabetical.
    std::unordered_map<std::string, W::TreeNode> folders;
    std::vector<std::string> order(kCategoryOrder,
                                   kCategoryOrder +
                                       std::size(kCategoryOrder));
    std::unordered_set<std::string> seen_order(order.begin(),
                                               order.end());
    for (const FxEntry& e : entries_) {
        if (!seen_order.count(e.category)) order.push_back(e.category);
    }
    for (const std::string& cat : order) {
        W::TreeNode folder;
        folder.id = "cat:" + cat;
        folder.label = cat;
        folder.is_branch = true;
        folders.emplace(cat, std::move(folder));
    }
    for (const FxEntry& e : entries_) {
        if (!matches(e)) continue;
        auto it = folders.find(e.category);
        if (it == folders.end()) continue;   // shouldn't happen
        W::TreeNode leaf;
        leaf.id = "fx:" + e.type;
        leaf.label = e.name + BadgeSuffix(e.badge);
        it->second.children.push_back(std::move(leaf));
    }
    for (const std::string& cat : order) {
        auto it = folders.find(cat);
        if (it != folders.end() && !it->second.children.empty()) {
            roots.push_back(std::move(it->second));
        }
    }

    roots_ = roots;
    tree_->SetItems(std::move(roots));
    if (!search_.empty()) {
        // Under an active search every surviving folder opens so the
        // matches are visible (the user's set is restored on clear).
        tree_->ExpandAll();
    } else if (!saved_expanded_.empty()) {
        tree_->SetExpanded(saved_expanded_);
    } else {
        // First build: open the headliner folders only — keeps the
        // browser dense on startup (pro-NLE collapsed-by-default).
        tree_->SetExpanded({"cat:Favorites", "cat:Color & Grading",
                            "cat:Blur & Sharpen"});
    }
    SyncApplyState();
}

// ---- apply -----------------------------------------------------------------

void EffectsPanel::ApplyType(const std::string& type) {
    if (type.empty()) return;
    std::vector<std::string> clip_ids =
        clip_provider_ ? clip_provider_() : std::vector<std::string>{};
    if (clip_ids.empty()) {
        if (on_status_) {
            on_status_("No clip selected — nothing to apply", true);
        }
        if (status_ != nullptr) {
            status_->SetText("No clip selected");
        }
        return;
    }
    if (on_apply_fx_) on_apply_fx_(clip_ids, type);
    if (status_ != nullptr) {
        const FxEntry* e = FindEntry(type);
        status_->SetText("Applied " + (e != nullptr ? e->name : type));
    }
}

void EffectsPanel::ApplySelected() { ApplyType(SelectedType()); }

void EffectsPanel::SyncApplyState() {
    const std::string type = SelectedType();
    if (apply_btn_ != nullptr) {
        apply_btn_->SetEnabled(!type.empty());
    }
    if (status_ != nullptr) {
        if (const FxEntry* e = FindEntry(type)) {
            status_->SetText(e->description);
        } else {
            status_->SetText("Select an effect");
        }
    }
}

// ---- context menu ------------------------------------------------------------

std::vector<EffectsPanel::FlatRef> EffectsPanel::FlatRows() const {
    // The tree flattens DFS honoring the id-keyed expanded set —
    // replicate it so a list row index maps back to its TreeNode.
    std::vector<FlatRef> out;
    static const std::set<std::string> kEmptyExpanded;
    const auto& expanded =
        tree_ != nullptr ? tree_->GetExpanded() : kEmptyExpanded;
    const std::function<void(const std::vector<W::TreeNode>&, int)>
        walk = [&](const std::vector<W::TreeNode>& nodes, int depth) {
            for (const auto& n : nodes) {
                out.push_back({&n, depth});
                if (expanded.count(n.id)) {
                    walk(n.children, depth + 1);
                }
            }
        };
    walk(roots_, 0);
    return out;
}

int EffectsPanel::ListIndexAt(Layout::Point point) const {
    if (tree_ == nullptr || tree_->GetList() == nullptr) return -1;
    auto list = tree_->GetList();
    const Layout::Rect b = list->GetBounds();
    if (!b.Contains(point)) return -1;
    const int row = static_cast<int>(
        (point.y - b.y + list->GetScrollY()) / list->GetItemHeight());
    return row >= 0 ? row : -1;
}

bool EffectsPanel::ShowContextMenuAt(Layout::Point point) {
    EnsureChildren();
    const int index = ListIndexAt(point);
    const auto flat = FlatRows();
    if (index < 0 || index >= static_cast<int>(flat.size())) {
        return false;
    }
    const FlatRef& ref = flat[static_cast<std::size_t>(index)];
    const FxEntry* entry = EntryForTreeId(ref.node->id);
    std::vector<W::ContextMenu::Item> items;
    if (entry != nullptr) {
        const std::string type = entry->type;
        W::ContextMenu::Item apply;
        apply.label = "Apply to Selection";
        apply.action = [this, type] { ApplyType(type); };
        items.push_back(std::move(apply));

        W::ContextMenu::Item fav;
        const bool is_fav = IsFavorite(type);
        fav.label = is_fav ? "Remove from Favorites"
                           : "Add to Favorites";
        fav.action = [this, type] { ToggleFavorite(type); };
        items.push_back(std::move(fav));
    } else {
        // Branch row — collapse-all convenience.
        W::ContextMenu::Item expand;
        expand.label = tree_ != nullptr &&
                               tree_->IsExpanded(ref.node->id)
                           ? "Collapse"
                           : "Expand";
        expand.action = [this, id = ref.node->id] {
            tree_->SetNodeExpanded(id, !tree_->IsExpanded(id));
        };
        items.push_back(std::move(expand));
    }
    auto menu = W::ContextMenu::Create(std::move(items));
    menu->SetAnchor(point);
    ShowOverlay(menu);
    return true;
}

void EffectsPanel::ToggleFavorite(const std::string& type) {
    if (type.empty()) return;
    if (favorites_.count(type)) {
        favorites_.erase(type);
    } else {
        favorites_.insert(type);
    }
    RebuildTree();
}

// ---- widget contract -----------------------------------------------------------

void EffectsPanel::Render(XER::UI::Rendering::Renderer& renderer,
                          const XER::UI::Theme::Tokens& tokens) {
    renderer.FillRect(GetBounds(), tokens.panel, 0.0f);
}

void EffectsPanel::Layout(const Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float x = bounds.x + Theme::kSpaceSm;
    float y = bounds.y + Theme::kSpaceSm;
    const float w = bounds.width - Theme::kSpaceSm * 2.0f;

    search_input_->Layout({x, y, w, kToolbarH}, tokens);
    y += kToolbarH + kToolbarGap;

    const float foot = kStatusH + kToolbarH + kToolbarGap * 2.0f;
    const float tree_h =
        bounds.y + bounds.height - Theme::kSpaceSm - foot - y;
    tree_->Layout({x, y, w, std::max(0.0f, tree_h)}, tokens);
    y += std::max(0.0f, tree_h) + kToolbarGap;

    apply_btn_->Layout({x, y, w, kToolbarH}, tokens);
    y += kToolbarH + 2.0f;
    status_->Layout({x, y, w, kStatusH}, tokens);
}

}  // namespace XER::VideoEditor::Ui2
