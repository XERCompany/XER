# FindFFMPEG.cmake — pkg-config based FFmpeg discovery for Linux/macOS.
#
# Stock CMake ships no FindFFMPEG module and vcpkg's copy lives in the
# toolchain, so this project-local module resolves libav*/libsw* through
# pkg-config (provided by Debian ffmpeg dev packages and Homebrew ffmpeg).
#
# Sets the same variables the vcpkg module exposes:
#   FFMPEG_FOUND, FFMPEG_INCLUDE_DIRS, FFMPEG_LIBRARY_DIRS, FFMPEG_LIBRARIES,
#   FFMPEG_VERSION
#
# Honors COMPONENTS: libavdevice libavfilter libavformat libavcodec
# libswresample libswscale libavutil (the set the project actually uses).

include(CMakeFindDependencyMacro)
find_dependency(PkgConfig)

set(_FFMPEG_ALL_COMPONENTS
    libavdevice libavfilter libavformat libavcodec
    libswresample libswscale libavutil)

if(NOT FFmpeg_FIND_COMPONENTS)
    set(FFmpeg_FIND_COMPONENTS ${_FFMPEG_ALL_COMPONENTS})
endif()

set(FFMPEG_LIBRARIES)
set(FFMPEG_INCLUDE_DIRS)
set(FFMPEG_LIBRARY_DIRS)
set(FFMPEG_VERSION "")
set(_FFMPEG_MISSING "")

foreach(_comp IN LISTS FFmpeg_FIND_COMPONENTS)
    string(TOUPPER "${_comp}" _COMP_UPPER)
    string(REPLACE "LIB" "" _COMP_PKG "${_comp}")
    # pkg-config module names match the library names (libavcodec -> libavcodec).
    pkg_check_modules(PC_${_COMP_UPPER} QUIET ${_comp})
    if(PC_${_COMP_UPPER}_FOUND)
        list(APPEND FFMPEG_LIBRARIES ${PC_${_COMP_UPPER}_LINK_LIBRARIES})
        list(APPEND FFMPEG_INCLUDE_DIRS ${PC_${_COMP_UPPER}_INCLUDE_DIRS})
        list(APPEND FFMPEG_LIBRARY_DIRS ${PC_${_COMP_UPPER}_LIBRARY_DIRS})
        if(NOT FFMPEG_VERSION AND PC_${_COMP_UPPER}_VERSION)
            set(FFMPEG_VERSION "${PC_${_COMP_UPPER}_VERSION}")
        endif()
    else()
        list(APPEND _FFMPEG_MISSING ${_comp})
    endif()
endforeach()

list(REMOVE_DUPLICATES FFMPEG_LIBRARIES)
list(REMOVE_DUPLICATES FFMPEG_INCLUDE_DIRS)
list(REMOVE_DUPLICATES FFMPEG_LIBRARY_DIRS)

# Static FFmpeg (MSYS2 default): replace the .dll.a link set with the
# full pkg-config --static closure (libav*.a + every transitive dep:
# x264/x265/vpx/zlib/brotli/...) so produced binaries do not drag
# avutil-*.dll et al. Libs with a real .a go under -Bstatic; the few
# that only ship .dll.a (whisper, ggml, dl) stay dynamic.
# -lstdc++ is rewritten to -lc++ (libc++ toolchain).
# Set XER_FFMPEG_STATIC=OFF to keep DLL linking.
option(XER_FFMPEG_STATIC
       "Link FFmpeg static archives instead of av*.dll import libs" ${MINGW})
if(XER_FFMPEG_STATIC)
    set(_FFMPEG_STATIC_LIBS)
    set(_FFMPEG_STATIC_OK TRUE)
    foreach(_comp IN LISTS FFmpeg_FIND_COMPONENTS)
        string(TOUPPER "${_comp}" _COMP_UPPER)
        if(PC_${_COMP_UPPER}_FOUND)
            pkg_check_modules(PCS_${_COMP_UPPER} QUIET ${_comp})
            list(APPEND _FFMPEG_STATIC_LIBS ${PCS_${_COMP_UPPER}_STATIC_LDFLAGS})
        else()
            set(_FFMPEG_STATIC_OK FALSE)
        endif()
    endforeach()
    # ffmpeg's .pc files name direct deps (e.g. -lrsvg-2, -ljxl) without
    # their transitive Requires closure — expand the heavy ones through
    # their own .pc files.
    foreach(_m librsvg-2.0 libjxl libwebp libmpg123 gnutls libplacebo
              libsrt libssh libbluray fontconfig freetype2)
        string(REGEX REPLACE "[^A-Za-z0-9]" "_" _mk "${_m}")
        string(TOUPPER "${_mk}" _MK)
        pkg_check_modules(PCX_${_MK} QUIET ${_m})
        if(PCX_${_MK}_FOUND)
            list(APPEND _FFMPEG_STATIC_LIBS ${PCX_${_MK}_STATIC_LDFLAGS})
        endif()
    endforeach()
    if(_FFMPEG_STATIC_OK AND _FFMPEG_STATIC_LIBS)
        list(REMOVE_DUPLICATES _FFMPEG_STATIC_LIBS)
        set(_libdir "${PC_libavformat_LIBDIR}")
        foreach(_flag IN LISTS _FFMPEG_STATIC_LIBS)
            if(NOT _libdir AND _flag MATCHES "^-L(.+)")
                get_filename_component(_libdir "${CMAKE_MATCH_1}" ABSOLUTE)
            endif()
        endforeach()
        set(_FF_S)
        set(_FF_D)
        foreach(_flag IN LISTS _FFMPEG_STATIC_LIBS)
            if(_flag MATCHES "^-l(.+)")
                set(_n "${CMAKE_MATCH_1}")
                if(_n STREQUAL "stdc++")
                    set(_n "c++")
                endif()
                if(EXISTS "${_libdir}/lib${_n}.a")
                    # Absolute path, not -l: FFMPEG_LIBRARIES can appear
                    # twice on a link line with dedup stripping the
                    # -Bstatic markers of the second copy, silently
                    # flipping those libs to .dll.a.
                    list(APPEND _FF_S "${_libdir}/lib${_n}.a")
                elseif(EXISTS "${_libdir}/lib${_n}.dll.a")
                    list(APPEND _FF_D "-l${_n}")
                endif()
                # else: no archive at all (e.g. -ldl on MSYS2, where
                # ffmpeg's .pc carries a vestigial UNIX dep) — drop it.
            elseif(_flag MATCHES "^-L")
                list(APPEND _FF_S "${_flag}")
            endif()
        endforeach()
        list(REMOVE_DUPLICATES _FF_S)
        list(REMOVE_DUPLICATES _FF_D)
        if(WIN32)
            # Rust static archives in the closure (e.g. librsvg-2.a)
            # import ntdll internals (NtReadFile, RtlNtStatusToDosError)
            # that pkg-config never lists.
            list(APPEND _FF_D -lntdll)
        endif()
        # Requires.private gaps in MSYS2 .pc files: libjxl needs brotli
        # + jxl_cms + hwy, gnutls needs tasn1 + crypt32 + ncrypt, plus
        # OpenMP (__kmpc_*), libdovi, libunibreak — none reach the
        # closure. Vulkan ships only an import lib, so it stays dynamic.
        foreach(_n brotlienc brotlidec brotlicommon jxl_cms hwy tasn1
                  crypt32 ncrypt omp dovi unibreak
                  lcms2 zstd idn2 unistring charset avrt ssl crypto)
            if(EXISTS "${_libdir}/lib${_n}.a")
                list(APPEND _FF_S "${_libdir}/lib${_n}.a")
            endif()
        endforeach()
        if(EXISTS "${_libdir}/libvulkan-1.dll.a")
            list(APPEND _FF_D -lvulkan-1)
        endif()

        # XER_FFMPEG_YA_DLL: pack the static closure into shared
        # YA.FFmpeg.dll once instead of ~170 MB into every executable.
        # The public libav*/libsw* archives are whole-archived (full API
        # exported); libxml2/iconv/z/charset go to a separate
        # YA.LibXml2.dll that both FFmpeg internals and UI consumers
        # share; everything else is pulled on demand inside the DLL.
        include(${CMAKE_CURRENT_LIST_DIR}/YaDll.cmake)
        option(XER_FFMPEG_YA_DLL
               "Pack static FFmpeg closure into YA.FFmpeg.dll" ON)
        if(XER_FFMPEG_YA_DLL AND COMMAND xer_add_ya_dll)
            set(_ff_api)
            set(_ff_xml)
            set(_ff_deps)
            set(_ff_libdirs)
            foreach(_item IN LISTS _FF_S)
                if(_item MATCHES "^-L")
                    list(APPEND _ff_libdirs "${_item}")
                elseif(_item MATCHES
                       "lib(avdevice|avfilter|avformat|avcodec|swresample|swscale|avutil)\\.a$")
                    list(APPEND _ff_api "${_item}")
                elseif(_item MATCHES "lib(xml2|iconv|z|charset)\\.a$")
                    list(APPEND _ff_xml "${_item}")
                else()
                    list(APPEND _ff_deps "${_item}")
                endif()
            endforeach()
            if(NOT TARGET YA_LibXml2)
                xer_add_ya_dll(YA_LibXml2 EXPORT_NAME "YA.LibXml2"
                               API_ARCHIVES ${_ff_xml}
                               LIBS -lbcrypt)
            endif()
            # The FFmpeg closure itself is wrapped by
            # YA.MediaDecoderUniversal in src/wrapper/ffmpeg — stash the
            # classified archive lists for it; consumers just link the
            # DLL's import library.
            set(XER_FFMPEG_API_ARCHIVES "${_ff_api}" CACHE INTERNAL "")
            set(XER_FFMPEG_DEP_ARCHIVES "${_ff_deps}" CACHE INTERNAL "")
            set(XER_FFMPEG_DYN_LIBS "${_ff_libdirs};${_FF_D}"
                CACHE INTERNAL "")
            set(FFMPEG_LIBRARIES YA_MediaDecoderUniversal)
            list(LENGTH _ff_api _na)
            list(LENGTH _ff_deps _nd)
            list(LENGTH _FF_D _ndy)
            message(STATUS
                    "FFMPEG: YA.MediaDecoderUniversal.dll (${_na} api + ${_nd} dep static, ${_ndy} dynamic)")
        else()
            # --allow-multiple-definition: several archives in the
            # closure are Rust static libs (librsvg-2, librav1e) that
            # each export rust_eh_personality.
            set(FFMPEG_LIBRARIES -Wl,--allow-multiple-definition
                -Wl,--start-group ${_FF_S} -Wl,--end-group
                -Wl,-Bdynamic ${_FF_D})
            list(LENGTH _FF_S _ns)
            list(LENGTH _FF_D _nd)
            message(STATUS
                    "FFMPEG: static link enabled (${_ns} static + ${_nd} dynamic libs)")
        endif()
    else()
        message(STATUS
                "FFMPEG: static archives incomplete — keeping DLL import libs")
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFMPEG
    FOUND_VAR FFMPEG_FOUND
    REQUIRED_VARS FFMPEG_LIBRARIES FFMPEG_INCLUDE_DIRS
    VERSION_VAR FFMPEG_VERSION
    FAIL_MESSAGE "Could not find FFmpeg via pkg-config - install the FFmpeg development package (apt libavcodec-dev et al, brew ffmpeg).")

if(NOT FFMPEG_FOUND AND _FFMPEG_MISSING)
    message(STATUS "FFMPEG: missing pkg-config modules: ${_FFMPEG_MISSING}")
endif()
