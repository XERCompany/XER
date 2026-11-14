# Shim config: maps vcpkg package name onto system packages.
include(CMakeFindDependencyMacro)
find_dependency(PkgConfig)
pkg_check_modules(XER_UNOFFICIAL_BROTLI_0 REQUIRED libbrotlidec)
pkg_check_modules(XER_UNOFFICIAL_BROTLI_1 REQUIRED libbrotlienc)
pkg_check_modules(XER_UNOFFICIAL_BROTLI_2 REQUIRED libbrotlicommon)

if(XER_UNOFFICIAL_BROTLI_0_FOUND AND XER_UNOFFICIAL_BROTLI_1_FOUND AND XER_UNOFFICIAL_BROTLI_2_FOUND)
    if(NOT TARGET unofficial::brotli::brotlidec)
        add_library(unofficial::brotli::brotlidec INTERFACE IMPORTED GLOBAL)
        set_target_properties(unofficial::brotli::brotlidec PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${XER_UNOFFICIAL_BROTLI_0_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${XER_UNOFFICIAL_BROTLI_0_LINK_LIBRARIES}"
            INTERFACE_LINK_DIRECTORIES "${XER_UNOFFICIAL_BROTLI_0_LIBRARY_DIRS}")
    endif()
    if(NOT TARGET unofficial::brotli::brotlienc)
        add_library(unofficial::brotli::brotlienc INTERFACE IMPORTED GLOBAL)
        set_target_properties(unofficial::brotli::brotlienc PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${XER_UNOFFICIAL_BROTLI_1_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${XER_UNOFFICIAL_BROTLI_1_LINK_LIBRARIES}"
            INTERFACE_LINK_DIRECTORIES "${XER_UNOFFICIAL_BROTLI_1_LIBRARY_DIRS}")
    endif()
    if(NOT TARGET unofficial::brotli::brotlicommon)
        add_library(unofficial::brotli::brotlicommon INTERFACE IMPORTED GLOBAL)
        set_target_properties(unofficial::brotli::brotlicommon PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${XER_UNOFFICIAL_BROTLI_2_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${XER_UNOFFICIAL_BROTLI_2_LINK_LIBRARIES}"
            INTERFACE_LINK_DIRECTORIES "${XER_UNOFFICIAL_BROTLI_2_LIBRARY_DIRS}")
    endif()
    set(unofficial-brotli_FOUND TRUE)
else()
    set(unofficial-brotli_FOUND FALSE)
endif()
