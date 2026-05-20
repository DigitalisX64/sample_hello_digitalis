cmake_minimum_required(VERSION 3.22.1)
project(hellosha1crypto LANGUAGES CXX)

add_library(hellosha1crypto SHARED native-lib.cpp)

# +crypto enables the ARMv8 cryptographic extension intrinsics
# (vsha1cq_u32, vsha1pq_u32, vsha1mq_u32, vsha1h_u32) which lower to
# the SHA1C/SHA1P/SHA1M/SHA1H instructions.
target_compile_options(hellosha1crypto PRIVATE -march=armv8-a+crypto)

target_link_libraries(hellosha1crypto
    PRIVATE
    log
)
