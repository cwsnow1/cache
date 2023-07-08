cmake_minimum_required(VERSION 3.16)

include_directories(../inc)
set(CMAKE_CXX_FLAGS "-g -Wall  -pedantic ")
add_executable(decoder sim_trace_decoder.cpp)