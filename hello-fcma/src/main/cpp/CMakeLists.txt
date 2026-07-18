cmake_minimum_required(VERSION 3.22.1)
project(hellofcma LANGUAGES CXX)

add_library(hellofcma SHARED native-lib.cpp)

# ARMv8.3-A enables the complex-number FCADD/FCMLA intrinsics
# (vcaddq_rot90/270_f32, vcmlaq[_rot90/180/270]_f32). The probe runs them in a
# hot loop so the heavy JIT tier lowers them, and self-checks against a scalar
# complex-arithmetic reference.
target_compile_options(hellofcma PRIVATE -march=armv8.3-a)

target_link_libraries(hellofcma
    PRIVATE
    log
)
