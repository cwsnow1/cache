
cmake_minimum_required(VERSION 3.10)

project(cache)

add_subdirectory(trace_generation)
add_subdirectory(sim_trace_decoder)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_COMPILER clang)
set(CMAKE_C_FLAGS "-Wall -Werror -pedantic")
if(NOT DEFINED CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

if(CMAKE_C_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
endif()

if(SIM_TRACE EQUAL 1)
    add_definitions(-DSIM_TRACE=1)
else()
    add_definitions(-DSIM_TRACE=0)
endif()

if(CONSOLE_PRINT EQUAL 1)
    add_definitions(-DCONSOLE_PRINT=1)
else()
    add_definitions(-DCONSOLE_PRINT=0)
endif()

include_directories(${cache_SOURCE_DIR}/inc)

file(GLOB_RECURSE SRC_FILES ${cache_SOURCE_DIR}/src/*.c)

add_executable(${PROJECT_NAME} ${SRC_FILES})
target_link_libraries(${PROJECT_NAME} pthread)

