# src/packaging — app bundler: PE dependency scan + staging tree +
# manifest + optional zip, used headless (xer_packager pack/scan CLI)
# and from the src/ui widget app.
#
#   xer_packaging  STATIC core lib — UI-free (usable headless).
#   xer_packager   GUI/CLI exe on src/ui widgets, CPU-rendered only
#                  (SoftwareRenderer pinned at runtime).
#
# Placed after tools/pe_deps so xer_stage_runtime_dlls(TARGET ...)
# computed staging applies to xer_packager.

# ---- core library -------------------------------------------------------

add_library(xer_packaging STATIC
    package_spec.h
    pe_imports.h
    pe_imports.cc
    dependency_scanner.h
    dependency_scanner.cc
    packager.h
    packager.cc
)

target_include_directories(xer_packaging
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# XER_JsonCpp: manifest.json; ZLIB: streaming crc32 of staged files.
target_link_libraries(xer_packaging
    PUBLIC
        XER_JsonCpp
        ZLIB::ZLIB
)

# Zip output rides the project's libarchive wrapper when the module is
# built; staging-tree-only builds keep working without it.
if(TARGET XER_Archive)
    target_link_libraries(xer_packaging PRIVATE XER_Archive)
    target_compile_definitions(xer_packaging
        PRIVATE XER_PACKAGING_HAS_ARCHIVE=1)
else()
    message(STATUS "xer_packaging: XER_Archive unavailable — "
                   "PackageFormat::Zip disabled")
endif()

target_compile_features(xer_packaging PUBLIC cxx_std_23)
if(MSVC)
    target_compile_options(xer_packaging PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(xer_packaging PRIVATE
        -Wall -Wextra -Wpedantic -O3)
endif()

# ---- GUI/CLI tool --------------------------------------------------------

# src/ui widgets are HWND-based today; the tool also runs headless
# (pack/scan/screenshot subcommands) so the exe is CLI-capable on the
# same binary, like video_editor.
if(WIN32 AND TARGET XER_UI_Core)
    add_executable(xer_packager
        ui/main.cc
        ui/packager_widget.h
        ui/packager_widget.cc
        ui/png_writer.h
        ui/png_writer.cc
    )
    target_include_directories(xer_packager PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
    )
    target_compile_features(xer_packager PRIVATE cxx_std_23)
    target_link_libraries(xer_packager PRIVATE
        xer_packaging
        XER_UI_Core
        comdlg32    # GetOpenFileNameW pickers
        shell32     # SHBrowseForFolderW folder picker
        ole32       # CoInitializeEx/CoTaskMemFree for the picker
    )
    set_target_properties(xer_packager PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    # Computed PE staging (xer_pe_deps POST_BUILD): stage exactly the
    # toolchain DLLs xer_packager.exe imports transitively — the same
    # contract video_editor/IDEXEJSO use.
    if(MINGW AND COMMAND xer_stage_runtime_dlls)
        get_filename_component(_pk_tc_bin "${CMAKE_CXX_COMPILER}"
                               DIRECTORY)
        xer_stage_runtime_dlls(TARGET xer_packager
                               SEARCH "${_pk_tc_bin}")
    endif()
else()
    message(STATUS "xer_packager: skipped (needs WIN32 + XER_UI_Core)")
endif()
