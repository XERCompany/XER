# Shim config: maps vcpkg package name onto system packages.
include(CMakeFindDependencyMacro)
find_dependency(PkgConfig)
pkg_check_modules(XER_UNOFFICIAL_SQLITE3_0 REQUIRED sqlite3)

if(XER_UNOFFICIAL_SQLITE3_0_FOUND)
    if(NOT TARGET unofficial::sqlite3::sqlite3)
        add_library(unofficial::sqlite3::sqlite3 INTERFACE IMPORTED GLOBAL)
        set_target_properties(unofficial::sqlite3::sqlite3 PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${XER_UNOFFICIAL_SQLITE3_0_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${XER_UNOFFICIAL_SQLITE3_0_LINK_LIBRARIES}"
            INTERFACE_LINK_DIRECTORIES "${XER_UNOFFICIAL_SQLITE3_0_LIBRARY_DIRS}")
    endif()
    set(unofficial-sqlite3_FOUND TRUE)
else()
    set(unofficial-sqlite3_FOUND FALSE)
endif()
