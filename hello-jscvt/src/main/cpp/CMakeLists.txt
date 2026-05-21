cmake_minimum_required(VERSION 3.22.1)
project(hellojscvt LANGUAGES CXX)

add_library(hellojscvt SHARED native-lib.cpp)

# Armv8.3-a unlocks JSCVT (FJCVTZS).  `+jscvt` makes the FJCVTZS encoding
# legal for the inline-asm probes below.  FJCVTZS converts a double to a
# 32-bit signed integer using ECMAScript ToInt32 semantics (round toward
# zero + modular reduction); if Digitalis ever routes FJCVTZS to
# Undefined() the entire native lib SIGILLs on the first call.
target_compile_options(hellojscvt PRIVATE
    -march=armv8.3-a+jscvt)

target_link_libraries(hellojscvt
    PRIVATE
    log
)
