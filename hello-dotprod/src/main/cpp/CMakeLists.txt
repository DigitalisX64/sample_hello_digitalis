cmake_minimum_required(VERSION 3.22.1)
project(hellodotprod LANGUAGES CXX)

add_library(hellodotprod SHARED native-lib.cpp)

# Armv8.4-a is the baseline that introduces SDOT/UDOT (signed/unsigned
# integer dot product).  The +dotprod sub-feature flag is required on
# some toolchains; v8.4 makes it mandatory but the explicit flag keeps
# behavior the same across NDK versions.  We still use raw `.inst`
# opcodes regardless so the probe doesn't depend on the assembler
# accepting the DOT mnemonics.
target_compile_options(hellodotprod PRIVATE
    -march=armv8.4-a+dotprod)

target_link_libraries(hellodotprod
    PRIVATE
    log
)
