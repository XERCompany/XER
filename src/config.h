#pragma once

// Platform detection macros for XER
// Sets one of XER_PLATFORM_WINDOWS, XER_PLATFORM_LINUX, XER_PLATFORM_MACOS, XER_PLATFORM_UBUNTU, XER_PLATFORM_OPENBSD, XER_PLATFORM_POSIX

#if defined(_WIN32) || defined(_WIN64)
    #define XER_PLATFORM_WINDOWS 1
#else
    #define XER_PLATFORM_WINDOWS 0
#endif

#if defined(__APPLE__) && defined(__MACH__)
    #define XER_PLATFORM_MACOS 1
#else
    #define XER_PLATFORM_MACOS 0
#endif

#if defined(__linux__)
    #define XER_PLATFORM_LINUX 1
#else
    #define XER_PLATFORM_LINUX 0
#endif

#if defined(__linux__) && defined(__ubuntu__)
    #define XER_PLATFORM_UBUNTU 1
#else
    #define XER_PLATFORM_UBUNTU 0
#endif

#if defined(__OpenBSD__)
    #define XER_PLATFORM_OPENBSD 1
#else
    #define XER_PLATFORM_OPENBSD 0
#endif

#if defined(_WIN32) || defined(_WIN64)
    #define XER_PLATFORM_POSIX 0
#else
    #define XER_PLATFORM_POSIX 1
#endif

// ---------------------------------------------------------------------------
// XER version — initial release line 1.0.01.
// Bump PATCH for fixes, MINOR for features, MAJOR for breaking changes;
// keep XER_VERSION_STRING and XER_VERSION_NUMBER in sync with the triplet.
#define XER_VERSION_MAJOR 1
#define XER_VERSION_MINOR 0
#define XER_VERSION_PATCH 1
#define XER_VERSION_STRING "1.0.01"
#define XER_VERSION_NUMBER 10001  /* major*10000 + minor*100 + patch */

// ===========================================================================
// >>> GENERATED BLOCK — DO NOT EDIT BY HAND <<<
//
// Everything below is produced by CMake configure_file() from this
// src/config.h.in template (see cmake/config_checks.cmake, included by the
// root CMakeLists.txt). Each XER_HAVE_* macro is the normalized 0/1 result
// of a real check_include_file() / check_include_file_cxx() /
// check_include_file_objcxx() probe run at configure time — a header that
// the toolchain cannot compile degrades to 0, never to a configure error.
// src/config.h is intentionally generated into the source tree and stays
// committed; re-run CMake configure to refresh the values.

// --- WinAPI ---------------------------------------------------------------
#define XER_HAVE_WINDOWS_H 1
#define XER_HAVE_WINCODEC_H 1
#define XER_HAVE_D2D1_H 1
#define XER_HAVE_D2D1_1_H 1
#define XER_HAVE_D3D11_H 1
#define XER_HAVE_D3D12_H 1
#define XER_HAVE_DXGI1_2_H 1
#define XER_HAVE_DWRITE_H 1
#define XER_HAVE_MMDEVICEAPI_H 1
#define XER_HAVE_AUDIOCLIENT_H 1
#define XER_HAVE_AVRT_H 1
#define XER_HAVE_SHLOBJ_H 1
#define XER_HAVE_WRL_CLIENT_H 1
#define XER_HAVE_DWMAPI_H 1

// --- POSIX / Linux --------------------------------------------------------
#define XER_HAVE_UNISTD_H 1
#define XER_HAVE_SYS_MMAN_H 0
#define XER_HAVE_SYS_SHM_H 0
#define XER_HAVE_FCNTL_H 1
#define XER_HAVE_DLFCN_H 0
#define XER_HAVE_PTHREAD_H 1
#define XER_HAVE_SCHED_H 1
#define XER_HAVE_SYS_EPOLL_H 0
#define XER_HAVE_SYS_INOTIFY_H 0
#define XER_HAVE_X11_XLIB_H 1

// --- macOS frameworks -----------------------------------------------------
#define XER_HAVE_COREFOUNDATION_COREFOUNDATION_H 0
#define XER_HAVE_COCOA_COCOA_H 0
#define XER_HAVE_AVFOUNDATION_AVFOUNDATION_H 0
#define XER_HAVE_COREVIDEO_COREVIDEO_H 0
#define XER_HAVE_MACH_MACH_TIME_H 0

// --- C++ standard library -------------------------------------------------
#define XER_HAVE_VERSION 1
#define XER_HAVE_SOURCE_LOCATION 1
#define XER_HAVE_FORMAT 1
#define XER_HAVE_JTHREAD 0
#define XER_HAVE_EXECUTION 1
#define XER_HAVE_FILESYSTEM 1
#define XER_HAVE_SPAN 1
#define XER_HAVE_EXPECTED 1

// <<< END GENERATED BLOCK >>>
