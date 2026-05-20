cmake_minimum_required(VERSION 3.22.1)
project(helloneon LANGUAGES CXX)

add_library(helloneon SHARED native-lib.cpp)

# +crypto pulls in the AES/SHA/PMULL family; +simd is implicit on armv8-a.
# PMULL (polynomial multiply long) lives in +crypto on AArch64.
target_compile_options(helloneon PRIVATE -march=armv8-a+crypto)

target_link_libraries(helloneon
    PRIVATE
    log
)
