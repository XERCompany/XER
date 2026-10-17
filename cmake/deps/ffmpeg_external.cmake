if(NOT XER_FETCH_DEPENDENCIES OR NOT ENABLE_MEDIA)
    return()
endif()

set(XER_FFMPEG_VERSION "9.0.2" CACHE STRING "FFmpeg source version")
set(XER_FFMPEG_BUILD_JOBS "4" CACHE STRING "Parallel jobs for the FFmpeg source build")

include(ExternalProject)
set(_xer_ffmpeg_install "${XER_DEPS_DIR}/ffmpeg-install")
set(_xer_ffmpeg_components avdevice avfilter avformat avcodec swscale swresample avutil)
set(_xer_ffmpeg_archives)
foreach(_component IN LISTS _xer_ffmpeg_components)
    list(APPEND _xer_ffmpeg_archives "${_xer_ffmpeg_install}/lib/lib${_component}.a")
endforeach()

set(_xer_ffmpeg_external_args
    GIT_REPOSITORY https://git.ffmpeg.org/ffmpeg.git
    GIT_TAG "n${XER_FFMPEG_VERSION}"
    GIT_SHALLOW TRUE
    PREFIX "${XER_DEPS_DIR}/ffmpeg-external"
    SOURCE_DIR "${XER_DEPS_DIR}/ffmpeg-src"
    BINARY_DIR "${XER_DEPS_DIR}/ffmpeg-build"
    INSTALL_DIR "${_xer_ffmpeg_install}")

if(UNIX)
    find_program(XER_FFMPEG_MAKE_EXECUTABLE NAMES gmake make)
endif()

if(UNIX AND XER_FFMPEG_MAKE_EXECUTABLE)
    find_package(Threads REQUIRED)
    list(APPEND _xer_ffmpeg_external_args
        CONFIGURE_COMMAND
            "${CMAKE_COMMAND}" -E env
            "CC=${CMAKE_C_COMPILER}"
            "CXX=${CMAKE_CXX_COMPILER}"
            "<SOURCE_DIR>/configure"
            "--prefix=<INSTALL_DIR>"
            --enable-static
            --disable-shared
            --enable-pic
            --disable-programs
            --disable-doc
            --disable-debug
            --disable-autodetect
            --disable-x86asm
            --enable-avcodec
            --enable-avformat
            --enable-avdevice
            --enable-avfilter
            --enable-swscale
            --enable-swresample
        BUILD_COMMAND "${XER_FFMPEG_MAKE_EXECUTABLE}" -j "${XER_FFMPEG_BUILD_JOBS}"
        INSTALL_COMMAND "${XER_FFMPEG_MAKE_EXECUTABLE}" install
        BUILD_BYPRODUCTS ${_xer_ffmpeg_archives})

    add_library(XERFFmpegLibraries INTERFACE)
    target_include_directories(XERFFmpegLibraries INTERFACE
        "${_xer_ffmpeg_install}/include")
    target_link_libraries(XERFFmpegLibraries INTERFACE
        ${_xer_ffmpeg_archives}
        Threads::Threads
        ${CMAKE_DL_LIBS}
        m)

    set(XER_FFMPEG_SOURCE_ENABLED TRUE)
else()
    message(STATUS "FFmpeg sources will be downloaded to ${XER_DEPS_DIR}/ffmpeg-src; native build unavailable on this platform, using installed FFmpeg if present")
    list(APPEND _xer_ffmpeg_external_args
    CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E echo "FFmpeg configure skipped: GNU Make is unavailable"
    BUILD_COMMAND "${CMAKE_COMMAND}" -E echo "FFmpeg build skipped: GNU Make is unavailable"
    INSTALL_COMMAND "${CMAKE_COMMAND}" -E echo "FFmpeg install skipped: GNU Make is unavailable")
endif()

ExternalProject_Add(XERFFmpegExternal ${_xer_ffmpeg_external_args})

if(XER_FFMPEG_SOURCE_ENABLED)
    set(XER_FFMPEG_INCLUDE_DIRS "${_xer_ffmpeg_install}/include")
    set(XER_FFMPEG_LIBRARIES XERFFmpegLibraries)
endif()