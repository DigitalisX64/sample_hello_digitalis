cmake_minimum_required(VERSION 3.22.1)
project(hellofpvector LANGUAGES CXX)

add_library(hellofpvector SHARED native-lib.cpp)

target_link_libraries(hellofpvector
    PRIVATE
    log
)
