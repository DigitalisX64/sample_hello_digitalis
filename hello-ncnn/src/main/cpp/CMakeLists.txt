cmake_minimum_required(VERSION 3.22.1)
project(helloncnn LANGUAGES CXX)

# ncnn ships a per-ABI CMake package (static libncnn.a + headers) rather than a
# Maven/prefab AAR; the build wires ncnn_DIR from build.gradle.kts to the
# vendored package under ncnn-prefab/. See NOTES.md for the vendoring step.
find_package(ncnn REQUIRED CONFIG)

add_library(helloncnn SHARED native-lib.cpp)

target_link_libraries(helloncnn
    PRIVATE
    ncnn
    log
)
