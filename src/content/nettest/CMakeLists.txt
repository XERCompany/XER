# XER Content: protocol message codec + async TCP client (nettest)

file(GLOB NETTEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
file(GLOB NETTEST_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_library(XER_ContentNetTest STATIC ${NETTEST_SOURCES} ${NETTEST_HEADERS})

target_include_directories(XER_ContentNetTest
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(XER_ContentNetTest
    PUBLIC
        XER_Network
        ZLIB::ZLIB
        re2::re2
        range-v3::range-v3
)

target_compile_features(XER_ContentNetTest PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_ContentNetTest PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_ContentNetTest PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
