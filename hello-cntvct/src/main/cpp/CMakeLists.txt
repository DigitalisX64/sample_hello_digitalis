cmake_minimum_required(VERSION 3.22.1)
project(hellocntvct LANGUAGES CXX)

add_library(hellocntvct SHARED native-lib.cpp)

# CNTFRQ_EL0/CNTVCT_EL0/CNTPCT_EL0 are base ARMv8.0-A EL0-readable system
# registers; no -march override is needed. The MRS reads are emitted via
# inline asm so the compiler can't fold them to a library clock call.

target_link_libraries(hellocntvct
    PRIVATE
    log
)
