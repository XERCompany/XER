
# XER SQL CMake Configuration

file(GLOB_RECURSE SQL_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
file(GLOB_RECURSE SQL_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(XER_SQL STATIC
    ${SQL_SOURCES}
    ${SQL_HEADERS}
)

target_include_directories(XER_SQL
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(XER_SQL PUBLIC cxx_std_23)

target_link_libraries(XER_SQL
    PUBLIC
        unofficial::sqlite3::sqlite3
        absl::strings
        absl::synchronization
)

# Compiler flags
if(MSVC)
    target_compile_options(XER_SQL PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_SQL PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

# Platform-specific flags
if(WIN32)
    target_compile_definitions(XER_SQL PRIVATE _USE_MATH_DEFINES)
endif()

# Expose SQL integration to the main app when needed
target_link_libraries(xer PRIVATE XER_SQL)
