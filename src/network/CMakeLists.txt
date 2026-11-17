# XER NetClient — small sync HTTP facade over content/network's
# Http::SyncRequest (net_client.h/.cc). Consumed by the video editor's
# app_super_net V8 module and any caller that wants request/response
# without the async stack.

file(GLOB NETCLIENT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
file(GLOB NETCLIENT_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(XER_NetClient STATIC ${NETCLIENT_SOURCES} ${NETCLIENT_HEADERS})

# Sibling style (content/network): self dir for "net_client.h" plus the
# src/ root so "network/net_client.h" and "content/network/..." resolve.
target_include_directories(XER_NetClient
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(XER_NetClient
    PUBLIC
        XER_Network
)

target_compile_features(XER_NetClient PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_NetClient PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_NetClient PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
