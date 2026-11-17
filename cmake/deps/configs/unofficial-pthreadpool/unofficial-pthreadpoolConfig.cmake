# Shim: unofficial-pthreadpool has no MSYS2 package — vendor
# Maratyszcza/pthreadpool under third_party/pthreadpool (repo root, or a
# src/ subdir; see docs/wiki/research/toolchain-migration-audit.md). On
# Linux/macOS the partial-vcpkg share may provide the real port config
# instead; defer to it so this shim never shadows an authoritative config.
#
# Target contract mirrors the vcpkg port: unofficial::pthreadpool links
# unofficial::fxdiv::fxdiv (work-stealing division) and exposes
# pthreadpool.h on its include path.
set(XER_DEFER_PKG "unofficial-pthreadpool")
include("${CMAKE_CURRENT_LIST_DIR}/../xer_defer_vcpkg_config.cmake")

if(NOT XER_DEFER_CONFIG_LOADED)
    get_filename_component(_xer_pp_vendor
        "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/pthreadpool" ABSOLUTE)

    # pthreadpool is a tiny C library; when the vendored copy is present
    # we build it directly (same as the vcpkg port does). Two layouts are
    # recognized:
    #   * legacy single-file: pthreadpool.c (or src/pthreadpool.c) with
    #     pthreadpool.h beside it;
    #   * upstream >= 2024 split (what third_party/pthreadpool vendors):
    #     public header in include/, implementation across src/*.c —
    #     portable-api.c is the layout marker. src/shim.c is
    #     Emscripten-only and must NOT be compiled; legacy-api.c carries
    #     the pthreadpool_* v1 API that consumers call (upstream builds
    #     it by default via PTHREADPOOL_ALLOW_DEPRECATED_API=ON).
    set(_xer_pp_srcs "")
    foreach(_xer_pp_cand IN ITEMS
            "${_xer_pp_vendor}/pthreadpool.c"
            "${_xer_pp_vendor}/src/pthreadpool.c")
        if(EXISTS "${_xer_pp_cand}")
            set(_xer_pp_srcs "${_xer_pp_cand}")
            break()
        endif()
    endforeach()
    if(NOT _xer_pp_srcs AND EXISTS "${_xer_pp_vendor}/src/portable-api.c")
        set(_xer_pp_srcs
            "${_xer_pp_vendor}/src/portable-api.c"
            "${_xer_pp_vendor}/src/memory.c"
            "${_xer_pp_vendor}/src/pthreads.c"
            "${_xer_pp_vendor}/src/fastpath.c"
            "${_xer_pp_vendor}/src/legacy-api.c")
    endif()

    if(_xer_pp_srcs)
        if(NOT TARGET unofficial::pthreadpool)
            # Real target gets a plain name: "::" names are reserved for
            # ALIAS/IMPORTED targets (CMake >= 4 errors on add_library with
            # a "::" name). The vcpkg-facing name is provided via ALIAS.
            add_library(xer_pthreadpool STATIC ${_xer_pp_srcs})
            # Public header lives in include/ for the split layout; the
            # single-file layout keeps pthreadpool.h next to the source.
            set(_xer_pp_inc "${_xer_pp_vendor}/include")
            if(NOT EXISTS "${_xer_pp_inc}/pthreadpool.h")
                get_filename_component(_xer_pp_inc "${_xer_pp_srcs}" DIRECTORY)
            endif()
            target_include_directories(xer_pthreadpool
                PUBLIC "${_xer_pp_inc}")
            set_target_properties(xer_pthreadpool PROPERTIES
                POSITION_INDEPENDENT_CODE ON)
            target_compile_features(xer_pthreadpool PRIVATE c_std_11)
            # Upstream CMakeLists.txt appends /experimental:c11atomics for
            # MSVC — threadpool-atomics.h includes <stdatomic.h> and MSVC's
            # C mode defines __STDC_NO_ATOMICS__ without it. clang/clang-cl
            # and mingw have working C11 atomics and need nothing.
            target_compile_options(xer_pthreadpool PRIVATE
                $<$<C_COMPILER_ID:MSVC>:/experimental:c11atomics>)
            # Upstream defaults to C11 <threads.h> on _WIN32, but MinGW
            # (winpthreads) ships only <pthread.h> — force the POSIX path.
            if(MINGW)
                target_compile_definitions(xer_pthreadpool PRIVATE
                    PTHREADPOOL_USE_PTHREADS=1)
            endif()
            find_package(Threads QUIET)
            if(TARGET Threads::Threads)
                target_link_libraries(xer_pthreadpool
                    PUBLIC Threads::Threads)
            endif()
            add_library(unofficial::pthreadpool ALIAS xer_pthreadpool)
        endif()
        set(unofficial-pthreadpool_FOUND TRUE)
    else()
        # No vendored copy — last resort: a system-installed library
        # (pkg-config is not probed: no pthreadpool .pc exists anywhere).
        find_path(XER_PTHREADPOOL_INCLUDE_DIR NAMES pthreadpool.h)
        find_library(XER_PTHREADPOOL_LIBRARY NAMES pthreadpool)
        if(XER_PTHREADPOOL_INCLUDE_DIR AND XER_PTHREADPOOL_LIBRARY)
            if(NOT TARGET unofficial::pthreadpool)
                add_library(unofficial::pthreadpool UNKNOWN IMPORTED GLOBAL)
                set_target_properties(unofficial::pthreadpool PROPERTIES
                    IMPORTED_LOCATION "${XER_PTHREADPOOL_LIBRARY}"
                    INTERFACE_INCLUDE_DIRECTORIES
                        "${XER_PTHREADPOOL_INCLUDE_DIR}")
            endif()
            set(unofficial-pthreadpool_FOUND TRUE)
        else()
            set(unofficial-pthreadpool_FOUND FALSE)
        endif()
    endif()

    # fxdiv is a hard dependency of pthreadpool (the vcpkg port links
    # unofficial::fxdiv::fxdiv); resolve it through the same shim chain.
    # IMPORTED targets reject target_link_libraries(PUBLIC) — their link
    # interface must go through INTERFACE_LINK_LIBRARIES instead.
    if(unofficial-pthreadpool_FOUND)
        find_package(unofficial-fxdiv CONFIG QUIET)
        if(TARGET unofficial::fxdiv::fxdiv AND TARGET unofficial::pthreadpool)
            get_target_property(_xer_pp_is_imported
                unofficial::pthreadpool IMPORTED)
            if(_xer_pp_is_imported)
                set_target_properties(unofficial::pthreadpool PROPERTIES
                    INTERFACE_LINK_LIBRARIES "unofficial::fxdiv::fxdiv")
            else()
                target_link_libraries(unofficial::pthreadpool
                    PUBLIC unofficial::fxdiv::fxdiv)
            endif()
        endif()
    endif()
else()
    set(unofficial-pthreadpool_FOUND TRUE)
endif()

unset(_xer_pp_vendor)
unset(_xer_pp_cand)
unset(_xer_pp_srcs)
unset(_xer_pp_inc)
unset(_xer_pp_is_imported)
