// Implementation of the declarative UI manifest loaders. Each format is
// parsed with the dependency that already owns it in the asset pack —
// toolbar.json via jsoncpp, layout.yaml via yaml-cpp — while
// strings.ini and theme.toml are small enough to parse by hand, keeping
// the dependency surface unchanged.
//
// All loaders are additive: a missing file fails soft so caller-side
// compiled defaults survive; malformed lines/elements are skipped
// rather than failing the whole file (except a missing top-level
// section, which is a hard error per the schema in ui_manifest.h).
#include "ui_manifest.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <utility>

#include <json/json.h>
#include <yaml-cpp/yaml.h>

namespace XER::VideoEditor::AppRuntime {
namespace {

// ---- shared text helpers -------------------------------------------------

std::string Trim(const std::string& s) {
    const auto is_space = [](unsigned char c) {
        return std::isspace(c) != 0;
    };
    const auto begin = std::find_if_not(s.begin(), s.end(), is_space);
    const auto end = std::find_if_not(s.rbegin(), s.rend(), is_space).base();
    return begin < end ? std::string(begin, end) : std::string();
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// ---- strings.ini helpers -------------------------------------------------

// ';' and '#' open a comment only at the start of a (trimmed) line or
// right after whitespace, so values like "a;b" or "C#" survive.
std::string StripIniComment(const std::string& line) {
    for (size_t i = 0; i < line.size(); ++i) {
        if ((line[i] == ';' || line[i] == '#') &&
            (i == 0 ||
             std::isspace(static_cast<unsigned char>(line[i - 1])))) {
            return line.substr(0, i);
        }
    }
    return line;
}

// ---- theme.toml helpers (basic profile only) -----------------------------

// '#' opens a comment anywhere outside a quoted string.
std::string StripTomlComment(const std::string& line) {
    bool in_string = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '\\' && in_string) {
            ++i;  // an escaped char can never close the string
            continue;
        }
        if (c == '"') in_string = !in_string;
        if (c == '#' && !in_string) return line.substr(0, i);
    }
    return line;
}

// Unescapes a TOML basic-string body. Only \" and \\ are special for
// the asset-pack profile; other escapes pass through verbatim.
std::string UnescapeTomlString(const std::string& body) {
    std::string out;
    out.reserve(body.size());
    for (size_t i = 0; i < body.size(); ++i) {
        if (body[i] == '\\' && i + 1 < body.size()) {
            const char c = body[++i];
            if (c == '"' || c == '\\') {
                out += c;
            } else {
                out += '\\';
                out += c;
            }
        } else {
            out += body[i];
        }
    }
    return out;
}

// "#RRGGBB" / "#AARRGGBB" → ARGB; the 6-digit form gets opaque alpha.
bool ParseHexColor(const std::string& s, std::uint32_t* argb) {
    if ((s.size() != 7 && s.size() != 9) || s[0] != '#') return false;
    for (size_t i = 1; i < s.size(); ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(s[i])))
            return false;
    }
    const unsigned long value = std::strtoul(s.c_str() + 1, nullptr, 16);
    *argb = s.size() == 7
                ? (0xFF000000u | static_cast<std::uint32_t>(value))
                : static_cast<std::uint32_t>(value);
    return true;
}

// ---- layout.yaml helpers -------------------------------------------------

// Scalar → bool, tolerant like the window-config parser (app_manifest).
bool ParseYamlBool(const YAML::Node& node, bool fallback) {
    if (!node.IsDefined() || !node.IsScalar()) return fallback;
    const std::string s = ToLower(node.Scalar());
    if (s == "true" || s == "1" || s == "yes") return true;
    if (s == "false" || s == "0" || s == "no") return false;
    return fallback;
}

bool ReadYamlInt(const YAML::Node& node, int* out) {
    int value = 0;
    if (node.IsDefined() && node.IsScalar() &&
        YAML::convert<int>::decode(node, value)) {
        *out = value;
        return true;
    }
    return false;
}

bool ReadYamlDouble(const YAML::Node& node, double* out) {
    double value = 0.0;
    if (node.IsDefined() && node.IsScalar() &&
        YAML::convert<double>::decode(node, value)) {
        *out = value;
        return true;
    }
    return false;
}

// Scalar → string; empty when the node is missing or not a scalar.
std::string ReadYamlString(const YAML::Node& node) {
    if (!node.IsDefined() || !node.IsScalar()) return {};
    return node.Scalar();
}

}  // namespace

// ---- toolbar.json --------------------------------------------------------

bool LoadToolbarJson(const std::filesystem::path& path,
                     std::vector<ToolbarButton>* buttons,
                     std::string* error) {
    if (!std::filesystem::exists(path)) {
        if (error) *error = "toolbar.json not found";
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        if (error) *error = "toolbar.json not found";
        return false;
    }

    Json::CharReaderBuilder reader_builder;
    Json::Value root;
    std::string parse_errors;
    bool parsed = false;
    try {
        parsed = Json::parseFromStream(reader_builder, in, &root,
                                       &parse_errors);
    } catch (const Json::Exception& e) {
        if (error) *error = e.what();
        return false;
    } catch (...) {
        // Never-throw contract, same as ProjectStore::Load: a crafted
        // deeply-nested file must not escape as an exception.
        if (error) *error = "toolbar.json: malformed JSON";
        return false;
    }
    if (!parsed) {
        if (error) *error = "toolbar.json: " + parse_errors;
        return false;
    }

    const Json::Value toolbar = root.get("Toolbar", Json::Value());
    if (!root.isObject() || !toolbar.isObject()) {
        if (error) *error = "toolbar.json: Toolbar section missing";
        return false;
    }
    const Json::Value items = toolbar.get("buttons", Json::Value());
    if (!items.isArray()) {
        if (error)
            *error = "toolbar.json: Toolbar.buttons section missing";
        return false;
    }

    for (const Json::Value& elem : items) {
        if (!elem.isObject()) continue;  // stray scalars are skipped
        ToolbarButton button;
        if (elem["type"].isString() &&
            elem["type"].asString() == "separator") {
            button.separator = true;
            if (elem["id"].isString())
                button.id = elem["id"].asString();
        } else {
            if (elem["id"].isString())
                button.id = elem["id"].asString();
            if (elem["label"].isString())
                button.label = elem["label"].asString();
            if (elem["icon"].isString())
                button.icon = elem["icon"].asString();
            if (elem["action"].isString())
                button.action = elem["action"].asString();
        }
        if (buttons) buttons->push_back(std::move(button));
    }
    return true;
}

// ---- strings.ini ---------------------------------------------------------

std::string UiStrings::Get(const std::string& key,
                           const std::string& fallback) const {
    const auto it = labels.find(key);
    return it != labels.end() ? it->second : fallback;
}

bool LoadStringsIni(const std::filesystem::path& path, UiStrings* out,
                    std::string* error) {
    if (!std::filesystem::exists(path)) {
        if (error) *error = "strings.ini not found";
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        if (error) *error = "strings.ini not found";
        return false;
    }

    std::string section;  // tracked for schema fidelity — keys stay flat
    std::string raw;
    while (std::getline(in, raw)) {
        const std::string line = Trim(StripIniComment(Trim(raw)));
        if (line.empty()) continue;

        if (line.front() == '[') {
            // Well-formed headers set the section; malformed "[..."
            // lines are dropped, never parsed as keys.
            if (line.back() == ']')
                section = Trim(line.substr(1, line.size() - 2));
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;  // malformed → skip
        const std::string key = Trim(line.substr(0, eq));
        const std::string value = Trim(line.substr(eq + 1));
        if (key.empty()) continue;
        // Keys are stored verbatim — no section prefixing, per the
        // flat-labels contract in ui_manifest.h.
        if (out) out->labels[key] = value;
    }
    (void)section;
    return true;
}

UiStrings& SharedUiStrings() {
    static UiStrings strings;
    return strings;
}

// ---- theme.toml ----------------------------------------------------------

bool LoadThemeToml(const std::filesystem::path& path,
                   ThemeOverride* out, std::string* error) {
    if (!std::filesystem::exists(path)) {
        if (error) *error = "theme.toml not found";
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        if (error) *error = "theme.toml not found";
        return false;
    }

    std::string section;  // "" = top level; "colors"/"metrics"/other
    std::string raw;
    while (std::getline(in, raw)) {
        const std::string line = Trim(StripTomlComment(raw));
        if (line.empty()) continue;

        if (line.front() == '[') {
            if (line.back() == ']')
                section = Trim(line.substr(1, line.size() - 2));
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;  // malformed → skip
        const std::string key = Trim(line.substr(0, eq));
        const std::string value = Trim(line.substr(eq + 1));
        if (key.empty() || value.empty()) continue;

        const bool is_quoted = value.size() >= 2 && value.front() == '"' &&
                               value.back() == '"';
        const std::string unquoted =
            is_quoted
                ? UnescapeTomlString(value.substr(1, value.size() - 2))
                : value;

        if (section == "colors") {
            std::uint32_t argb = 0;
            if (ParseHexColor(unquoted, &argb) && out)
                out->colors[key] = argb;
            continue;
        }

        // [metrics], the top level, and any other section all feed the
        // flat metrics table — numbers and booleans only.
        double number = 0.0;
        if (value == "true" || value == "false") {
            number = value == "true" ? 1.0 : 0.0;
        } else if (!is_quoted) {
            char* end = nullptr;
            number = std::strtod(value.c_str(), &end);
            if (end == value.c_str() || *end != '\0')
                continue;  // bare strings and junk are skipped
        } else {
            continue;  // quoted strings have no metrics meaning
        }
        if (out) out->metrics[key] = number;
    }
    return true;
}

ThemeOverride& SharedThemeOverride() {
    static ThemeOverride theme;
    return theme;
}

// ---- layout.yaml ---------------------------------------------------------

bool LoadLayoutYaml(const std::filesystem::path& path,
                    LayoutConfig* out, std::string* error) {
    if (!std::filesystem::exists(path)) {
        if (error) *error = "layout.yaml not found";
        return false;
    }

    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const YAML::Exception& e) {
        if (error) *error = e.what();
        return false;
    }

    const YAML::Node layout = root["Layout"];
    if (!layout.IsDefined() || !layout.IsMap()) {
        if (error) *error = "layout.yaml: Layout section missing";
        return false;
    }

    const YAML::Node nav = layout["NavRail"];
    if (nav.IsDefined() && nav.IsMap()) {
        const YAML::Node items = nav["items"];
        if (items.IsDefined() && items.IsSequence()) {
            std::vector<std::string> nav_items;
            for (const YAML::Node& item : items) {
                if (item.IsScalar()) nav_items.push_back(item.Scalar());
            }
            if (out) out->nav_items = std::move(nav_items);
        }
    }

    const YAML::Node panels = layout["Panels"];
    if (out && panels.IsDefined() && panels.IsMap()) {
        const YAML::Node left = panels["left"];
        if (left.IsDefined() && left.IsMap()) {
            out->left_visible =
                ParseYamlBool(left["visible"], out->left_visible);
            int width = 0;
            if (ReadYamlInt(left["width"], &width))
                out->left_width = std::clamp(width, 120, 800);
        }
        const YAML::Node right = panels["right"];
        if (right.IsDefined() && right.IsMap()) {
            out->right_visible =
                ParseYamlBool(right["visible"], out->right_visible);
            int width = 0;
            if (ReadYamlInt(right["width"], &width))
                out->right_width = std::clamp(width, 120, 800);
            int tab = 0;
            if (ReadYamlInt(right["tab"], &tab))
                out->right_tab = std::clamp(tab, 0, 7);  // else keep -1
        }
    }

    const YAML::Node monitor = layout["Monitor"];
    if (out && monitor.IsDefined() && monitor.IsMap()) {
        out->transport_center = ParseYamlBool(
            monitor["transport_center"], out->transport_center);
    }

    return true;
}

LayoutConfig& SharedLayoutConfig() {
    static LayoutConfig layout;
    return layout;
}

// ---- menus.yaml ----------------------------------------------------------

bool LoadMenusYaml(const std::filesystem::path& path,
                   std::vector<MenuDef>* out, std::string* error) {
    if (!std::filesystem::exists(path)) {
        if (error) *error = "menus.yaml not found";
        return false;
    }

    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const YAML::Exception& e) {
        if (error) *error = e.what();
        return false;
    }

    const YAML::Node menus = root["Menus"];
    if (!menus.IsDefined() || !menus.IsSequence()) {
        if (error) *error = "menus.yaml: Menus section missing";
        return false;
    }

    std::vector<MenuDef> parsed;
    for (const YAML::Node& entry : menus) {
        if (!entry.IsMap()) continue;  // stray entries are skipped
        const std::string id = ReadYamlString(entry["id"]);
        if (id.empty()) continue;  // a menu without id is unfindable
        MenuDef menu;
        menu.id = id;
        const YAML::Node items = entry["items"];
        if (items.IsDefined() && items.IsSequence()) {
            for (const YAML::Node& elem : items) {
                if (!elem.IsMap()) continue;
                MenuItemDef item;
                item.separator =
                    ParseYamlBool(elem["separator"], item.separator);
                item.danger = ParseYamlBool(elem["danger"], item.danger);
                item.action = ReadYamlString(elem["action"]);
                item.label = ReadYamlString(elem["label"]);
                item.icon = ReadYamlString(elem["icon"]);
                item.shortcut = ReadYamlString(elem["shortcut"]);
                menu.items.push_back(std::move(item));
            }
        }
        parsed.push_back(std::move(menu));
    }
    if (out) *out = std::move(parsed);
    return true;
}

std::vector<MenuDef>& SharedMenusConfig() {
    static std::vector<MenuDef> menus;
    return menus;
}

const MenuDef* FindMenuDef(const std::string& id) {
    for (const MenuDef& menu : SharedMenusConfig()) {
        if (menu.id == id) return &menu;
    }
    return nullptr;
}

// ---- effects.yaml --------------------------------------------------------

bool LoadEffectsYaml(const std::filesystem::path& path,
                     std::vector<EffectDef>* out, std::string* error) {
    if (!std::filesystem::exists(path)) {
        if (error) *error = "effects.yaml not found";
        return false;
    }

    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const YAML::Exception& e) {
        if (error) *error = e.what();
        return false;
    }

    const YAML::Node effects = root["Effects"];
    if (!effects.IsDefined() || !effects.IsSequence()) {
        if (error) *error = "effects.yaml: Effects section missing";
        return false;
    }

    std::vector<EffectDef> parsed;
    for (const YAML::Node& entry : effects) {
        if (!entry.IsMap()) continue;
        const std::string id = ReadYamlString(entry["id"]);
        if (id.empty()) continue;
        EffectDef effect;
        effect.id = id;
        effect.label = ReadYamlString(entry["label"]);
        effect.category = ReadYamlString(entry["category"]);
        effect.icon = ReadYamlString(entry["icon"]);
        const YAML::Node params = entry["params"];
        if (params.IsDefined() && params.IsSequence()) {
            for (const YAML::Node& elem : params) {
                if (!elem.IsMap()) continue;
                EffectParamDef param;
                param.id = ReadYamlString(elem["id"]);
                param.label = ReadYamlString(elem["label"]);
                param.kind = ReadYamlString(elem["kind"]);
                ReadYamlDouble(elem["min"], &param.min_v);
                ReadYamlDouble(elem["max"], &param.max_v);
                ReadYamlDouble(elem["default"], &param.default_v);
                param.unit = ReadYamlString(elem["unit"]);
                const YAML::Node choices = elem["choices"];
                if (choices.IsDefined() && choices.IsSequence()) {
                    for (const YAML::Node& choice : choices) {
                        if (choice.IsScalar())
                            param.choices.push_back(choice.Scalar());
                    }
                }
                effect.params.push_back(std::move(param));
            }
        }
        parsed.push_back(std::move(effect));
    }
    if (out) *out = std::move(parsed);
    return true;
}

std::vector<EffectDef>& SharedEffectsCatalog() {
    static std::vector<EffectDef> effects;
    return effects;
}

const EffectDef* FindEffectDef(const std::string& id) {
    for (const EffectDef& effect : SharedEffectsCatalog()) {
        if (effect.id == id) return &effect;
    }
    return nullptr;
}

// ---- inspector.yaml ------------------------------------------------------

bool LoadInspectorYaml(const std::filesystem::path& path,
                       std::vector<InspectorSchemaDef>* out,
                       std::string* error) {
    if (!std::filesystem::exists(path)) {
        if (error) *error = "inspector.yaml not found";
        return false;
    }

    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const YAML::Exception& e) {
        if (error) *error = e.what();
        return false;
    }

    const YAML::Node inspector = root["Inspector"];
    if (!inspector.IsDefined() || !inspector.IsSequence()) {
        if (error) *error = "inspector.yaml: Inspector section missing";
        return false;
    }

    std::vector<InspectorSchemaDef> parsed;
    for (const YAML::Node& entry : inspector) {
        if (!entry.IsMap()) continue;
        const std::string selection = ReadYamlString(entry["selection"]);
        if (selection.empty()) continue;
        InspectorSchemaDef schema;
        schema.selection = selection;
        const YAML::Node sections = entry["sections"];
        if (sections.IsDefined() && sections.IsSequence()) {
            for (const YAML::Node& elem : sections) {
                if (!elem.IsMap()) continue;
                InspectorSectionDef section;
                section.id = ReadYamlString(elem["id"]);
                section.title = ReadYamlString(elem["title"]);
                const YAML::Node fields = elem["fields"];
                if (fields.IsDefined() && fields.IsSequence()) {
                    for (const YAML::Node& f : fields) {
                        if (!f.IsMap()) continue;
                        InspectorFieldDef field;
                        field.id = ReadYamlString(f["id"]);
                        field.label = ReadYamlString(f["label"]);
                        field.kind = ReadYamlString(f["kind"]);
                        section.fields.push_back(std::move(field));
                    }
                }
                schema.sections.push_back(std::move(section));
            }
        }
        parsed.push_back(std::move(schema));
    }
    if (out) *out = std::move(parsed);
    return true;
}

std::vector<InspectorSchemaDef>& SharedInspectorSchemas() {
    static std::vector<InspectorSchemaDef> schemas;
    return schemas;
}

const InspectorSchemaDef* FindInspectorSchema(
    const std::string& selection) {
    for (const InspectorSchemaDef& schema : SharedInspectorSchemas()) {
        if (schema.selection == selection) return &schema;
    }
    return nullptr;
}

// ---- workspaces.yaml -----------------------------------------------------

bool LoadWorkspacesYaml(const std::filesystem::path& path,
                        std::vector<WorkspaceDef>* out,
                        std::string* error) {
    if (!std::filesystem::exists(path)) {
        if (error) *error = "workspaces.yaml not found";
        return false;
    }

    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const YAML::Exception& e) {
        if (error) *error = e.what();
        return false;
    }

    const YAML::Node workspaces = root["Workspaces"];
    if (!workspaces.IsDefined() || !workspaces.IsSequence()) {
        if (error)
            *error = "workspaces.yaml: Workspaces section missing";
        return false;
    }

    std::vector<WorkspaceDef> parsed;
    for (const YAML::Node& entry : workspaces) {
        if (!entry.IsMap()) continue;
        const std::string name = ReadYamlString(entry["name"]);
        if (name.empty()) continue;
        WorkspaceDef workspace;
        workspace.name = name;
        workspace.left_visible = ParseYamlBool(entry["left_visible"],
                                               workspace.left_visible);
        workspace.right_visible = ParseYamlBool(entry["right_visible"],
                                                workspace.right_visible);
        int tab = 0;
        if (ReadYamlInt(entry["right_tab"], &tab))
            workspace.right_tab = std::clamp(tab, 0, 7);
        double fraction = 0.0;
        if (ReadYamlDouble(entry["timeline_fraction"], &fraction))
            workspace.timeline_fraction = std::clamp(fraction, 0.05, 0.95);
        parsed.push_back(std::move(workspace));
    }
    if (out) *out = std::move(parsed);
    return true;
}

std::vector<WorkspaceDef>& SharedWorkspaces() {
    static std::vector<WorkspaceDef> workspaces;
    return workspaces;
}

// ---- monitor.yaml --------------------------------------------------------

bool LoadMonitorYaml(const std::filesystem::path& path,
                     MonitorConfig* out, std::string* error) {
    if (!std::filesystem::exists(path)) {
        if (error) *error = "monitor.yaml not found";
        return false;
    }

    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const YAML::Exception& e) {
        if (error) *error = e.what();
        return false;
    }

    const YAML::Node monitor = root["Monitor"];
    if (!monitor.IsDefined() || !monitor.IsMap()) {
        if (error) *error = "monitor.yaml: Monitor section missing";
        return false;
    }
    if (!out) return true;

    const YAML::Node transport = monitor["Transport"];
    if (transport.IsDefined() && transport.IsMap()) {
        const YAML::Node buttons = transport["buttons"];
        if (buttons.IsDefined() && buttons.IsSequence()) {
            std::vector<std::string> names;
            for (const YAML::Node& button : buttons) {
                if (button.IsScalar()) names.push_back(button.Scalar());
            }
            out->transport_buttons = std::move(names);
        }
    }

    const YAML::Node overlays = monitor["Overlays"];
    if (overlays.IsDefined() && overlays.IsMap()) {
        out->safe_margins = ParseYamlBool(overlays["safe_margins"],
                                          out->safe_margins);
        out->grid = ParseYamlBool(overlays["grid"], out->grid);
    }

    // default_quality sits at Monitor level in the asset pack; the
    // Overlays-level spelling from the schema comment is honored too.
    std::string quality = ReadYamlString(monitor["default_quality"]);
    if (quality.empty() && overlays.IsDefined() && overlays.IsMap())
        quality = ReadYamlString(overlays["default_quality"]);
    if (quality == "full" || quality == "half" || quality == "quarter")
        out->default_quality = quality;

    return true;
}

MonitorConfig& SharedMonitorConfig() {
    static MonitorConfig config;
    return config;
}

// ---- panels.yaml ---------------------------------------------------------

bool LoadPanelsConfig(const std::filesystem::path& path,
                      PanelsConfig* out, std::string* error) {
    if (!std::filesystem::exists(path)) {
        if (error) *error = "panels.yaml not found";
        return false;
    }

    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const YAML::Exception& e) {
        if (error) *error = e.what();
        return false;
    }

    const YAML::Node panels_root = root["Panels"];
    if (!panels_root.IsDefined() || !panels_root.IsMap()) {
        if (error) *error = "panels.yaml: Panels section missing";
        return false;
    }

    // Soft problems accumulate here and are reported through `error`
    // alongside a true return — "loaded with warnings".
    std::string notes;
    const auto add_note = [&notes](const std::string& note) {
        if (!notes.empty()) notes += "; ";
        notes += note;
    };

    PanelsConfig parsed;

    // Unit first — the size/min_size clamps below depend on it.
    std::string unit = ToLower(ReadYamlString(panels_root["dock_size_unit"]));
    if (unit == "px" || unit == "fraction") {
        parsed.dock_size_unit = unit;
    } else if (!unit.empty()) {
        add_note("unknown dock_size_unit '" + unit + "' -> px");
    }

    int thickness = 0;
    if (ReadYamlInt(panels_root["splitter_thickness"], &thickness)) {
        parsed.splitter_thickness = std::clamp(thickness, 1, 24);
    }

    const YAML::Node order = panels_root["workspace_order"];
    if (order.IsDefined() && order.IsSequence()) {
        for (const YAML::Node& name : order) {
            if (name.IsScalar())
                parsed.workspace_order.push_back(name.Scalar());
        }
    }

    const YAML::Node entries = panels_root["panels"];
    if (entries.IsDefined() && entries.IsSequence()) {
        const bool fraction = parsed.dock_size_unit == "fraction";
        for (const YAML::Node& entry : entries) {
            if (!entry.IsMap()) continue;  // stray entries are skipped
            const std::string id = ReadYamlString(entry["id"]);
            if (id.empty()) {
                add_note("panel without id skipped");
                continue;
            }
            PanelSpec panel;
            panel.id = id;
            panel.title = ReadYamlString(entry["title"]);

            const std::string dock =
                ToLower(ReadYamlString(entry["dock"]));
            if (dock.empty()) {
                // keep the compiled default ("right")
            } else if (dock == "left" || dock == "right" ||
                       dock == "bottom" || dock == "center" ||
                       dock == "top") {
                panel.dock = dock;
            } else {
                add_note("panel '" + id + "': unknown dock '" + dock +
                         "' -> right");
            }

            double size = 0.0;
            if (ReadYamlDouble(entry["size"], &size)) {
                // Fractions clamp to a visible slice; px sizes clamp to
                // a sane upper bound so a typo can't park a 40000px
                // panel off-screen.
                panel.size = static_cast<float>(
                    fraction ? std::clamp(size, 0.0, 1.0)
                             : std::clamp(size, 0.0, 4000.0));
            }

            int min_size = 0;
            if (ReadYamlInt(entry["min_size"], &min_size)) {
                panel.min_size = std::clamp(min_size, 0, 4000);
            }
            // A min above the default size is a contradiction — clamp
            // it down so resize code never sees an impossible range.
            if (panel.size > 0.0f &&
                static_cast<float>(panel.min_size) > panel.size) {
                panel.min_size = static_cast<int>(panel.size);
            }

            panel.visible =
                ParseYamlBool(entry["visible"], panel.visible);

            const YAML::Node tabs = entry["tabs"];
            if (tabs.IsDefined() && tabs.IsSequence()) {
                for (const YAML::Node& tab : tabs) {
                    if (tab.IsScalar()) panel.tabs.push_back(tab.Scalar());
                }
            }

            parsed.panels.push_back(std::move(panel));
        }
    }

    if (out) *out = std::move(parsed);
    if (error && !notes.empty()) {
        if (!error->empty()) *error += "; ";
        *error += notes;
    }
    return true;
}

PanelsConfig& SharedPanelsConfig() {
    static PanelsConfig config;
    return config;
}

// ---- workspaces.yaml (panel presets) -------------------------------------

bool LoadWorkspacesConfig(const std::filesystem::path& path,
                          std::vector<WorkspaceSpec>* out,
                          std::string* error) {
    if (!std::filesystem::exists(path)) {
        if (error) *error = "workspaces.yaml not found";
        return false;
    }

    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const YAML::Exception& e) {
        if (error) *error = e.what();
        return false;
    }

    const YAML::Node workspaces = root["Workspaces"];
    if (!workspaces.IsDefined() || !workspaces.IsSequence()) {
        if (error)
            *error = "workspaces.yaml: Workspaces section missing";
        return false;
    }

    std::vector<WorkspaceSpec> parsed;
    for (const YAML::Node& entry : workspaces) {
        if (!entry.IsMap()) continue;
        const std::string name = ReadYamlString(entry["name"]);
        if (name.empty()) continue;
        WorkspaceSpec workspace;
        workspace.name = name;
        workspace.active_tab = ReadYamlString(entry["active_tab"]);

        const YAML::Node visibility = entry["panel_visibility"];
        if (visibility.IsDefined() && visibility.IsMap()) {
            for (auto it = visibility.begin(); it != visibility.end();
                 ++it) {
                if (!it->first.IsScalar() || !it->second.IsScalar())
                    continue;
                workspace.panel_visibility[it->first.Scalar()] =
                    ParseYamlBool(it->second, true);
            }
        }

        const YAML::Node sizes = entry["panel_sizes"];
        if (sizes.IsDefined() && sizes.IsMap()) {
            for (auto it = sizes.begin(); it != sizes.end(); ++it) {
                if (!it->first.IsScalar() || !it->second.IsScalar())
                    continue;
                double size = 0.0;
                if (ReadYamlDouble(it->second, &size)) {
                    workspace.panel_sizes[it->first.Scalar()] =
                        static_cast<float>(
                            std::clamp(size, 0.0, 4000.0));
                }
            }
        }

        parsed.push_back(std::move(workspace));
    }
    if (out) *out = std::move(parsed);
    return true;
}

std::vector<WorkspaceSpec>& SharedWorkspacesConfig() {
    static std::vector<WorkspaceSpec> workspaces;
    return workspaces;
}

}  // namespace XER::VideoEditor::AppRuntime
