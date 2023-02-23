cmake_minimum_required(VERSION 3.10)

include_directories(../inc)
set(CMAKE_C_FLAGS "-g")
add_executable(decoder sim_trace_decoder.c)