cmake_minimum_required(VERSION 3.22.1)
project(helloaaudio LANGUAGES CXX)

add_library(helloaaudio SHARED native-lib.cpp)

target_link_libraries(helloaaudio
    PRIVATE
    log
    aaudio
)
