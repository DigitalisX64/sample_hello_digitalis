cmake_minimum_required(VERSION 3.22.1)
project(hellolseatomics LANGUAGES CXX)

add_library(hellolseatomics SHARED native-lib.cpp)

# ARMv8.1-A LSE (Large System Extensions) provides the single-instruction atomic
# RMW ops LDSET/LDCLR/LDEOR, LDSMAX/LDSMIN/LDUMAX/LDUMIN and CASP. The probe
# emits them via inline asm in a hot loop so the heavy JIT tier lowers them, and
# self-checks the returned old value and the resulting memory each iteration.
target_compile_options(hellolseatomics PRIVATE -march=armv8.1-a)

target_link_libraries(hellolseatomics
    PRIVATE
    log
)
