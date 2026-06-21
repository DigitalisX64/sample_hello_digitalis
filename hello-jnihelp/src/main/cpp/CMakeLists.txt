cmake_minimum_required(VERSION 3.22.1)
project(hellojnihelp LANGUAGES CXX)

add_library(hellojnihelp SHARED native-lib.cpp)

target_link_libraries(hellojnihelp
    PRIVATE
    log
)
