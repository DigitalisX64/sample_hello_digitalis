cmake_minimum_required(VERSION 3.22.1)
project(hellofp16 LANGUAGES CXX)

add_library(hellofp16 SHARED native-lib.cpp)

# Armv8.2-FP16 enables half-precision scalar arithmetic encodings
# (FADD/FSUB/FMUL/FDIV/FMADD/FMSUB/FMAX/FMIN/FCMP on Hd, Hn, Hm with
# ftype=11 in the FP encoding).  We emit the instructions via raw
# .inst opcodes so the probe doesn't depend on the assembler accepting
# the mnemonics in any specific clang release.
target_compile_options(hellofp16 PRIVATE
    -march=armv8.2-a+fp16)

target_link_libraries(hellofp16
    PRIVATE
    log
)
