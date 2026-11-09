# XER Content: password generator (CNG RNG, character-class policies)

file(GLOB PASSGEN_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
file(GLOB PASSGEN_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_library(XER_ContentPasswordGen STATIC ${PASSGEN_SOURCES} ${PASSGEN_HEADERS})

target_include_directories(XER_ContentPasswordGen
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_compile_features(XER_ContentPasswordGen PUBLIC cxx_std_23)

if(WIN32)
    target_link_libraries(XER_ContentPasswordGen PUBLIC bcrypt)
endif()

if(MSVC)
    target_compile_options(XER_ContentPasswordGen PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_ContentPasswordGen PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
