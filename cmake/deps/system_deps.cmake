# XER system dependency provider (Linux / macOS).
#
# Included from the root CMakeLists.txt on non-Windows platforms when
# XER_USE_SYSTEM_DEPS=ON (the default there). It resolves the project's
# dependencies from OS packages instead of vcpkg:
#
#   * "Normal" libraries come from apt (Ubuntu/Debian) or Homebrew (macOS)
#     and are discovered through their own CMake package configs, pkg-config
#     .pc files or find_library — whichever the distro ships.
#   * vcpkg-*named* dependencies (unofficial-*, libuv, re2, FFTW3f, MKL, ...)
#     are satisfied by thin shim configs under cmake/deps/configs/ that map
#     the vcpkg package/target names onto the system equivalents, so the
#     ~80 find_package() calls across the tree keep working unchanged.
#   * A few ports have no system equivalent at all (Skia, ggml, folly,
#     pthreadpool, fxdiv, fp16, date, pffft, tinyexr, onnxruntime, ...).
#     For those, XER_VCPKG_ROOT + XER_VCPKG_TRIPLET point at a *partial*
#     vcpkg installation whose share/ dir is appended to CMAKE_PREFIX_PATH —
#     only the heavy ports are installed there, everything else resolves
#     from the system first.
#
# Windows never includes this file: the vcpkg toolchain covers everything.

# ---------------------------------------------------------------------------
# Search paths
# ---------------------------------------------------------------------------

# 1) Shim package configs (unofficial-*, libuv, re2, FFTW3f, MKL, ...).
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_LIST_DIR}/configs")

# 2) Find-module overrides that shadow nothing on Windows but are required
#    here because no builtin module exists (FFMPEG).
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# 3) Partial vcpkg install for ports without a system package.
#    XER_VCPKG_ROOT defaults to $ENV{VCPKG_ROOT}; the triplet defaults per OS.
if(NOT DEFINED XER_VCPKG_ROOT)
    if(DEFINED ENV{VCPKG_ROOT})
        set(XER_VCPKG_ROOT "$ENV{VCPKG_ROOT}")
    endif()
endif()
if(NOT DEFINED XER_VCPKG_TRIPLET)
    if(APPLE)
        if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64|ARM64")
            set(XER_VCPKG_TRIPLET "arm64-osx")
        else()
            set(XER_VCPKG_TRIPLET "x64-osx")
        endif()
    else()
        set(XER_VCPKG_TRIPLET "x64-linux")
    endif()
endif()
set(XER_VCPKG_TRIPLET "${XER_VCPKG_TRIPLET}" CACHE STRING
    "vcpkg triplet used for the heavy-dependency subset (skia, ggml, ...)")
if(XER_VCPKG_ROOT)
    set(_xer_vcpkg_share "${XER_VCPKG_ROOT}/installed/${XER_VCPKG_TRIPLET}/share")
    if(IS_DIRECTORY "${_xer_vcpkg_share}")
        list(APPEND CMAKE_PREFIX_PATH "${_xer_vcpkg_share}")
        # Some vcpkg configs locate sibling files relative to share/; expose
        # the classic VCPKG_INSTALLED_DIR-style variables for the few places
        # that still probe them (e.g. src/content/video encoder libs).
        set(VCPKG_INSTALLED_DIR "${XER_VCPKG_ROOT}/installed" CACHE PATH "")
        set(VCPKG_TARGET_TRIPLET "${XER_VCPKG_TRIPLET}" CACHE STRING "")
        message(STATUS "XER deps: partial vcpkg at ${_xer_vcpkg_share}")
    else()
        message(STATUS "XER deps: no partial vcpkg share dir at ${_xer_vcpkg_share} "
                       "(heavy ports skia/ggml/folly/pthreadpool/fxdiv will be missing)")
    endif()
endif()

# ---------------------------------------------------------------------------
# Target-name compatibility aliases
#
# System packages sometimes ship the same library under a different CMake
# target name than the vcpkg port the code was written against. Resolve the
# real package once here and alias the expected name so downstream
# target_link_libraries() calls keep working.
# ---------------------------------------------------------------------------

# zstd: Debian ships zstd::libzstd_shared / zstd::libzstd_static, code links
# zstd::libzstd (vcpkg name).
find_package(zstd CONFIG QUIET)
if(TARGET zstd::libzstd_shared AND NOT TARGET zstd::libzstd)
    if(BUILD_SHARED_LIBS)
        add_library(zstd::libzstd ALIAS zstd::libzstd_shared)
    else()
        add_library(zstd::libzstd ALIAS zstd::libzstd_static)
    endif()
elseif(TARGET zstd::libzstd_static AND NOT TARGET zstd::libzstd)
    add_library(zstd::libzstd ALIAS zstd::libzstd_static)
endif()

# flatbuffers: Debian config is FlatBuffersConfig.cmake (capital B), the code
# asks for package "flatbuffers" and target flatbuffers::flatbuffers.
find_package(FlatBuffers QUIET)
foreach(_fb_target IN ITEMS flatbuffers::flatbuffers flatbuffers)
    if(TARGET "${_fb_target}" AND NOT TARGET flatbuffers::flatbuffers)
        if(NOT TARGET flatbuffers::flatbuffers)
            add_library(flatbuffers::flatbuffers ALIAS "${_fb_target}")
        endif()
    endif()
endforeach()

# OpenBLAS: distro configs export openblas::openblas (or similar); code links
# OpenBLAS::OpenBLAS.
find_package(OpenBLAS QUIET)
if(NOT TARGET OpenBLAS::OpenBLAS)
    foreach(_oblas_target IN ITEMS openblas::openblas OpenBLAS::blaser openblas)
        if(TARGET "${_oblas_target}")
            add_library(OpenBLAS::OpenBLAS ALIAS "${_oblas_target}")
            break()
        endif()
    endforeach()
endif()

# SPIRV-Tools: code links SPIRV-Tools-shared; distro exports
# SPIRV-Tools::{SPIRV-Tools,spirv-tools} style names.
find_package(SPIRV-Tools QUIET)
if(NOT TARGET SPIRV-Tools-shared)
    foreach(_spt_target IN ITEMS SPIRV-Tools::SPIRV-Tools SPIRV-Tools::spirv-tools SPIRV-Tools-shared SPIRV-Tools::SPIRV-Tools-shared)
        if(TARGET "${_spt_target}")
            add_library(SPIRV-Tools-shared ALIAS "${_spt_target}")
            break()
        endif()
    endforeach()
endif()

# RapidJSON: header-only; distro config only sets RAPIDJSON_INCLUDE_DIRS.
if(NOT TARGET RapidJSON)
    find_package(RapidJSON QUIET)
    if(RAPIDJSON_INCLUDE_DIRS)
        add_library(RapidJSON INTERFACE IMPORTED GLOBAL)
        set_target_properties(RapidJSON PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${RAPIDJSON_INCLUDE_DIRS}")
    endif()
endif()

# Leptonica: pkg-config only on Debian; code checks TARGET leptonica.
if(NOT TARGET leptonica)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(XER_LEPTONICA QUIET lept)
        if(XER_LEPTONICA_FOUND)
            add_library(leptonica INTERFACE IMPORTED GLOBAL)
            set_target_properties(leptonica PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${XER_LEPTONICA_INCLUDE_DIRS}"
                INTERFACE_LINK_LIBRARIES "${XER_LEPTONICA_LINK_LIBRARIES}"
                INTERFACE_LINK_DIRECTORIES "${XER_LEPTONICA_LIBRARY_DIRS}")
        endif()
    endif()
endif()

# Tesseract: pkg-config only on Debian; code checks TARGET Tesseract::libtesseract.
if(NOT TARGET Tesseract::libtesseract)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(XER_TESSERACT QUIET tesseract)
        if(XER_TESSERACT_FOUND)
            add_library(Tesseract::libtesseract INTERFACE IMPORTED GLOBAL)
            set_target_properties(Tesseract::libtesseract PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${XER_TESSERACT_INCLUDE_DIRS}"
                INTERFACE_LINK_LIBRARIES "${XER_TESSERACT_LINK_LIBRARIES}"
                INTERFACE_LINK_DIRECTORIES "${XER_TESSERACT_LIBRARY_DIRS}")
        endif()
    endif()
endif()

message(STATUS "XER deps: system provider active "
               "(configs=${CMAKE_CURRENT_LIST_DIR}/configs)")
