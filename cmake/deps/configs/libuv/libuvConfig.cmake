# Shim: libuv1-dev ships only pkg-config files; recreate the vcpkg target
# pair libuv::uv_a (static) + libuv::uv (shared).
include(CMakeFindDependencyMacro)
find_dependency(PkgConfig)
pkg_check_modules(XER_LIBUV REQUIRED libuv)

get_filename_component(_xer_uv_libdir "${XER_LIBUV_LIBRARY_DIRS}" DIRECTORY)
find_library(XER_LIBUV_A NAMES libuv.a PATHS "${XER_LIBUV_LIBRARY_DIRS}" NO_DEFAULT_PATH)
find_library(XER_LIBUV_SO NAMES libuv.so libuv PATHS "${XER_LIBUV_LIBRARY_DIRS}" NO_DEFAULT_PATH)

if(NOT TARGET libuv::uv_a)
    add_library(libuv::uv_a UNKNOWN IMPORTED GLOBAL)
    set_target_properties(libuv::uv_a PROPERTIES
        IMPORTED_LOCATION "${XER_LIBUV_A}"
        INTERFACE_INCLUDE_DIRECTORIES "${XER_LIBUV_INCLUDE_DIRS}")
endif()
if(NOT TARGET libuv::uv)
    add_library(libuv::uv UNKNOWN IMPORTED GLOBAL)
    set_target_properties(libuv::uv PROPERTIES
        IMPORTED_LOCATION "${XER_LIBUV_SO}"
        INTERFACE_INCLUDE_DIRECTORIES "${XER_LIBUV_INCLUDE_DIRS}")
endif()
set(libuv_FOUND TRUE)
