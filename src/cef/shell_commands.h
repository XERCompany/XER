// shell_commands.h — XERBrowserShell browser-side command registry.
//
// Web pages call native functionality through the injected `xer` object
// (see shell_render.cc): `xer.call(command, args)` travels
// renderer -> browser as a CefProcessMessage and lands in
// HandleShellMessage below, which runs the registered handler on a
// worker thread and posts the JSON result back. Every built-in command
// is implemented on top of src/ APIs only (network/net_client,
// std::filesystem, Win32 sysinfo) — no direct curl, no external SDKs.
//
// Command contract: args arrive as a JSON string (the page's value is
// JSON.stringify'ed when it is not already a string). Handlers return a
// JSON object string; the JS promise resolves with that object parsed.
// Convention: include "ok": true/false and "error" on failure.
#pragma once

#include <functional>
#include <string>

#if defined(XER_HAS_CEF)
#include "include/cef_browser.h"
#include "include/cef_frame.h"
#include "include/cef_process_message.h"
#endif

namespace XER::Cef {

// A command handler receives the args JSON and returns the result JSON.
// It runs on a detached worker thread (safe to block — net.fetch does).
using ShellCommandHandler =
    std::function<std::string(const std::string& args_json)>;

// Registers (or replaces) a command. Apps call this at startup to add
// host-specific commands — e.g. the video editor wires "editor.*" here.
void RegisterShellCommand(const std::string& name,
                          ShellCommandHandler handler);
void UnregisterShellCommand(const std::string& name);

// nullptr when the name is unknown. Returned handler is a copy — safe
// to invoke on any thread.
ShellCommandHandler FindShellCommand(const std::string& name);

// Host hook: commands prefixed "editor." are forwarded to this hook
// instead of the registry when set (returns raw result JSON). Lets the
// host app answer without polluting the registry.
void SetShellHostHook(ShellCommandHandler hook);

// Registers the built-in command set (net.*, fs.*, sys.*, xer.*).
// Called once automatically; exposed for tests.
void RegisterBuiltinShellCommands();

#if defined(XER_HAS_CEF)
// CefClient::OnProcessMessageReceived sink — returns true when the
// message was a shell call (name "xer_call"). The result is delivered
// asynchronously via frame->SendProcessMessage("xer_result").
bool HandleShellMessage(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefProcessMessage> message);
#endif

}  // namespace XER::Cef
