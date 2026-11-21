# tools/ide_xejso — IDEXEJSO.exe, the VideoEditor extension IDE.
#
# Standalone GUI for authoring .xexrj extension projects: file tree +
# text editor + build/test strip. Reuses the video_editor app shell
# (XER_UI_Core window/WidgetTree/AppTitleBar) and drives the sibling
# toolchain binaries the same way the editor does:
#   ztzmake.exe                — pack source dir -> .xexrj (CreateProcess)
#   VE.ExtensionsRuntime.exe   — ext_load/ext_call over worker_pipe
#   video_editor.exe           — deploy + launch ("Run in Editor")
#
# ztzmake's pack logic only exists inside ztzmake.cc's main() (the
# xsjo compile step links libclang-cpp + LLVM), so the IDE shells out
# to the sibling exe instead of linking it — the same relationship
# video_editor_assets has with ztzmake at pack time.
#
# The headless half (scaffold/build/test/selftest) stays UI-free in
# ide_project.cc so `--selftest` runs in CI without a window system;
# the GUI TUs are Windows-gated like the video_editor ui/ sources.

add_executable(IDEXEJSO
    main.cc
    ide_project.cc
    # Live-preview files compile on every platform — the Windows half
    # is guarded internally so headless presets link unchanged and
    # IdePreviewSelftest stays callable from RunSelftest.
    ide_preview_widget.cc
    ide_file_watch.cc
    # In-process formatters (clang-format for C++/JSON, builtin for
    # JS) — headless like ide_project.cc so --selftest covers them.
    ide_format.cc
)
target_include_directories(IDEXEJSO PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../video_editor   # worker_pipe.h
    ${CMAKE_SOURCE_DIR}/src                       # tz/tz.h
)
# WorkerPipe + ExtRuntimePipe + the JSON-lines ext_* wire live in the
# VE core (the same link VE.ExtensionsRuntime makes); XER_Tz for the
# archive verify/member-list the selftest does in-process.
target_link_libraries(IDEXEJSO PRIVATE
    XER_VideoEditor_Core
    XER_Tz
    XER_JsonCpp
)
target_compile_features(IDEXEJSO PRIVATE cxx_std_23)
set_target_properties(IDEXEJSO PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(WIN32)
    target_sources(IDEXEJSO PRIVATE
        ide_app.cc
        file_tree_widget.cc
        code_editor_widget.cc
    )
    target_link_libraries(IDEXEJSO PRIVATE
        XER_UI_Core
        shell32    # SHBrowseForFolder / SHGetPathFromIDList
        ole32      # CoInitializeEx / CoTaskMemFree
    )
    # CEF browser embedding for the live preview — the lib arrives
    # transitively via XER_VideoEditor_Core (XER_HAS_CEF + libcef link
    # propagate PUBLIC); the explicit link keeps that true if the core
    # ever stops re-exporting it. Stub build when CEF is absent, so no
    # option/guard is needed here.
    if(TARGET XER_Cef)
        target_link_libraries(IDEXEJSO PRIVATE XER_Cef)
    endif()
endif()

# Sibling CEF subprocess — cef_runtime resolves
# <exe>/xer_browser_shell.exe at init; the shared bin/ output dir puts
# it beside IDEXEJSO.exe automatically.
if(TARGET xer_browser_shell)
    add_dependencies(IDEXEJSO xer_browser_shell)
endif()
# NOTE: libcef.dll + *.pak + locales are staged into the shared bin/
# by video_editor_cef_deploy (already deployed in existing builds).
# Deliberately NOT a dependency of IDEXEJSO: that target fails the
# build when a running exe locks bin/, and the deploy is idempotent —
# a fresh tree gets it via the video_editor target.

# ide_preview_probe — CLI harness that runs IdePreviewSelftest
# (watcher round trip + headless CEF check) until the IDE's
# --selftest wires it in. Windows-only like the preview pane itself.
if(WIN32)
    add_executable(ide_preview_probe
        ide_preview_probe.cc
        ide_preview_widget.cc
        ide_file_watch.cc
    )
    target_include_directories(ide_preview_probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(ide_preview_probe PRIVATE XER_UI_Core)
    if(TARGET XER_Cef)
        target_link_libraries(ide_preview_probe PRIVATE XER_Cef)
    endif()
    target_compile_features(ide_preview_probe PRIVATE cxx_std_23)
    set_target_properties(ide_preview_probe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

# In-process clang-format for the Format action (C++/JSON buffers go
# through clang::format::reformat — no clang-format.exe subprocess).
# Same monolithic clang-cpp/LLVM link ztzmake uses for in-process XSJO
# compiles; without it the IDE still builds and C++ formatting just
# reports "unavailable" (the builtin JS formatter has no deps).
find_package(LLVM CONFIG QUIET)
find_package(Clang CONFIG QUIET)
set(_ide_clang_ok OFF)
if(TARGET clang-cpp AND TARGET LLVM)
    set(_ide_clang_ok ON)
    target_link_libraries(IDEXEJSO PRIVATE clang-cpp LLVM)
    target_include_directories(IDEXEJSO SYSTEM PRIVATE
        ${LLVM_INCLUDE_DIRS})
else()
    # No package configs — bare import-lib discovery, still the
    # single-DLL style (libclang-cpp.dll.a + libLLVM-*.dll.a).
    get_filename_component(_ide_tc_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_ide_tc_root "${_ide_tc_bin}/.." ABSOLUTE)
    find_library(XER_IDE_CLANGCPP_LIB
        NAMES clang-cpp libclang-cpp.dll.a
        PATHS "${_ide_tc_root}/lib" NO_DEFAULT_PATH)
    find_library(XER_IDE_LLVM_LIB
        NAMES LLVM LLVM-23 LLVM-21 LLVM-20 LLVM-19 LLVM-18 LLVM-17
              libLLVM libLLVM-23.dll.a
        PATHS "${_ide_tc_root}/lib" NO_DEFAULT_PATH)
    find_path(XER_IDE_LLVM_INCLUDE llvm/Support/TargetSelect.h
        PATHS "${_ide_tc_root}/include" NO_DEFAULT_PATH)
    if(XER_IDE_CLANGCPP_LIB AND XER_IDE_LLVM_LIB AND
       XER_IDE_LLVM_INCLUDE)
        set(_ide_clang_ok ON)
        target_link_libraries(IDEXEJSO PRIVATE
            ${XER_IDE_CLANGCPP_LIB} ${XER_IDE_LLVM_LIB})
        target_include_directories(IDEXEJSO SYSTEM PRIVATE
            ${XER_IDE_LLVM_INCLUDE})
    endif()
endif()
if(_ide_clang_ok)
    target_compile_definitions(IDEXEJSO PRIVATE XER_IDE_HAS_CLANG=1)
    # The libclang-cpp/libLLVM runtime DLLs must sit beside the exe —
    # ztzmake stages them into the same bin dir; repeat the seed here
    # so IDEXEJSO stays self-sufficient even when built alone.
    if(MINGW AND COMMAND xer_stage_runtime_dlls)
        get_filename_component(_ide_tc_bin
            "${CMAKE_CXX_COMPILER}" DIRECTORY)
        file(GLOB _ide_llvm_dlls
             "${_ide_tc_bin}/libLLVM*.dll"
             "${_ide_tc_bin}/libclang-cpp.dll")
        set(_ide_seeds)
        foreach(_f IN LISTS _ide_llvm_dlls)
            get_filename_component(_n "${_f}" NAME)
            list(APPEND _ide_seeds "${_n}")
        endforeach()
        if(_ide_seeds)
            xer_stage_runtime_dlls("${CMAKE_BINARY_DIR}/bin"
                                   "${_ide_tc_bin}" ${_ide_seeds})
        endif()
    endif()
    message(STATUS "IDEXEJSO: in-process clang-format enabled")
else()
    message(STATUS "IDEXEJSO: Clang/LLVM not found — C++ formatting "
                   "disabled (JS formatter unaffected)")
endif()

if(MSVC)
    target_compile_options(IDEXEJSO PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(IDEXEJSO PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

# The IDE shells out to its sibling toolchain at runtime — building it
# produces the workers it expects beside it (they degrade gracefully
# in the UI when absent: build/test just log "not found").
if(TARGET ztzmake)
    add_dependencies(IDEXEJSO ztzmake)
endif()
if(TARGET VE.ExtensionsRuntime)
    add_dependencies(IDEXEJSO VE.ExtensionsRuntime)
endif()
