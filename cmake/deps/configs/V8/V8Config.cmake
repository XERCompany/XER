# Shim config: maps the V8::V8 target onto the system V8 package
# (MSYS2 mingw-w64-clang-x86_64-v8 / distro v8) discovered through
# pkg-config. Replaces the old GN source-build integration for
# system-deps builds; vcpkg presets are unaffected.
include(CMakeFindDependencyMacro)
find_dependency(PkgConfig)
pkg_check_modules(XER_V8_PC QUIET v8 v8_libbase v8_libplatform)

if(XER_V8_PC_FOUND)
    if(NOT TARGET V8::V8)
        add_library(V8::V8 INTERFACE IMPORTED GLOBAL)
        set_target_properties(V8::V8 PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${XER_V8_PC_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${XER_V8_PC_LINK_LIBRARIES}"
            INTERFACE_LINK_DIRECTORIES "${XER_V8_PC_LIBRARY_DIRS}"
            # The MSYS2/distro V8 is built with pointer compression +
            # sandbox enabled; every embedder TU must see the same
            # build flags or V8 aborts at Isolate::New with an
            # "embedder-vs-V8 build configuration mismatch".
            INTERFACE_COMPILE_DEFINITIONS
                "V8_COMPRESS_POINTERS;V8_ENABLE_SANDBOX")
    endif()
    set(V8_FOUND TRUE)
    set(V8_VERSION "${XER_V8_PC_v8_VERSION}")
    message(STATUS "V8 ${V8_VERSION}: system package (pkg-config)")
else()
    set(V8_FOUND FALSE)
endif()
