cmake_minimum_required(VERSION 3.22.1)
project(hellobinderndk LANGUAGES CXX)

add_library(hellobinderndk SHARED native-lib.cpp)

target_link_libraries(hellobinderndk
    PRIVATE
    log
    binder_ndk
)
