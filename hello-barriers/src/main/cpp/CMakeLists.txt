cmake_minimum_required(VERSION 3.22.1)
project(hellobarriers LANGUAGES CXX)

add_library(hellobarriers SHARED native-lib.cpp)

# Armv8.5-a unlocks the SB (speculation barrier) opcode in addition to the
# Armv8-A baseline DMB/DSB/ISB/CLREX/WFE/WFI/SEV/SEVL/YIELD.  Inline asm
# uses explicit mnemonics, so this flag is mainly to keep the assembler
# from rejecting SB.
target_compile_options(hellobarriers PRIVATE -march=armv8.5-a)

target_link_libraries(hellobarriers
    PRIVATE
    log
)
