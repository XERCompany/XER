# Shim config: maps vcpkg package name onto system packages.
include(CMakeFindDependencyMacro)
find_dependency(PkgConfig)
pkg_check_modules(XER_RE2_0 REQUIRED re2)

if(XER_RE2_0_FOUND)
    if(NOT TARGET re2::re2)
        add_library(re2::re2 INTERFACE IMPORTED GLOBAL)
        set_target_properties(re2::re2 PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${XER_RE2_0_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${XER_RE2_0_LINK_LIBRARIES}"
            INTERFACE_LINK_DIRECTORIES "${XER_RE2_0_LIBRARY_DIRS}")
    endif()
    set(re2_FOUND TRUE)
else()
    set(re2_FOUND FALSE)
endif()
