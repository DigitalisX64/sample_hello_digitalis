cmake_minimum_required(VERSION 3.22.1)
project(hellogles1 LANGUAGES CXX)

add_library(hellogles1 SHARED native-lib.cpp)

target_link_libraries(hellogles1
    PRIVATE
    log
    GLESv1_CM
)
