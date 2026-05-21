cmake_minimum_required(VERSION 3.22.1)
project(hellopacret LANGUAGES CXX)

add_library(hellopacret SHARED native-lib.cpp)

# Armv8.3-a unlocks PAuth (PACIASP/AUTIASP/PACIA/AUTIA/XPACI/XPACD etc.).
# `-mbranch-protection=pac-ret` makes the compiler emit PACIASP at the start
# and AUTIASP at the end of every non-leaf function (the so-called PAC-RET
# pattern) -- which is exactly what the §F1 plan task probes.  On a CPU
# without PAuth backing (which is what Digitalis emulates) these are HINTs
# and must be no-ops; if Digitalis ever routes one to Undefined() the entire
# native lib SIGILLs on the very first function call.
target_compile_options(hellopacret PRIVATE
    -march=armv8.3-a
    -mbranch-protection=pac-ret)

target_link_libraries(hellopacret
    PRIVATE
    log
)
