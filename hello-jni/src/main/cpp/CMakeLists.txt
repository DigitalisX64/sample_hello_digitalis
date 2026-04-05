cmake_minimum_required(VERSION 3.22.1)
project("hello-jni" LANGUAGES CXX)

add_library(hello-jni SHARED
    hello-jni.cpp
)

target_link_libraries(hello-jni
    PRIVATE
    android
    log
)
