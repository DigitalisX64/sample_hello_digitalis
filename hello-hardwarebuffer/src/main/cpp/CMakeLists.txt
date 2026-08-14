cmake_minimum_required(VERSION 3.22.1)
project(hellohardwarebuffer LANGUAGES CXX)

add_library(hellohardwarebuffer SHARED native-lib.cpp)

# AHardwareBuffer_toHardwareBuffer/_fromHardwareBuffer are exported by
# libandroid only; the rest of the AHardwareBuffer surface is in
# libnativewindow.
target_link_libraries(hellohardwarebuffer
    PRIVATE
    android
    log
    nativewindow
)
