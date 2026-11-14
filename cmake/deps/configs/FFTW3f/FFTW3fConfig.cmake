# Shim config: maps vcpkg package name onto system packages.
include(CMakeFindDependencyMacro)
find_dependency(PkgConfig)
pkg_check_modules(XER_FFTW3F_0 REQUIRED fftw3f)

if(XER_FFTW3F_0_FOUND)
    if(NOT TARGET FFTW3::fftw3f)
        add_library(FFTW3::fftw3f INTERFACE IMPORTED GLOBAL)
        set_target_properties(FFTW3::fftw3f PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${XER_FFTW3F_0_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${XER_FFTW3F_0_LINK_LIBRARIES}"
            INTERFACE_LINK_DIRECTORIES "${XER_FFTW3F_0_LIBRARY_DIRS}")
    endif()
    set(FFTW3f_FOUND TRUE)
else()
    set(FFTW3f_FOUND FALSE)
endif()
