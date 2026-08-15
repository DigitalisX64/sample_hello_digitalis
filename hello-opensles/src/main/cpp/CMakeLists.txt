cmake_minimum_required(VERSION 3.22.1)
project(helloopensles LANGUAGES CXX)

add_library(helloopensles SHARED native-lib.cpp)

target_link_libraries(helloopensles
    PRIVATE
    log
    OpenSLES
)
