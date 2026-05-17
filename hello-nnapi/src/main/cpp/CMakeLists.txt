cmake_minimum_required(VERSION 3.22.1)
project(hellonnapi LANGUAGES CXX)

add_library(hellonnapi SHARED native-lib.cpp)

target_link_libraries(hellonnapi
    PRIVATE
    log
    neuralnetworks
)
