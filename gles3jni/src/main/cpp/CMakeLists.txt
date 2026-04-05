cmake_minimum_required(VERSION 3.22.1)
project(Gles3Jni LANGUAGES CXX)

add_library(gles3jni SHARED
    gles3jni.cpp
    RendererES2.cpp
    RendererES3.cpp
)

target_link_libraries(gles3jni
    PRIVATE
    GLESv3
    EGL
    log
)
