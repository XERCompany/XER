# Shim config: maps BZip2 onto the system bzip2 (pkg-config bzip2.pc).
include(CMakeFindDependencyMacro)
find_dependency(PkgConfig)
pkg_check_modules(XER_BZIP2_0 QUIET bzip2)

if(XER_BZIP2_0_FOUND)
    if(NOT TARGET BZip2::BZip2)
        add_library(BZip2::BZip2 INTERFACE IMPORTED GLOBAL)
        set_target_properties(BZip2::BZip2 PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${XER_BZIP2_0_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${XER_BZIP2_0_LINK_LIBRARIES}"
            INTERFACE_LINK_DIRECTORIES "${XER_BZIP2_0_LIBRARY_DIRS}")
    endif()
    set(BZip2_FOUND TRUE)
else()
    find_library(XER_BZ2_LIBRARY NAMES bz2)
    find_path(XER_BZ2_INCLUDE_DIR NAMES bzlib.h)
    if(XER_BZ2_LIBRARY AND XER_BZ2_INCLUDE_DIR)
        if(NOT TARGET BZip2::BZip2)
            add_library(BZip2::BZip2 INTERFACE IMPORTED GLOBAL)
            set_target_properties(BZip2::BZip2 PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${XER_BZ2_INCLUDE_DIR}"
                INTERFACE_LINK_LIBRARIES "${XER_BZ2_LIBRARY}")
        endif()
        set(BZip2_FOUND TRUE)
    else()
        set(BZip2_FOUND FALSE)
    endif()
endif()
set(BZIP2_FOUND ${BZip2_FOUND})
set(BZIP2_LIBRARIES BZip2::BZip2)
