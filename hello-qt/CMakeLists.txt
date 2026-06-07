# Copyright (C) 2026 utzcoz
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

# Minimal Qt 6 Widgets app for the Digitalis sample suite (ARM64-only).
# Built with Qt's qt-cmake + androiddeployqt against the android_arm64_v8a Qt.
# See README.md for the exact reproducible build commands.

cmake_minimum_required(VERSION 3.21)

project(hello_qt VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# qt_add_executable produces a shared library on Android (loaded by the Qt
# loader Activity) and wires up the androiddeployqt deployment settings.
qt_add_executable(hello_qt
    src/main.cpp
)

target_link_libraries(hello_qt PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# Android packaging metadata. These map onto the generated AndroidManifest.xml
# placeholders. The application id matches the Digitalis sample namespace.
if(ANDROID)
    set_target_properties(hello_qt PROPERTIES
        QT_ANDROID_PACKAGE_NAME "com.example.hellodigitalis.helloqt"
        QT_ANDROID_APP_NAME "hello-qt"
        QT_ANDROID_MIN_SDK_VERSION 28
        QT_ANDROID_TARGET_SDK_VERSION 34
        QT_ANDROID_VERSION_NAME "1.0"
        QT_ANDROID_VERSION_CODE 1
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
    )
endif()
