cmake_minimum_required(VERSION 3.16)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_COMPILER clang)

add_executable(sequential_read trace.c)