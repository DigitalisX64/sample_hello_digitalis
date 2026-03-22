cmake_minimum_required(VERSION 3.22.1)
# region digitalis
# project(hello_digitalis CXX)
project(hello_digitalis C CXX)
# endregion

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Android native app glue
set(APP_GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue)
add_library(native_app_glue STATIC ${APP_GLUE_DIR}/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC ${APP_GLUE_DIR})

# Ensure native_app_glue symbols are not stripped
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")

add_library(hello_digitalis SHARED
    main.cpp
    vulkan_renderer.cpp
)

find_library(android-lib android)
find_library(log-lib log)

target_include_directories(hello_digitalis PRIVATE
    ${APP_GLUE_DIR}
)

target_link_libraries(hello_digitalis
    native_app_glue
    ${android-lib}
    ${log-lib}
    vulkan
)
