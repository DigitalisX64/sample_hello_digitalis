cmake_minimum_required(VERSION 3.22.1)
project(helloglyphblit LANGUAGES CXX)

add_library(helloglyphblit SHARED native-lib.cpp)

# Base armv8-a (NEON implicit). This sample reproduces Skia's hand-written NEON
# A8 glyph blit (blit_mask_d32_a8_neon) and self-checks it against a scalar
# reference, so only the base Advanced SIMD instructions are needed.
target_compile_options(helloglyphblit PRIVATE -march=armv8-a)

target_link_libraries(helloglyphblit
    PRIVATE
    log
)
