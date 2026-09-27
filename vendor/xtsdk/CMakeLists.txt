cmake_minimum_required(VERSION 3.5)

project(xtsdk_src VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# msvc setting
if(MSVC)
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
endif()

# ################################################################
add_subdirectory(xtsdk)
add_subdirectory(sdk_example)
