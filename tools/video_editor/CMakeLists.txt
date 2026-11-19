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
    "${CMAKE_CURRENT_SOURCE_DIR}/ai/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/app_runtime/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/color/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/extensions/*.cc"
)
# Exclude the exe's main() from the core lib glob (it must not be
# compiled into XER_VideoEditor_Core).
list(FILTER VIDEO_EDITOR_CORE_SOURCES EXCLUDE REGEX "/main\\.cc$")

set(_ve_core_deps
    XER_Video          # video::Frame / FrameExporter (clip_poster)
    XER_ContentVideo   # XER::Content::Video::ProbeVideo (catalog)
    XER_FFmpeg         # Bridge::FFmpeg::VideoDecoder (clip_poster)
    XER_SQL            # SQLiteDatabase (media_catalog)
    XER_Cache          # atomic export temp/commit helpers
    XER_NetClient      # app_super_net fetch/download (native_modules)
    XER_Tz             # .xexrj archives (extensions/ext_host)
    XER_Cef            # xer-ext:// scheme (stub when CEF absent)
    XER_Helper         # Path/env/jsoncpp helpers
    XER_JsonCpp        # project_store (.xerproj)
    XER_AudioCore      # VST3 host (plugin_wrappers) + DSP
)

if(XER_YA_DLLS)
    # Branded YA.VideoEditor-* modules — XER_VideoEditor_Core stays as
    # an INTERFACE bundle so video_editor/tests link unchanged.
    # The editor's subsystems are mutually dependent (app_runtime boots
    # color/decoder/poster while catalog/ai call back into them), so
    # YA.VideoEditor-Core carries the whole module set; the specialized
    # names ship as reserved extension modules.
    xer_ya_module(YA_VideoEditor_Core EXPORT_NAME YA.VideoEditor-Core
        SOURCES ${VIDEO_EDITOR_CORE_SOURCES}
        DEPS ${_ve_core_deps})
    foreach(_ya_ve_ext IN ITEMS
            "YA_VideoEditor_OCIO|YA.VideoEditor-OCIO"
            "YA_VideoEditor_Engine|YA.VideoEditor-Engine"
            "YA_Extenstions|YA.Extenstions"
            "YA_ProjectStoreSQL_VE|YA.ProjectStoreSQL-VideoEditor"
            "YA_VideoEditor_FrameVideo|YA.VideoEditor-FrameVideo"
            "YA_VideoEditor_ToolsDesgin|YA.VideoEditor-ToolsDesgin")
        string(REGEX MATCH "^([^|]+)\\|(.*)$" _m "${_ya_ve_ext}")
        xer_ya_module(${CMAKE_MATCH_1} EXPORT_NAME ${CMAKE_MATCH_2}
            DEPS ${_ve_core_deps} YA_VideoEditor_Core)
    endforeach()
    xer_ya_bundle(XER_VideoEditor_Core
        MODULES YA_VideoEditor_Core YA_VideoEditor_Engine
                YA_VideoEditor_OCIO YA_VideoEditor_FrameVideo
                YA_VideoEditor_ToolsDesgin YA_Extenstions
                YA_ProjectStoreSQL_VE
        DEPS ${_ve_core_deps}
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
                 ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
    set(_xer_ve_scope INTERFACE)
else()
    add_library(XER_VideoEditor_Core STATIC ${VIDEO_EDITOR_CORE_SOURCES})

    target_include_directories(XER_VideoEditor_Core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    )
    set(_xer_ve_scope PUBLIC)
endif()

# App-runtime assets layer: real V8 (MSYS2 package via the V8 shim
# config) runs assets/runtime.js; yaml-cpp parses app.yaml/window.yaml.
# Both are QUIET/optional — without them the app boots with
# compiled-in defaults (the assets layer degrades gracefully).
find_package(V8 QUIET)
find_package(yaml-cpp QUIET)
xer_retarget_imported_to_ya_dll("yaml" YA_YAML)
find_package(OpenColorIO QUIET)
find_package(OpenImageIO QUIET)
find_package(librsvg QUIET)
if(TARGET librsvg::librsvg)
    target_link_libraries(XER_VideoEditor_Core ${_xer_ve_scope} librsvg::librsvg)
    target_compile_definitions(XER_VideoEditor_Core ${_xer_ve_scope}
        XER_VE_HAS_RSVG=1)
    if(XER_YA_DLLS)
        target_link_libraries(YA_VideoEditor_Core PRIVATE librsvg::librsvg)
        target_link_libraries(YA_VideoEditor_Core_objs PRIVATE librsvg::librsvg)
        target_compile_definitions(YA_VideoEditor_Core_objs PRIVATE XER_VE_HAS_RSVG=1)
    endif()
endif()
if(TARGET OpenColorIO::OpenColorIO)
    target_link_libraries(XER_VideoEditor_Core ${_xer_ve_scope}
        OpenColorIO::OpenColorIO)
    target_compile_definitions(XER_VideoEditor_Core ${_xer_ve_scope}
        XER_VE_HAS_OCIO=1)
    if(XER_YA_DLLS)
        # OCIO consumers live in color/ -> YA.VideoEditor-Core.
        target_link_libraries(YA_VideoEditor_Core PRIVATE OpenColorIO::OpenColorIO)
        target_link_libraries(YA_VideoEditor_Core_objs PRIVATE OpenColorIO::OpenColorIO)
        target_compile_definitions(YA_VideoEditor_Core_objs PRIVATE XER_VE_HAS_OCIO=1)
    endif()
endif()
if(TARGET OpenImageIO::OpenImageIO)
    target_link_libraries(XER_VideoEditor_Core ${_xer_ve_scope}
        OpenImageIO::OpenImageIO)
    target_compile_definitions(XER_VideoEditor_Core ${_xer_ve_scope}
        XER_VE_HAS_OIIO=1)
    if(XER_YA_DLLS)
        target_link_libraries(YA_VideoEditor_Core PRIVATE OpenImageIO::OpenImageIO)
        target_link_libraries(YA_VideoEditor_Core_objs PRIVATE OpenImageIO::OpenImageIO)
        target_compile_definitions(YA_VideoEditor_Core_objs PRIVATE XER_VE_HAS_OIIO=1)
    endif()
endif()
if(TARGET V8::V8)
    target_link_libraries(XER_VideoEditor_Core ${_xer_ve_scope} V8::V8)
    target_compile_definitions(XER_VideoEditor_Core ${_xer_ve_scope}
        XER_VE_HAS_V8=1)
    if(XER_YA_DLLS)
        target_link_libraries(YA_VideoEditor_Core PRIVATE V8::V8)
        target_link_libraries(YA_VideoEditor_Core_objs PRIVATE V8::V8)
        target_compile_definitions(YA_VideoEditor_Core_objs PRIVATE XER_VE_HAS_V8=1)
    endif()
endif()
if(TARGET yaml-cpp::yaml-cpp)
    set(_ve_yaml_target yaml-cpp::yaml-cpp)
elseif(TARGET yaml-cpp)
    set(_ve_yaml_target yaml-cpp)
endif()
if(_ve_yaml_target)
    target_link_libraries(XER_VideoEditor_Core ${_xer_ve_scope} ${_ve_yaml_target})
    target_compile_definitions(XER_VideoEditor_Core ${_xer_ve_scope}
        XER_VE_HAS_YAMLCPP=1)
    if(XER_YA_DLLS)
        # app_runtime/color yaml loaders link inside the module itself.
        target_link_libraries(YA_VideoEditor_Core PRIVATE ${_ve_yaml_target})
        target_link_libraries(YA_VideoEditor_Core_objs PRIVATE ${_ve_yaml_target})
        target_compile_definitions(YA_VideoEditor_Core_objs PRIVATE
            XER_VE_HAS_YAMLCPP=1)
    endif()
endif()

if(WIN32)
    # WASAPI preview-audio sink (playback/wasapi_sink.cc).
    target_link_libraries(XER_VideoEditor_Core ${_xer_ve_scope} ole32)
endif()

if(NOT XER_YA_DLLS)
    target_compile_features(XER_VideoEditor_Core PUBLIC cxx_std_23)
    if(MSVC)
        target_compile_options(XER_VideoEditor_Core PRIVATE /W4 /WX- /O2)
    else()
        target_compile_options(XER_VideoEditor_Core PRIVATE -Wall -Wextra -Wpedantic -O3)
    endif()
endif()

# video_editor.exe — Phase 1C headless CLI (--export/--info) plus the
# Phase 1D desktop workspace. UI sources live under ui/ so the core lib
# stays UI-free; they are exe-side only and Windows-gated like
# tools/preview.
add_executable(video_editor main.cc)
target_link_libraries(video_editor PRIVATE XER_VideoEditor_Core)
add_subdirectory(plugins)
if(TARGET video_editor_plugins)
    add_dependencies(video_editor video_editor_plugins)
endif()
if(WIN32)
    # Recursive: Phase-2 panels/tools live in ui/ subdirs.
    file(GLOB_RECURSE VIDEO_EDITOR_UI_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cc"
    )
    target_sources(video_editor PRIVATE ${VIDEO_EDITOR_UI_SOURCES})
    target_link_libraries(video_editor PRIVATE XER_UI_Core)
    # CEF browser embedding — stub lib when the distribution is absent.
    if(TARGET XER_Cef)
        target_link_libraries(video_editor PRIVATE XER_Cef)
    endif()

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

        # Optional runtime-probed DLLs: the AI features (whisper
        # transcription, ggml backends) LoadLibrary these at run time,
        # so CopyDLL.ps1 never sees them — ship them when the toolchain
        # provides them (MSYS2 clang64 ships whisper.cpp + ggml).
        get_filename_component(_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
        file(GLOB XER_OPT_RT_DLLS
             "${_compiler_bin}/libwhisper*.dll"
             "${_compiler_bin}/whisper.dll"
             "${_compiler_bin}/ggml-*.dll"
             "${_compiler_bin}/libv8*.dll")

        # App assets: app.yaml + runtime.js + window/window.yaml — the
        # declarative bootstrap the app_runtime layer executes via V8.
        add_custom_command(TARGET video_editor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                    "$<TARGET_FILE_DIR:video_editor>/assets"
            COMMENT "Deploying app assets (runtime.js, yaml configs)"
            VERBATIM)
        # Always-run sibling of the POST_BUILD copy above: the hook
        # only fires on relink, so asset-only edits (icons, yaml,
        # runtime.js) would otherwise never reach the output dir. A
        # custom target builds on every build, deploying fresh assets
        # without a relink.
        add_custom_target(video_editor_assets
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                    "$<TARGET_FILE_DIR:video_editor>/assets"
            COMMENT "Deploying app assets (icons, yaml, runtime.js)"
            VERBATIM)
        # .xexrj extensions: every dir under assets/extensions_src/ is
        # packed by ztzmake into <exe>/assets/extensions/<name>.xexrj
        # — the same artifact a user gets from running ztzmake.exe.
        if(TARGET ztzmake)
            file(GLOB XER_EXT_SRC_DIRS CONFIGURE_DEPENDS
                 LIST_DIRECTORIES true
                 "${CMAKE_CURRENT_SOURCE_DIR}/assets/extensions_src/*")
            foreach(ext_src IN LISTS XER_EXT_SRC_DIRS)
                if(IS_DIRECTORY "${ext_src}")
                    get_filename_component(ext_name "${ext_src}" NAME)
                    add_custom_command(TARGET video_editor_assets
                        POST_BUILD
                        COMMAND $<TARGET_FILE:ztzmake>
                                "${ext_src}"
                                -o "$<TARGET_FILE_DIR:video_editor>/assets/extensions/${ext_name}.xexrj"
                        COMMENT "Packing ${ext_name}.xexrj"
                        VERBATIM)
                endif()
            endforeach()
            add_dependencies(video_editor_assets ztzmake)
        endif()
        add_dependencies(video_editor video_editor_assets)

        # CEF runtime: libcef.dll + resources beside the exe — the
        # CefRuntime opens them from the executable directory. Manual
        # install via InstallCef.ps1 -> third_party/cef/cef_windows64.
        find_package(CEF QUIET)
        if(TARGET CEF::libcef)
            add_custom_target(video_editor_cef_deploy
                COMMAND ${CMAKE_COMMAND} -DSRC=${CEF_RELEASE_DIR} -DDST=$<TARGET_FILE_DIR:video_editor>
                           -P ${PROJECT_SOURCE_DIR}/cmake/CopyDirRetry.cmake
                COMMAND ${CMAKE_COMMAND} -DSRC=${CEF_RESOURCES_DIR} -DDST=$<TARGET_FILE_DIR:video_editor>
                           -P ${PROJECT_SOURCE_DIR}/cmake/CopyDirRetry.cmake
                COMMENT "Deploying CEF runtime (libcef.dll, *.pak, locales)"
                VERBATIM)
    set_property(TARGET video_editor_cef_deploy PROPERTY JOB_POOL xer_cef_deploy)
            add_dependencies(video_editor video_editor_cef_deploy)
            message(STATUS "CEF deploy target enabled (${CEF_ROOT})")
        endif()
        if(XER_OPT_RT_DLLS)
            add_custom_command(TARGET video_editor POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${XER_OPT_RT_DLLS}
                        "$<TARGET_FILE_DIR:video_editor>"
                COMMENT "Deploying optional AI runtime DLLs (whisper/ggml)"
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
