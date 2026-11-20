# cmake/config_checks.cmake — real configure-time feature detection for
# src/config.h.
#
# Included once from the root CMakeLists.txt right after the compilers and
# feature options are known. Each probe runs check_include_file() /
# check_include_file_cxx() (or check_include_file_objcxx() for the
# Objective-C-only Apple frameworks) and is normalized to a strict 0/1
# before configure_file() substitutes it into src/config.h — a header the
# toolchain cannot compile degrades to 0, never to an empty token or a
# configure error, so cross-toolchain presets stay safe.
#
# The result is written into the source tree on purpose: src/config.h is
# committed (do NOT gitignore it) and is refreshed on every configure.

include(CheckIncludeFile)
include(CheckIncludeFileCXX)
include(CheckIncludeFiles)

# xer_probe_header(<header> <C|CXX>)
#   Maps <header> to XER_HAVE_<NAME> (uppercased, '/' and '.' -> '_'),
#   runs the matching CheckIncludeFile* probe, and leaves a normal
#   variable set to literal 0/1 in the including scope.
#
#   Implemented as a macro (not a function) so the normalized result needs
#   no PARENT_SCOPE hop and is directly visible to configure_file().
macro(xer_probe_header header lang)
    string(TOUPPER "${header}" _xer_have_var)
    string(REGEX REPLACE "[/.]" "_" _xer_have_var "${_xer_have_var}")
    set(_xer_have_var "XER_HAVE_${_xer_have_var}")
    if("${lang}" STREQUAL "CXX")
        check_include_file_cxx("${header}" ${_xer_have_var})
    else()
        check_include_file("${header}" ${_xer_have_var})
    endif()
    if(${_xer_have_var})
        set(${_xer_have_var} 1)
    else()
        set(${_xer_have_var} 0)
    endif()
endmacro()

# xer_probe_winapi_header(<header>)
#   Same mapping/normalization as xer_probe_header(), but the probe source
#   includes <windows.h> before <header>. Several SDK headers (avrt.h and
#   friends) are not self-contained — they are only ever used after
#   <windows.h>, so probing them standalone would report 0 for a header
#   that is actually installed and usable. On non-Windows toolchains
#   <windows.h> itself is missing and the combined probe degrades to 0.
#   The try_compile result is cached under a private _XER_PROBE_* name so
#   a stale standalone XER_HAVE_* result can never shadow it.
macro(xer_probe_winapi_header header)
    string(TOUPPER "${header}" _xer_have_var)
    string(REGEX REPLACE "[/.]" "_" _xer_have_var "${_xer_have_var}")
    set(_xer_have_var "XER_HAVE_${_xer_have_var}")
    check_include_files("windows.h;${header}" _XER_PROBE_${_xer_have_var})
    if(_XER_PROBE_${_xer_have_var})
        set(${_xer_have_var} 1)
    else()
        set(${_xer_have_var} 0)
    endif()
endmacro()

# --- WinAPI ----------------------------------------------------------------
# windows.h itself is probed standalone; every other SDK header is probed
# in its real usage context (after <windows.h>).
xer_probe_header("windows.h" C)
foreach(_xer_h IN ITEMS
        wincodec.h d2d1.h d2d1_1.h d3d11.h d3d12.h dxgi1_2.h
        dwrite.h mmdeviceapi.h audioclient.h avrt.h shlobj.h dwmapi.h)
    xer_probe_winapi_header("${_xer_h}")
endforeach()

# --- POSIX / Linux ----------------------------------------------------------
foreach(_xer_h IN ITEMS
        unistd.h sys/mman.h sys/shm.h fcntl.h dlfcn.h pthread.h sched.h
        sys/epoll.h sys/inotify.h X11/Xlib.h)
    xer_probe_header("${_xer_h}" C)
endforeach()

# --- macOS (C-compatible frameworks) ---------------------------------------
foreach(_xer_h IN ITEMS
        CoreFoundation/CoreFoundation.h CoreVideo/CoreVideo.h
        mach/mach_time.h)
    xer_probe_header("${_xer_h}" C)
endforeach()

# Cocoa.h / AVFoundation.h only parse as Objective-C(++) — a plain C or C++
# probe would report 0 even on macOS where they exist. Probe them with the
# ObjC++ compiler on Apple and hard-wire 0 elsewhere.
if(APPLE)
    enable_language(OBJCXX)
    include(CheckIncludeFileOBJCXX)
    check_include_file_objcxx("Cocoa/Cocoa.h" XER_HAVE_COCOA_COCOA_H)
    check_include_file_objcxx("AVFoundation/AVFoundation.h"
        XER_HAVE_AVFOUNDATION_AVFOUNDATION_H)
endif()
foreach(_xer_v XER_HAVE_COCOA_COCOA_H XER_HAVE_AVFOUNDATION_AVFOUNDATION_H)
    if(${_xer_v})
        set(${_xer_v} 1)
    else()
        set(${_xer_v} 0)
    endif()
endforeach()

# --- C++-only headers -------------------------------------------------------
# wrl/client.h is Microsoft's COM WRL (C++ templates); vulkan/vulkan.h is
# the optional Vulkan SDK header — the UI Vulkan backend also compiles
# without it (vendored-header fallback + runtime dll loading), this probe
# only documents SDK availability; the rest are C++ standard library
# headers. check_include_file_cxx() honors CMAKE_CXX_STANDARD (23), so
# <expected>/<format> report their real availability for this toolchain's
# STL.
foreach(_xer_h IN ITEMS
        wrl/client.h vulkan/vulkan.h
        version source_location format jthread execution filesystem span
        expected)
    xer_probe_header("${_xer_h}" CXX)
endforeach()

# Every XER_HAVE_* is now a literal 0/1 — generate src/config.h in the
# source tree (committed on purpose; see src/config.h.in).
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/config.h.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/config.h"
    @ONLY)
