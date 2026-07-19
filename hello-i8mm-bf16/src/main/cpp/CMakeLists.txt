cmake_minimum_required(VERSION 3.22.1)
project(helloi8mmbf16 LANGUAGES CXX)

add_library(helloi8mmbf16 SHARED native-lib.cpp)

# ARMv8.6 I8MM matrix-multiply (SMMLA/UMMLA/USMMLA), BF16 (BFDOT/BFMMLA/
# BFMLALB/T) and the ARMv8.3 indexed FCMLA — the quantized-ML instruction set.
# Hot loops past the gear-up threshold exercise the heavy-tier lowerings.
target_compile_options(helloi8mmbf16 PRIVATE -march=armv8.6-a+i8mm+bf16)

target_link_libraries(helloi8mmbf16
    PRIVATE
    log
)
