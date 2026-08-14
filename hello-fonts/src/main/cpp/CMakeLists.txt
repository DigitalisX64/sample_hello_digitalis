cmake_minimum_required(VERSION 3.22.1)
project(hellofonts LANGUAGES CXX)

add_library(hellofonts SHARED native-lib.cpp)

target_link_libraries(hellofonts
    PRIVATE
    android
    log
)
