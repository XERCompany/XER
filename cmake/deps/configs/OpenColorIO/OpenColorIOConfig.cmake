# Shim config: OpenColorIO from the system package (MSYS2 / distro)
# via pkg-config → OpenColorIO::OpenColorIO.
include(CMakeFindDependencyMacro)
find_dependency(PkgConfig)
pkg_check_modules(XER_OCIO_PC QUIET OpenColorIO)

if(XER_OCIO_PC_FOUND)
    if(NOT TARGET OpenColorIO::OpenColorIO)
        add_library(OpenColorIO::OpenColorIO INTERFACE IMPORTED GLOBAL)
        set_target_properties(OpenColorIO::OpenColorIO PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${XER_OCIO_PC_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${XER_OCIO_PC_LINK_LIBRARIES}"
            INTERFACE_LINK_DIRECTORIES "${XER_OCIO_PC_LIBRARY_DIRS}")
    endif()
    set(OpenColorIO_FOUND TRUE)
    set(OpenColorIO_VERSION "${XER_OCIO_PC_VERSION}")
    message(STATUS "OpenColorIO ${OpenColorIO_VERSION}: system package")
else()
    set(OpenColorIO_FOUND FALSE)
endif()
