cmake_minimum_required(VERSION 3.22.1)
project(helloldinterleave LANGUAGES CXX)

add_library(helloldinterleave SHARED native-lib.cpp)

target_link_libraries(helloldinterleave
    PRIVATE
    log
)
