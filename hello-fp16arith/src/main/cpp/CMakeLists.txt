cmake_minimum_required(VERSION 3.22.1)
project(hellofp16arith LANGUAGES CXX)

add_library(hellofp16arith SHARED native-lib.cpp)

# ARMv8.2 scalar FP16 (FEAT_FP16) arithmetic probe. _Float16 codegen emits the
# half-precision scalar instructions directly; the hot loop pushes regions past
# the gear-up threshold so the heavy tier's F16C round-trip lowering runs.
target_compile_options(hellofp16arith PRIVATE -march=armv8.2-a+fp16)

target_link_libraries(hellofp16arith
    PRIVATE
    log
)
