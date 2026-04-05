#
# Copyright (C)  2017 The Android Open Source Project
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
project(CameraBasic LANGUAGES C CXX)

include(AndroidNdkModules)
android_ndk_import_module_native_app_glue()

add_library(ndk_camera SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/android_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_listeners.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/image_reader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_ui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/camera_utils.cpp
)

target_include_directories(ndk_camera PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ndk_camera
    PRIVATE
    -Wl,--whole-archive
    native_app_glue
    -Wl,--no-whole-archive
    android
    log
    m
    camera2ndk
    mediandk
)
