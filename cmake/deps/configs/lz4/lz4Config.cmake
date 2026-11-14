# Shim config: maps vcpkg package name onto system packages.
include(CMakeFindDependencyMacro)
find_dependency(PkgConfig)
pkg_check_modules(XER_LZ4_0 QUIET liblz4)

if(XER_LZ4_0_FOUND)
    if(NOT TARGET LZ4::lz4_shared)
        add_library(LZ4::lz4_shared INTERFACE IMPORTED GLOBAL)
        set_target_properties(LZ4::lz4_shared PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${XER_LZ4_0_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${XER_LZ4_0_LINK_LIBRARIES}"
            INTERFACE_LINK_DIRECTORIES "${XER_LZ4_0_LIBRARY_DIRS}")
    endif()
    if(NOT TARGET LZ4::lz4)
        add_library(LZ4::lz4 ALIAS LZ4::lz4_shared)
    endif()
    set(lz4_FOUND TRUE)
else()
    set(lz4_FOUND FALSE)
endif()
