cmake_minimum_required(VERSION 3.22.1)
project(helloldxp LANGUAGES CXX)

add_library(helloldxp SHARED native-lib.cpp)

# LDXP/STXP/LDAXP/STLXP are base ARMv8.0-A; no -march override is needed. The
# load/store-exclusive pair opcodes are emitted directly via inline asm so the
# compiler can't substitute a single-register sequence.

target_link_libraries(helloldxp
    PRIVATE
    log
)
