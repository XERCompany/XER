#pragma once

// LSUI — Lua-scripted immediate-mode UI host.
// Each Lua window is a plain Win32 frame HWND with a UI::Canvas child;
// the script drives drawing through the `ui.*` helpers and receives
// input through the win.on_draw / win.on_input / win.on_tick /
// win.on_close callbacks.

struct lua_State;

namespace XER::Lsui {

// Registers the `ui` global table and helpers on the state.
void RegisterApi(lua_State* L);

// Registers the `term` global table (flux terminal output) on the state.
void RegisterTermApi(lua_State* L);

// Loads the embedded Lua helper layer (watch(), widget sugar).
// Returns false when the chunk fails — error is left on the stack.
bool LoadHelpers(lua_State* L);

// Runs the Win32 message loop until all windows close or ui.quit().
int RunLoop();

// Loads the helper layer, executes the Lua script at `path`, then runs
// the message loop. Returns the process exit code.
int RunScript(lua_State* L, const char* path);

// Tears down every window (used on shutdown / script error).
void ShutdownAll();

} // namespace XER::Lsui