# CEFConfig.cmake — Chromium Embedded Framework binary distribution shim.
#
# Locates a manually-installed CEF binary distribution (no package manager
# carries it — InstallCef.ps1 downloads the Spotify build into
# third_party/cef/cef_windows64) and exposes:
#   CEF::libcef              — imported shared lib (libcef.lib -> libcef.dll)
#   CEF::libcef_dll_wrapper  — static wrapper compiled from shipped sources
#   CEF_ROOT / CEF_RELEASE_DIR / CEF_RESOURCES_DIR — for deployment steps
#   CEF_FOUND
#
# Layout expected (official cef_binary_*_windows64_minimal tarball):
#   <root>/include/cef_*.h
#   <root>/libcef_dll/{base,cpptoc,ctocpp,wrapper,...}
#   <root>/Release/libcef.{dll,lib}, bootstrap*.exe, *.dll
#   <root>/Resources/{*.pak,icudtl.dat,locales/}
#
# Missing/absent root -> CEF_FOUND=FALSE, callers degrade gracefully.

if(NOT DEFINED XER_CEF_ROOT)
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/cef/cef_windows64/include/cef_app.h")
        set(XER_CEF_ROOT "${CMAKE_SOURCE_DIR}/third_party/cef/cef_windows64")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/third_party/cef/cef_windows64_minimal/include/cef_app.h")
        set(XER_CEF_ROOT "${CMAKE_SOURCE_DIR}/third_party/cef/cef_windows64_minimal")
    endif()
endif()

if(NOT XER_CEF_ROOT OR NOT EXISTS "${XER_CEF_ROOT}/Release/libcef.lib")
    set(CEF_FOUND FALSE)
    if(NOT CEF_FIND_QUIETLY)
        message(STATUS "CEF: binary distribution not found "
                "(expected third_party/cef/cef_windows64) — HTML panels disabled")
    endif()
    return()
endif()

get_filename_component(CEF_ROOT "${XER_CEF_ROOT}" ABSOLUTE)
set(CEF_RELEASE_DIR "${CEF_ROOT}/Release")
set(CEF_RESOURCES_DIR "${CEF_ROOT}/Resources")
set(CEF_FOUND TRUE)

# ---------------------------------------------------------------- libcef --
if(NOT TARGET CEF::libcef)
    add_library(CEF::libcef SHARED IMPORTED)
    set_target_properties(CEF::libcef PROPERTIES
        IMPORTED_IMPLIB "${CEF_RELEASE_DIR}/libcef.lib"
        IMPORTED_LOCATION "${CEF_RELEASE_DIR}/libcef.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${CEF_ROOT};${CEF_ROOT}/include")
endif()

# ------------------------------------------------------ libcef_dll_wrapper -
# The C++ wrapper ships as source — compiled locally so it matches our
# toolchain/ABI (the distribution's own CMakeLists assumes MSVC flag style,
# so we compile the sources directly instead of add_subdirectory).
if(NOT TARGET CEF::libcef_dll_wrapper)
    file(GLOB_RECURSE _cef_wrapper_srcs
        "${CEF_ROOT}/libcef_dll/base/*.cc"
        "${CEF_ROOT}/libcef_dll/cpptoc/*.cc"
        "${CEF_ROOT}/libcef_dll/ctocpp/*.cc"
        "${CEF_ROOT}/libcef_dll/wrapper/*.cc"
        "${CEF_ROOT}/libcef_dll/*.cc")
    # Test/util sources that are not part of the runtime wrapper.
    list(FILTER _cef_wrapper_srcs EXCLUDE REGEX "/test/|_unittest|cef_dump_without_crashing")
    add_library(cef_dll_wrapper STATIC ${_cef_wrapper_srcs})
    add_library(CEF::libcef_dll_wrapper ALIAS cef_dll_wrapper)
    target_include_directories(cef_dll_wrapper PUBLIC
        "${CEF_ROOT}" "${CEF_ROOT}/include")
    target_compile_definitions(cef_dll_wrapper PUBLIC
        USING_CEF_SHARED WRAPPING_CEF_SHARED NOMINMAX
        WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(cef_dll_wrapper PRIVATE
            -Wno-unused-parameter -Wno-missing-field-initializers
            -fno-strict-aliasing)
    endif()
    # CEF headers want C++17+; the project is already at C++23.
    target_compile_features(cef_dll_wrapper PUBLIC cxx_std_17)
    target_link_libraries(cef_dll_wrapper PUBLIC CEF::libcef comctl32 rpcrt4 shlwapi ws2_32)
    set_target_properties(cef_dll_wrapper PROPERTIES FOLDER third_party)
endif()
