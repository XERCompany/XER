# XER Content: open/save/directory dialogs ("opendialog")
#
# Native file pickers with embedded custom controls. The full
# implementation is Windows-only (IFileOpenDialog/IFileSaveDialog +
# IFileDialogCustomize widgets); other platforms compile a stub that
# reports "unsupported" at runtime — same headers, same call sites.

file(GLOB OPENDIALOG_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
file(GLOB OPENDIALOG_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_library(XER_ContentOpenDialog STATIC ${OPENDIALOG_SOURCES} ${OPENDIALOG_HEADERS})

target_include_directories(XER_ContentOpenDialog
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# XER_Helper provides the UTF-8 <-> UTF-16 conversion helpers.
target_link_libraries(XER_ContentOpenDialog PUBLIC XER_Helper)

if(WIN32)
    # COM item dialogs + shell items + legacy comdlg32 fallback.
    target_link_libraries(XER_ContentOpenDialog PRIVATE
        ole32 shell32 comdlg32 uuid)
endif()

target_compile_features(XER_ContentOpenDialog PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_ContentOpenDialog PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_ContentOpenDialog PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
