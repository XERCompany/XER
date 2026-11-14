# Shim: libxnnpack-dev ships xnnpack.h + libXNNPACK but no cmake/pkg-config.
include(CMakeFindDependencyMacro)
find_path(XER_XNNPACK_INCLUDE_DIR NAMES xnnpack.h
    PATHS /usr/include /usr/local/include /opt/homebrew/include)
find_library(XER_XNNPACK_LIBRARY NAMES XNNPACK xnnpack)
if(XER_XNNPACK_INCLUDE_DIR AND XER_XNNPACK_LIBRARY)
    if(NOT TARGET unofficial::xnnpack::XNNPACK)
        add_library(unofficial::xnnpack::XNNPACK UNKNOWN IMPORTED GLOBAL)
        set_target_properties(unofficial::xnnpack::XNNPACK PROPERTIES
            IMPORTED_LOCATION "${XER_XNNPACK_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${XER_XNNPACK_INCLUDE_DIR}")
    endif()
    set(unofficial-xnnpack_FOUND TRUE)
else()
    set(unofficial-xnnpack_FOUND FALSE)
endif()
