cmake_minimum_required(VERSION 3.22.1)
project(hellosha LANGUAGES CXX)

add_library(hellosha SHARED native-lib.cpp)

# +crypto enables SHA-1/SHA-256 + AES + PMULL on ARMv8.0.
# +sha3 enables SHA-512 (SHA512H/H2/SU0/SU1) and SHA-3 helpers (RAX1/XAR/...);
# requires -march=armv8.2-a or newer. Without +sha3 the vsha512* intrinsics
# don't compile.
target_compile_options(hellosha PRIVATE -march=armv8.2-a+crypto+sha3)

target_link_libraries(hellosha
    PRIVATE
    log
)
