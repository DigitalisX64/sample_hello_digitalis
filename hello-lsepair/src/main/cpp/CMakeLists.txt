cmake_minimum_required(VERSION 3.22.1)
project(hellolsepair LANGUAGES CXX)

add_library(hellolsepair SHARED native-lib.cpp)

# LSE atomics completion probe: byte/halfword fetch-and-op forms, the 64-bit
# CASP pair, and LDXP/STXP pair-exclusive loops — all in hot loops past the
# gear-up threshold so the heavy tier's sized CMPXCHG / CMPXCHG16B lowerings run.
target_compile_options(hellolsepair PRIVATE -march=armv8.1-a)

target_link_libraries(hellolsepair
    PRIVATE
    log
)
