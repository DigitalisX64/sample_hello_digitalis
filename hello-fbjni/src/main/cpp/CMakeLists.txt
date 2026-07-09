cmake_minimum_required(VERSION 3.22.1)
project(hellofbjni LANGUAGES CXX)

# fbjni ships a prefab module (headers + libfbjni.so) in its AAR; AGP's
# prefab=true generates the package config that find_package resolves.
find_package(fbjni REQUIRED CONFIG)

add_library(hellofbjni SHARED native-lib.cpp)

target_link_libraries(hellofbjni
    PRIVATE
    fbjni::fbjni
    log)
