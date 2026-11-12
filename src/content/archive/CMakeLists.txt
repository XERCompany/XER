# XER Archive Subsystem CMake Configuration
#
# Universal archive manager backed by libarchive (vcpkg). Kept as its own
# library so libarchive stays an optional dependency: when the port is not
# installed, src/content/CMakeLists.txt excludes this directory from the
# XER_Content glob and ArchiveManager is not built.

find_package(LibArchive REQUIRED)

add_library(XER_Archive STATIC
    archive_manager.cc
    archive_manager.h
)

target_include_directories(XER_Archive
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(XER_Archive
    PUBLIC
        LibArchive::LibArchive
)

target_compile_features(XER_Archive PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_Archive PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_Archive PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
