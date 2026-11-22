# tools/pe_deps — xer_pe_deps.exe, the build-time PE dependency
# scanner behind xer_stage_runtime_dlls(TARGET <t> SEARCH <dir>).
#
# It runs at POST_BUILD time, before anything has been staged into
# bin/, so it must not itself need a staged runtime: it links the
# STATIC LLVM object libraries (not libLLVM-*.dll) plus the same
# static libc++/mingw runtime selection as the YA modules, leaving
# only OS DLLs (KERNEL32/msvcrt) as imports.
#
# The static archives + headers come from the compiling toolchain
# (clang64 on the windows-clang-msys2 preset). When they are absent
# (MSVC/vcpkg presets, trimmed toolchain) the target is skipped and
# TARGET-mode staging quietly degrades to the seed-only behaviour.

get_filename_component(_pd_tc_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_filename_component(_pd_tc_root "${_pd_tc_bin}/.." ABSOLUTE)

find_path(XER_PEDEPS_LLVM_INCLUDE_DIR
    NAMES llvm/Object/COFF.h
    PATHS "${_pd_tc_root}/include"
    NO_DEFAULT_PATH)

set(_pd_llvm_libs "")
set(_pd_missing "")
# Static link set = `llvm-config --link-static --libs object` — the
# ObjectFile factory drags in every object format plus the IR reader
# the archive/symtab paths reference. Support needs z/zstd/xml2 for
# Compression.cpp; those must be the STATIC archives (not the .dll.a
# implibs) or the tool itself would need staged DLLs — the exact
# problem it exists to solve. Win32 import libs (ntdll/ole32/uuid/…)
# cover mapLastWindowsError and the COM helpers in Support/Path.cpp.
foreach(_pd_lib LLVMObject LLVMTextAPI LLVMMCParser LLVMIRReader
                LLVMAsmParser LLVMMC LLVMDebugInfoDWARFLowLevel
                LLVMBitReader LLVMCore LLVMRemarks LLVMBinaryFormat
                LLVMTargetParser LLVMBitstreamReader LLVMSupport
                LLVMDemangle)
    find_library(XER_PEDEPS_${_pd_lib}_LIBRARY
        NAMES ${_pd_lib}
        PATHS "${_pd_tc_root}/lib"
        NO_DEFAULT_PATH)
    if(XER_PEDEPS_${_pd_lib}_LIBRARY)
        list(APPEND _pd_llvm_libs "${XER_PEDEPS_${_pd_lib}_LIBRARY}")
    else()
        list(APPEND _pd_missing "${_pd_lib}")
    endif()
endforeach()
# Static compression/xml archives — resolve by explicit filename so a
# sibling .dll.a can never win the lookup.
foreach(_pd_lib z zstd xml2)
    find_library(XER_PEDEPS_${_pd_lib}_STATIC
        NAMES lib${_pd_lib}.a
        PATHS "${_pd_tc_root}/lib"
        NO_DEFAULT_PATH)
    if(XER_PEDEPS_${_pd_lib}_STATIC)
        list(APPEND _pd_llvm_libs "${XER_PEDEPS_${_pd_lib}_STATIC}")
    else()
        list(APPEND _pd_missing "lib${_pd_lib}.a")
    endif()
endforeach()
# MinGW import libs for the Win32 APIs Support calls. These are
# import stubs into OS DLLs, not runtime deps.
foreach(_pd_lib ntdll ole32 uuid psapi shell32 advapi32 ws2_32)
    find_library(XER_PEDEPS_${_pd_lib}_LIBRARY
        NAMES ${_pd_lib}
        PATHS "${_pd_tc_root}/lib"
             "${_pd_tc_root}/x86_64-w64-mingw32/lib"
        NO_DEFAULT_PATH)
    if(XER_PEDEPS_${_pd_lib}_LIBRARY)
        list(APPEND _pd_llvm_libs "${XER_PEDEPS_${_pd_lib}_LIBRARY}")
    endif()
endforeach()

if(XER_PEDEPS_LLVM_INCLUDE_DIR AND NOT _pd_missing)
    add_executable(xer_pe_deps pe_deps.cc)
    target_include_directories(xer_pe_deps SYSTEM PRIVATE
        ${XER_PEDEPS_LLVM_INCLUDE_DIR})
    # --start-group: the static archives have cyclic refs
    # (Object <-> BinaryFormat <-> Support).
    target_link_libraries(xer_pe_deps PRIVATE
        -Wl,--start-group ${_pd_llvm_libs} -Wl,--end-group)
    target_compile_features(xer_pe_deps PRIVATE cxx_std_23)
    set_target_properties(xer_pe_deps PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    # Static C++/mingw runtimes — the tool runs mid-build, before any
    # toolchain DLL is staged, so libc++.dll/libunwind/libwinpthread
    # must not be runtime imports either.
    if(MINGW AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_link_options(xer_pe_deps PRIVATE
            -Wl,-Bstatic,-lc++,-lc++abi,-lunwind,-lwinpthread,-Bdynamic)
    endif()
    if(MSVC)
        target_compile_options(xer_pe_deps PRIVATE /W4 /WX- /O2)
    else()
        target_compile_options(xer_pe_deps PRIVATE
            -Wall -Wextra -Wpedantic -O3)
    endif()
    message(STATUS "xer_pe_deps: computed PE staging enabled "
                   "(static LLVM object libs)")
else()
    message(STATUS "xer_pe_deps: LLVM static object libraries not found "
                   "(missing: ${_pd_missing}; headers: "
                   "${XER_PEDEPS_LLVM_INCLUDE_DIR}) — TARGET-mode "
                   "staging disabled, seed staging unchanged")
endif()
