cmake_minimum_required(VERSION 3.22.1)
project(helloopenmaxal LANGUAGES CXX)

add_library(helloopenmaxal SHARED native-lib.cpp)

target_link_libraries(helloopenmaxal
    PRIVATE
    log
    OpenMAXAL
)
