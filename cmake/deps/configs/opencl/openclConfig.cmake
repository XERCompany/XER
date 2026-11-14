# Shim config: maps vcpkg package name onto system packages.
include(CMakeFindDependencyMacro)
find_dependency(PkgConfig)
pkg_check_modules(XER_OPENCL_0 QUIET OpenCL)

if(XER_OPENCL_0_FOUND)
    if(NOT TARGET OpenCL::OpenCL)
        add_library(OpenCL::OpenCL INTERFACE IMPORTED GLOBAL)
        set_target_properties(OpenCL::OpenCL PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${XER_OPENCL_0_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${XER_OPENCL_0_LINK_LIBRARIES}"
            INTERFACE_LINK_DIRECTORIES "${XER_OPENCL_0_LIBRARY_DIRS}")
    endif()
    find_path(XER_OPENCL_INC NAMES CL/cl.h OpenCL/cl.h
        PATHS /usr/include /usr/local/include /opt/homebrew/include)
    if(XER_OPENCL_INC AND NOT TARGET OpenCL::Headers)
        add_library(OpenCL::Headers INTERFACE IMPORTED GLOBAL)
        set_target_properties(OpenCL::Headers PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${XER_OPENCL_INC}")
    endif()
    set(opencl_FOUND TRUE)
else()
    set(opencl_FOUND FALSE)
endif()
