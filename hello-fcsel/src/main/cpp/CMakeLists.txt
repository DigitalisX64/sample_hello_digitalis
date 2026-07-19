cmake_minimum_required(VERSION 3.22.1)
project(hellofcsel LANGUAGES CXX)

add_library(hellofcsel SHARED native-lib.cpp)

# FCSEL region-interaction probe: hot loops past the JIT gear-up threshold so
# the heavy tier's conditional-select lowering is exercised, including the
# flag-liveness and source-reuse shapes that once miscompiled at region level.
target_compile_options(hellofcsel PRIVATE -march=armv8-a)

target_link_libraries(hellofcsel
    PRIVATE
    log
)
