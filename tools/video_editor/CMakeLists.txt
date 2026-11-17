# VideoEditor core — headless timeline model/history/store/catalog/
# poster modules (Phase 1B). UI-free; consumed by the future
# video_editor exe (1C/1D) and XER_VideoEditor_Tests.
# CONFIGURE_DEPENDS so adding .cc files does not require a manual
# re-configure under Ninja.
# NOTE: argument order matters — <variable> must precede
# CONFIGURE_DEPENDS (flag-first parses but yields an empty list on
# CMake 4.4.x).
# Core sources: top level plus feature subdirs (playback/, vst/, lab/)
# added in Phase 2 — subdirs stay UI-free like the top level.
file(GLOB VIDEO_EDITOR_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/playback/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/vst/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/lab/*.cc"
)
# Exclude the exe's main() from the core lib glob (it must not be
# compiled into XER_VideoEditor_Core).
list(FILTER VIDEO_EDITOR_CORE_SOURCES EXCLUDE REGEX "/main\\.cc$")

add_library(XER_VideoEditor_Core STATIC ${VIDEO_EDITOR_CORE_SOURCES})

target_include_directories(XER_VideoEditor_Core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(XER_VideoEditor_Core
    PUBLIC
        XER_Video          # video::Frame / FrameExporter (clip_poster)
        XER_ContentVideo   # XER::Content::Video::ProbeVideo (catalog)
        XER_FFmpeg         # Bridge::FFmpeg::VideoDecoder (clip_poster)
        XER_SQL            # SQLiteDatabase (media_catalog)
        XER_Cache          # atomic export temp/commit helpers
        XER_Helper         # Path/env/jsoncpp helpers
        jsoncpp_lib        # project_store (.xerproj)
        XER_AudioCore      # VST3 host (plugin_wrappers) + DSP
)

if(WIN32)
    # WASAPI preview-audio sink (playback/wasapi_sink.cc).
    target_link_libraries(XER_VideoEditor_Core PUBLIC ole32)
endif()

target_compile_features(XER_VideoEditor_Core PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_VideoEditor_Core PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_VideoEditor_Core PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

# video_editor.exe — Phase 1C headless CLI (--export/--info) plus the
# Phase 1D desktop workspace. UI sources live under ui/ so the core lib
# stays UI-free; they are exe-side only and Windows-gated like
# tools/preview.
add_executable(video_editor main.cc)
target_link_libraries(video_editor PRIVATE XER_VideoEditor_Core)
if(WIN32)
    # Recursive: Phase-2 panels/tools live in ui/ subdirs.
    file(GLOB_RECURSE VIDEO_EDITOR_UI_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cc"
    )
    target_sources(video_editor PRIVATE ${VIDEO_EDITOR_UI_SOURCES})
    target_link_libraries(video_editor PRIVATE XER_UI_Core)

    # Runtime-DLL deployment (CopyDLL.ps1 discovers imports with
    # llvm-objdump and recurses transitively — FFmpeg alone pulls ~80
    # DLLs). POST_BUILD keeps the exe runnable without clang64 on PATH;
    # `video_editor_dist` produces a self-contained package on demand.
    find_program(XER_POWERSHELL NAMES powershell pwsh
                 DOC "PowerShell host for CopyDLL.ps1")
    if(XER_POWERSHELL)
        # MSYS2 packaging quirk: libdovi/librav1e import short names.
        set(XER_DEPLOY_DLL_ALIASES
            "dovi.dll=libdovi.dll,rav1e.dll=librav1e.dll")

        option(XER_VIDEO_EDITOR_DEPLOY_DLLS
               "Copy runtime DLLs next to video_editor.exe after build" ON)
        if(XER_VIDEO_EDITOR_DEPLOY_DLLS)
            add_custom_command(TARGET video_editor POST_BUILD
                COMMAND "${XER_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
                        -File "${CMAKE_SOURCE_DIR}/CopyDLL.ps1"
                        -ExePath "$<TARGET_FILE:video_editor>"
                        -Alias "${XER_DEPLOY_DLL_ALIASES}"
                COMMENT "Deploying runtime DLLs next to video_editor.exe"
                VERBATIM)
        endif()

        add_custom_target(video_editor_dist
            COMMAND "${XER_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_SOURCE_DIR}/CopyDLL.ps1"
                    -ExePath "$<TARGET_FILE:video_editor>"
                    -DestDir "${CMAKE_BINARY_DIR}/dist/video_editor"
                    -CopyExe
                    -Alias "${XER_DEPLOY_DLL_ALIASES}"
            DEPENDS video_editor
            COMMENT "Packaging video_editor + DLLs into dist/video_editor"
            VERBATIM)
    else()
        message(STATUS "PowerShell not found — DLL deploy targets skipped")
    endif()
endif()
target_compile_features(video_editor PRIVATE cxx_std_23)
set_target_properties(video_editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(MSVC)
    target_compile_options(video_editor PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(video_editor PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
