// LSUI — Lua-scripted UI host (see lsui.h).
//
// Model: one lua_State, N top-level Win32 windows. Each window owns a
// UI::Canvas child filling the client area. The script draws with the
// immediate-mode ui.* helpers inside win.on_draw and receives input via
// win.on_input plus ui.mouse_*/ui.key_*/ui.in_rect state queries.
// watch() and widget sugar live in the embedded Lua chunk (kLuaHelpers).

#include "lsui.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <windows.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "ui/canvas/canvas.h"

namespace UIC = XER::UI::Canvas;

namespace XER::Lsui {
namespace {

// ── Embedded Lua helpers ───────────────────────────────────────────────────

const char kLuaHelpers[] = R"LUA(
-- LSUI helper layer: watch() + immediate-mode widget sugar.
-- Loaded automatically; scripts may also require("lsui") for the same.

local watchers = {}

-- watch(key, getfn, on_change[, interval_ms])
-- Polls getfn on the UI tick; on_change(new, old) fires whenever the
-- value differs (first observation reports old == nil).
function watch(key, getfn, on_change, interval)
  watchers[#watchers + 1] = {
    key = key, get = getfn, cb = on_change,
    last = nil, fired = false, t = -1e9,
    interval = interval or 0,
  }
end

function _lsui_tick_watchers(now)
  for _, w in ipairs(watchers) do
    if now - w.t >= w.interval then
      w.t = now
      local ok, v = pcall(w.get)
      if ok and (not w.fired or v ~= w.last) then
        local old = w.last
        w.last = v
        w.fired = true
        local cok, err = pcall(w.cb, v, old)
        if not cok then print("[lsui watch] " .. tostring(err)) end
      end
    end
  end
end

-- ── Widget sugar (all take optional leading win arg; default = window
-- currently inside on_draw) ────────────────────────────────────────────

local function W(...) return select('#', ...) > 0 and ... or nil end

function ui.button(x, y, w, h, label)
  local col
  if ui.mouse_down(0) and ui.in_rect(x, y, w, h) then
    col = {0.16, 0.32, 0.72, 1}
  elseif ui.in_rect(x, y, w, h) then
    col = {0.24, 0.26, 0.30, 1}
  else
    col = {0.17, 0.18, 0.21, 1}
  end
  ui.rect(x, y, w, h, col[1], col[2], col[3], col[4], 4)
  if label then ui.text(x + 8, y + h * 0.62, label, 13, 0.92, 0.92, 0.94, 1) end
  return ui.clicked_in_rect(x, y, w, h)
end

function ui.label(x, y, text, size)
  ui.text(x, y, text, size or 13, 0.85, 0.86, 0.88, 1)
end

function ui.panel(x, y, w, h)
  ui.rect(x, y, w, h, 0.13, 0.14, 0.16, 1, 6)
end

-- ── Node.js-style module system ────────────────────────────────────────
-- require() resolves:
--   "./mod", "../mod" → relative to the requiring file's directory
--   "/abs/path", "C:\\path" → absolute
--   "mod"              → node_modules walk-up from the requirer dir,
--                        then LSUI_PATH env roots (';'-separated),
--                        then stock package.path via the original require
-- Candidates per base: <base>.lua, <base>/init.lua, <base> (explicit ext).
-- Modules run once, cached by resolved path. A module may `return t`
-- (Lua style) or assign `exports.*` / `module.exports` (Node style);
-- globals __dirname/__filename/module/exports exist while it loads.

local _orig_require = require
local _mod_cache = {}

local function _dir_of(p)
  p = tostring(p):gsub("\\", "/")
  return (p:match("^(.*)/[^/]*$") or ".")
end

local function _norm(p)
  p = tostring(p):gsub("\\", "/")
  local parts = {}
  for seg in p:gmatch("[^/]+") do
    if seg == ".." then
      if #parts > 0 and parts[#parts] ~= ".." then table.remove(parts) end
    elseif seg ~= "." then
      table.insert(parts, seg)
    end
  end
  local prefix = p:match("^/") and "/" or ""
  return prefix .. table.concat(parts, "/")
end

local function _file_exists(p)
  local f = io.open(p, "rb")
  if f then f:close() return true end
  return false
end

local function _try_file(base)
  if base:sub(-4) == ".lua" then
    if _file_exists(base) then return base end
    return nil
  end
  for _, c in ipairs{base .. ".lua", base .. "/init.lua"} do
    if _file_exists(c) then return c end
  end
  return nil
end

local _main_dir = "."
if arg and arg[0] then _main_dir = _dir_of(arg[0]) end

local function _lsui_path_dirs()
  local dirs = {}
  local env = os.getenv("LSUI_PATH")
  if env then
    for d in env:gmatch("[^;]+") do dirs[#dirs + 1] = d end
  end
  return dirs
end

local function _requirer_dir(src)
  if type(src) ~= "string" or src == "" then return _main_dir end
  local p = src:sub(1, 1) == "@" and src:sub(2) or src
  if p:sub(1, 1) == "=" then return _main_dir end  -- e.g. "=lsui_helpers"
  return _dir_of(p)
end

local function _resolve(name, from_src)
  local dir = _requirer_dir(from_src)
  local bases = {}
  if name:sub(1, 2) == "./" or name:sub(1, 3) == "../" then
    table.insert(bases, _norm(dir .. "/" .. name))
  elseif name:sub(1, 1) == "/" or name:match("^%a:[/\\]") then
    table.insert(bases, _norm(name))
  else
    -- node_modules walk-up (Node semantics), then LSUI_PATH roots
    local d = dir
    while true do
      table.insert(bases, _norm(d .. "/node_modules/" .. name))
      local parent = _dir_of(d)
      if parent == d then break end
      d = parent
    end
    for _, rd in ipairs(_lsui_path_dirs()) do
      table.insert(bases, _norm(rd .. "/" .. name))
    end
  end
  for _, b in ipairs(bases) do
    local hit = _try_file(b)
    if hit then return hit end
  end
  return nil
end

local function _lsui_require(name)
  if type(name) ~= "string" then
    error("require: module name must be a string", 2)
  end
  local info = debug.getinfo(2, "S")
  local src = info and info.source or ""
  local path = _resolve(name, src)
  if not path then
    return _orig_require(name)  -- stock searchers / C modules
  end
  if _mod_cache[path] ~= nil then return _mod_cache[path] end

  local chunk, cerr = loadfile(path)
  if not chunk then
    error(cerr or ("cannot load module " .. path), 2)
  end

  -- Node-style module environment; exports is cached early so circular
  -- requires observe the partially-filled table (CJS semantics).
  local mod = {
    exports = {}, id = path, filename = path,
    dirname = _dir_of(path), loaded = false,
  }
  _mod_cache[path] = mod.exports
  local s_mod, s_exp = module, exports
  local s_dir, s_file = __dirname, __filename
  module, exports = mod, mod.exports
  __dirname, __filename = mod.dirname, path
  local ok, ret = pcall(chunk)
  module, exports = s_mod, s_exp
  __dirname, __filename = s_dir, s_file
  if not ok then
    _mod_cache[path] = nil
    error(ret, 0)
  end
  mod.loaded = true
  local result = (ret ~= nil) and ret or mod.exports
  _mod_cache[path] = result
  return result
end

-- require is a callable table so require.resolve() can hang off it.
require = setmetatable({
  resolve = function(name)
    local info = debug.getinfo(2, "S")
    local src = info and info.source or ""
    return _resolve(name, src)
  end,
}, {__call = function(_, name) return _lsui_require(name) end})

-- Main script gets Node-style globals too.
if arg and arg[0] then
  __filename = arg[0]
  __dirname = _dir_of(arg[0])
end
)LUA";

// ── Window registry ────────────────────────────────────────────────────────

struct Win {
    HWND frame = nullptr;
    UIC::Canvas canvas;
    int ref = LUA_NOREF;   // registry ref of the Lua window table
    // Input state in canvas (DIP) coordinates.
    double mouse_x = 0, mouse_y = 0;
    bool mouse[3] = {};
    bool clicked[3] = {};  // button-down edges since last draw
    int wheel_delta = 0;
    bool keys[256] = {};
    int last_char = 0;     // WM_CHAR (UTF-16 unit), consumed via ui.input_char
    bool closing = false;
};

lua_State* g_L = nullptr;
std::unordered_map<HWND, std::unique_ptr<Win>> g_wins;
Win* g_current = nullptr;   // window whose on_draw/on_input is running
bool g_quit = false;
const wchar_t* kFrameClass = L"LsuiFrame";
constexpr UINT_PTR kTickTimer = 1;
constexpr UINT kTickMs = 33;

HWND AppHwnd() {
    // Message-only parent is unnecessary — frames are top-level.
    return nullptr;
}

// ── UTF-8 ↔ UTF-16 ─────────────────────────────────────────────────────────

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                      static_cast<int>(s.size()),
                                      nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        w.data(), n);
    return w;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(),
                                      static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                        s.data(), n, nullptr, nullptr);
    return s;
}

// ── Lua plumbing ───────────────────────────────────────────────────────────

void PushWin(lua_State* L, Win* w) {
    if (w == nullptr || w->ref == LUA_NOREF) {
        lua_pushnil(L);
        return;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, w->ref);
}

Win* WinFromTable(lua_State* L, int idx) {
    if (lua_type(L, idx) != LUA_TTABLE) return nullptr;
    lua_getfield(L, idx, "_ptr");
    Win* w = static_cast<Win*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return w;
}

// Resolve the window argument used by ui.* helpers: either an explicit
// leading window table or the window currently inside a callback.
Win* ResolveWin(lua_State* L, int arg) {
    if (lua_type(L, arg) == LUA_TTABLE) return WinFromTable(L, arg);
    return g_current;
}

void PushArg(lua_State* L, const char* s) { lua_pushstring(L, s); }
void PushArg(lua_State* L, const std::string& s) { lua_pushlstring(L, s.data(), s.size()); }
void PushArg(lua_State* L, std::string_view s) { lua_pushlstring(L, s.data(), s.size()); }
void PushArg(lua_State* L, double v) { lua_pushnumber(L, v); }
void PushArg(lua_State* L, int v) { lua_pushinteger(L, v); }
void PushArg(lua_State* L, unsigned v) { lua_pushinteger(L, static_cast<lua_Integer>(v)); }
void PushArg(lua_State* L, bool v) { lua_pushboolean(L, v ? 1 : 0); }

// Calls win.<field>(win, args...). nret Lua results are left on the stack.
template <typename... A>
void CallField(lua_State* L, Win* w, const char* field, int nret, A&&... args) {
    if (w == nullptr || w->ref == LUA_NOREF) return;
    PushWin(L, w);                    // [win]
    lua_getfield(L, -1, field);       // [win, fn]
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2);
        return;
    }
    lua_remove(L, -2);                // [fn]
    PushWin(L, w);                    // [fn, win]
    (PushArg(L, std::forward<A>(args)), ...);
    if (lua_pcall(L, 1 + static_cast<int>(sizeof...(args)), nret, 0) != LUA_OK) {
        std::fprintf(stderr, "[lsui] %s: %s\n", field, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

void CallDraw(Win* w) {
    g_current = w;
    // clicked[] edges are per-frame: clear before the script sees them.
    w->clicked[0] = w->clicked[1] = w->clicked[2] = false;
    w->wheel_delta = 0;
    CallField(g_L, w, "on_draw", 0);
    g_current = nullptr;
}

void CallTick(Win* w, double dt_ms) {
    CallField(g_L, w, "on_tick", 0, dt_ms);
    // Watchers tick once per frame callback regardless of window count —
    // the interval guard inside _lsui_tick_watchers dedupes repeats.
    lua_getglobal(g_L, "_lsui_tick_watchers");
    if (lua_isfunction(g_L, -1)) {
        lua_pushnumber(g_L, static_cast<double>(GetTickCount()));
        if (lua_pcall(g_L, 1, 0, 0) != LUA_OK) {
            std::fprintf(stderr, "[lsui] tick: %s\n", lua_tostring(g_L, -1));
            lua_pop(g_L, 1);
        }
    } else {
        lua_pop(g_L, 1);
    }
}

// ── Frame window proc ──────────────────────────────────────────────────────

LRESULT CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto it = g_wins.find(hwnd);
    Win* w = (it != g_wins.end()) ? it->second.get() : nullptr;
    switch (msg) {
    case WM_SIZE: {
        if (w != nullptr) {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            w->canvas.SetBounds(0.0f, 0.0f,
                                static_cast<float>(rc.right),
                                static_cast<float>(rc.bottom));
        }
        return 0;
    }
    case WM_TIMER:
        if (w != nullptr && wp == kTickTimer) {
            CallTick(w, static_cast<double>(kTickMs));
            w->canvas.Invalidate();
        }
        return 0;
    case WM_CLOSE: {
        bool allow = true;
        if (w != nullptr) {
            CallField(g_L, w, "on_close", 1);
            if (lua_gettop(g_L) > 0) {
                allow = lua_toboolean(g_L, -1) != 0;
                lua_pop(g_L, 1);
            }
        }
        if (allow) DestroyWindow(hwnd);
        return 0;
    }
    case WM_DESTROY:
        if (w != nullptr) {
            KillTimer(hwnd, kTickTimer);
            w->canvas.Destroy();
            if (w->ref != LUA_NOREF) {
                luaL_unref(g_L, LUA_REGISTRYINDEX, w->ref);
                w->ref = LUA_NOREF;
            }
        }
        g_wins.erase(hwnd);
        if (g_wins.empty() && !g_quit) PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

void RegisterFrameClass() {
    static bool done = false;
    if (done) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = FrameProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.hbrBackground = CreateSolidBrush(RGB(24, 24, 28));
    wc.lpszClassName = kFrameClass;
    done = RegisterClassExW(&wc) != 0;
}

// ── ui.* C functions ───────────────────────────────────────────────────────

// Each helper resolves its window either from a leading table arg or from
// g_current (set while on_draw/on_input runs). `has_win_arg` reports
// whether arg 1 was consumed as the window.

// The render callback sets g_draw_ctx; helpers use it. (Canvas pointer is
// only valid during the callback.)
struct DrawCtx {
    UIC::Canvas* c = nullptr;
    Win* w = nullptr;
};

DrawCtx g_draw;

void NeedDraw(lua_State* L, int* base) {
    *base = lua_istable(L, 1) ? 2 : 1;
    if (!g_draw.c) luaL_error(L, "ui.* draw calls are only valid inside on_draw");
}

int UiWindow(lua_State* L) {
    // ui.window{title=, width=, height=, x=, y=, resizable=}
    luaL_checktype(L, 1, LUA_TTABLE);
    auto field_str = [&](const char* k, const char* dflt) {
        lua_getfield(L, 1, k);
        const char* s = lua_tostring(L, -1);
        std::string r = s ? s : dflt;
        lua_pop(L, 1);
        return r;
    };
    auto field_num = [&](const char* k, double dflt) {
        lua_getfield(L, 1, k);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            return dflt;
        }
        const double v = lua_tonumber(L, -1);
        lua_pop(L, 1);
        return v;
    };
    const std::string title = field_str("title", "LSUI");
    const int w = static_cast<int>(field_num("width", 800));
    const int h = static_cast<int>(field_num("height", 600));
    const int x = static_cast<int>(field_num("x", CW_USEDEFAULT));
    const int y = static_cast<int>(field_num("y", CW_USEDEFAULT));

    RegisterFrameClass();
    auto win = std::make_unique<Win>();
    const std::wstring wt = Utf8ToWide(title);
    HWND frame = CreateWindowExW(
        0, kFrameClass, wt.c_str(),
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        x, y, w, h, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (frame == nullptr) {
        lua_pushnil(L);
        lua_pushstring(L, "CreateWindowEx failed");
        return 2;
    }
    win->frame = frame;
    SetWindowLongPtrW(frame, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(win.get()));

    if (!win->canvas.Create(frame)) {
        DestroyWindow(frame);
        lua_pushnil(L);
        lua_pushstring(L, "canvas create failed");
        return 2;
    }
    RECT rc{};
    GetClientRect(frame, &rc);
    win->canvas.SetBounds(0.0f, 0.0f, static_cast<float>(rc.right),
                          static_cast<float>(rc.bottom));
    win->canvas.SetRenderCallback([win_ptr = win.get()](UIC::Canvas& c) {
        g_draw.c = &c;
        g_draw.w = win_ptr;
        CallDraw(win_ptr);
        g_draw.c = nullptr;
        g_draw.w = nullptr;
    });
    win->canvas.SetInputCallback([win_ptr = win.get()](
                                      unsigned msg, int x, int y, int button,
                                      int wheel, int key, int mods) {
        Win* w = win_ptr;
        g_current = w;
        w->mouse_x = x;
        w->mouse_y = y;
        const char* name = "unknown";
        switch (msg) {
        case WM_MOUSEMOVE: name = "mousemove"; break;
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
            name = "mousedown"; w->mouse[0] = true; w->clicked[0] = true; break;
        case WM_LBUTTONUP:
            name = "mouseup"; w->mouse[0] = false; break;
        case WM_RBUTTONDOWN:
            name = "mousedown"; w->mouse[1] = true; w->clicked[1] = true; break;
        case WM_RBUTTONUP:
            name = "mouseup"; w->mouse[1] = false; break;
        case WM_MBUTTONDOWN:
            name = "mousedown"; w->mouse[2] = true; w->clicked[2] = true; break;
        case WM_MBUTTONUP:
            name = "mouseup"; w->mouse[2] = false; break;
        case WM_MOUSEWHEEL:
            name = "wheel"; w->wheel_delta += wheel; break;
        case WM_KEYDOWN:
            name = "keydown"; if (key >= 0 && key < 256) w->keys[key] = true; break;
        case WM_KEYUP:
            name = "keyup"; if (key >= 0 && key < 256) w->keys[key] = false; break;
        case WM_CHAR:
            name = "char"; w->last_char = key; break;
        default: break;
        }
        // mods bitmask matches NodeGraphEditorWidget: 1=shift 2=ctrl 4=alt
        CallField(g_L, w, "on_input", 0, name, x, y, button, wheel, key, mods);
        g_current = nullptr;
        w->canvas.Invalidate();
        return true;
    });

    // Lua window table: _ptr + methods via metatable __index.
    lua_createtable(L, 0, 4);
    lua_pushlightuserdata(L, win.get());
    lua_setfield(L, -2, "_ptr");
    lua_newtable(L);  // methods
    lua_pushcfunction(L, [](lua_State* L2) -> int {
        Win* w = WinFromTable(L2, 1);
        if (w) DestroyWindow(w->frame);
        return 0;
    });
    lua_setfield(L, -2, "close");
    lua_pushcfunction(L, [](lua_State* L2) -> int {
        Win* w = WinFromTable(L2, 1);
        if (w && lua_isstring(L2, 2)) {
            SetWindowTextW(w->frame, Utf8ToWide(lua_tostring(L2, 2)).c_str());
        }
        return 0;
    });
    lua_setfield(L, -2, "set_title");
    lua_pushcfunction(L, [](lua_State* L2) -> int {
        Win* w = WinFromTable(L2, 1);
        if (w) {
            int x = static_cast<int>(luaL_optnumber(L2, 2, 0));
            int y = static_cast<int>(luaL_optnumber(L2, 3, 0));
            SetWindowPos(w->frame, nullptr, x, y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER);
        }
        return 0;
    });
    lua_setfield(L, -2, "set_pos");
    lua_pushcfunction(L, [](lua_State* L2) -> int {
        Win* w = WinFromTable(L2, 1);
        if (w) {
            int ww = static_cast<int>(luaL_optnumber(L2, 2, 0));
            int hh = static_cast<int>(luaL_optnumber(L2, 3, 0));
            SetWindowPos(w->frame, nullptr, 0, 0, ww, hh,
                         SWP_NOMOVE | SWP_NOZORDER);
        }
        return 0;
    });
    lua_setfield(L, -2, "set_size");
    lua_pushcfunction(L, [](lua_State* L2) -> int {
        Win* w = WinFromTable(L2, 1);
        if (w) {
            RECT rc{}; GetWindowRect(w->frame, &rc);
            int sw = GetSystemMetrics(SM_CXSCREEN);
            int sh = GetSystemMetrics(SM_CYSCREEN);
            SetWindowPos(w->frame, nullptr,
                         (sw - (rc.right - rc.left)) / 2,
                         (sh - (rc.bottom - rc.top)) / 2, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER);
        }
        return 0;
    });
    lua_setfield(L, -2, "center");
    lua_pushcfunction(L, [](lua_State* L2) -> int {
        Win* w = WinFromTable(L2, 1);
        lua_pushnumber(L2, w ? w->canvas.GetWidth() : 0);
        lua_pushnumber(L2, w ? w->canvas.GetHeight() : 0);
        return 2;
    });
    lua_setfield(L, -2, "size");
    lua_pushcfunction(L, [](lua_State* L2) -> int {
        Win* w = WinFromTable(L2, 1);
        if (w) ShowWindow(w->frame, SW_SHOW);
        return 0;
    });
    lua_setfield(L, -2, "show");

    // Stack: [win, methods]. Metatable __index → methods; the ref must
    // point at the window table (which carries _ptr and script fields).
    lua_newtable(L);               // [win, methods, mt]
    lua_pushvalue(L, -2);          // [win, methods, mt, methods]
    lua_setfield(L, -2, "__index");// mt.__index = methods -> [win, methods, mt]
    lua_remove(L, -2);             // [win, mt]  (methods kept via mt)
    lua_setmetatable(L, -2);       // [win]

    win->ref = luaL_ref(L, LUA_REGISTRYINDEX);
    win->canvas.Invalidate();
    g_wins[frame] = std::move(win);
    ShowWindow(frame, SW_SHOW);
    SetTimer(frame, kTickTimer, kTickMs, nullptr);
    PushWin(L, g_wins[frame].get());
    return 1;
}

int UiQuit(lua_State*) { g_quit = true; PostQuitMessage(0); return 0; }

float F(lua_State* L, int i) {
    return static_cast<float>(luaL_checknumber(L, i));
}
float FO(lua_State* L, int i, double dflt) {
    return static_cast<float>(luaL_optnumber(L, i, dflt));
}

// Drawing helpers — all coordinates in canvas DIPs.

int UiClear(lua_State* L) {
    int b; NeedDraw(L, &b);
    float r = F(L, b), g = F(L, b + 1);
    float bl = F(L, b + 2), a = F(L, b + 3);
    g_draw.c->Clear(UIC::Canvas::Color{r, g, bl, a});
    return 0;
}

// Drawing must run while the canvas render callback is active — the
// helpers therefore operate on g_current only and are no-ops otherwise.
// Each takes (win?, args...) where win is the optional leading table.

int UiFillRect(lua_State* L) {
    int b; NeedDraw(L, &b);
    float x = F(L, b), y = F(L, b + 1);
    float ww = F(L, b + 2), hh = F(L, b + 3);
    float r = F(L, b + 4), g = F(L, b + 5);
    float bl = F(L, b + 6), a = F(L, b + 7);
    float rad = FO(L, b + 8, 0.0);
    g_draw.c->FillRect(UIC::Canvas::Rect{x, y, ww, hh},
                       UIC::Canvas::Color{r, g, bl, a}, rad);
    return 0;
}

int UiStrokeRect(lua_State* L) {
    int b; NeedDraw(L, &b);
    float x = F(L, b), y = F(L, b + 1);
    float ww = F(L, b + 2), hh = F(L, b + 3);
    float r = F(L, b + 4), g = F(L, b + 5);
    float bl = F(L, b + 6), a = F(L, b + 7);
    float width = FO(L, b + 8, 1.0);
    float rad = FO(L, b + 9, 0.0);
    g_draw.c->StrokeRect(UIC::Canvas::Rect{x, y, ww, hh},
                         UIC::Canvas::Color{r, g, bl, a}, width, rad);
    return 0;
}

int UiLine(lua_State* L) {
    int b; NeedDraw(L, &b);
    float x1 = F(L, b), y1 = F(L, b + 1);
    float x2 = F(L, b + 2), y2 = F(L, b + 3);
    float r = F(L, b + 4), g = F(L, b + 5);
    float bl = F(L, b + 6), a = F(L, b + 7);
    float w = FO(L, b + 8, 1.0);
    g_draw.c->DrawLine(x1, y1, x2, y2, UIC::Canvas::Color{r, g, bl, a}, w);
    return 0;
}

int UiCircle(lua_State* L) {
    int b; NeedDraw(L, &b);
    float cx = F(L, b), cy = F(L, b + 1);
    float rr = F(L, b + 2);
    float r = F(L, b + 3), g = F(L, b + 4);
    float bl = F(L, b + 5), a = F(L, b + 6);
    g_draw.c->FillCircle(cx, cy, rr, UIC::Canvas::Color{r, g, bl, a});
    return 0;
}

int UiText(lua_State* L) {
    int b; NeedDraw(L, &b);
    float x = F(L, b), y = F(L, b + 1);
    const char* s = luaL_checkstring(L, b + 2);
    float size = F(L, b + 3);
    float r = F(L, b + 4), g = F(L, b + 5);
    float bl = F(L, b + 6), a = F(L, b + 7);
    g_draw.c->DrawText(s, x, y, size, UIC::Canvas::Color{r, g, bl, a});
    return 0;
}

int UiBezier(lua_State* L) {
    int b; NeedDraw(L, &b);
    float x1 = F(L, b), y1 = F(L, b + 1);
    float cx1 = F(L, b + 2), cy1 = F(L, b + 3);
    float cx2 = F(L, b + 4), cy2 = F(L, b + 5);
    float x2 = F(L, b + 6), y2 = F(L, b + 7);
    float r = F(L, b + 8), g = F(L, b + 9);
    float bl = F(L, b + 10), a = F(L, b + 11);
    float w = FO(L, b + 12, 1.0);
    g_draw.c->DrawCubicBezier(x1, y1, cx1, cy1, cx2, cy2, x2, y2,
                              UIC::Canvas::Color{r, g, bl, a}, w);
    return 0;
}

int UiClipPush(lua_State* L) {
    int b; NeedDraw(L, &b);
    float x = F(L, b), y = F(L, b + 1);
    float ww = F(L, b + 2), hh = F(L, b + 3);
    g_draw.c->PushClip(UIC::Canvas::Rect{x, y, ww, hh});
    return 0;
}

int UiClipPop(lua_State* L) {
    int b; NeedDraw(L, &b);
    (void)b;
    g_draw.c->PopClip();
    return 0;
}

// ── Input-state helpers ────────────────────────────────────────────────────

int UiMouseX(lua_State* L) {
    Win* w = ResolveWin(L, 1);
    lua_pushnumber(L, w ? w->mouse_x : 0);
    return 1;
}
int UiMouseY(lua_State* L) {
    Win* w = ResolveWin(L, 1);
    lua_pushnumber(L, w ? w->mouse_y : 0);
    return 1;
}
int UiMouseDown(lua_State* L) {
    Win* w = ResolveWin(L, 1);
    int btn = static_cast<int>(luaL_checkinteger(L, lua_istable(L, 1) ? 2 : 1));
    lua_pushboolean(L, (w && btn >= 0 && btn < 3 && w->mouse[btn]) ? 1 : 0);
    return 1;
}
int UiMouseClicked(lua_State* L) {
    Win* w = ResolveWin(L, 1);
    int btn = static_cast<int>(luaL_checkinteger(L, lua_istable(L, 1) ? 2 : 1));
    lua_pushboolean(L, (w && btn >= 0 && btn < 3 && w->clicked[btn]) ? 1 : 0);
    return 1;
}
int UiWheel(lua_State* L) {
    Win* w = ResolveWin(L, 1);
    lua_pushinteger(L, w ? w->wheel_delta : 0);
    return 1;
}
int UiKeyDown(lua_State* L) {
    Win* w = ResolveWin(L, 1);
    int vk = static_cast<int>(luaL_checkinteger(L, lua_istable(L, 1) ? 2 : 1));
    lua_pushboolean(L, (w && vk >= 0 && vk < 256 && w->keys[vk]) ? 1 : 0);
    return 1;
}
int UiInputChar(lua_State* L) {
    Win* w = ResolveWin(L, 1);
    if (!w || w->last_char == 0) { lua_pushnil(L); return 1; }
    wchar_t wc = static_cast<wchar_t>(w->last_char);
    char buf[8];
    int n = WideCharToMultiByte(CP_UTF8, 0, &wc, 1, buf, sizeof(buf),
                                nullptr, nullptr);
    w->last_char = 0;
    if (n <= 0) { lua_pushnil(L); return 1; }
    lua_pushlstring(L, buf, n);
    return 1;
}
int UiInRect(lua_State* L) {
    // ui.in_rect(x,y,w,h) — window arg optional
    int b = lua_istable(L, 1) ? 2 : 1;
    double x = F(L, b), y = F(L, b + 1);
    double ww = F(L, b + 2), hh = F(L, b + 3);
    Win* w = ResolveWin(L, 1);
    bool inside = w && w->mouse_x >= x && w->mouse_x <= x + ww &&
                  w->mouse_y >= y && w->mouse_y <= y + hh;
    lua_pushboolean(L, inside ? 1 : 0);
    return 1;
}
int UiClickedInRect(lua_State* L) {
    int b = lua_istable(L, 1) ? 2 : 1;
    double x = F(L, b), y = F(L, b + 1);
    double ww = F(L, b + 2), hh = F(L, b + 3);
    Win* w = ResolveWin(L, 1);
    bool hit = false;
    if (w) {
        for (int btn = 0; btn < 3 && !hit; ++btn) {
            if (w->clicked[btn] && w->mouse_x >= x && w->mouse_x <= x + ww &&
                w->mouse_y >= y && w->mouse_y <= y + hh) {
                hit = true;
            }
        }
    }
    lua_pushboolean(L, hit ? 1 : 0);
    return 1;
}

// ── API registration ───────────────────────────────────────────────────────

void RegisterApiImpl(lua_State* L) {
    lua_newtable(L);  // ui
    lua_pushcfunction(L, UiWindow);       lua_setfield(L, -2, "window");
    lua_pushcfunction(L, UiQuit);         lua_setfield(L, -2, "quit");
    lua_pushcfunction(L, UiClear);        lua_setfield(L, -2, "clear");
    lua_pushcfunction(L, UiFillRect);     lua_setfield(L, -2, "rect");
    lua_pushcfunction(L, UiStrokeRect);   lua_setfield(L, -2, "stroke_rect");
    lua_pushcfunction(L, UiLine);         lua_setfield(L, -2, "line");
    lua_pushcfunction(L, UiCircle);       lua_setfield(L, -2, "circle");
    lua_pushcfunction(L, UiText);         lua_setfield(L, -2, "text");
    lua_pushcfunction(L, UiBezier);       lua_setfield(L, -2, "bezier");
    lua_pushcfunction(L, UiClipPush);     lua_setfield(L, -2, "clip_push");
    lua_pushcfunction(L, UiClipPop);      lua_setfield(L, -2, "clip_pop");
    lua_pushcfunction(L, UiMouseX);       lua_setfield(L, -2, "mouse_x");
    lua_pushcfunction(L, UiMouseY);       lua_setfield(L, -2, "mouse_y");
    lua_pushcfunction(L, UiMouseDown);    lua_setfield(L, -2, "mouse_down");
    lua_pushcfunction(L, UiMouseClicked); lua_setfield(L, -2, "mouse_clicked");
    lua_pushcfunction(L, UiWheel);        lua_setfield(L, -2, "wheel");
    lua_pushcfunction(L, UiKeyDown);      lua_setfield(L, -2, "key_down");
    lua_pushcfunction(L, UiInputChar);    lua_setfield(L, -2, "input_char");
    lua_pushcfunction(L, UiInRect);       lua_setfield(L, -2, "in_rect");
    lua_pushcfunction(L, UiClickedInRect);lua_setfield(L, -2, "clicked_in_rect");
    lua_setglobal(L, "ui");

    RegisterTermApi(L);
}

bool LoadHelpersImpl(lua_State* L) {
    if (luaL_loadbuffer(L, kLuaHelpers, sizeof(kLuaHelpers) - 1,
                        "=lsui_helpers") != LUA_OK ||
        lua_pcall(L, 0, 0, 0) != LUA_OK) {
        return false;
    }
    return true;
}

// ── Script entry + message loop ────────────────────────────────────────────

int RunLoopImpl();

int RunScriptImpl(lua_State* L, const char* path) {
    g_L = L;
    if (!LoadHelpersImpl(L)) {
        std::fprintf(stderr, "[lsui] helpers: %s\n", lua_tostring(L, -1));
        return 1;
    }
    if (luaL_dofile(L, path) != LUA_OK) {
        std::fprintf(stderr, "[lsui] %s: %s\n", path, lua_tostring(L, -1));
        return 1;
    }
    return RunLoopImpl();
}

void ShutdownAllImpl() {
    for (auto& [hwnd, w] : g_wins) {
        KillTimer(hwnd, kTickTimer);
        w->canvas.Destroy();
        if (w->ref != LUA_NOREF && g_L) {
            luaL_unref(g_L, LUA_REGISTRYINDEX, w->ref);
        }
        DestroyWindow(hwnd);
    }
    g_wins.clear();
}

int RunLoopImpl() {
    MSG msg{};
    while (!g_quit && !g_wins.empty()) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { g_quit = true; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (g_quit || g_wins.empty()) break;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
    }
    return g_quit ? 0 : 0;
}

} // namespace

void RegisterApi(lua_State* L) { RegisterApiImpl(L); }
bool LoadHelpers(lua_State* L) { return LoadHelpersImpl(L); }
int RunLoop() { return RunLoopImpl(); }
void ShutdownAll() { ShutdownAllImpl(); }
int RunScript(lua_State* L, const char* path) { return RunScriptImpl(L, path); }

} // namespace XER::Lsui