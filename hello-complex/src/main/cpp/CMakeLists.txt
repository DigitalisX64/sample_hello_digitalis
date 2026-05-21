cmake_minimum_required(VERSION 3.22.1)
project(hellocomplex LANGUAGES CXX)

add_library(hellocomplex SHARED native-lib.cpp)

# Armv8.3-a is the baseline that introduces FCMA (Floating-point Complex
# Multiply-Accumulate): FCADD and FCMLA.  No sub-feature flag is required;
# FCMA is mandatory in v8.3.  We use raw `.inst` opcodes regardless so the
# probe is portable across NDK versions.
# +fp16 enables the scalar half-precision load/store mnemonics (`ldr h0,
# [..]` / `str h0, [..]` / `frinta h0, h1`) used by the FRINTA scalar probe.
target_compile_options(hellocomplex PRIVATE
    -march=armv8.3-a+fp16)

target_link_libraries(hellocomplex
    PRIVATE
    log
)
