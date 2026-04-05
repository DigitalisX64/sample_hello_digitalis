cmake_minimum_required(VERSION 3.22.1)
project(BitmapPlasma LANGUAGES CXX)

add_library(plasma SHARED
    jni.cpp
    plasma.cpp
)

target_link_libraries(plasma
    PRIVATE
    jnigraphics
    log
)
