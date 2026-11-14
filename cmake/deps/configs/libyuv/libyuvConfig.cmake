# Shim: libyuv-dev has no cmake/pkg-config metadata; locate libyuv directly.
# The code links the plain name "yuv".
include(CMakeFindDependencyMacro)
find_path(XER_LIBYUV_INCLUDE_DIR NAMES libyuv/basic_types.h
    PATHS /usr/include /usr/local/include /opt/homebrew/include)
find_library(XER_LIBYUV_LIBRARY NAMES yuv libyuv)
if(XER_LIBYUV_INCLUDE_DIR AND XER_LIBYUV_LIBRARY)
    if(NOT TARGET yuv)
        add_library(yuv UNKNOWN IMPORTED GLOBAL)
        set_target_properties(yuv PROPERTIES
            IMPORTED_LOCATION "${XER_LIBYUV_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${XER_LIBYUV_INCLUDE_DIR}")
    endif()
    set(libyuv_FOUND TRUE)
else()
    set(libyuv_FOUND FALSE)
endif()
