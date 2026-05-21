cmake_minimum_required(VERSION 3.22.1)
project(hellobf16 LANGUAGES CXX)

add_library(hellobf16 SHARED native-lib.cpp)

# Armv8.6-a + `+bf16` unlocks BFloat16 instructions: BFDOT/BFMMLA/BFCVT
# scalar, BFMLALB/BFMLALT vector and indexed, BFDOT indexed, and
# BFCVTN/BFCVTN2 vector.  These are all single-instruction probes via
# inline asm; if Digitalis ever routes any BF16 encoding to Undefined()
# the entire native lib SIGILLs on the first call.
target_compile_options(hellobf16 PRIVATE
    -march=armv8.6-a+bf16)

target_link_libraries(hellobf16
    PRIVATE
    log
)
