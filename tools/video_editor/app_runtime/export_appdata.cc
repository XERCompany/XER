// export_appdata.cc — the app_super_exportAppData native module.
// runtime.js pulls it via NativeImport("app_super_exportAppData") and
// calls setOutAppData({key: value, ...}) to publish out-data (window
// yaml path, event handlers). Object arguments reach the native side
// as flat JSON text in OutValue::str; function-valued properties are
// serialised by the engine as {"$handler": <id>} markers since a JS
// function cannot survive a JSON round-trip.
#include "native_modules.h"

#include <cstdlib>
#include <string>

#include "js_engine.h"

namespace XER::VideoEditor::AppRuntime {
namespace {

void SkipWs(const std::string& text, size_t* pos) {
    while (*pos < text.size() &&
           (text[*pos] == ' ' || text[*pos] == '\t' ||
            text[*pos] == '\n' || text[*pos] == '\r')) {
        ++*pos;
    }
}

// Parses a JSON string at *pos (which must sit on the opening quote).
// Handles the standard escapes; \uXXXX is emitted as UTF-8.
bool ParseString(const std::string& text, size_t* pos, std::string* out) {
    if (*pos >= text.size() || text[*pos] != '"') {
        return false;
    }
    ++*pos;
    out->clear();
    while (*pos < text.size()) {
        const char c = text[(*pos)++];
        if (c == '"') {
            return true;
        }
        if (c != '\\' || *pos >= text.size()) {
            out->push_back(c);
            continue;
        }
        const char esc = text[(*pos)++];
        switch (esc) {
            case 'n': out->push_back('\n'); break;
            case 't': out->push_back('\t'); break;
            case 'r': out->push_back('\r'); break;
            case 'b': out->push_back('\b'); break;
            case 'f': out->push_back('\f'); break;
            case 'u': {
                if (*pos + 4 > text.size()) {
                    return false;
                }
                unsigned cp = 0;
                for (int i = 0; i < 4; ++i) {
                    const char h = text[(*pos)++];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') {
                        cp |= static_cast<unsigned>(h - '0');
                    } else if (h >= 'a' && h <= 'f') {
                        cp |= static_cast<unsigned>(h - 'a' + 10);
                    } else if (h >= 'A' && h <= 'F') {
                        cp |= static_cast<unsigned>(h - 'A' + 10);
                    } else {
                        return false;
                    }
                }
                if (cp < 0x80) {
                    out->push_back(static_cast<char>(cp));
                } else if (cp < 0x800) {
                    out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else {
                    out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    out->push_back(
                        static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                break;
            }
            default: out->push_back(esc); break;
        }
    }
    return false;  // unterminated string
}

// Parses one value token at *pos into `out`. Strings / numbers /
// booleans / null map to the matching OutValue kinds; the function
// marker {"$handler": <id>} becomes a Handler reference into the
// engine's handle table. On malformed input *pos is left somewhere
// inside the token — the caller resyncs on the next '"' / ',' / '}'.
bool ParseValue(const std::string& text, size_t* pos, OutValue* out) {
    SkipWs(text, pos);
    if (*pos >= text.size()) {
        return false;
    }
    if (text[*pos] == '"') {
        std::string v;
        if (!ParseString(text, pos, &v)) {
            return false;
        }
        *out = StrVal(v);
        return true;
    }
    if (text[*pos] == '{') {
        ++*pos;
        SkipWs(text, pos);
        std::string marker;
        if (!ParseString(text, pos, &marker)) {
            return false;
        }
        SkipWs(text, pos);
        if (*pos >= text.size() || text[*pos] != ':') {
            return false;
        }
        ++*pos;
        SkipWs(text, pos);
        const char* begin = text.c_str() + *pos;
        char* end = nullptr;
        const long id = std::strtol(begin, &end, 10);
        if (end == begin) {
            return false;
        }
        *pos += static_cast<size_t>(end - begin);
        SkipWs(text, pos);
        if (*pos >= text.size() || text[*pos] != '}') {
            return false;
        }
        ++*pos;
        if (marker != "$handler") {
            return false;  // unknown marker object — skip the entry
        }
        out->kind = OutValue::Kind::Handler;
        out->handler_id = static_cast<int>(id);
        return true;
    }
    if (text.compare(*pos, 4, "true") == 0) {
        *pos += 4;
        *out = BoolVal(true);
        return true;
    }
    if (text.compare(*pos, 5, "false") == 0) {
        *pos += 5;
        *out = BoolVal(false);
        return true;
    }
    if (text.compare(*pos, 4, "null") == 0) {
        *pos += 4;
        *out = UndefVal();
        return true;
    }
    const char* begin = text.c_str() + *pos;
    char* end = nullptr;
    const double num = std::strtod(begin, &end);
    if (end == begin) {
        return false;
    }
    *pos += static_cast<size_t>(end - begin);
    *out = NumVal(num);
    return true;
}

// Walks the flat {"key": value, ...} object marshalled by the engine
// and stores each entry via SetOutData. Entries whose key or value
// fails to parse are skipped — the scan resynchronises on the next
// quote / comma / brace so one bad pair never fails the whole call.
void StoreOutData(JsEngine& engine, const std::string& json) {
    size_t pos = 0;
    SkipWs(json, &pos);
    if (pos >= json.size() || json[pos] != '{') {
        return;
    }
    ++pos;
    while (pos < json.size()) {
        SkipWs(json, &pos);
        if (pos >= json.size() || json[pos] == '}') {
            return;
        }
        if (json[pos] == ',') {
            ++pos;
            continue;
        }
        std::string key;
        if (!ParseString(json, &pos, &key)) {
            ++pos;  // resync: skip a char and scan for the next pair
            continue;
        }
        SkipWs(json, &pos);
        if (pos >= json.size() || json[pos] != ':') {
            continue;
        }
        ++pos;
        OutValue value;
        if (ParseValue(json, &pos, &value)) {
            engine.SetOutData(key, value);
        }
    }
}

// Wraps an engine object handle so it can be passed as a property value
// — the engine's handle table is shared between objects and handlers,
// so a handle id travels in the handler_id slot.
OutValue ObjectRef(int object_id) {
    OutValue v;
    v.kind = OutValue::Kind::Handler;
    v.handler_id = object_id;
    return v;
}

OutValue SetOutAppData(JsEngine& engine,
                       const std::vector<OutValue>& args) {
    if (args.empty() || args[0].kind == OutValue::Kind::Undefined ||
        args[0].str.empty()) {
        return UndefVal();
    }
    StoreOutData(engine, args[0].str);
    return UndefVal();
}

}  // namespace

void RegisterExportAppData(JsEngine& engine) {
    engine.RegisterModule("app_super_exportAppData", [](JsEngine& e) {
        const int module_obj = e.NewObject();
        const int enum_obj = e.NewObject();
        const int fn_obj = e.NewObject();

        e.SetObjectProperty(fn_obj, "HANDLER_EVENT_LOAD_PLUGIN",
                            StrVal(kHandlerPluginLoad));
        e.SetObjectProperty(enum_obj, "CONFIG_WINDOW",
                            StrVal(kConfigWindow));
        e.SetObjectProperty(enum_obj, "CONFIG_ICONS",
                            StrVal(kConfigIcons));
        e.SetObjectProperty(enum_obj, "CONFIG_ICONS_DIR",
                            StrVal(kConfigIconsDir));
        e.SetObjectProperty(enum_obj, "CONFIG_EDITOR",
                            StrVal(kConfigEditor));
        e.SetObjectProperty(enum_obj, "CONFIG_SHORTCUTS",
                            StrVal(kConfigShortcuts));
        e.SetObjectProperty(enum_obj, "CONFIG_TOOLBAR",
                            StrVal(kConfigToolbar));
        e.SetObjectProperty(enum_obj, "CONFIG_STRINGS",
                            StrVal(kConfigStrings));
        e.SetObjectProperty(enum_obj, "CONFIG_THEME",
                            StrVal(kConfigTheme));
        e.SetObjectProperty(enum_obj, "CONFIG_LAYOUT",
                            StrVal(kConfigLayout));
        e.SetObjectProperty(enum_obj, "CONFIG_PANELS",
                            StrVal(kConfigPanels));
        e.SetObjectProperty(enum_obj, "CONFIG_WORKSPACES",
                            StrVal(kConfigWorkspaces));
        e.SetObjectProperty(enum_obj, "CONFIG_COLOR",
                            StrVal(kConfigColor));
        e.SetObjectProperty(enum_obj, "FUNCTION", ObjectRef(fn_obj));
        e.SetObjectProperty(module_obj, "ENUM", ObjectRef(enum_obj));

        e.DefineFunction(module_obj, "setOutAppData",
                         [&e](const std::vector<OutValue>& args) {
                             return SetOutAppData(e, args);
                         });
    });
}

}  // namespace XER::VideoEditor::AppRuntime
