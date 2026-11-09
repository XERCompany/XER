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

# Create UI core library
add_library(XER_UI_Core STATIC ${UI_SOURCES} ${UI_HEADERS})

target_include_directories(XER_UI_Core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Link with vcpkg libraries
target_link_libraries(XER_UI_Core
    PUBLIC
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
        unofficial::skia::skia
)

# Application::Initialize calls InitCommonControlsEx (comctl32).
if(WIN32)
    target_link_libraries(XER_UI_Core PUBLIC comctl32)
endif()

# MKL is optional: link and enable the XER_HAVE_MKL fast paths only when the
# package was found (absent on macOS and minimal Linux installs).
if(TARGET MKL::MKL)
    target_link_libraries(XER_UI_Core PUBLIC MKL::MKL)
    target_compile_definitions(XER_UI_Core PUBLIC XER_HAVE_MKL)
endif()

# Project avatars are transcoded to icons/<id>/icon.jpg through the FFmpeg
# wrapper (engine::bridge::ffmpeg::ImageToJpeg).
if(TARGET XER_FFmpeg)
    target_link_libraries(XER_UI_Core PUBLIC XER_FFmpeg)
endif()

# C++ standard
target_compile_features(XER_UI_Core PUBLIC cxx_std_23)

# Compiler flags
if(MSVC)
    target_compile_options(XER_UI_Core PRIVATE /W4 /WX- /FS)
else()
    target_compile_options(XER_UI_Core PRIVATE -Wall -Wextra -Wpedantic)
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
        PUBLIC
            user32
            gdi32
            d2d1
            d3d12
            dxgi
            dwrite
            comdlg32
            windowscodecs
            ole32
    )
    # Prevent winsock.h / min-max macros from interfering with std::
    target_compile_definitions(XER_UI_Core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
endif()