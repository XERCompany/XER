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
    )
    target_link_libraries(IDEXEJSO PRIVATE
        XER_UI_Core
        shell32    # SHBrowseForFolder / SHGetPathFromIDList
        ole32      # CoInitializeEx / CoTaskMemFree
    )
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
