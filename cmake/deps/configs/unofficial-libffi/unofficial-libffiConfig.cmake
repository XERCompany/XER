# Shim config: maps vcpkg package name onto system packages.
include(CMakeFindDependencyMacro)
find_dependency(PkgConfig)
pkg_check_modules(XER_UNOFFICIAL_LIBFFI_0 QUIET libffi)

if(XER_UNOFFICIAL_LIBFFI_0_FOUND)
    if(NOT TARGET unofficial::libffi::libffi)
        add_library(unofficial::libffi::libffi INTERFACE IMPORTED GLOBAL)
        set_target_properties(unofficial::libffi::libffi PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${XER_UNOFFICIAL_LIBFFI_0_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${XER_UNOFFICIAL_LIBFFI_0_LINK_LIBRARIES}"
            INTERFACE_LINK_DIRECTORIES "${XER_UNOFFICIAL_LIBFFI_0_LIBRARY_DIRS}")
    endif()
    set(unofficial-libffi_FOUND TRUE)
else()
    set(unofficial-libffi_FOUND FALSE)
endif()
