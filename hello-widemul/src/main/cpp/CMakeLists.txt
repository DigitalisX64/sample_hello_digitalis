cmake_minimum_required(VERSION 3.22.1)
project(hellowidemul LANGUAGES CXX)

add_library(hellowidemul SHARED native-lib.cpp)

# Armv8-A baseline (no extension needed) covers SMULL/UMULL/PMULL on 8-bit
# inputs.  PMULL with 64-bit inputs (PMULL64) requires the AES/PMULL crypto
# extension — armv8-a+crypto enables it.  We use raw .inst opcodes so the
# probe doesn't depend on the assembler accepting the mnemonics.
target_compile_options(hellowidemul PRIVATE
    -march=armv8-a+crypto)

target_link_libraries(hellowidemul
    PRIVATE
    log
)
