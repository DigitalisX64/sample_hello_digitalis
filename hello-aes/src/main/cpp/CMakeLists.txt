cmake_minimum_required(VERSION 3.22.1)
project(helloaes LANGUAGES CXX)

add_library(helloaes SHARED native-lib.cpp)

# +crypto enables the AES crypto-extension intrinsics (AESE/AESD/AESMC/AESIMC
# via vaeseq_u8/vaesdq_u8/vaesmcq_u8/vaesimcq_u8). The round instructions are
# emitted directly so the FIPS-197 KAT exercises them under translation.
target_compile_options(helloaes PRIVATE -march=armv8-a+crypto)

target_link_libraries(helloaes
    PRIVATE
    log
)
