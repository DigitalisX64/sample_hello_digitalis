cmake_minimum_required(VERSION 3.22.1)
project(HelloGl2 LANGUAGES CXX)

add_library(gl2jni SHARED
    gl_code.cpp
)

target_link_libraries(gl2jni
    PRIVATE
    GLESv2
    log
)
