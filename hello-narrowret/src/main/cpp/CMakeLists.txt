cmake_minimum_required(VERSION 3.22.1)
project(hellonarrowret LANGUAGES C CXX ASM)

add_library(hellonarrowret SHARED native-lib.cpp call_raw.S)

target_link_libraries(hellonarrowret
    PRIVATE
    log
    mediandk
)
