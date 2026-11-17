# Shim config: OpenImageIO from the system package (MSYS2 / distro)
# via pkg-config → OpenImageIO::OpenImageIO.
include(CMakeFindDependencyMacro)
find_dependency(PkgConfig)
pkg_check_modules(XER_OIIO_PC QUIET OpenImageIO)

if(XER_OIIO_PC_FOUND)
    if(NOT TARGET OpenImageIO::OpenImageIO)
        add_library(OpenImageIO::OpenImageIO INTERFACE IMPORTED GLOBAL)
        set_target_properties(OpenImageIO::OpenImageIO PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${XER_OIIO_PC_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${XER_OIIO_PC_LINK_LIBRARIES}"
            INTERFACE_LINK_DIRECTORIES "${XER_OIIO_PC_LIBRARY_DIRS}")
    endif()
    set(OpenImageIO_FOUND TRUE)
    set(OpenImageIO_VERSION "${XER_OIIO_PC_VERSION}")
    message(STATUS "OpenImageIO ${OpenImageIO_VERSION}: system package")
else()
    set(OpenImageIO_FOUND FALSE)
endif()
