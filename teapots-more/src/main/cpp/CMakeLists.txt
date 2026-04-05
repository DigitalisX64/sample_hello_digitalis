#
# Copyright (C) The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

cmake_minimum_required(VERSION 3.22.1)
project(MoreTeapotsNativeActivity LANGUAGES C CXX)

# Build the ndk_helper library (includes native_app_glue)
add_subdirectory(ndk_helper)

# Build app's shared library
add_library(${PROJECT_NAME} SHARED
    MoreTeapotsNativeActivity.cpp
    MoreTeapotsRenderer.cpp
)
set_target_properties(${PROJECT_NAME}
    PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

# Export ANativeActivity_onCreate(),
# Refer to: https://github.com/android-ndk/ndk/issues/381.
set_target_properties(${PROJECT_NAME}
    PROPERTIES LINK_FLAGS "-u ANativeActivity_onCreate")

# Link native_app_glue with --whole-archive
target_link_libraries(${PROJECT_NAME}
    PRIVATE
    NdkHelper
    -Wl,--whole-archive
    native_app_glue
    -Wl,--no-whole-archive
    GLESv2
    EGL
    android
    log
)
