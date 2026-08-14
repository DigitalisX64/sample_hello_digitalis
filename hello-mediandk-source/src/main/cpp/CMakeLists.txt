cmake_minimum_required(VERSION 3.22.1)
project(hellomediandksource LANGUAGES CXX)

add_library(hellomediandksource SHARED native-lib.cpp)

# The API-29 members (setGetAvailableSize, close) are weak imports probed
# behind __builtin_available; minSdk stays 28.
target_compile_definitions(hellomediandksource PRIVATE __ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__)

target_link_libraries(hellomediandksource
    PRIVATE
    mediandk
    android
    log
)
