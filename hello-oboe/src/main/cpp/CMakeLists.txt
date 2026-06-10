cmake_minimum_required(VERSION 3.22.1)
project(hellooboe LANGUAGES CXX)

find_package(oboe REQUIRED CONFIG)

add_library(hellooboe SHARED native-lib.cpp)

target_link_libraries(hellooboe
    PRIVATE
    oboe::oboe
    log
)
