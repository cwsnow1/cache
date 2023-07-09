
cmake_minimum_required(VERSION 3.16)

project(cache)

add_subdirectory(trace_generation)
add_subdirectory(sim_trace_decoder)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_COMPILER g++)
IF (!WIN32)
set(CMAKE_CXX_FLAGS "-Wall -Werror -pedantic")
ENDIF()

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

file(GLOB_RECURSE SRC_FILES ${cache_SOURCE_DIR}/src/*.cpp)

add_executable(${PROJECT_NAME} ${SRC_FILES})
IF (!WIN32)
target_link_libraries(${PROJECT_NAME} pthread)
ENDIF()

include(CTest)

add_test(NAME ls-l_run COMMAND cache ../ls-l.trace)

