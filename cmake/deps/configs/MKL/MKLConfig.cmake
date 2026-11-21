# Shim config: maps vcpkg package name onto system packages.
include(CMakeFindDependencyMacro)
find_dependency(PkgConfig)
pkg_check_modules(XER_MKL_0 QUIET mkl-dynamic-lp64-seq)

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

# No pkg-config provider on Windows (MSYS2 ships no MKL at all): probe a
# manual Intel oneAPI install instead. MANUAL INSTALL: Intel oneAPI Base
# Toolkit (MKL component) — https://www.intel.com/content/www/us/en/
# developer/tools/oneapi/base-toolkit-download.html — installs to
# "C:/Program Files (x86)/Intel/oneAPI/mkl/latest" and exports
# %ONEAPI_ROOT% / %MKLROOT%. We link the single mkl_rt dispatcher lib;
# mkl_rt resolves mkl_core/mkl_def/mkl_avx* DLLs at run time, so the
# oneAPI redist dir must stay on PATH (lab/mkl_accel.cc probes the same
# DLLs directly and needs nothing at link time).
if(NOT MKL_FOUND AND WIN32)
    set(_xer_mkl_roots "")
    foreach(_e MKLROOT ONEAPI_ROOT)
        if(DEFINED ENV{${_e}} AND IS_DIRECTORY "$ENV{${_e}}")
            list(APPEND _xer_mkl_roots "$ENV{${_e}}")
        endif()
    endforeach()
    file(GLOB _xer_mkl_oneapi
        "C:/Program Files (x86)/Intel/oneAPI/mkl/*"
        "C:/Program Files/Intel/oneAPI/mkl/*")
    list(APPEND _xer_mkl_roots ${_xer_mkl_oneapi})

    find_path(XER_MKL_INCLUDE_DIR mkl.h
        HINTS ${_xer_mkl_roots}
        PATH_SUFFIXES include include/mkl)
    find_library(XER_MKL_RT_LIBRARY mkl_rt
        HINTS ${_xer_mkl_roots}
        PATH_SUFFIXES lib lib/intel64)

    if(XER_MKL_INCLUDE_DIR AND XER_MKL_RT_LIBRARY)
        if(NOT TARGET MKL::MKL)
            add_library(MKL::MKL UNKNOWN IMPORTED GLOBAL)
            set_target_properties(MKL::MKL PROPERTIES
                IMPORTED_LOCATION "${XER_MKL_RT_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${XER_MKL_INCLUDE_DIR}")
            # The oneAPI bin dir is never on PATH: an eager mkl_rt
            # import would kill every consumer exe at startup. Attach
            # the delay-load flag to the imported target itself so it
            # propagates to everything that links MKL::MKL (lld syntax
            # — the MSVC toolchain consumes MKL via vcpkg instead).
            # MKL_BIN_DIR is exported for the runtime-DLL staging pass
            # in tools/video_editor.
            get_filename_component(_xer_mkl_libdir
                "${XER_MKL_RT_LIBRARY}" DIRECTORY)
            file(GLOB _xer_mkl_rt_dlls
                "${_xer_mkl_libdir}/../bin/mkl_rt*.dll")
            if(_xer_mkl_rt_dlls)
                get_filename_component(MKL_BIN_DIR
                    "${_xer_mkl_libdir}/../bin" ABSOLUTE)
                if(MINGW)
                    foreach(_d IN LISTS _xer_mkl_rt_dlls)
                        get_filename_component(_n "${_d}" NAME)
                        set_property(TARGET MKL::MKL APPEND PROPERTY
                            INTERFACE_LINK_OPTIONS
                            "-Wl,--delayload,${_n}")
                    endforeach()
                endif()
            endif()
            unset(_xer_mkl_libdir)
            unset(_xer_mkl_rt_dlls)
        endif()
        set(MKL_FOUND TRUE)
    endif()
    unset(_xer_mkl_roots)
    unset(_xer_mkl_oneapi)
endif()
