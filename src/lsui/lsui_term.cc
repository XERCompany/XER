// LSUI `term` module — console output backed by the flux terminal
// framework (src/flux/terminal): buffered ANSI-aware writes, snapshots,
// window-fit helpers and style sequences.
//
// Lua API:
//   term.write(s) / term.write_err(s)
//   term.print(...) / term.println(...)   — args joined, trailing newline
//   term.clear()                          — reset the flux snapshot buffer
//   term.snapshot() / term.text()         — ANSI / plain-text snapshot
//   term.size()                           — {columns, rows, interactive}
//   term.fit(s) / term.center(s[,fill]) / term.rule([fill])
//   term.fg(color) / term.bg(color)       — ANSI color sequences
//   term.style{ text=, fg=, bg=, bold=, dim=, italic=, underline=, inverse= }
//   term.reset()
//
// Colors accept a name ("red", "bright_blue", ...) or an index 0-15.

#include <cstdio>
#include <string>
#include <string_view>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include "flux/terminal/terminal_colors.h"
#include "flux/terminal/terminal_output_renderer.h"
#include "flux/terminal/terminal_size_detector.h"
#include "flux/terminal/terminal_styles.h"
#include "flux/terminal/terminal_window.h"

namespace XER::Lsui {
namespace {

using XER::flux::terminal::OutputStream;
using XER::flux::terminal::TerminalColor;
using XER::flux::terminal::TerminalStyle;

std::string_view ArgStr(lua_State* L, int i) {
    size_t n = 0;
    const char* s = luaL_checklstring(L, i, &n);
    return {s, n};
}

int TermWrite(lua_State* L) {
    XER::flux::terminal::Write(OutputStream::kStdout, ArgStr(L, 1));
    return 0;
}

int TermWriteErr(lua_State* L) {
    XER::flux::terminal::Write(OutputStream::kStderr, ArgStr(L, 1));
    return 0;
}

// Joins all arguments with spaces (tostring semantics) + newline.
int TermPrint(lua_State* L) {
    const int n = lua_gettop(L);
    std::string out;
    for (int i = 1; i <= n; ++i) {
        if (i > 1) out += ' ';
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        out.append(s, len);
        lua_pop(L, 1);
    }
    out += '\n';
    XER::flux::terminal::WriteLine(OutputStream::kStdout, out);
    return 0;
}

int TermClear(lua_State*) {
    XER::flux::terminal::ClearSnapshot();
    return 0;
}

int TermSnapshot(lua_State* L) {
    const std::string s = XER::flux::terminal::Snapshot(OutputStream::kStdout);
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

int TermText(lua_State* L) {
    const std::string s =
        XER::flux::terminal::SnapshotPlainText(OutputStream::kStdout);
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

int TermSize(lua_State* L) {
    const auto sz = XER::flux::terminal::DetectTerminalSize();
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, static_cast<lua_Integer>(sz.columns));
    lua_setfield(L, -2, "columns");
    lua_pushinteger(L, static_cast<lua_Integer>(sz.rows));
    lua_setfield(L, -2, "rows");
    lua_pushboolean(L, sz.interactive ? 1 : 0);
    lua_setfield(L, -2, "interactive");
    return 1;
}

XER::flux::terminal::TerminalWindow& TermWindow() {
    static XER::flux::terminal::TerminalWindow w(XER::flux::terminal::DetectTerminalSize());
    return w;
}

// Fill character: string (first byte) or integer codepoint.
char FillChar(lua_State* L, int idx, char dflt) {
    if (lua_isnoneornil(L, idx)) return dflt;
    if (lua_isstring(L, idx)) {
        size_t n = 0;
        const char* str = lua_tolstring(L, idx, &n);
        return n > 0 ? str[0] : dflt;
    }
    return static_cast<char>(luaL_checkinteger(L, idx));
}

int TermFit(lua_State* L) {
    const std::string out = TermWindow().FitToWidth(ArgStr(L, 1));
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

int TermCenter(lua_State* L) {
    const char fill = FillChar(L, 2, ' ');
    const std::string out = TermWindow().Centered(ArgStr(L, 1), fill);
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

int TermRule(lua_State* L) {
    const char fill = FillChar(L, 1, '-');
    const std::string out = TermWindow().MakeRule(fill);
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

// Parses a color argument: integer 0-15 or a name like "red",
// "bright_blue", "bright blue".
bool ParseColor(lua_State* L, int idx, TerminalColor& out) {
    if (lua_isnumber(L, idx)) {
        const int v = static_cast<int>(lua_tointeger(L, idx));
        if (v < -1 || v > 15) return false;
        out = static_cast<TerminalColor>(v);
        return true;
    }
    std::string name(ArgStr(L, idx));
    for (char& c : name) {
        if (c == ' ') c = '_';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    static const char* kNames[] = {
        "default",   "black",        "red",          "green",
        "yellow",    "blue",         "magenta",      "cyan",
        "white",     "bright_black", "bright_red",   "bright_green",
        "bright_yellow", "bright_blue", "bright_magenta", "bright_cyan",
        "bright_white",
    };
    for (int i = 0; i < 16; ++i) {
        if (name == kNames[i]) {
            out = static_cast<TerminalColor>(i - 1);
            return true;
        }
    }
    return false;
}

int TermFg(lua_State* L) {
    TerminalColor c;
    if (!ParseColor(L, 1, c)) {
        return luaL_error(L, "term.fg: unknown color");
    }
    const std::string s = XER::flux::terminal::ForegroundSequence(c);
    XER::flux::terminal::Write(OutputStream::kStdout, s);
    return 0;
}

int TermBg(lua_State* L) {
    TerminalColor c;
    if (!ParseColor(L, 1, c)) {
        return luaL_error(L, "term.bg: unknown color");
    }
    const std::string s = XER::flux::terminal::BackgroundSequence(c);
    XER::flux::terminal::Write(OutputStream::kStdout, s);
    return 0;
}

int TermReset(lua_State*) {
    XER::flux::terminal::Write(OutputStream::kStdout,
                         XER::flux::terminal::ResetStyleSequence());
    return 0;
}

int TermStyle(lua_State* L) {
    // term.style{ text=..., fg=..., bg=..., bold=, dim=, italic=,
    //             underline=, inverse= }
    luaL_checktype(L, 1, LUA_TTABLE);
    TerminalStyle st;
    auto get_bool = [&](const char* k) {
        lua_getfield(L, 1, k);
        const bool v = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        return v;
    };
    st.bold = get_bool("bold");
    st.dim = get_bool("dim");
    st.italic = get_bool("italic");
    st.underline = get_bool("underline");
    st.inverse = get_bool("inverse");
    lua_getfield(L, 1, "fg");
    if (!lua_isnil(L, -1)) ParseColor(L, -1, st.foreground);
    lua_pop(L, 1);
    lua_getfield(L, 1, "bg");
    if (!lua_isnil(L, -1)) ParseColor(L, -1, st.background);
    lua_pop(L, 1);
    lua_getfield(L, 1, "text");
    const std::string_view text = lua_isnoneornil(L, -1)
                                      ? std::string_view{}
                                      : ArgStr(L, -1);
    lua_pop(L, 1);
    std::string out = XER::flux::terminal::ApplyStyle(text, st);
    XER::flux::terminal::Write(OutputStream::kStdout, out);
    return 0;
}

} // namespace

void RegisterTermApi(lua_State* L) {
    lua_newtable(L);  // term
    lua_pushcfunction(L, TermWrite);     lua_setfield(L, -2, "write");
    lua_pushcfunction(L, TermWriteErr);  lua_setfield(L, -2, "write_err");
    lua_pushcfunction(L, TermPrint);     lua_setfield(L, -2, "print");
    lua_pushcfunction(L, TermPrint);     lua_setfield(L, -2, "println");
    lua_pushcfunction(L, TermClear);     lua_setfield(L, -2, "clear");
    lua_pushcfunction(L, TermSnapshot);  lua_setfield(L, -2, "snapshot");
    lua_pushcfunction(L, TermText);      lua_setfield(L, -2, "text");
    lua_pushcfunction(L, TermSize);      lua_setfield(L, -2, "size");
    lua_pushcfunction(L, TermFit);       lua_setfield(L, -2, "fit");
    lua_pushcfunction(L, TermCenter);    lua_setfield(L, -2, "center");
    lua_pushcfunction(L, TermRule);      lua_setfield(L, -2, "rule");
    lua_pushcfunction(L, TermFg);        lua_setfield(L, -2, "fg");
    lua_pushcfunction(L, TermBg);        lua_setfield(L, -2, "bg");
    lua_pushcfunction(L, TermReset);     lua_setfield(L, -2, "reset");
    lua_pushcfunction(L, TermStyle);     lua_setfield(L, -2, "style");
    lua_setglobal(L, "term");
}

} // namespace XER::Lsui