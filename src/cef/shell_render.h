// shell_render.h — XERBrowserShell render-process side.
//
// The shell executable (xer_browser_shell.exe) serves two roles:
//   * CEF subprocess for host apps — CreateBrowserShellApp() returns an
//     app whose render-process handler injects the `xer` JS object into
//     every page's V8 context (xer.call/version/log);
//   * standalone browser — xer_browser_shell.exe <url> opens a plain
//     window; subprocesses it spawns inherit the same renderer handler.
#pragma once

#if defined(XER_HAS_CEF)
#include "include/cef_app.h"

namespace XER::Cef {

// The CefApp handed to CefExecuteProcess / CefInitialize by the shell
// executable's entry point. Implements both browser- and render-
// process handlers; CEF queries the right one per process type.
CefRefPtr<CefApp> CreateBrowserShellApp();

}  // namespace XER::Cef
#endif  // XER_HAS_CEF
