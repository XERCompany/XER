# Shim config: maps vcpkg package name onto system packages.
include(CMakeFindDependencyMacro)
find_dependency(PkgConfig)
pkg_check_modules(XER_MKL_0 REQUIRED mkl-dynamic-lp64-seq)

if(XER_MKL_0_FOUND)
    if(NOT TARGET MKL::MKL)
        add_library(MKL::MKL INTERFACE IMPORTED GLOBAL)
        set_target_properties(MKL::MKL PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${XER_MKL_0_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${XER_MKL_0_LINK_LIBRARIES}"
            INTERFACE_LINK_DIRECTORIES "${XER_MKL_0_LIBRARY_DIRS}")
    endif()
    set(MKL_FOUND TRUE)
else()
    set(MKL_FOUND FALSE)
endif()
