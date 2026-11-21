# tools/tz — command-line TZ packer and integrity verifier.
#
#   ztzmake    <dir> -o <out.tz|.xexrj> — pack a directory into TZ
#   ztzvertfy  <archive> [-r member] [-l] — verify crc + OVERFLOW
#   XSJOBuildX <dir> -o <lib.xejslib>  — compile a C++ library into
#              the .xejslib container (xejslib.h): XSJ1-wrapped COFF
#              modules + a .xejshead header bundle, which ztzmake
#              consumes via --dep / [native] deps / libs/*.xejslib.
#
# ztzmake also JIT-packs extension native code: *.cc/*.cpp/*.cxx
# sources selected via manifest.toml [native] sources (or auto-scanned
# under native/) are compiled IN-PROCESS through the Clang frontend
# into COFF objects packed as mobjective/<stem>.xsjo. That path needs
# libclang-cpp + LLVM from the MSYS2 toolchain; when they are absent
# the tool still builds (XER_ZTZ_HAS_CLANG unset) and reports a clear
# "compile not supported" error only if a package actually carries
# native sources.

add_executable(ztzmake ztzmake.cc xsjo_compile.cc xejslib.cc)
target_link_libraries(ztzmake PRIVATE XER_Tz)
set_target_properties(ztzmake PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(XSJOBuildX xsjobuildx.cc xsjo_compile.cc xejslib.cc)
target_link_libraries(XSJOBuildX PRIVATE XER_Tz)
set_target_properties(XSJOBuildX PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ztzmake + XSJOBuildX share the in-process Clang compile path and
# the toolchain discovery below.
set(_ztz_clang_tools ztzmake XSJOBuildX)

# Clang/LLVM from the same toolchain that compiles this tree. The
# package exports monolithic shared targets (clang-cpp, LLVM) —
# distro-style single-DLL linking, matching the import libs in
# clang64/lib. Include dirs come via LLVM_INCLUDE_DIRS/CLANG include
# dirs set by the configs; fall back to find_library/find_path for
# toolchains without CMake configs.
find_package(LLVM CONFIG QUIET)
find_package(Clang CONFIG QUIET)

set(_ztz_llvm_ok OFF)
if(TARGET clang-cpp AND TARGET LLVM)
    set(_ztz_llvm_ok ON)
    foreach(_t IN LISTS _ztz_clang_tools)
        target_link_libraries(${_t} PRIVATE clang-cpp LLVM)
        target_include_directories(${_t} SYSTEM PRIVATE
            ${LLVM_INCLUDE_DIRS})
    endforeach()
else()
    # No package configs — try bare import-lib discovery (still the
    # same single-DLL style: libclang-cpp.dll.a + libLLVM-*.dll.a).
    get_filename_component(_ztz_tc_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_ztz_tc_root "${_ztz_tc_bin}/.." ABSOLUTE)
    find_library(XER_ZTZ_CLANGCPP_LIB
        NAMES clang-cpp libclang-cpp.dll.a
        PATHS "${_ztz_tc_root}/lib" NO_DEFAULT_PATH)
    find_library(XER_ZTZ_LLVM_LIB
        NAMES LLVM LLVM-23 LLVM-21 LLVM-20 LLVM-19 LLVM-18 LLVM-17
              libLLVM libLLVM-23.dll.a
        PATHS "${_ztz_tc_root}/lib" NO_DEFAULT_PATH)
    find_path(XER_ZTZ_LLVM_INCLUDE llvm/Support/TargetSelect.h
        PATHS "${_ztz_tc_root}/include" NO_DEFAULT_PATH)
    if(XER_ZTZ_CLANGCPP_LIB AND XER_ZTZ_LLVM_LIB AND
       XER_ZTZ_LLVM_INCLUDE)
        set(_ztz_llvm_ok ON)
        foreach(_t IN LISTS _ztz_clang_tools)
            target_link_libraries(${_t} PRIVATE
                ${XER_ZTZ_CLANGCPP_LIB} ${XER_ZTZ_LLVM_LIB})
            target_include_directories(${_t} SYSTEM PRIVATE
                ${XER_ZTZ_LLVM_INCLUDE})
        endforeach()
        set(LLVM_INCLUDE_DIRS ${XER_ZTZ_LLVM_INCLUDE})
    endif()
endif()

if(_ztz_llvm_ok)
    # Export so tools/video_editor (added after tools/tz) can gate the
    # XSJOBuildX-driven .xejslib demo packaging on real Clang support.
    set(XER_ZTZ_HAS_CLANG ON CACHE INTERNAL
        "in-process Clang native packing available in ztzmake/XSJOBuildX")
    foreach(_t IN LISTS _ztz_clang_tools)
        target_compile_definitions(${_t} PRIVATE XER_ZTZ_HAS_CLANG=1)
    endforeach()
    # Bake the toolchain root in so xsjo_compile.cc can locate the
    # Clang resource dir + libc++/CRT headers at pack time. Derive it
    # from the compiler location (clang64/bin -> clang64).
    get_filename_component(_ztz_tc_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_ztz_tc_root "${_ztz_tc_bin}/.." ABSOLUTE)
    if(DEFINED Clang_DIR)
        get_filename_component(_ztz_clang_prefix
            "${Clang_DIR}/../../.." ABSOLUTE)
        if(EXISTS "${_ztz_clang_prefix}/bin/clang++.exe")
            set(_ztz_tc_root "${_ztz_clang_prefix}")
        endif()
    endif()
    foreach(_t IN LISTS _ztz_clang_tools)
        target_compile_definitions(${_t} PRIVATE
            "XER_XSJO_CLANG_PREFIX=\"${_ztz_tc_root}\"")
    endforeach()
    message(STATUS "ztzmake/XSJOBuildX: in-process Clang native "
                   "packing enabled (${_ztz_tc_root})")
    # ztzmake runs inside builds (packing .xexrj assets) — the LLVM /
    # libclang-cpp runtime DLLs must sit next to the exe or it dies
    # with exit 127 whenever clang64/bin is not on PATH.
    if(MINGW AND COMMAND xer_stage_runtime_dlls)
        file(GLOB _ztz_llvm_dlls
             "${_ztz_tc_bin}/libLLVM*.dll"
             "${_ztz_tc_bin}/libclang-cpp.dll")
        set(_ztz_seeds)
        foreach(_f IN LISTS _ztz_llvm_dlls)
            get_filename_component(_n "${_f}" NAME)
            list(APPEND _ztz_seeds "${_n}")
        endforeach()
        if(_ztz_seeds)
            xer_stage_runtime_dlls("${CMAKE_BINARY_DIR}/bin"
                                   "${_ztz_tc_bin}" ${_ztz_seeds})
        endif()
    endif()
else()
    message(STATUS "ztzmake: Clang/LLVM not found — native .xsjo "
                   "packing disabled (archives stay source-less)")
endif()

add_executable(ztzvertfy ztzvertfy.cc)
target_link_libraries(ztzvertfy PRIVATE XER_Tz)
set_target_properties(ztzvertfy PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
