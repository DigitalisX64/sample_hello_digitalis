cmake_minimum_required(VERSION 3.22.1)
project(helloimagedecoder LANGUAGES CXX)

add_library(helloimagedecoder SHARED native-lib.cpp)

# API-31 symbols (AImageDecoder_resultToString, the animation APIs) become weak
# imports so this minSdk-30 build can probe them behind __builtin_available.
target_compile_definitions(helloimagedecoder PRIVATE __ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__)

target_link_libraries(helloimagedecoder
    PRIVATE
    jnigraphics
    android
    log
)
