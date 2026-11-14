# FindFFMPEG.cmake — pkg-config based FFmpeg discovery for Linux/macOS.
#
# Stock CMake ships no FindFFMPEG module and vcpkg's copy lives in the
# toolchain, so this project-local module resolves libav*/libsw* through
# pkg-config (provided by Debian ffmpeg dev packages and Homebrew ffmpeg).
#
# Sets the same variables the vcpkg module exposes:
#   FFMPEG_FOUND, FFMPEG_INCLUDE_DIRS, FFMPEG_LIBRARY_DIRS, FFMPEG_LIBRARIES,
#   FFMPEG_VERSION
#
# Honors COMPONENTS: libavdevice libavfilter libavformat libavcodec
# libswresample libswscale libavutil (the set the project actually uses).

include(CMakeFindDependencyMacro)
find_dependency(PkgConfig)

set(_FFMPEG_ALL_COMPONENTS
    libavdevice libavfilter libavformat libavcodec
    libswresample libswscale libavutil)

if(NOT FFmpeg_FIND_COMPONENTS)
    set(FFmpeg_FIND_COMPONENTS ${_FFMPEG_ALL_COMPONENTS})
endif()

set(FFMPEG_LIBRARIES)
set(FFMPEG_INCLUDE_DIRS)
set(FFMPEG_LIBRARY_DIRS)
set(FFMPEG_VERSION "")
set(_FFMPEG_MISSING "")

foreach(_comp IN LISTS FFmpeg_FIND_COMPONENTS)
    string(TOUPPER "${_comp}" _COMP_UPPER)
    string(REPLACE "LIB" "" _COMP_PKG "${_comp}")
    # pkg-config module names match the library names (libavcodec -> libavcodec).
    pkg_check_modules(PC_${_COMP_UPPER} QUIET ${_comp})
    if(PC_${_COMP_UPPER}_FOUND)
        list(APPEND FFMPEG_LIBRARIES ${PC_${_COMP_UPPER}_LINK_LIBRARIES})
        list(APPEND FFMPEG_INCLUDE_DIRS ${PC_${_COMP_UPPER}_INCLUDE_DIRS})
        list(APPEND FFMPEG_LIBRARY_DIRS ${PC_${_COMP_UPPER}_LIBRARY_DIRS})
        if(NOT FFMPEG_VERSION AND PC_${_COMP_UPPER}_VERSION)
            set(FFMPEG_VERSION "${PC_${_COMP_UPPER}_VERSION}")
        endif()
    else()
        list(APPEND _FFMPEG_MISSING ${_comp})
    endif()
endforeach()

list(REMOVE_DUPLICATES FFMPEG_LIBRARIES FFMPEG_INCLUDE_DIRS FFMPEG_LIBRARY_DIRS)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFMPEG
    FOUND_VAR FFMPEG_FOUND
    REQUIRED_VARS FFMPEG_LIBRARIES FFMPEG_INCLUDE_DIRS
    VERSION_VAR FFMPEG_VERSION
    FAIL_MESSAGE "Could not find FFmpeg via pkg-config. Install the FFmpeg development package (apt: libavcodec-dev libavformat-dev libavutil-dev libavfilter-dev libswscale-dev libswresample-dev; brew: ffmpeg).")

if(NOT FFMPEG_FOUND AND _FFMPEG_MISSING)
    message(STATUS "FFMPEG: missing pkg-config modules: ${_FFMPEG_MISSING}")
endif()
