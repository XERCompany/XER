include(FindPackageHandleStandardArgs)

find_path(LibNode_INCLUDE_DIR
    NAMES v8.h
    HINTS ${LibNode_ROOT} ENV LIBNODE_ROOT
    PATH_SUFFIXES include include/node node)

find_library(LibNode_LIBRARY
    NAMES node libnode
    HINTS ${LibNode_ROOT} ENV LIBNODE_ROOT
    PATH_SUFFIXES lib lib64)

find_package_handle_standard_args(LibNode
    REQUIRED_VARS LibNode_INCLUDE_DIR LibNode_LIBRARY)

if(LibNode_FOUND AND NOT TARGET LibNode::LibNode)
    add_library(LibNode::LibNode UNKNOWN IMPORTED)
    set_target_properties(LibNode::LibNode PROPERTIES
        IMPORTED_LOCATION "${LibNode_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LibNode_INCLUDE_DIR}")
endif()

mark_as_advanced(LibNode_INCLUDE_DIR LibNode_LIBRARY)