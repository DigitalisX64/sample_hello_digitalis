cmake_minimum_required(VERSION 3.22.1)
project(hellosigaction LANGUAGES CXX)

add_library(hellosigaction SHARED native-lib.cpp)

# Base ARMv8.0-A; no -march override is needed. The probe installs POSIX signal
# handlers and reads back the previous action via sigaction(), modelling a
# native crash reporter's startup.

target_link_libraries(hellosigaction
    PRIVATE
    log
)
