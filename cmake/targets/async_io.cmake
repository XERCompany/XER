# ============================================================
# XERAsyncIO static library
# ============================================================

add_library(XERAsyncIO STATIC ${ENGINE_ASYNC_IO_CC})
target_sources(XERAsyncIO PRIVATE ${ENGINE_ASYNC_IO_H})
target_include_directories(XERAsyncIO PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(Vulkan_FOUND AND TARGET Vulkan::Vulkan)
    target_compile_definitions(XERAsyncIO PRIVATE XER_ASYNC_IO_HAS_VULKAN=1)
    target_link_libraries(XERAsyncIO PRIVATE Vulkan::Vulkan)
endif()

if(ENABLE_CUDA AND CUDAToolkit_FOUND)
    target_compile_definitions(XERAsyncIO PRIVATE XER_ASYNC_IO_HAS_CUDA=1)
    target_include_directories(XERAsyncIO PRIVATE ${CUDAToolkit_INCLUDE_DIRS})
    if(TARGET CUDA::cudart)
        target_link_libraries(XERAsyncIO PRIVATE CUDA::cudart)
    endif()
endif()

if(ENABLE_CUDNN AND CUDNN_INCLUDE_DIR)
    target_include_directories(XERAsyncIO PRIVATE ${CUDNN_INCLUDE_DIR})
endif()

if(ENABLE_CUDNN AND CUDNN_LIBRARY)
    target_link_libraries(XERAsyncIO PRIVATE ${CUDNN_LIBRARY})
endif()
