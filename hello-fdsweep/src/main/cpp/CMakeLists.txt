cmake_minimum_required(VERSION 3.22.1)
project(hellofdsweep LANGUAGES CXX)

add_library(hellofdsweep SHARED native-lib.cpp)

target_link_libraries(hellofdsweep
    PRIVATE
    log
)
