# src/cef — CEF-to-HWND browser embedding (optional).
#
# The CEF binary distribution is a manual install (no winget/choco/MSYS2
# package exists): InstallCef.ps1 downloads the Spotify build into
# third_party/cef/. When absent, XER_Cef still compiles — every entry
# point is an inert stub reporting unavailability, so dependents never
# need #if guards of their own beyond feature-gating UI.

find_package(CEF QUIET)

add_library(XER_Cef STATIC
    cef_runtime.cc
    cef_host.cc
    cef_ext_scheme.cc
    shell_commands.cc
    cef_runtime.h
    cef_host.h
    cef_ext_scheme.h
    shell_commands.h
)

target_include_directories(XER_Cef PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(TARGET CEF::libcef)
    target_compile_definitions(XER_Cef PUBLIC XER_HAS_CEF=1)
    target_link_libraries(XER_Cef PUBLIC
        CEF::libcef CEF::libcef_dll_wrapper
        XER_Tz            # xer-ext:// serves members from TZ archives
        XER_NetClient     # shell_commands net.* built-ins
        jsoncpp_lib)      # args/result marshaling
    message(STATUS "XER_Cef: CEF browser embedding ENABLED (${CEF_ROOT})")

    # XERBrowserShell — own CEF subprocess (replaces stock
    # bootstrap.exe): injects the `xer` src/ API object into every
    # page's V8 context; also runs standalone as a minimal browser.
    add_executable(xer_browser_shell WIN32
        xer_browser_shell.cc
        shell_render.cc
        shell_render.h)
    target_link_libraries(xer_browser_shell PRIVATE
        XER_Cef
        XER_Helper)      # String helpers needed by XER_Network
    set_target_properties(xer_browser_shell PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
else()
    target_compile_definitions(XER_Cef PRIVATE XER_HAS_CEF=0)
    message(STATUS "XER_Cef: CEF not found — stub build")
endif()
