cmake_minimum_required(VERSION 3.22.1)
project(helloneonmisc LANGUAGES CXX)

add_library(helloneonmisc SHARED native-lib.cpp)

# NEON residue probe: vector FP-misc (FABS/FNEG/FSQRT f32+f64, FACGE/FACGT,
# FCVTL/FCVTN, FRECPE/FRSQRTE, SQABS/SQNEG), byte-lane MUL/MLA/shifts, scalar
# pairwise and scalar by-element FP — hot loops past the gear-up threshold.
target_compile_options(helloneonmisc PRIVATE -march=armv8-a+crc)

target_link_libraries(helloneonmisc
    PRIVATE
    log
)
