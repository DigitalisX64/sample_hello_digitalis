cmake_minimum_required(VERSION 3.22.1)
project(native_midi LANGUAGES CXX)

add_library(${PROJECT_NAME} SHARED
    AppMidiManager.cpp
    MainActivity.cpp
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    amidi
    log
)
