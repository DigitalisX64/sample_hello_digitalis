cmake_minimum_required(VERSION 3.22.1)
project(helloseccomp LANGUAGES CXX)

add_library(helloseccomp SHARED native-lib.cpp)

# Base ARMv8.0-A; no -march override is needed. The probe installs a guest
# seccomp-bpf filter via the seccomp() syscall, modelling a sandboxed engine's
# (Chromium/Gecko renderer) startup. Headers come from the NDK sysroot.

target_link_libraries(helloseccomp
    PRIVATE
    log
)
