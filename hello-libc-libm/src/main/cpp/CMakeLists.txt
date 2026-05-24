cmake_minimum_required(VERSION 3.22.1)
project(hellolibclibm LANGUAGES CXX)

add_library(hellolibclibm SHARED native-lib.cpp)

# No extra -march flag: probes target plain bionic libc/libm symbols
# (LFS-64 aliases, isnan/isinf family, cospi/sinpi, *_finite math). The
# sample exists to verify the Digitalis-side extra fast-path trampolines
# in libberberis_digitalis_extra_proxy_arm64.

target_link_libraries(hellolibclibm
    PRIVATE
    log
)
