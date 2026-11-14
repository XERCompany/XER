# Shim config: maps vcpkg package name onto system packages.
include(CMakeFindDependencyMacro)
find_dependency(PkgConfig)
pkg_check_modules(XER_UNOFFICIAL_SHADERC_0 QUIET shaderc_combined)

if(XER_UNOFFICIAL_SHADERC_0_FOUND)
    if(NOT TARGET unofficial::shaderc::shaderc)
        add_library(unofficial::shaderc::shaderc INTERFACE IMPORTED GLOBAL)
        set_target_properties(unofficial::shaderc::shaderc PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${XER_UNOFFICIAL_SHADERC_0_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${XER_UNOFFICIAL_SHADERC_0_LINK_LIBRARIES}"
            INTERFACE_LINK_DIRECTORIES "${XER_UNOFFICIAL_SHADERC_0_LIBRARY_DIRS}")
    endif()
    if(NOT TARGET unofficial::shaderc::shaderc_util)
        add_library(unofficial::shaderc::shaderc_util INTERFACE IMPORTED GLOBAL)
        set_target_properties(unofficial::shaderc::shaderc_util PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${XER_UNOFFICIAL_SHADERC_0_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${XER_UNOFFICIAL_SHADERC_0_LINK_LIBRARIES}"
            INTERFACE_LINK_DIRECTORIES "${XER_UNOFFICIAL_SHADERC_0_LIBRARY_DIRS}")
    endif()
    set(unofficial-shaderc_FOUND TRUE)
else()
    set(unofficial-shaderc_FOUND FALSE)
endif()
