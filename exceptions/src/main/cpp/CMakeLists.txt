cmake_minimum_required(VERSION 3.22.1)
project(exceptions LANGUAGES CXX)

add_library(
    exceptions
    SHARED
    native-lib.cpp
    exception_helper.cpp
)

target_link_libraries(
    exceptions
    PRIVATE
    log
)
