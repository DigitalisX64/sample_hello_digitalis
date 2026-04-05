cmake_minimum_required(VERSION 3.22.1)
project(NativeActivity LANGUAGES C CXX)

# Import the native_app_glue module from the NDK.
include(AndroidNdkModules)
android_ndk_import_module_native_app_glue()

add_library(native-activity SHARED
    main.cpp
)

set_target_properties(native-activity PROPERTIES
    CXX_STANDARD 20
)

# Link native_app_glue with whole-archive so the linker keeps
# ANativeActivity_onCreate even though our code doesn't call it directly.
target_link_libraries(native-activity
    PRIVATE
    -Wl,--whole-archive
    native_app_glue
    -Wl,--no-whole-archive
    android
    log
)
