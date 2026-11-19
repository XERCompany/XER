# Shim config: librsvg + cairo from the system package (MSYS2 / distro)
# via pkg-config → librsvg::librsvg.
include(CMakeFindDependencyMacro)
find_dependency(PkgConfig)
pkg_check_modules(XER_RSVG_PC QUIET librsvg-2.0 cairo)

if(XER_RSVG_PC_FOUND)
    # Static embed on MinGW: use the full pkg-config --static closure
    # (rsvg's Rust archive needs pango/cairo-gobject/glib/harfbuzz +
    # -lntdll which the plain -l list omits — they used to ride inside
    # FFMPEG_LIBRARIES, now packed into YA.FFmpeg.dll). Resolve each
    # -l to an absolute .a where it exists, keep .dll.a-only ones as -l.
    if(MINGW)
        set(_xer_rsvg_link_libs "")
        foreach(_xer_rsvg_flag IN LISTS XER_RSVG_PC_STATIC_LDFLAGS)
            if(_xer_rsvg_flag MATCHES "^-l(.+)$")
                set(_xer_rsvg_static "")
                foreach(_xer_rsvg_dir IN LISTS XER_RSVG_PC_STATIC_LIBRARY_DIRS)
                    if(EXISTS "${_xer_rsvg_dir}/lib${CMAKE_MATCH_1}.a")
                        set(_xer_rsvg_static "${_xer_rsvg_dir}/lib${CMAKE_MATCH_1}.a")
                    endif()
                endforeach()
                if(_xer_rsvg_static)
                    list(APPEND _xer_rsvg_link_libs "${_xer_rsvg_static}")
                else()
                    set(_xer_rsvg_dyn "")
                    foreach(_xer_rsvg_dir IN LISTS XER_RSVG_PC_STATIC_LIBRARY_DIRS)
                        if(EXISTS "${_xer_rsvg_dir}/lib${CMAKE_MATCH_1}.dll.a")
                            set(_xer_rsvg_dyn TRUE)
                        endif()
                    endforeach()
                    if(_xer_rsvg_dyn)
                        list(APPEND _xer_rsvg_link_libs "${_xer_rsvg_flag}")
                    endif()
                    # else: no archive at all (e.g. -ldl vestige) — drop.
                endif()
            else()
                list(APPEND _xer_rsvg_link_libs "${_xer_rsvg_flag}")
            endif()
        endforeach()
        # Rust-archive internals (NtReadFile/Rtl*), libxml2's crypto.
        list(APPEND _xer_rsvg_link_libs -lntdll -lbcrypt)
        set(XER_RSVG_PC_LINK_LIBRARIES "${_xer_rsvg_link_libs}")
    endif()
    if(NOT TARGET librsvg::librsvg)
        add_library(librsvg::librsvg INTERFACE IMPORTED GLOBAL)
        set_target_properties(librsvg::librsvg PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${XER_RSVG_PC_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${XER_RSVG_PC_LINK_LIBRARIES}"
            INTERFACE_LINK_DIRECTORIES "${XER_RSVG_PC_LIBRARY_DIRS}")
    endif()
    set(librsvg_FOUND TRUE)
    set(librsvg_VERSION "${XER_RSVG_PC_VERSION}")
    message(STATUS "librsvg ${librsvg_VERSION}: system package")
else()
    set(librsvg_FOUND FALSE)
endif()
