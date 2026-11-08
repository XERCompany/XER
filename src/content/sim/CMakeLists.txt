# XER Content: crowd/traffic simulator (agents, FSM, spatial hash,
# remote job dispatch over the existing network/async APIs)

file(GLOB SIM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
file(GLOB SIM_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_library(XER_ContentSim STATIC ${SIM_SOURCES} ${SIM_HEADERS})

target_include_directories(XER_ContentSim
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(XER_ContentSim
    PUBLIC
        absl::flat_hash_map
        range-v3::range-v3
        XER_ContentNetTest
        XER_AsyncIO
)

target_compile_features(XER_ContentSim PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_ContentSim PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_ContentSim PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
