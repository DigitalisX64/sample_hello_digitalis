cmake_minimum_required(VERSION 3.22.1)
project(hellofpvector LANGUAGES CXX)

add_library(hellofpvector SHARED native-lib.cpp)

# armv8.5-a enables the FRINTTS (FRINT32Z/X, FRINT64Z/X) mnemonics used by the
# FP-rounding probe. The other probes are arch-agnostic.
target_compile_options(hellofpvector PRIVATE -march=armv8.5-a)

target_link_libraries(hellofpvector
    PRIVATE
    log
)
