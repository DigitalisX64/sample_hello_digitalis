cmake_minimum_required(VERSION 3.22.1)
project(hellosuperpackregress LANGUAGES CXX)

add_library(hellosuperpackregress SHARED native-lib.cpp)

target_link_libraries(hellosuperpackregress
    PRIVATE
    log
)

# +lse: needed for SWPB, LDADDB, CAS atomic instructions.
target_compile_options(hellosuperpackregress PRIVATE -march=armv8-a+lse)
