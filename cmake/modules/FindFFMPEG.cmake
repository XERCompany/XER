include(FindPackageHandleStandardArgs)

set(FFMPEG_INCLUDE_DIRS)
set(FFMPEG_LIBRARIES)

foreach(_component IN ITEMS avutil avcodec avformat swscale swresample avfilter avdevice)
    string(TOUPPER "${_component}" _component_upper)
    set(FFMPEG_lib${_component}_FOUND FALSE)

    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG_${_component_upper} QUIET IMPORTED_TARGET GLOBAL lib${_component})
    endif()

    if(FFMPEG_${_component_upper}_FOUND)
        set(FFMPEG_lib${_component}_FOUND TRUE)
        list(APPEND FFMPEG_INCLUDE_DIRS ${FFMPEG_${_component_upper}_INCLUDE_DIRS})
        list(APPEND FFMPEG_LIBRARIES PkgConfig::FFMPEG_${_component_upper})
    else()
        find_path(FFMPEG_${_component_upper}_INCLUDE_DIR
            NAMES lib${_component}/${_component}.h
            PATH_SUFFIXES include)
        find_library(FFMPEG_${_component_upper}_LIBRARY
            NAMES ${_component} lib${_component})
        if(FFMPEG_${_component_upper}_INCLUDE_DIR AND FFMPEG_${_component_upper}_LIBRARY)
            set(FFMPEG_lib${_component}_FOUND TRUE)
            list(APPEND FFMPEG_INCLUDE_DIRS ${FFMPEG_${_component_upper}_INCLUDE_DIR})
            list(APPEND FFMPEG_LIBRARIES ${FFMPEG_${_component_upper}_LIBRARY})
        endif()
    endif()
endforeach()

list(REMOVE_DUPLICATES FFMPEG_INCLUDE_DIRS)
find_package_handle_standard_args(FFMPEG
    REQUIRED_VARS
        FFMPEG_INCLUDE_DIRS
        FFMPEG_libavutil_FOUND
        FFMPEG_libavcodec_FOUND
        FFMPEG_libavformat_FOUND
        FFMPEG_libswscale_FOUND
        FFMPEG_libswresample_FOUND)

mark_as_advanced(
    FFMPEG_AVUTIL_INCLUDE_DIR FFMPEG_AVUTIL_LIBRARY
    FFMPEG_AVCODEC_INCLUDE_DIR FFMPEG_AVCODEC_LIBRARY
    FFMPEG_AVFORMAT_INCLUDE_DIR FFMPEG_AVFORMAT_LIBRARY
    FFMPEG_SWSCALE_INCLUDE_DIR FFMPEG_SWSCALE_LIBRARY
    FFMPEG_SWRESAMPLE_INCLUDE_DIR FFMPEG_SWRESAMPLE_LIBRARY
    FFMPEG_AVFILTER_INCLUDE_DIR FFMPEG_AVFILTER_LIBRARY
    FFMPEG_AVDEVICE_INCLUDE_DIR FFMPEG_AVDEVICE_LIBRARY)