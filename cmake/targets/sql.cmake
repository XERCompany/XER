if(NOT SQLite3_FOUND OR NOT TARGET SQLite::SQLite3)
    message(STATUS "SQLite3 not found: SQL storage target disabled")
    return()
endif()

file(GLOB_RECURSE ENGINE_SQL_CC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sql/*.cc")
file(GLOB_RECURSE ENGINE_SQL_H CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sql/*.h")

add_library(XERSQL STATIC ${ENGINE_SQL_CC} ${ENGINE_SQL_H})
target_include_directories(XERSQL PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    ${SQLite3_INCLUDE_DIRS})
target_link_libraries(XERSQL PUBLIC SQLite::SQLite3)