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
