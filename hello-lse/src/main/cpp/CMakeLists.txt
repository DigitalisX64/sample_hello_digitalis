cmake_minimum_required(VERSION 3.22.1)
project(hellolse LANGUAGES CXX)

add_library(hellolse SHARED native-lib.cpp)

# Armv8.1-a+lse forces the compiler to emit LSE atomics (CAS/SWP/LDADD/etc.)
# in place of the LL/SC fallback path.  Without this flag, clang would emit
# LDXR/STXR pairs and bypass the §B1-§B4 decoder dispatch entirely.
target_compile_options(hellolse PRIVATE -march=armv8.1-a+lse)

target_link_libraries(hellolse
    PRIVATE
    log
)
