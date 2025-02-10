cmake_minimum_required(VERSION 3.10)
project(UnityThreadOptimizer)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(UnityThreadOptimizer SHARED
    src/dllmain.cpp
    src/defs.h
)

target_include_directories(UnityThreadOptimizer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(UnityThreadOptimizer PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

target_compile_definitions(UnityThreadOptimizer PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(UnityThreadOptimizer PRIVATE
    kernel32
    synchronization
) 