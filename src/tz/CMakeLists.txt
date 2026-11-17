# src/tz — XER TZ archive format (indexed blob + OVERFLOW manifest).
# Used by the .xexrj extension packages, the ztzmake/ztzvertfy tools
# and the in-memory readers (extension host, xer-ext:// CEF scheme).

add_library(XER_Tz STATIC
    tz.cc
    tz.h
)

target_include_directories(XER_Tz PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(XER_Tz PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_Tz PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_Tz PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
