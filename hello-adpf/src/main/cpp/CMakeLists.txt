cmake_minimum_required(VERSION 3.22.1)
project(helloadpf LANGUAGES CXX)

add_library(helloadpf SHARED native-lib.cpp)

# API 34+/35+ ADPF symbols (setThreads, AWorkDuration/reportActualWorkDuration2,
# setPreferPowerEfficiency, headroom thresholds) are referenced as weak imports
# and guarded with __builtin_available at runtime; minSdk stays 33.
target_compile_definitions(helloadpf PRIVATE __ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__)

target_link_libraries(helloadpf
    PRIVATE
    android
    log
)
