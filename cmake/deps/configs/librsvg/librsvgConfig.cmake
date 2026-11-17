# Shim config: librsvg + cairo from the system package (MSYS2 / distro)
# via pkg-config → librsvg::librsvg.
include(CMakeFindDependencyMacro)
find_dependency(PkgConfig)
pkg_check_modules(XER_RSVG_PC QUIET librsvg-2.0 cairo)

if(XER_RSVG_PC_FOUND)
    if(NOT TARGET librsvg::librsvg)
        add_library(librsvg::librsvg INTERFACE IMPORTED GLOBAL)
        set_target_properties(librsvg::librsvg PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${XER_RSVG_PC_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${XER_RSVG_PC_LINK_LIBRARIES}"
            INTERFACE_LINK_DIRECTORIES "${XER_RSVG_PC_LIBRARY_DIRS}")
    endif()
    set(librsvg_FOUND TRUE)
    set(librsvg_VERSION "${XER_RSVG_PC_VERSION}")
    message(STATUS "librsvg ${librsvg_VERSION}: system package")
else()
    set(librsvg_FOUND FALSE)
endif()
