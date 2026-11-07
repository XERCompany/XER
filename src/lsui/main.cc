// lsui.exe — Lua-scripted UI host.
// Usage: lsui.exe script.lua [script args...]
// The script creates windows via ui.window{...} and draws inside
// win.on_draw using the immediate-mode ui.* helpers. watch() and widget
// sugar are preloaded; require() resolves .lua modules relative to the
// script directory first.

#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "lsui.h"

#include <windows.h>

namespace {

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    const void* addr = ep->ExceptionRecord->ExceptionAddress;
    const DWORD_PTR base = reinterpret_cast<DWORD_PTR>(GetModuleHandleW(nullptr));
    std::fprintf(stderr, "[lsui] CRASH code=0x%08lX addr=%p base=%p rva=0x%llX\n",
                 code, addr, reinterpret_cast<void*>(base),
                 static_cast<unsigned long long>(
                     reinterpret_cast<DWORD_PTR>(addr) - base));
    return EXCEPTION_EXECUTE_HANDLER;
}

std::string ScriptDir(const char* path) {
    std::string p = path;
    const auto pos = p.find_last_of("/\\");
    return pos == std::string::npos ? "." : p.substr(0, pos);
}

void SetArgTable(lua_State* L, int argc, char** argv, const char* script) {
    lua_newtable(L);
    for (int i = 0; i < argc; ++i) {
        lua_pushinteger(L, i);
        lua_pushstring(L, argv[i]);
        lua_rawset(L, -3);
    }
    lua_pushinteger(L, -1);
    lua_pushstring(L, script);
    lua_rawset(L, -3);
    lua_setglobal(L, "arg");
}

} // namespace

int main(int argc, char** argv) {
    SetUnhandledExceptionFilter(CrashFilter);

    // Enable ANSI escape processing so term.* colors/styles render on
    // conhost/Windows Terminal (no-op when stdout is not a console).
    for (const DWORD id : {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
        HANDLE h = GetStdHandle(id);
        DWORD mode = 0;
        if (h != nullptr && h != INVALID_HANDLE_VALUE &&
            GetConsoleMode(h, &mode)) {
            SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
    if (argc < 2 || std::strcmp(argv[1], "--help") == 0 ||
        std::strcmp(argv[1], "-h") == 0) {
        std::fprintf(stderr,
                     "lsui — Lua UI host\n"
                     "usage: lsui.exe script.lua [args...]\n"
                     "script API: ui.window{...}, ui.rect/text/line/...\n"
                     "             win.on_draw/on_input/on_tick/on_close\n"
                     "             watch(key, getfn, on_change[, ms])\n"
                     "             require() for .lua modules\n");
        return argc < 2 ? 1 : 0;
    }

    const char* script = argv[1];
    lua_State* L = luaL_newstate();
    if (L == nullptr) {
        std::fprintf(stderr, "lsui: cannot create Lua state\n");
        return 1;
    }
    luaL_openlibs(L);

    // require() resolves script-local modules first, then stock paths.
    const std::string dir = ScriptDir(script);
    std::string pkg = "package.path";
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "path");
    const char* oldc = lua_tostring(L, -1);
    std::string old = oldc ? oldc : "";
    lua_pop(L, 1);                 // [package]
    const std::string np =
        dir + "/?.lua;" + dir + "/?/init.lua;" + old;
    lua_pushstring(L, np.c_str()); // [package, newpath]
    lua_setfield(L, -2, "path");   // [package]
    lua_pop(L, 1);                 // []

    XER::Lsui::RegisterApi(L);
    SetArgTable(L, argc - 1, argv + 1, script);

    const int rc = XER::Lsui::RunScript(L, script);
    lua_close(L);
    return rc;
}
