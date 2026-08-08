cmake_minimum_required(VERSION 3.22.1)
project(hellonativewindow LANGUAGES CXX)

add_library(hellonativewindow SHARED native-lib.cpp)

target_link_libraries(hellonativewindow
    PRIVATE
    log
    android
)
