cmake_minimum_required(VERSION 3.22.1)
project(hellowebviewfunctor LANGUAGES CXX)

add_library(hellowebviewfunctor SHARED native-lib.cpp)

target_link_libraries(hellowebviewfunctor
    PRIVATE
    log
)
