# Shim config: maps vcpkg package name onto system packages.
include(CMakeFindDependencyMacro)
find_dependency(PkgConfig)
pkg_check_modules(XER_UNOFFICIAL_LIBMUPDF_0 REQUIRED mupdf)

if(XER_UNOFFICIAL_LIBMUPDF_0_FOUND)
    if(NOT TARGET unofficial::libmupdf::libmupdf)
        add_library(unofficial::libmupdf::libmupdf INTERFACE IMPORTED GLOBAL)
        set_target_properties(unofficial::libmupdf::libmupdf PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${XER_UNOFFICIAL_LIBMUPDF_0_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${XER_UNOFFICIAL_LIBMUPDF_0_LINK_LIBRARIES}"
            INTERFACE_LINK_DIRECTORIES "${XER_UNOFFICIAL_LIBMUPDF_0_LIBRARY_DIRS}")
    endif()
    set(unofficial-libmupdf_FOUND TRUE)
else()
    set(unofficial-libmupdf_FOUND FALSE)
endif()
