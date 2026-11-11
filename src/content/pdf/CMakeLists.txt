# XER PDF Subsystem CMake Configuration
#
# PDF reading, editing, creation and rendering (bitmap + HWND/HDC),
# backed by MuPDF (vcpkg port "libmupdf"). Kept as its own library so
# MuPDF stays an optional dependency: when the port is not installed,
# exclude src/content/pdf from the build (same pattern as
# src/content/archive with LibArchive).
#
# Root CMakeLists.txt wiring (add near the other src/content/*
# add_subdirectory lines, inside an availability guard):
#
#   find_package(unofficial-libmupdf CONFIG QUIET)
#   if(TARGET unofficial::libmupdf::libmupdf)
#       add_subdirectory(src/content/pdf)
#   endif()

find_package(unofficial-libmupdf CONFIG REQUIRED)

add_library(XER_ContentPdf STATIC
    pdf_document.h
    pdf_internal.h
    pdf_document.cc
    pdf_content.cc
    pdf_edit.cc
    pdf_render.cc
)

target_include_directories(XER_ContentPdf
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(XER_ContentPdf
    PUBLIC
        unofficial::libmupdf::libmupdf
)

target_compile_features(XER_ContentPdf PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_ContentPdf PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_ContentPdf PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

if(WIN32)
    # GDI blit path (StretchDIBits / AlphaBlend) used by
    # RenderPageToHdc / RenderPageToHwnd.
    target_link_libraries(XER_ContentPdf PRIVATE gdi32 msimg32)
endif()
