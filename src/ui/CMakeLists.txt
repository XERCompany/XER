# XER UI Subsystem CMake Configuration

# Collect UI sources
file(GLOB_RECURSE UI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
if(APPLE)
    file(GLOB_RECURSE _xer_apple_mm "${CMAKE_CURRENT_SOURCE_DIR}/*.mm")
    list(APPEND UI_SOURCES ${_xer_apple_mm})
endif()
list(FILTER UI_SOURCES EXCLUDE REGEX "/widgets/openai/")
list(FILTER UI_SOURCES EXCLUDE REGEX "/backends/flux/")

# Windows-only backends: Direct2D renderer and Win32 input translation.
# POSIX backends are added under platform/{linux,apple} as the port proceeds.
if(NOT WIN32)
    list(FILTER UI_SOURCES EXCLUDE REGEX "/platform/windows/")
    list(FILTER UI_SOURCES EXCLUDE REGEX "/rendering/windows/")
endif()

file(GLOB_RECURSE UI_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
list(FILTER UI_HEADERS EXCLUDE REGEX "/backends/flux/")
if(NOT WIN32)
    list(FILTER UI_HEADERS EXCLUDE REGEX "/platform/windows/")
    list(FILTER UI_HEADERS EXCLUDE REGEX "/rendering/windows/")
endif()

# Skia (unofficial port): GPU-capable 2D canvas used by Canvas/VideoPreview.
find_package(unofficial-skia CONFIG REQUIRED)

# Intel oneMKL (vcpkg intel-mkl): SIMD vector math for batched animation
# kernels (motion/accel.*). Optional — the port supports Windows/Linux x64
# only, so macOS and MKL-less installs take the scalar fallback path.
# Sequential threading — animation runs on the UI thread and must never
# block on OpenMP workers; static link keeps Editor.exe free of extra
# runtime DLLs.
set(MKL_THREADING "sequential" CACHE STRING "" FORCE)
set(MKL_LINK "static" CACHE STRING "" FORCE)
set(MKL_INTERFACE "lp64" CACHE STRING "" FORCE)
find_package(mkl CONFIG QUIET)

set(_ui_public_deps
    absl::strings
    fmt::fmt
    XER_Cache
    XER_ContentClipboard
    XER_ContentVideo
    XER_NodeGraph
    XER_AsyncIO
    XER_SecurityFlash
    XER_Helper
    XER_ContentPasswordGen
    unofficial::skia::skia)

if(XER_YA_DLLS)
    # Branded YA.*.dll modules; XER_UI_Core stays as INTERFACE bundle.
    # Widget/theme/rendering code is mutually referenced by Window — it
    # all lands in YA.UICore; the IPC render stream/host is a separate
    # low-level module (self-contained: Renderer is a header-only
    # abstract interface, so it needs no UICore symbols). YA.UICore
    # depends on it because Application::Initialize pre-spawns the
    # xer_engine_ui_render.exe worker pool through UiRenderHost.
    # UIRender/UIDesgin ship as reserved extension modules.
    #
    # app_audio_controller.cc (XER::App::AudioController) is compiled
    # here instead of XER_App: UI widgets call it, and App->UI is the
    # higher layer — embedding breaks the App<->UI archive cycle.
    set(_ya_ui "${UI_SOURCES}")
    xer_ya_partition(_ya_ui
        "YA.UIRenderIPC|render_stream|render_host|render_store"
        "YA.UICore|.")

    # YA.DesginCSS — the canonical XER::Content::CSS style engine
    # (src/content/css, per the design doc: style calculation belongs
    # to Desgin UI). Defined here (not in src/media) so YA.UICore can
    # link it — Widget and style/widget_style_host consume it.
    file(GLOB _css_sources
        "${CMAKE_SOURCE_DIR}/src/content/css/*.cc")
    xer_ya_module(YA_DesginCSS EXPORT_NAME YA.DesginCSS
        SOURCES ${_css_sources})

    xer_ya_module(YA_UIRenderIPC EXPORT_NAME YA.UIRenderIPC
        SOURCES ${XER_YA_SRC_YA.UIRenderIPC}
        DEPS ${_ui_public_deps})
    xer_ya_module(YA_UICore EXPORT_NAME YA.UICore
        SOURCES ${XER_YA_SRC_YA.UICore} ${UI_HEADERS}
            "${CMAKE_SOURCE_DIR}/src/app/app_audio_controller.cc"
        DEPS ${_ui_public_deps} YA_UIRenderIPC YA_DesginCSS
            XER_ContentAudio XER_AudioCore)
    xer_ya_module(YA_UIRender EXPORT_NAME YA.UIRender
        DEPS ${_ui_public_deps} YA_UICore)
    xer_ya_module(YA_UIDesgin EXPORT_NAME YA.UIDesgin
        DEPS ${_ui_public_deps} YA_UICore)
    # NOTE: libskia.dll can NOT be delay-loaded — its implib exports
    # data symbols (__declspec(dllimport) vtables) and GNU delay-load
    # only handles function imports. It stays an eager dependency, so
    # stage the single required runtime next to YA.UICore — every UI
    # exe then runs without clang64/bin on PATH.
    if(MINGW AND TARGET unofficial::skia::skia)
        get_filename_component(_toolchain_bin "${CMAKE_CXX_COMPILER}"
                               DIRECTORY)
        # libskia + its full MSYS2 transitive closure — the only
        # runtime files a UI exe can't get from the static link (skia
        # ships shared-only). Stage them once next to YA.UICore so
        # every UI exe runs without clang64/bin on PATH.
        set(_skia_rt libskia.dll
            libpng16-16.dll libjpeg-8.dll zlib1.dll
            libharfbuzz-0.dll libharfbuzz-subset-0.dll
            libwebp-7.dll libwebpdemux-2.dll libwebpmux-3.dll
            libsharpyuv-0.dll libexpat-1.dll libc++.dll
            libfreetype-6.dll libbz2-1.dll libbrotlicommon.dll
            libbrotlidec.dll libglib-2.0-0.dll libgraphite2.dll
            libiconv-2.dll libintl-8.dll libpcre2-8-0.dll)
        foreach(_dll IN LISTS _skia_rt)
            if(EXISTS "${_toolchain_bin}/${_dll}")
                add_custom_command(TARGET YA_UICore POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_toolchain_bin}/${_dll}"
                        "${CMAKE_BINARY_DIR}/bin/${_dll}"
                    VERBATIM)
            endif()
        endforeach()
    endif()
    if(WIN32)
        target_link_libraries(YA_UICore PRIVATE
            user32 gdi32 d2d1 d3d11 d3d12 dxgi dwrite comdlg32
            windowscodecs ole32 dwmapi gdiplus comctl32)
        target_compile_definitions(YA_UICore_objs PRIVATE
            WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()
    if(TARGET MKL::MKL)
        target_link_libraries(YA_UICore PRIVATE MKL::MKL)
        target_link_libraries(YA_UICore_objs PRIVATE MKL::MKL)
        target_compile_definitions(YA_UICore_objs PRIVATE XER_HAVE_MKL)
    endif()
    xer_ya_bundle(XER_UI_Core
        MODULES YA_UICore YA_UIRender YA_UIDesgin YA_UIRenderIPC
                YA_DesginCSS
        DEPS ${_ui_public_deps}
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
                 ${CMAKE_CURRENT_SOURCE_DIR}/..)
else()
    # Create UI core library — src/content/css joins so Widget and
    # style/ resolve their XER::Content::CSS references statically.
    file(GLOB _css_content_sources
        "${CMAKE_SOURCE_DIR}/src/content/css/*.cc")
    add_library(XER_UI_Core STATIC ${UI_SOURCES} ${UI_HEADERS}
        ${_css_content_sources})
    target_include_directories(XER_UI_Core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/..
    )
    target_link_libraries(XER_UI_Core PUBLIC ${_ui_public_deps})
endif()

if(XER_YA_DLLS)
    set(_xer_ui_scope INTERFACE)
else()
    set(_xer_ui_scope PUBLIC)
endif()

# Application::Initialize calls InitCommonControlsEx (comctl32).
if(WIN32)
    target_link_libraries(XER_UI_Core ${_xer_ui_scope} comctl32)
endif()

# MKL is optional: link and enable the XER_HAVE_MKL fast paths only when the
# package was found (absent on macOS and minimal Linux installs).
if(TARGET MKL::MKL)
    target_link_libraries(XER_UI_Core ${_xer_ui_scope} MKL::MKL)
    target_compile_definitions(XER_UI_Core ${_xer_ui_scope} XER_HAVE_MKL)
endif()

# libxml2: XML document model for the DevTools widget-tree dump
# (src/ui/debug/debug_map_ui.cc). Optional — without it the debug module
# is compiled out and DumpXml returns an empty string.
find_package(libxml2 CONFIG QUIET)
xer_retarget_imported_to_static("^LibXml2::")
xer_retarget_imported_to_ya_dll("^LibXml2::" YA_LibXml2)
if(TARGET LibXml2::LibXml2)
    target_link_libraries(XER_UI_Core ${_xer_ui_scope} LibXml2::LibXml2)
    target_compile_definitions(XER_UI_Core ${_xer_ui_scope} XER_UI_HAS_LIBXML2=1)
    if(MINGW AND NOT XER_YA_DLLS)
        # Retargeted to libxml2.a — headers must not emit dllimport.
        target_compile_definitions(XER_UI_Core ${_xer_ui_scope} LIBXML_STATIC)
    endif()
    if(XER_YA_DLLS)
        # xml/ and debug/ sources compile inside YA_UICore — give the
        # module itself the libxml2 usage requirements.
        target_link_libraries(YA_UICore PRIVATE LibXml2::LibXml2)
        target_link_libraries(YA_UICore_objs PRIVATE LibXml2::LibXml2)
        target_compile_definitions(YA_UICore_objs PRIVATE XER_UI_HAS_LIBXML2=1)
    endif()
endif()

# Project avatars are transcoded to icons/<id>/icon.jpg through the FFmpeg
# wrapper (engine::bridge::ffmpeg::ImageToJpeg).
if(TARGET XER_FFmpeg)
    target_link_libraries(XER_UI_Core ${_xer_ui_scope} XER_FFmpeg)
endif()

if(NOT XER_YA_DLLS)
    target_compile_features(XER_UI_Core PUBLIC cxx_std_23)
    if(MSVC)
        target_compile_options(XER_UI_Core PRIVATE /W4 /WX- /FS)
    else()
        target_compile_options(XER_UI_Core PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

add_library(XER_UI_Flux STATIC
    backends/flux/flux_renderer.cc
    backends/flux/flux_renderer.h
    backends/flux/flux_input_adapter.cc
    backends/flux/flux_input_adapter.h
)

target_include_directories(XER_UI_Flux
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(XER_UI_Flux
    PUBLIC
        XER_UI_Core
        XER_Flux_Core
)

target_compile_features(XER_UI_Flux PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_UI_Flux PRIVATE /W4 /WX- /FS)
else()
    target_compile_options(XER_UI_Flux PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Platform-specific linking
if(WIN32)
    target_link_libraries(XER_UI_Core
        ${_xer_ui_scope}
            user32
            gdi32
            d2d1
            d3d11
            d3d12
            dxgi
            dwrite
            comdlg32
            windowscodecs
            ole32
            dwmapi
            gdiplus
    )
    # Prevent winsock.h / min-max macros from interfering with std::
    target_compile_definitions(XER_UI_Core ${_xer_ui_scope} WIN32_LEAN_AND_MEAN NOMINMAX)
endif()