cmake_minimum_required(VERSION 3.22.1)
project(hellovktexture LANGUAGES CXX)

add_library(hellovktexture SHARED native-lib.cpp)

target_link_libraries(hellovktexture
    PRIVATE
    vulkan
    log
)
