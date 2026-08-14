cmake_minimum_required(VERSION 3.22.1)
project(hellosharedmem LANGUAGES CXX)

add_library(hellosharedmem SHARED native-lib.cpp)

target_link_libraries(hellosharedmem
    PRIVATE
    android
    log
)
