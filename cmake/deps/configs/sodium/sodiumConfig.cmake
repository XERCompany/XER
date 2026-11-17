# Shim config: maps sodium onto the system libsodium (pkg-config libsodium.pc).
include(CMakeFindDependencyMacro)
find_dependency(PkgConfig)
pkg_check_modules(XER_SODIUM_0 QUIET libsodium)

if(XER_SODIUM_0_FOUND)
    if(NOT TARGET sodium)
        add_library(sodium INTERFACE IMPORTED GLOBAL)
        set_target_properties(sodium PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${XER_SODIUM_0_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${XER_SODIUM_0_LINK_LIBRARIES}"
            INTERFACE_LINK_DIRECTORIES "${XER_SODIUM_0_LIBRARY_DIRS}")
    endif()
    if(NOT TARGET sodium::sodium)
        add_library(sodium::sodium ALIAS sodium)
    endif()
    set(sodium_FOUND TRUE)
else()
    find_library(XER_SODIUM_LIBRARY NAMES sodium libsodium)
    find_path(XER_SODIUM_INCLUDE_DIR NAMES sodium.h)
    if(XER_SODIUM_LIBRARY AND XER_SODIUM_INCLUDE_DIR)
        if(NOT TARGET sodium)
            add_library(sodium INTERFACE IMPORTED GLOBAL)
            set_target_properties(sodium PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${XER_SODIUM_INCLUDE_DIR}"
                INTERFACE_LINK_LIBRARIES "${XER_SODIUM_LIBRARY}")
        endif()
        set(sodium_FOUND TRUE)
    else()
        set(sodium_FOUND FALSE)
    endif()
endif()
