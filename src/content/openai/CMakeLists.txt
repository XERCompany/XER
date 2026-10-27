# OpenAI Content Subsystem CMake Configuration

# Collect OpenAI sources
file(GLOB_RECURSE OPENAI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)

file(GLOB_RECURSE OPENAI_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

# Create OpenAI library
add_library(XER_OpenAI STATIC ${OPENAI_SOURCES} ${OPENAI_HEADERS})

target_include_directories(XER_OpenAI
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# Link with XER dependencies
target_link_libraries(XER_OpenAI
    PUBLIC
        XER_AsyncIO
        XER_AudioCore
        XER_AudioDSP
        XER_Network
        absl::strings
        absl::synchronization
        absl::flat_hash_map
        absl::flat_hash_set
        re2::re2
)

# Link with jsoncpp from vcpkg
target_link_libraries(XER_OpenAI PUBLIC jsoncpp_lib)
target_compile_definitions(XER_OpenAI PRIVATE OPENAI_JSONCPP_AVAILABLE=1)

# Link with OpenSSL from vcpkg
if(TARGET OpenSSL::SSL AND TARGET OpenSSL::Crypto)
    target_link_libraries(XER_OpenAI PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

# Link with zlib from vcpkg
target_link_libraries(XER_OpenAI PUBLIC ZLIB::ZLIB)

# Link with zstd from vcpkg
target_link_libraries(XER_OpenAI PUBLIC zstd::libzstd)

# Platform-specific linking
if(WIN32)
    target_link_libraries(XER_OpenAI
        PUBLIC
            ws2_32
            crypt32
    )
endif()

# C++ standard
target_compile_features(XER_OpenAI PUBLIC cxx_std_23)

# Compiler flags
if(MSVC)
    target_compile_options(XER_OpenAI PRIVATE /W4 /WX-)
else()
    target_compile_options(XER_OpenAI PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Install
install(TARGETS XER_OpenAI
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES ${OPENAI_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/content/openai
)
