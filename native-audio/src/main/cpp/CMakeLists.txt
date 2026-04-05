cmake_minimum_required(VERSION 3.22.1)
project(NativeAudio LANGUAGES CXX)

add_library(native-audio-jni SHARED
    native-audio-jni.cpp
)

target_link_libraries(native-audio-jni
    PRIVATE
    android
    log
    OpenSLES
)
